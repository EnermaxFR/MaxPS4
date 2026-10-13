import Foundation
import Combine
import UIKit

// All native resources belong to the established serial FEX queue. The lock
// carries only buttons/cancellation between UI and worker; no native pointers.
private final class MaxPS4PlayableWorker {
    private let lock = NSLock()
    private var cancelled = false
    private var buttons: UInt32 = 0
    private var timer: DispatchSourceTimer?
    private var pendingPresentation = false
#if MAXPS4_FEX_APP
    private var engine: OpaquePointer?
#endif
    func input(_ value: UInt32) { lock.lock(); buttons = value & 31; lock.unlock() }
    func stop() {
        lock.lock(); cancelled = true; lock.unlock()
        MaxPS4FEXMetalExecution.queue.async { self.close() }
    }
    private func close() {
        timer?.cancel(); timer = nil
#if MAXPS4_FEX_APP
        if let engine { maxps4_fex_engine_close(engine); self.engine = nil }
#endif
    }
    func start(url: URL, deliver: @escaping (UIImage?, String, Bool) -> Void) {
        MaxPS4FEXMetalExecution.queue.async {
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            do {
                let (bundle, data) = try MaxPS4PlayableBundle.load(url)
#if MAXPS4_FEX_APP
                var strings: [UnsafeMutablePointer<CChar>] = []
                var deps: [UnsafeMutablePointer<UnsafePointer<CChar>?>] = []
                defer { strings.forEach { free($0) }; deps.forEach { $0.deallocate() } }
                func string(_ value: String) throws -> UnsafePointer<CChar> {
                    guard let p = strdup(value) else { throw CocoaError(.fileReadUnknown) }
                    strings.append(p); return UnsafePointer(p)
                }
                let storage = data.map { $0 as NSData }
                var modules: [maxps4_orbis_module] = []
                for (i, module) in bundle.modules.enumerated() {
                    let pointers = UnsafeMutablePointer<UnsafePointer<CChar>?>.allocate(capacity: max(1,module.dependencies.count))
                    deps.append(pointers)
                    for (j, dependency) in module.dependencies.enumerated() { pointers[j] = try string(dependency) }
                    modules.append(maxps4_orbis_module(path: try string(module.path),
                        bytes: storage[i].bytes.assumingMemoryBound(to: UInt8.self), size: storage[i].length,
                        load_bias: module.loadBias, dependencies: UnsafePointer(pointers), dependency_count: module.dependencies.count))
                }
                var opened: OpaquePointer?
                let status = maxps4_fex_engine_open(&opened)
                guard status == 0, let opened else { throw NativeError(status: status, detail: "Ouverture FEX") }
                self.engine = opened
                var diagnostic = [CChar](repeating: 0, count: 1024)
                let root = try string(bundle.root)
                let prepared = withExtendedLifetime(storage) {
                    modules.withUnsafeBufferPointer { modules in
                        diagnostic.withUnsafeMutableBufferPointer { text in
                            maxps4_fex_engine_prepare_orbis(opened,modules.baseAddress,modules.count,root,65536,text.baseAddress,text.count)
                        }
                    }
                }
                guard prepared == 0 else { throw NativeError(status: prepared, detail: String(cString: diagnostic)) }
                let f = bundle.frame
                let configured = maxps4_fex_engine_configure_frame(opened,f.pixels,f.input,f.width,f.height)
                guard configured == 0 else { throw NativeError(status: configured, detail: "Framebuffer refusé") }
                let initialized = maxps4_fex_engine_initialize(opened)
                guard initialized == 0 else { throw NativeError(status: initialized, detail: "JIT indisponible : activer StikDebug puis relancer. Exécution non tentée") }
                let timer = DispatchSource.makeTimerSource(queue: MaxPS4FEXMetalExecution.queue)
                self.timer = timer
                timer.schedule(deadline: .now(), repeating: .milliseconds(33), leeway: .milliseconds(3))
                timer.setEventHandler { [weak self] in self?.step(frame: f, deliver: deliver) }
                timer.resume()
#else
                _ = bundle; _ = data
                throw NativeError(status: -95, detail: "FEX absent de cette compilation")
#endif
            } catch {
                self.close()
                DispatchQueue.main.async { deliver(nil,error.localizedDescription,false) }
            }
        }
    }
    private struct NativeError: LocalizedError {
        let status: Int32; let detail: String
        var errorDescription: String? { "\(detail) (\(status))" }
    }
#if MAXPS4_FEX_APP
    private func step(frame: MaxPS4PlayableBundle.Frame, deliver: @escaping (UIImage?, String, Bool) -> Void) {
        lock.lock(); let stopped = cancelled; let input = buttons; lock.unlock()
        if stopped { close(); return }
        // At most one snapshot awaits presentation. Skip ticks under UI pressure.
        guard !pendingPresentation, let engine else { return }
        var bytes = Data(count: Int(frame.width * frame.height * 4))
        var sequence: UInt64 = 0; var backend: UInt32 = 0
        let status = bytes.withUnsafeMutableBytes {
            maxps4_fex_engine_step_frame(engine,input,$0.bindMemory(to: UInt8.self).baseAddress,$0.count,&sequence,&backend)
        }
        guard status == 0, backend == UInt32(MAXPS4_CPU_FEX_ARM64_JIT),
              let cg = MaxPS4GuestFrameImage.make(bytes: bytes, width: frame.width, height: frame.height) else {
            close()
            DispatchQueue.main.async { deliver(nil,"Image FEX refusée (\(status)), backend=\(backend)",false) }
            return
        }
        // FNV-1a identifies the copied bytes in shareable device evidence.
        let hash = bytes.reduce(UInt64(14695981039346656037)) { ($0 ^ UInt64($1)) &* 1099511628211 }
        let image = UIImage(cgImage: cg)
        pendingPresentation = true
        DispatchQueue.main.async {
            deliver(image,"FEX ARM64 natif · image \(sequence) · entrée \(input) · RGBA FNV64 \(String(hash, radix: 16))",true)
            MaxPS4FEXMetalExecution.queue.async { self.pendingPresentation = false }
        }
    }
#endif
}

@MainActor final class MaxPS4PlayableSession: ObservableObject {
    @Published private(set) var image: UIImage?
    @Published private(set) var report = "Démo synthétique : aucun lancement effectué."
    @Published private(set) var running = false
    private var worker: MaxPS4PlayableWorker?
    private var generation = UUID()
    func start(url: URL) {
        stop()
        let id = UUID(); generation = id
        let worker = MaxPS4PlayableWorker(); self.worker = worker
        running = true; report = "Vérification SCE/PRX et préparation FEX…"
        worker.start(url: url) { [weak self] image, report, running in
            guard let self, self.generation == id else { return }
            self.image = image; self.report = report; self.running = running
        }
    }
    func input(_ buttons: UInt32) { worker?.input(buttons) }
    func importFailed(_ error: Error) { report = "Import refusé : \(error.localizedDescription)" }
    func missingDemo() { report = "Dossier PlayableDemo absent de cette compilation." }
    func stop() {
        generation = UUID(); worker?.stop(); worker = nil
        running = false; image = nil
        report = "Arrêt demandé ; libération du moteur et de la mémoire sur la file FEX."
    }
    deinit { worker?.stop() }
}
