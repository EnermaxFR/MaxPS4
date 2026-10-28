import SwiftUI
import MetalKit
import GameController

#if MAXPS4_FEX_APP
private final class GameMetalDisplay: NSObject, MTKViewDelegate {
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private var source: MaxPS4GameSource?
    private var lastTexture: MTLTexture?
    private let snapshots = DispatchQueue(label: "com.enermaxfr.maxps4.display-copy", qos: .userInteractive)
    private var snapshotID: UUID?
    private var pendingTexture: MTLTexture?
    private var reportTime = ProcessInfo.processInfo.systemUptime
    private var reportFrames = 0
    // MTKView refresh callbacks (not the redraws a finished copy asks for):
    // the screen's real rate, 30 Hz in Low Power Mode.
    private var refreshes = 0, manualDraw = false
    private var preparationSeconds = 0.0
    private var needsRedraw = true
    // "fit" (whole image, black bars), "fill" (cropped) or "stretch".
    var scaling = "fit" { didSet { if scaling != oldValue { needsRedraw = true } } }
    var report: (String) -> Void
    var failed: (String) -> Void
    init(device: MTLDevice, report: @escaping (String) -> Void, failed: @escaping (String) -> Void) throws {
        guard let queue = device.makeCommandQueue() else { throw NSError(domain: "MaxPS4.Game", code: -12) }
        self.queue = queue; self.pipeline = try MaxPS4VideoOutMetal.make(device: device)
        self.report = report; self.failed = failed
        super.init()
    }
    func use(_ source: MaxPS4GameSource?) {
        if self.source !== source {
            cancel(); self.source = source; lastTexture = nil
            needsRedraw = true
            reportTime = ProcessInfo.processInfo.systemUptime; reportFrames = 0; preparationSeconds = 0; refreshes = 0
        }
    }
    func cancel() {
        snapshotID = nil; pendingTexture = nil
    }
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) { needsRedraw = true }
    // Each flip is copied into an owned texture and handed back to the game at
    // once (completed as presented), so the game never waits for the screen:
    // the next copy starts while this one is shown. Every refresh shows the
    // newest copy; flips the copy loop misses are skipped by VideoOut.
    func draw(in view: MTKView) {
        if !manualDraw { refreshes += 1 }
        guard !view.isPaused, view.drawableSize.width > 0, view.drawableSize.height > 0, let source else { return }
        // Acquire the drawable and encode only inside MTKView's draw cycle.
        if let texture = pendingTexture {
            pendingTexture = nil
            present(texture, in: view)
        } else if needsRedraw, let lastTexture {
            present(lastTexture, in: view)
        }
        guard snapshotID == nil else { return }
        let id = UUID(); snapshotID = id
        snapshots.async { [weak self, weak view, source] in
            let started = ProcessInfo.processInfo.systemUptime
            let result = Result { try source.snapshot() }
            if case .success(let frame?) = result { source.complete(ticket: frame.info.ticket, presented: true) }
            let elapsed = ProcessInfo.processInfo.systemUptime - started
            DispatchQueue.main.async { [weak self, weak view] in
                guard let self, let view, self.snapshotID == id, self.source === source, !view.isPaused else { return }
                self.snapshotID = nil
                switch result {
                case .success(let frame):
                    guard let frame else { return }
                    self.preparationSeconds += elapsed
                    self.reportFrames += 1
                    let now = ProcessInfo.processInfo.systemUptime
                    let span = now - self.reportTime
                    // Report once per second instead of invalidating the
                    // entire SwiftUI game view for every displayed frame.
                    if span >= 1 {
                        let fps = Double(self.reportFrames) / span
                        let ms = self.preparationSeconds * 1000 / Double(self.reportFrames)
                        let hertz = Double(self.refreshes) / span
                        let power = ProcessInfo.processInfo.isLowPowerModeEnabled ? " (économie d’énergie)" : ""
                        self.report(String(format: "%.1f images/s · écran %.0f Hz%@ · préparation image %.2f ms · ", fps, hertz, power, ms) + frame.report)
                        self.reportTime = now; self.reportFrames = 0; self.preparationSeconds = 0; self.refreshes = 0
                    }
                    self.pendingTexture = frame.texture
                    // Show it now and start the next copy, rather than losing
                    // a refresh interval before the MTKView timer runs.
                    self.manualDraw = true; view.draw(); self.manualDraw = false
                case .failure(let error): self.failed(error.localizedDescription)
                }
            }
        }
    }
    private func present(_ texture: MTLTexture, in view: MTKView) {
        guard let pass = view.currentRenderPassDescriptor, let drawable = view.currentDrawable else {
            pendingTexture = texture // Retry on the next draw cycle.
            return
        }
        guard let command = queue.makeCommandBuffer(), let encoder = command.makeRenderCommandEncoder(descriptor: pass) else {
            failed("Création de la présentation Metal impossible."); return
        }
        MaxPS4VideoOutMetal.encode(encoder: encoder, pipeline: pipeline, texture: texture,
            output: view.drawableSize, fill: scaling == "fill", stretch: scaling == "stretch")
        encoder.endEncoding()
        command.addCompletedHandler { [weak self] buffer in
            guard buffer.status != .completed || buffer.error != nil else { return }
            let reason = buffer.error?.localizedDescription ?? "statut \(buffer.status.rawValue)"
            DispatchQueue.main.async { self?.failed("Présentation Metal : échec de la commande GPU (\(reason)).") }
        }
        lastTexture = texture
        needsRedraw = false
        command.present(drawable); command.commit()
    }
}

private struct GameMetalSurface: UIViewRepresentable {
    let device: MTLDevice
    let source: MaxPS4GameSource?
    let active: Bool
    let scaling: String
    let report: (String) -> Void
    let failed: (String) -> Void
    final class Coordinator { var renderer: GameMetalDisplay? }
    func makeCoordinator() -> Coordinator { Coordinator() }
    func makeUIView(context: Context) -> MTKView {
        let view = MTKView(frame: .zero, device: device)
        view.colorPixelFormat = .bgra8Unorm_srgb
        view.clearColor = MTLClearColorMake(0, 0, 0, 1)
        // 120 Hz where the screen allows it (more chances to show a new copy
        // early) and two drawables instead of three: one less queued image.
        view.preferredFramesPerSecond = 120; view.isPaused = true
        (view.layer as? CAMetalLayer)?.maximumDrawableCount = 2
        do {
            context.coordinator.renderer = try GameMetalDisplay(device: device, report: report, failed: failed)
            view.delegate = context.coordinator.renderer
        } catch { DispatchQueue.main.async { failed(error.localizedDescription) } }
        return view
    }
    func updateUIView(_ view: MTKView, context: Context) {
        context.coordinator.renderer?.report = report
        context.coordinator.renderer?.failed = failed
        context.coordinator.renderer?.scaling = scaling
        context.coordinator.renderer?.use(source)
        view.isPaused = !active || context.coordinator.renderer == nil
        if !active { context.coordinator.renderer?.cancel() }
    }
    static func dismantleUIView(_ view: MTKView, coordinator: Coordinator) {
        view.isPaused = true; view.delegate = nil
        coordinator.renderer?.cancel(); coordinator.renderer?.use(nil)
        view.releaseDrawables(); coordinator.renderer = nil
    }
}
#endif

struct MaxPS4GameView: View {
    let directory: URL
    @Environment(\.dismiss) private var dismiss
    @Environment(\.scenePhase) private var scenePhase
    @State private var showingMenu = false
    @State private var showingDetails = false
    @State private var details = ""
#if MAXPS4_FEX_APP
    @StateObject private var session = MaxPS4GameSession()
    @State private var device = MTLCreateSystemDefaultDevice()
    @State private var touch: UInt32 = 0
    @State private var hasLaunched = false
    @State private var gamepad = MaxPS4GameView.gamepadConnected()
    @AppStorage("maxps4.metal.nativeTextures") private var nativeTextures = false
    @AppStorage("maxps4.fex.tso") private var tso = true
    @AppStorage("maxps4.display.scaling") private var scaling = "fit"
    @AppStorage("maxps4.video.height") private var outputHeight = 1080
    @AppStorage("maxps4.fex.profile") private var profiling = false
    private let poll = Timer.publish(every: 1.0 / 60, on: .main, in: .common).autoconnect()
#endif
    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
#if MAXPS4_FEX_APP
            if let device {
                GameMetalSurface(device: device, source: session.source,
                    active: session.running && scenePhase == .active, scaling: scaling,
                    report: session.presented, failed: session.failed)
                    .ignoresSafeArea()
                    .accessibilityIdentifier("maxps4.game.surface")
                // A connected controller replaces the touch controls.
                if session.running && !showingMenu && !gamepad { screenControls }
                if session.running && !session.hasPresented {
                    VStack(spacing: 12) {
                        ProgressView().tint(.cyan)
                        Text(session.report).font(.footnote).multilineTextAlignment(.center)
                    }
                    .padding(20)
                    .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 20))
                    .padding(30).allowsHitTesting(false)
                }
                if !session.running {
                    VStack(spacing: 18) {
                        Text(directory.lastPathComponent).font(.title2.bold())
                        if hasLaunched {
                            Text("Session terminée. Consulte le menu pour les détails.")
                                .font(.footnote).foregroundStyle(.secondary)
                        }
                        Button(hasLaunched ? "Rejouer" : "Jouer") {
                            touch = 0; hasLaunched = true
                            session.start(url: directory, device: device, nativeTextures: nativeTextures, tso: tso,
                                          outputHeight: outputHeight, profiling: profiling)
                        }.buttonStyle(.borderedProminent).tint(.cyan)
                        Picker("Affichage", selection: $scaling) {
                            Text("Ajuster").tag("fit")
                            Text("Remplir").tag("fill")
                            Text("Étirer").tag("stretch")
                        }.pickerStyle(.segmented).frame(maxWidth: 320)
                        Picker("Résolution du jeu", selection: $outputHeight) {
                            Text("1080p").tag(1080)
                            Text("720p").tag(720)
                        }.pickerStyle(.segmented).frame(maxWidth: 320)
                        Text("Résolution annoncée au jeu : 720p peut alléger le rendu si le jeu s’y adapte.")
                            .font(.caption2).foregroundStyle(.secondary).multilineTextAlignment(.center).frame(maxWidth: 320)
                        Toggle("Textures Metal natives (expérimental)", isOn: $nativeTextures)
                            .font(.footnote).frame(maxWidth: 320)
                        Toggle("Ordre mémoire x86 (TSO)", isOn: $tso)
                            .font(.footnote).frame(maxWidth: 320)
                        Toggle("Profileur processeur (diagnostic)", isOn: $profiling)
                            .font(.footnote).frame(maxWidth: 320)
                        Text("Tourne l’iPhone pour jouer en paysage.")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                    .padding(28)
                    .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 24))
                    .padding(24)
                }
            } else { Text("Metal indisponible sur cet appareil.") }
#else
            Text("Le runtime FEX n’est pas inclus dans cette compilation.")
#endif
            VStack {
                HStack {
                    Button {
#if MAXPS4_FEX_APP
                        touch = 0; session.input(0)
#endif
                        showingMenu = true
                    } label: {
                        Image(systemName: "ellipsis")
                            .font(.headline).frame(width: 44, height: 44)
                            .background(.black.opacity(0.35), in: Circle())
                            .overlay(Circle().stroke(.cyan.opacity(0.4)))
                    }
                    .accessibilityLabel("Menu du jeu")
                    .accessibilityIdentifier("maxps4.game.menu")
                    Spacer()
                }
                Spacer()
            }.padding(12)
        }
        .preferredColorScheme(.dark)
        .statusBarHidden(true)
        .persistentSystemOverlays(.hidden)
        .sheet(isPresented: $showingMenu) {
            NavigationStack {
                ZStack {
                    MaxPS4Background()
                    ScrollView {
                        VStack(alignment: .leading, spacing: 22) {
                            Text(directory.lastPathComponent).font(.title2.bold())
                            Button("Revenir au jeu") { showingMenu = false }
                                .buttonStyle(.borderedProminent).tint(.cyan)
#if MAXPS4_FEX_APP
                            Picker("Affichage", selection: $scaling) {
                                Text("Ajuster").tag("fit")
                                Text("Remplir").tag("fill")
                                Text("Étirer").tag("stretch")
                            }.pickerStyle(.segmented)
                            if session.running {
                                Button("Arrêter la session", role: .destructive) {
                                    touch = 0; session.stop(); showingMenu = false
                                }
                            }
                            // A snapshot of the report, refreshed on demand: the live
                            // report changes every second while the game runs.
                            DisclosureGroup("Détails de la session", isExpanded: $showingDetails) {
                                VStack(alignment: .leading, spacing: 12) {
                                    Text(verbatim: details).font(.footnote.monospaced())
                                    HStack {
                                        Button("Actualiser") { details = session.sharedReport }
                                        Spacer()
                                        ShareLink(item: details) { Label("Partager le rapport", systemImage: "square.and.arrow.up") }
                                    }
                                }.padding(.top, 8)
                            }
                            .tint(.cyan)
                            .onChange(of: showingDetails) { _, open in if open { details = session.sharedReport } }
#endif
                            Button("Quitter le jeu", role: .destructive) { dismiss() }
                        }.padding(24).frame(maxWidth: .infinity, alignment: .leading)
                    }
                }
                .navigationTitle("Menu du jeu").navigationBarTitleDisplayMode(.inline)
                .toolbar { Button("Fermer") { showingMenu = false } }
            }.preferredColorScheme(.dark)
        }
#if MAXPS4_FEX_APP
        .onReceive(poll) { _ in
            guard session.running, scenePhase == .active, !showingMenu else { return }
            var buttons = touch
            for controller in GCController.controllers() {
                if let pad = controller.extendedGamepad {
                    if pad.dpad.left.isPressed || pad.leftThumbstick.xAxis.value < -0.3 { buttons |= 0x80 }
                    if pad.dpad.right.isPressed || pad.leftThumbstick.xAxis.value > 0.3 { buttons |= 0x20 }
                    if pad.dpad.up.isPressed || pad.leftThumbstick.yAxis.value > 0.3 { buttons |= 0x10 }
                    if pad.dpad.down.isPressed || pad.leftThumbstick.yAxis.value < -0.3 { buttons |= 0x40 }
                    if pad.buttonA.isPressed { buttons |= 0x4000 }
                    if pad.buttonB.isPressed { buttons |= 0x2000 }
                    if pad.buttonX.isPressed { buttons |= 0x8000 }
                    if pad.buttonY.isPressed { buttons |= 0x1000 }
                    if pad.buttonMenu.isPressed { buttons |= 0x8 }
                } else if let pad = controller.microGamepad {
                    if pad.dpad.xAxis.value < -0.3 { buttons |= 0x80 }
                    if pad.dpad.xAxis.value > 0.3 { buttons |= 0x20 }
                    if pad.dpad.yAxis.value > 0.3 { buttons |= 0x10 }
                    if pad.dpad.yAxis.value < -0.3 { buttons |= 0x40 }
                    if pad.buttonA.isPressed { buttons |= 0x4000 }
                    if pad.buttonX.isPressed { buttons |= 0x8000 }
                    if pad.buttonMenu.isPressed { buttons |= 0x8 }
                }
            }
            session.input(buttons)
        }
        .onReceive(NotificationCenter.default.publisher(for: .GCControllerDidConnect)) { _ in
            gamepad = Self.gamepadConnected(); if gamepad { touch = 0 }
        }
        .onReceive(NotificationCenter.default.publisher(for: .GCControllerDidDisconnect)) { _ in
            gamepad = Self.gamepadConnected()
        }
        .onDisappear { touch = 0; session.stop(); UIApplication.shared.isIdleTimerDisabled = false }
        .onChange(of: scenePhase) { _, phase in if phase != .active { touch = 0; session.stop() } }
        // A running game keeps the screen awake: auto-lock would stop it.
        .onChange(of: session.running, initial: true) { _, running in UIApplication.shared.isIdleTimerDisabled = running }
#endif
    }
#if MAXPS4_FEX_APP
    private static func gamepadConnected() -> Bool {
        GCController.controllers().contains { $0.extendedGamepad != nil || $0.microGamepad != nil }
    }
    private var screenControls: some View {
        GeometryReader { geometry in
            let size = max(44, min(56, (geometry.size.width - 32) / 6))
            VStack(spacing: 0) {
                control("Options", label: "Options", bit: 0x8, size: 44)
                Spacer(minLength: 0)
            }.frame(maxWidth: .infinity)
            HStack(alignment: .bottom, spacing: 0) {
                VStack(spacing: 0) {
                    control("↑", label: "Haut", bit: 0x10, size: size)
                    HStack(spacing: 0) {
                        control("←", label: "Gauche", bit: 0x80, size: size)
                        Color.clear.frame(width: size, height: size).allowsHitTesting(false)
                        control("→", label: "Droite", bit: 0x20, size: size)
                    }
                    control("↓", label: "Bas", bit: 0x40, size: size)
                }
                Spacer(minLength: 0)
                VStack(spacing: 0) {
                    control("△", label: "Triangle", bit: 0x1000, size: size)
                    HStack(spacing: 0) {
                        control("□", label: "Carré", bit: 0x8000, size: size)
                        Color.clear.frame(width: size, height: size).allowsHitTesting(false)
                        control("○", label: "Rond", bit: 0x2000, size: size)
                    }
                    control("✕", label: "Croix, sauter", bit: 0x4000, size: size)
                }
            }.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
        }
        .padding(6)
        .accessibilityIdentifier("maxps4.game.controls")
    }

    private func control(_ name: String, label: LocalizedStringKey, bit: UInt32, size: CGFloat) -> some View {
        let pressed = touch & bit != 0
        return Text(name)
            .font(.system(size: name == "Options" ? 10 : 22, weight: .bold, design: .rounded))
            .foregroundStyle(.white.opacity(pressed ? 1 : 0.85))
            .frame(width: size, height: size)
            .background(pressed ? Color.cyan.opacity(0.50) : Color.black.opacity(0.25), in: Circle())
            .overlay(Circle().stroke(pressed ? Color.cyan : Color.white.opacity(0.35), lineWidth: 1))
            .shadow(color: .purple.opacity(pressed ? 0.45 : 0.15), radius: 6)
            .contentShape(Circle())
            .accessibilityLabel(label).accessibilityAddTraits(.isButton)
            .accessibilityAction {
                touch |= bit; session.input(touch)
                touch &= ~bit
            }
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { _ in touch |= bit; session.input(touch) }
                .onEnded { _ in touch &= ~bit; session.input(touch) })
    }
#endif
}
