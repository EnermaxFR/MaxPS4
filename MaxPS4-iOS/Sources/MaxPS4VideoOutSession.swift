import Foundation
import Combine
#if MAXPS4_FEX_APP
import Darwin

// Native execution/close stay on the established serial FEX queue. Snapshot
// and completion use the native concurrent VideoOut API, with an extra lock
// to serialize callbacks against closing this source. Late callbacks see nil.
final class MaxPS4VideoOutSource: @unchecked Sendable {
    struct Frame { let info: maxps4_videoout_frame; let bytes: Data }
    private let lock = NSLock()
    private var engine: OpaquePointer?
    private var ready = false
    private var stopped = false
    private var presentations = 0
    private var buttons: UInt32 = 0

    func input(_ buttons: UInt32) {
        lock.lock(); defer { lock.unlock() }
        guard !stopped else { return }
        self.buttons = buttons
        if ready, let engine {
            _ = maxps4_fex_engine_pad_update(engine, buttons, 128, 128, 128, 128, 0, 0, 1)
        }
    }

    func snapshot() throws -> Frame? {
        lock.lock(); defer { lock.unlock() }
        guard !stopped, ready, let engine else { return nil }
        var info = maxps4_videoout_frame()
        let query = maxps4_fex_engine_videoout_snapshot(engine, &info, nil, 0)
        if query == -EAGAIN { return nil }
        guard query == 0, info.ticket != 0, info.byte_count > 0,
              info.byte_count <= 2048 * 1080 * 4 else { throw failure("Image VideoOut refusée", query) }
        var bytes = Data(count: Int(info.byte_count))
        let copied = bytes.withUnsafeMutableBytes {
            maxps4_fex_engine_videoout_snapshot(engine, &info, $0.bindMemory(to: UInt8.self).baseAddress, $0.count)
        }
        if copied == -EAGAIN { return nil }
        guard copied == 0, info.byte_count <= UInt64(bytes.count) else { throw failure("Copie VideoOut refusée", copied) }
        bytes.count = Int(info.byte_count)
        return Frame(info: info, bytes: bytes)
    }

    @discardableResult func complete(ticket: UInt64, presented: Bool) -> Bool {
        lock.lock(); defer { lock.unlock() }
        guard let engine else { return false }
        let confirmed = presented && !stopped
        let status = maxps4_fex_engine_videoout_complete(engine, ticket, confirmed ? 1 : 0)
        if status == 0 && confirmed { presentations += 1 }
        return status == 0 && confirmed
    }

    func stop() {
        lock.lock(); stopped = true; lock.unlock()
        MaxPS4FEXMetalExecution.queue.async { self.close() }
    }

    private func close() {
        dispatchPrecondition(condition: .onQueue(MaxPS4FEXMetalExecution.queue))
        lock.lock(); defer { lock.unlock() }
        ready = false
        if let engine { self.engine = nil; maxps4_fex_engine_close(engine) }
    }

    func run(interactive: Bool = false) -> String {
        // Called only on the serial FEX queue; close cannot overtake run_orbis.
        dispatchPrecondition(condition: .onQueue(MaxPS4FEXMetalExecution.queue))
        defer { close() }
        do {
            guard let url = Bundle.main.url(forResource: interactive ? "pad-videoout" : "videoout", withExtension: "elf", subdirectory: "VideoOutDemo") else {
                throw failure("Démo VideoOut absente de cette compilation", -2)
            }
            let bytes = try Data(contentsOf: url)
            guard !bytes.isEmpty, bytes.count <= 65536 else { throw failure("Démo VideoOut invalide", -22) }
            var opened: OpaquePointer?
            let status = maxps4_fex_engine_open(&opened)
            guard status == 0, let opened else { throw failure("Ouverture FEX", status) }
            lock.lock()
            if stopped { lock.unlock(); maxps4_fex_engine_close(opened); return "Arrêté." }
            engine = opened; lock.unlock()
            var relocation: UInt64 = 0
            var diagnostic = [CChar](repeating: 0, count: 1024)
            let prepared = "videoout.elf".withCString { path in
                bytes.withUnsafeBytes { raw in
                    var module = maxps4_orbis_module(path: path, bytes: raw.bindMemory(to: UInt8.self).baseAddress,
                        size: raw.count, load_bias: 0x100000, dependencies: nil, dependency_count: 0)
                    return diagnostic.withUnsafeMutableBufferPointer {
                        maxps4_fex_engine_prepare_orbis_general(opened, &module, 1, path, &relocation, $0.baseAddress, $0.count)
                    }
                }
            }
            guard prepared == 0 else { throw failure(String(cString: diagnostic), prepared) }
            let initialized = maxps4_fex_engine_initialize(opened)
            guard initialized == 0 else { throw failure("Activer le JIT avec StikDebug puis réessayer", initialized) }
            let limited = maxps4_fex_engine_set_time_limit(opened, 5000)
            guard limited == 0 else { throw failure("Limite d’exécution indisponible", limited) }
            lock.lock(); ready = !stopped; let cancelled = stopped
            if ready { _ = maxps4_fex_engine_pad_update(opened, buttons, 128, 128, 128, 128, 0, 0, 1) }
            lock.unlock()
            if cancelled { return "Arrêté." }
            var result: UInt64 = 0, entry: UInt64 = 0
            var backend: UInt32 = 0
            let ran = maxps4_fex_engine_run_orbis(opened, &result, &backend, &entry)
            var report = maxps4_fex_cpu_report()
            let reported = maxps4_fex_engine_last_cpu_report(opened, &report)
            lock.lock(); let displayed = presentations; lock.unlock()
            guard ran == 0, reported == 0, report.exited == 1, report.exit_status == 123,
                  backend == UInt32(MAXPS4_CPU_FEX_ARM64_JIT), displayed == (interactive ? 120 : 1) else {
                throw failure("Test incomplet : sortie \(report.exit_status), backend \(backend), présentation \(displayed)", ran)
            }
            return interactive
                ? "120 images x86-64 présentées via Metal. Entrées lues par libScePad, FEX ARM64 natif, sortie 123."
                : "Deux pixels x86-64 présentés via Metal. FEX ARM64 natif, flip confirmé, sortie 123."
        } catch { return "Échec : \(error.localizedDescription)" }
    }

    private func failure(_ message: String, _ code: Int32) -> NSError {
        NSError(domain: "MaxPS4.VideoOut", code: Int(code), userInfo: [NSLocalizedDescriptionKey: "\(message) (\(code))"])
    }
}

@MainActor final class MaxPS4VideoOutSession: ObservableObject {
    @Published private(set) var source: MaxPS4VideoOutSource?
    @Published private(set) var running = false
    @Published private(set) var report = "Aucune présentation effectuée."
    // Stored separately so nonisolated deinit need not call an actor-isolated
    // @Published accessor. The source itself serializes stop/close safely.
    private var worker: MaxPS4VideoOutSource?
    private var generation = UUID()

    func input(_ buttons: UInt32) { worker?.input(buttons) }
    func start(interactive: Bool = false) {
        stop()
        let source = MaxPS4VideoOutSource(), id = UUID()
        generation = id; self.source = source; worker = source; running = true
        report = "Préparation FEX et attente de présentation Metal…"
        MaxPS4FEXMetalExecution.queue.async { [weak self] in
            let report = source.run(interactive: interactive)
            DispatchQueue.main.async {
                guard let self, self.generation == id else { return }
                self.report = report; self.running = false
            }
        }
    }
    func rendererReport(_ text: String) { if running { report = text } }
    func stop() {
        generation = UUID(); worker?.stop(); worker = nil; source = nil; running = false
        report = "Arrêt demandé ; aucun nouveau flip ne sera confirmé."
    }
    deinit { worker?.stop() }
}
#endif
