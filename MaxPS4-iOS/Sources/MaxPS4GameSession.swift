import Foundation
import Combine
import Metal
#if MAXPS4_FEX_APP
import Darwin

// The FEX worker owns execution and close. Concurrent display/input/stop calls
// retain the engine under lock, using only the native concurrent APIs.
final class MaxPS4GameSource: @unchecked Sendable {
    struct Frame { let info: maxps4_videoout_frame; let texture: MTLTexture; let report: String }
    private let lock = NSLock()
    private let inputLock = NSLock()
    private var engine: OpaquePointer?
    private var bridge: MaxPS4GameMetalBridge?
    private var stopped = false
    private var ready = false
    private var port: UInt64 = 1
    private var missingSince: Date?
    private var buttons: UInt32 = 0
    private var displayed = 0
    private var started: TimeInterval?
    private var vblanksSent = 0
    private var lastTicket: UInt64 = 0
    private var vblankClock = MaxPS4VideoOutClock()
    private var vblankTimer: DispatchSourceTimer?
    private let vblanks = DispatchQueue(label: "com.enermaxfr.maxps4.vblank", qos: .userInteractive)

    private func tick(at time: TimeInterval) {
        guard lock.try() else { return }
        defer { lock.unlock() }
        guard ready, !stopped, let engine else { return }
        sendInputLocked(engine)
        guard vblankClock.due(at: time) else { return }
        _ = maxps4_fex_engine_videoout_vblank(engine, port)
        vblanksSent += 1
    }

    // Progress counters refreshed even when no new image arrives, so a stalled
    // game can be told apart from a slow one. Nil while another call holds the lock.
    func heartbeat() -> String? {
        guard lock.try() else { return nil }
        defer { lock.unlock() }
        guard let started, let engine else { return nil }
        let seconds = Int(ProcessInfo.processInfo.systemUptime - started)
        var activity = [CChar](repeating: 0, count: 4096)
        _ = maxps4_fex_engine_thread_activity(engine, &activity, activity.count)
        return "Depuis le démarrage du JIT : \(seconds) s · dernier ticket VideoOut \(lastTicket) · images affichées \(displayed) · VBlank envoyés \(vblanksSent)\n" + String(cString: activity)
    }

    func input(_ buttons: UInt32) {
        // Retain the latest state even when another engine API owns the lock.
        // The independent timer retries it without waiting for a displayed frame.
        inputLock.lock(); self.buttons = buttons; inputLock.unlock()
        guard lock.try() else { return }
        defer { lock.unlock() }
        if ready, !stopped, let engine {
            sendInputLocked(engine)
        }
    }
    private func sendInputLocked(_ engine: OpaquePointer) {
        inputLock.lock(); let latest = buttons; inputLock.unlock()
        _ = maxps4_fex_engine_pad_update(engine, latest, 128, 128, 128, 128, 0, 0, 1)
    }
    private func snapshotTarget() throws -> (maxps4_videoout_frame, UInt64, MaxPS4GameMetalBridge)? {
        lock.lock(); defer { lock.unlock() }
        guard ready, !stopped, let engine, let bridge else { return nil }
        if let failure = bridge.failure { throw self.failure(failure, -EIO) }
        var info = maxps4_videoout_frame(), address: UInt64 = 0
        let status = maxps4_fex_engine_videoout_target(engine, &info, &address)
        if status == -EAGAIN { return nil }
        guard status == 0, info.ticket != 0 else { throw failure("VideoOut", status) }
        port = info.port; lastTicket = info.ticket
        return (info, address, bridge)
    }
    func snapshot() throws -> Frame? {
        guard let (info, address, bridge) = try snapshotTarget() else { return nil }
        // The retained bridge owns the texture copy. No engine pointer is used
        // here, so GPU completion need not block pad updates, vblank or stop.
        guard let texture = bridge.copyTarget(address: address, width: info.width, height: info.height) else {
            if missingSince == nil { missingSince = Date() }
            if Date().timeIntervalSince(missingSince!) > 5 {
                throw failure("Aucune texture Metal pour le buffer retourné", -EIO)
            }
            return nil
        }
        missingSince = nil
        return Frame(info: info, texture: texture, report: bridge.report)
    }
    @discardableResult func complete(ticket: UInt64, presented: Bool) -> Bool {
        lock.lock(); defer { lock.unlock() }
        guard let engine else { return false }
        let confirmed = presented && !stopped
        let result = maxps4_fex_engine_videoout_complete(engine, ticket, confirmed ? 1 : 0)
        if result == 0 && confirmed { displayed += 1 }
        return result == 0 && confirmed
    }
    func stop() {
        lock.lock()
        stopped = true; ready = false
        vblankTimer?.cancel(); vblankTimer = nil
        if let engine { _ = maxps4_fex_engine_request_stop(engine) }
        lock.unlock()
        // close runs after run_orbis returns, never while FEX/GPU uses memory.
        MaxPS4FEXMetalExecution.queue.async { self.close() }
    }
    private func close() {
        dispatchPrecondition(condition: .onQueue(MaxPS4FEXMetalExecution.queue))
        lock.lock(); defer { lock.unlock() }
        ready = false
        vblankTimer?.cancel(); vblankTimer = nil
        if let engine {
            _ = maxps4_fex_engine_gnm_backend(engine, 0)
            _ = maxps4_fex_engine_gnm_host_consumer(engine, nil, nil)
            bridge = nil; self.engine = nil
            maxps4_fex_engine_close(engine)
        }
    }
    func run(url: URL, device: MTLDevice, nativeTextures: Bool = false, progress: @escaping @Sendable (String) -> Void) -> String {
        dispatchPrecondition(condition: .onQueue(MaxPS4FEXMetalExecution.queue))
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        defer { close() }
        do {
            progress("Ouverture du moteur…")
            var opened: OpaquePointer?
            let status = maxps4_fex_engine_open(&opened)
            guard status == 0, let opened else { throw failure("Ouverture FEX", status) }
            lock.lock()
            if stopped { lock.unlock(); maxps4_fex_engine_close(opened); return "Arrêté." }
            engine = opened; lock.unlock()
            // Same permissive import probe as the validated Sonic boot probe.
            // This remains experimental: unresolved imports are reported by FEX.
            _ = maxps4_fex_engine_set_import_probe(opened, 1)
            var diagnostic = [CChar](repeating: 0, count: 2048)
            var relocation: UInt64 = 0
            progress("Lecture du dossier et chargement de eboot.bin et des PRX…")
            let loaded = url.path.withCString { path in
                diagnostic.withUnsafeMutableBufferPointer {
                    maxps4_fex_engine_load_game(opened, path, &relocation, $0.baseAddress, $0.count)
                }
            }
            guard loaded == 0 else { throw failure(String(cString: diagnostic), loaded) }
            progress("Jeu chargé. Préparation du rendu Metal…")
            guard let metal = MaxPS4GameMetalBridge(device: device, reader: { address, destination, count in
                maxps4_fex_engine_read_memory(opened, address, destination, count) == 0
            }, nativeTextures: nativeTextures) else { throw failure("Création du renderer Metal", -ENOMEM) }
            let attached = maxps4_fex_engine_gnm_host_consumer(opened,
                Unmanaged.passUnretained(metal).toOpaque(), { context, words, count in
                    guard let context, let words else { return -EINVAL }
                    return Unmanaged<MaxPS4GameMetalBridge>.fromOpaque(context).takeUnretainedValue().submit(words, count: count)
                })
            guard attached == 0 else { throw failure("Connexion GNM", attached) }
            let graphics = maxps4_fex_engine_gnm_backend(opened, 4)
            guard graphics == 0 else { throw failure("Backend Metal", graphics) }
            progress("Initialisation du JIT…")
            let initialized = maxps4_fex_engine_initialize(opened)
            guard initialized == 0 else { throw failure("Activer le JIT avec StikDebug puis réessayer", initialized) }
            lock.lock()
            bridge = metal; ready = !stopped
            started = ProcessInfo.processInfo.systemUptime
            let cancelled = stopped
            if ready { sendInputLocked(opened) }
            if ready {
                let timer = DispatchSource.makeTimerSource(queue: vblanks)
                timer.schedule(deadline: .now(), repeating: 1.0 / 60, leeway: .milliseconds(1))
                timer.setEventHandler { [weak self] in self?.tick(at: ProcessInfo.processInfo.systemUptime) }
                vblankTimer = timer; timer.resume()
            }
            lock.unlock()
            if cancelled { return "Arrêté." }
            progress("Jeu en cours d’exécution. En attente de la première image…")
            var value: UInt64 = 0, entry: UInt64 = 0
            var backend: UInt32 = 0
            let ran = maxps4_fex_engine_run_orbis(opened, &value, &backend, &entry)
            lock.lock(); let wasStopped = stopped; let frames = displayed; lock.unlock()
            if wasStopped { return "Arrêté après \(frames) images présentées. \(metal.report)" }
            var fault = maxps4_fex_guest_fault()
            _ = maxps4_fex_engine_last_guest_fault(opened, &fault)
            let relativeRIP = fault.rip >= relocation ? fault.rip - relocation : fault.rip
            return "Exécution terminée (\(ran)), backend \(backend), \(frames) images. \(metal.report)\nSignal \(fault.signal), code \(fault.code), adresse mémoire 0x\(String(fault.address, radix: 16)), RIP 0x\(String(fault.rip, radix: 16)).\nRelocation 0x\(String(relocation, radix: 16)), RIP relatif 0x\(String(relativeRIP, radix: 16))."
        } catch { return "Échec : \(error.localizedDescription)" }
    }
    private func failure(_ text: String, _ code: Int32) -> NSError {
        NSError(domain: "MaxPS4.Game", code: Int(code), userInfo: [NSLocalizedDescriptionKey: "\(text) (\(code))"])
    }
}

@MainActor final class MaxPS4GameSession: ObservableObject {
    @Published private(set) var source: MaxPS4GameSource?
    @Published private(set) var running = false
    @Published private(set) var hasPresented = false
    @Published private(set) var report = "Activer le JIT avec StikDebug avant de lancer le jeu."
    @Published private(set) var heartbeat = ""
    private var worker: MaxPS4GameSource?
    private var generation = UUID()
    private var heartbeatTimer: Timer?
    func start(url: URL, device: MTLDevice, nativeTextures: Bool = false) {
        stop()
        let source = MaxPS4GameSource(), id = UUID()
        generation = id; self.source = source; worker = source; running = true
        hasPresented = false; heartbeat = ""
        report = "Chargement du jeu et préparation Metal…"
        heartbeatTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, self.generation == id, let beat = source.heartbeat() else { return }
                self.heartbeat = beat
            }
        }
        MaxPS4FEXMetalExecution.queue.async { [weak self] in
            let result = source.run(url: url, device: device, nativeTextures: nativeTextures) { [weak self] text in
                DispatchQueue.main.async { [weak self] in
                    guard let self, self.generation == id, self.running else { return }
                    self.report = text
                }
            }
            DispatchQueue.main.async {
                guard let self, self.generation == id else { return }
                self.report = result; self.running = false
            }
        }
    }
    func input(_ buttons: UInt32) { worker?.input(buttons) }
    func presented(_ text: String) { if running { hasPresented = true; report = text } }
    func failed(_ text: String) { stop(); report = text }
    func stop() {
        generation = UUID(); worker?.stop(); worker = nil; source = nil; running = false
        heartbeatTimer?.invalidate(); heartbeatTimer = nil
        report = "Arrêt demandé."
    }
    // What "Partager le rapport" sends: the last image report plus live progress.
    var sharedReport: String { heartbeat.isEmpty ? report : report + "\n" + heartbeat }
    deinit { worker?.stop() }
}
#endif
