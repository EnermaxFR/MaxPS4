import Foundation
import Combine
import Metal
#if MAXPS4_FEX_APP
import Darwin
import AVFoundation

// Game audio: an AVAudioSourceNode pulls the engine's mixed AudioOut ports
// (48 kHz stereo). The render thread never waits: while the engine is being
// detached it plays silence. After stop() returns the engine is never read.
final class MaxPS4GameAudio: @unchecked Sendable {
    private final class Tap: @unchecked Sendable {
        static let capacity = 4096
        let lock = NSLock()
        var engine: OpaquePointer?
        let mix = UnsafeMutablePointer<Float>.allocate(capacity: Tap.capacity * 2)
        deinit { mix.deallocate() }
    }
    private let tap = Tap()
    private let output = AVAudioEngine()
    // Nil on success, otherwise why playback could not start.
    func start(_ engine: OpaquePointer) -> String? {
        guard let format = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 2) else { return "format 48 kHz refusé" }
        let tap = self.tap
        let node = AVAudioSourceNode(format: format) { silence, _, frameCount, list in
            let frames = min(Int(frameCount), Tap.capacity)
            var filled: Int32 = 0
            if tap.lock.try() {
                if let engine = tap.engine { filled = maxps4_fex_engine_audio_read(engine, tap.mix, UInt32(frames)) }
                tap.lock.unlock()
            }
            if filled <= 0 { silence.pointee = true }
            for (channel, buffer) in UnsafeMutableAudioBufferListPointer(list).enumerated() {
                guard let data = buffer.mData?.assumingMemoryBound(to: Float.self) else { continue }
                let count = Int(buffer.mDataByteSize) / MemoryLayout<Float>.size
                for i in 0..<count { data[i] = filled > 0 && i < frames ? tap.mix[2 * i + min(channel, 1)] : 0 }
            }
            return noErr
        }
        output.attach(node)
        output.connect(node, to: output.mainMixerNode, format: format)
        do {
            #if os(iOS)
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
            try session.setActive(true)
            #endif
            tap.lock.lock(); tap.engine = engine; tap.lock.unlock()
            try output.start()
            return nil
        } catch {
            stop()
            return error.localizedDescription
        }
    }
    func stop() {
        output.stop()
        tap.lock.lock(); tap.engine = nil; tap.lock.unlock()
    }
}

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
    private var lockWait: TimeInterval = 0 // Last snapshot's wait for `lock`.
    private var buttons: UInt32 = 0
    private var inputEngine: OpaquePointer?
    private var displayed = 0
    private var started: TimeInterval?
    private var vblanksSent = 0
    private var lastTicket: UInt64 = 0
    private var relocation: UInt64 = 0
    private var vblankClock = MaxPS4VideoOutClock()
    private var vblankTimer: DispatchSourceTimer?
    private let audio = MaxPS4GameAudio()
    private var audioFailure: String?
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
        let now = ProcessInfo.processInfo.systemUptime
        let seconds = Int(now - started)
        var activity = [CChar](repeating: 0, count: 4096)
        _ = maxps4_fex_engine_thread_activity(engine, &activity, activity.count)
        return "Depuis le démarrage du JIT : \(seconds) s · dernier ticket VideoOut \(lastTicket) · images affichées \(displayed) · sautées \(maxps4_fex_engine_videoout_dropped(engine)) · VBlank envoyés \(vblanksSent)\n" + rates(engine, now: now) + audioStatus(engine) + "\n" + String(cString: activity) + profile(engine) + mainThreadSample(engine)
    }
    // Since the previous heartbeat: images the game asked to flip, images the
    // host displayed, and the share of the time its thread spent in Metal
    // submissions (the rest is emulated CPU and HLE calls).
    private var lastRates: (time: TimeInterval, requested: UInt64, displayed: Int, submitMs: Double)?
    private func rates(_ engine: OpaquePointer, now: TimeInterval) -> String {
        let requested = maxps4_fex_engine_videoout_requested(engine)
        let submitMs = bridge?.submitMilliseconds ?? 0
        defer { lastRates = (now, requested, displayed, submitMs) }
        guard let last = lastRates, now > last.time else { return "" }
        let span = now - last.time
        let game = Double(requested - last.requested) / span
        let shown = Double(displayed - last.displayed) / span
        let metal = (submitMs - last.submitMs) / (span * 10)
        return String(format: "Cadence : jeu %.1f images/s · affichées %.1f/s · rendu Metal %.0f %% du temps du jeu\n", game, shown, metal)
    }
    private func audioStatus(_ engine: OpaquePointer) -> String {
        if let audioFailure { return "Audio : sortie iOS indisponible (\(audioFailure))" }
        var outputs: UInt64 = 0, written: UInt64 = 0, dropped: UInt64 = 0, silent: UInt64 = 0, audible: UInt64 = 0
        var ports: UInt32 = 0
        _ = maxps4_fex_engine_audio_status(engine, &outputs, &written, &dropped, &silent, &ports, &audible)
        return "Audio : \(ports) ports · \(outputs) appels Output · \(written) trames écrites (\(audible) non muettes) · \(dropped) perdues · \(silent) silencieuses"
    }
    // The CPU profile since the run started; empty unless profiling was asked.
    private func profile(_ engine: OpaquePointer) -> String {
        var text = [CChar](repeating: 0, count: 2048)
        _ = maxps4_fex_engine_profile_report(engine, &text, text.count)
        return String(cString: text)
    }
    // One sample of where the main guest thread is (only while outside HLE).
    private func mainThreadSample(_ engine: OpaquePointer) -> String {
        var sample = maxps4_fex_thread_sample()
        let status = maxps4_fex_engine_sample_main_thread(engine, &sample)
        guard status == 0 else { return status == -EBUSY ? "" : "Échantillon thread 1 : indisponible (\(status))" }
        func text<T>(_ field: T) -> String {
            withUnsafeBytes(of: field) { String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self) }
        }
        let rip = sample.guest_rip, relative = rip >= relocation ? rip - relocation : rip
        let position = "RIP 0x\(String(rip, radix: 16)) (relatif 0x\(String(relative, radix: 16)))"
        // Method 1: the signal was never taken; the thread was read through Mach.
        let mach = sample.method == 1
        let label = mach ? "Échantillon thread 1 (Mach, signal non reçu)" : "Échantillon thread 1"
        let pc = "PC 0x\(String(sample.host_pc, radix: 16))"
        let place: String
        switch sample.location {
        case 1: place = mach ? "\(label) : code JIT (\(pc)), \(position)" : "\(label) : code JIT, \(position)"
        case 2: place = "\(label) : répartiteur FEX, dernier \(position)"
        default: place = "\(label) : code hôte \(text(sample.symbol)) appelé depuis \(text(sample.caller)), dernier \(position)"
        }
        guard mach else { return place }
        func words<T>(_ field: T) -> [UInt64] {
            withUnsafeBytes(of: field) { Array($0.bindMemory(to: UInt64.self)) }
        }
        func hex(_ value: UInt64) -> String { "0x" + String(value, radix: 16) }
        let rips = words(sample.rips).prefix(Int(sample.rip_count))
            .map { hex($0 >= relocation ? $0 - relocation : $0) }
        let registers = words(sample.registers).enumerated().map { "x\($0.offset)=\(hex($0.element))" }
        var lines = [place]
        lines.append("RIP JIT relatifs (16 lectures) : " + (rips.isEmpty ? "aucun" : rips.joined(separator: " ")))
        lines.append("Registres hôte : " + registers.joined(separator: " "))
        let frames = text(sample.backtrace)
        if !frames.isEmpty { lines.append("Pile thread 1 : " + frames) }
        return lines.joined(separator: "\n")
    }

    // Input goes straight to the engine under inputLock alone, never waiting
    // for `lock` (display copies, completions, reports). `inputEngine` is set
    // while the game runs and cleared under inputLock before the engine closes.
    func input(_ buttons: UInt32) {
        inputLock.lock(); defer { inputLock.unlock() }
        self.buttons = buttons
        if let inputEngine { _ = maxps4_fex_engine_pad_update(inputEngine, buttons, 128, 128, 128, 128, 0, 0, 1) }
    }
    private func sendInputLocked(_ engine: OpaquePointer) {
        inputLock.lock(); let latest = buttons; inputLock.unlock()
        _ = maxps4_fex_engine_pad_update(engine, latest, 128, 128, 128, 128, 0, 0, 1)
    }
    private func snapshotTarget() throws -> (maxps4_videoout_frame, UInt64, MaxPS4GameMetalBridge)? {
        let asked = ProcessInfo.processInfo.systemUptime
        lock.lock(); defer { lock.unlock() }
        lockWait = ProcessInfo.processInfo.systemUptime - asked
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
        let started = ProcessInfo.processInfo.systemUptime
        guard let (info, address, bridge) = try snapshotTarget() else { return nil }
        let located = ProcessInfo.processInfo.systemUptime
        // The retained bridge owns the texture copy. No engine pointer is used
        // here, so GPU completion need not block pad updates, vblank or stop.
        let copied = bridge.copyTarget(address: address, width: info.width, height: info.height)
        let timing = String(format: "Préparation : verrou session %.1f ms · cible %.1f ms · copie %.1f ms",
                            lockWait * 1000, (located - started - lockWait) * 1000,
                            (ProcessInfo.processInfo.systemUptime - located) * 1000)
        guard let texture = copied else {
            if missingSince == nil { missingSince = Date() }
            if Date().timeIntervalSince(missingSince!) > 5 {
                throw failure("Aucune texture Metal pour le buffer retourné", -EIO)
            }
            return nil
        }
        missingSince = nil
        return Frame(info: info, texture: texture, report: bridge.report + "\n" + timing)
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
        // Silence at once: a stop interrupts AudioOut's pacing, so until the
        // run ends the game's audio thread would queue its last buffers in a
        // tight loop and the speaker would stutter them. close() repeats this.
        audio.stop()
        inputLock.lock(); inputEngine = nil; inputLock.unlock()
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
        inputLock.lock(); inputEngine = nil; inputLock.unlock()
        lock.lock(); defer { lock.unlock() }
        ready = false
        vblankTimer?.cancel(); vblankTimer = nil
        if let engine {
            audio.stop()
            _ = maxps4_fex_engine_gnm_backend(engine, 0)
            _ = maxps4_fex_engine_gnm_host_consumer(engine, nil, nil)
            bridge = nil; self.engine = nil
            maxps4_fex_engine_close(engine)
        }
    }
    func run(url: URL, device: MTLDevice, nativeTextures: Bool = false, tso: Bool = true, outputHeight: Int = 1080, profiling: Bool = false, progress: @escaping @Sendable (String) -> Void) -> String {
        dispatchPrecondition(condition: .onQueue(MaxPS4FEXMetalExecution.queue))
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        defer { close() }
        do {
            progress("Ouverture du moteur…")
            _ = maxps4_fex_set_tso_emulation(tso ? 1 : 0)
            var opened: OpaquePointer?
            let status = maxps4_fex_engine_open(&opened)
            guard status == 0, let opened else { throw failure("Ouverture FEX", status) }
            lock.lock()
            if stopped { lock.unlock(); maxps4_fex_engine_close(opened); return "Arrêté." }
            engine = opened; lock.unlock()
            // Same permissive import probe as the validated Sonic boot probe.
            // This remains experimental: unresolved imports are reported by FEX.
            // Mode 2: stubs without the per-call stderr trace, which cost the
            // game's thread thousands of formatted lines per second.
            _ = maxps4_fex_engine_set_import_probe(opened, 2)
            var diagnostic = [CChar](repeating: 0, count: 2048)
            var relocation: UInt64 = 0
            progress("Lecture du dossier et chargement de eboot.bin et des PRX…")
            let loaded = url.path.withCString { path in
                diagnostic.withUnsafeMutableBufferPointer {
                    maxps4_fex_engine_load_game(opened, path, &relocation, $0.baseAddress, $0.count)
                }
            }
            guard loaded == 0 else { throw failure(String(cString: diagnostic), loaded) }
            // The output resolution the game is told (sceVideoOutGetResolutionStatus).
            _ = maxps4_fex_engine_videoout_output(opened, outputHeight == 720 ? 1280 : 1920, outputHeight == 720 ? 720 : 1080)
            _ = maxps4_fex_engine_set_profiling(opened, profiling ? 1 : 0)
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
            self.relocation = relocation
            let cancelled = stopped
            if ready {
                sendInputLocked(opened)
                inputLock.lock(); inputEngine = opened; inputLock.unlock()
            }
            if ready {
                let timer = DispatchSource.makeTimerSource(queue: vblanks)
                timer.schedule(deadline: .now(), repeating: 1.0 / 60, leeway: .milliseconds(1))
                timer.setEventHandler { [weak self] in self?.tick(at: ProcessInfo.processInfo.systemUptime) }
                vblankTimer = timer; timer.resume()
            }
            lock.unlock()
            if cancelled { return "Arrêté." }
            let audioError = audio.start(opened)
            lock.lock(); audioFailure = audioError; lock.unlock()
            progress("Jeu en cours d’exécution. En attente de la première image…")
            var value: UInt64 = 0, entry: UInt64 = 0
            var backend: UInt32 = 0
            let ran = maxps4_fex_engine_run_orbis(opened, &value, &backend, &entry)
            lock.lock(); let wasStopped = stopped; let frames = displayed; lock.unlock()
            var fault = maxps4_fex_guest_fault()
            _ = maxps4_fex_engine_last_guest_fault(opened, &fault)
            let relativeRIP = fault.rip >= relocation ? fault.rip - relocation : fault.rip
            // A stop interrupts the main thread: where it was shows what a stalled game waits on.
            if wasStopped {
                return "Arrêté après \(frames) images présentées. \(metal.report)\nThread principal interrompu à RIP 0x\(String(fault.rip, radix: 16)) (relatif 0x\(String(relativeRIP, radix: 16)), relocation 0x\(String(relocation, radix: 16)))."
            }
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
    private var stopping: UUID?
    private var heartbeatTimer: Timer?
    func start(url: URL, device: MTLDevice, nativeTextures: Bool = false, tso: Bool = true, outputHeight: Int = 1080, profiling: Bool = false) {
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
            let result = source.run(url: url, device: device, nativeTextures: nativeTextures, tso: tso, outputHeight: outputHeight, profiling: profiling) { [weak self] text in
                DispatchQueue.main.async { [weak self] in
                    guard let self, self.generation == id, self.running else { return }
                    self.report = text
                }
            }
            DispatchQueue.main.async {
                guard let self else { return }
                // A stopped session's result (where its main thread was) still
                // replaces "Arrêt demandé." unless another session has started.
                if self.generation == id || (self.stopping == id && !self.running) {
                    self.report = result; self.running = false
                }
            }
        }
    }
    func input(_ buttons: UInt32) { worker?.input(buttons) }
    func presented(_ text: String) { if running { hasPresented = true; report = text } }
    func failed(_ text: String) { stop(); stopping = nil; report = text }
    func stop() {
        stopping = worker == nil ? nil : generation
        generation = UUID(); worker?.stop(); worker = nil; source = nil; running = false
        heartbeatTimer?.invalidate(); heartbeatTimer = nil
        report = "Arrêt demandé."
    }
    // What "Partager le rapport" sends: the last image report plus live progress.
    var sharedReport: String { heartbeat.isEmpty ? report : report + "\n" + heartbeat }
    deinit { worker?.stop() }
}
#endif
