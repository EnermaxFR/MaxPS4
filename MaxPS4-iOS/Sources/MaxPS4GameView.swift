import SwiftUI
import MetalKit
import GameController

#if MAXPS4_FEX_APP
private final class GameMetalDisplay: NSObject, MTKViewDelegate {
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private var source: MaxPS4GameSource?
    private var presentation: MaxPS4VideoOutPresentation?
    private var activeID: UUID?
    private var lastTexture: MTLTexture?
    var report: (String) -> Void
    var failed: (String) -> Void
    init(device: MTLDevice, report: @escaping (String) -> Void, failed: @escaping (String) -> Void) throws {
        guard let queue = device.makeCommandQueue() else { throw NSError(domain: "MaxPS4.Game", code: -12) }
        self.queue = queue; self.pipeline = try MaxPS4VideoOutMetal.make(device: device)
        self.report = report; self.failed = failed
        super.init()
    }
    func use(_ source: MaxPS4GameSource?) {
        if self.source !== source { cancel(); self.source = source; lastTexture = nil }
    }
    func cancel() { presentation?.cancel(); presentation = nil; activeID = nil }
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}
    func draw(in view: MTKView) {
        guard activeID == nil, view.drawableSize.width > 0, view.drawableSize.height > 0 else { return }
        var frame: MaxPS4GameSource.Frame?
        do { frame = try source?.snapshot() }
        catch { failed(error.localizedDescription); return }
        guard let pass = view.currentRenderPassDescriptor, let drawable = view.currentDrawable else { return }
        guard let command = queue.makeCommandBuffer(), let encoder = command.makeRenderCommandEncoder(descriptor: pass) else {
            if let frame { source?.complete(ticket: frame.info.ticket, presented: false) }
            failed("Création de la présentation Metal impossible."); return
        }
        let texture = frame?.texture ?? lastTexture
        if let texture {
            MaxPS4VideoOutMetal.encode(encoder: encoder, pipeline: pipeline, texture: texture, output: view.drawableSize)
        }
        encoder.endEncoding()
        if let frame, let source {
            let id = UUID(); activeID = id
            let gate = MaxPS4VideoOutPresentation { [weak self, source] displayed in
                let accepted = source.complete(ticket: frame.info.ticket, presented: displayed)
                DispatchQueue.main.async { [weak self] in
                    guard let self, self.activeID == id else { return }
                    self.activeID = nil; self.presentation = nil
                    if accepted { self.report("Image \(frame.info.ticket) · \(frame.report)") }
                    else { self.failed("Présentation Metal interrompue ; session arrêtée.") }
                }
            }
            presentation = gate
            drawable.addPresentedHandler { gate.drawablePresented(at: $0.presentedTime) }
            command.addCompletedHandler { gate.gpuFinished($0.status == .completed && $0.error == nil) }
            DispatchQueue.global().asyncAfter(deadline: .now() + 3) { gate.cancel() }
        }
        lastTexture = texture
        command.present(drawable); command.commit()
    }
}

private struct GameMetalSurface: UIViewRepresentable {
    let device: MTLDevice
    let source: MaxPS4GameSource?
    let active: Bool
    let report: (String) -> Void
    let failed: (String) -> Void
    final class Coordinator { var renderer: GameMetalDisplay? }
    func makeCoordinator() -> Coordinator { Coordinator() }
    func makeUIView(context: Context) -> MTKView {
        let view = MTKView(frame: .zero, device: device)
        view.colorPixelFormat = .bgra8Unorm_srgb
        view.clearColor = MTLClearColorMake(0, 0, 0, 1)
        view.preferredFramesPerSecond = 60; view.isPaused = true
        do {
            context.coordinator.renderer = try GameMetalDisplay(device: device, report: report, failed: failed)
            view.delegate = context.coordinator.renderer
        } catch { DispatchQueue.main.async { failed(error.localizedDescription) } }
        return view
    }
    func updateUIView(_ view: MTKView, context: Context) {
        context.coordinator.renderer?.report = report
        context.coordinator.renderer?.failed = failed
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
#if MAXPS4_FEX_APP
    @StateObject private var session = MaxPS4GameSession()
    @State private var device = MTLCreateSystemDefaultDevice()
    @State private var touch: UInt32 = 0
    @State private var hasLaunched = false
    private let poll = Timer.publish(every: 1.0 / 60, on: .main, in: .common).autoconnect()
#endif
    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
#if MAXPS4_FEX_APP
            if let device {
                GameMetalSurface(device: device, source: session.source,
                    active: session.running && scenePhase == .active,
                    report: session.presented, failed: session.failed)
                    .ignoresSafeArea()
                    .accessibilityIdentifier("maxps4.game.surface")
                if session.running && !showingMenu { screenControls }
                if !session.running {
                    VStack(spacing: 18) {
                        Text(directory.lastPathComponent).font(.title2.bold())
                        if hasLaunched {
                            Text("Session terminée. Consulte le menu pour les détails.")
                                .font(.footnote).foregroundStyle(.secondary)
                        }
                        Button(hasLaunched ? "Rejouer" : "Jouer") {
                            touch = 0; hasLaunched = true
                            session.start(url: directory, device: device)
                        }.buttonStyle(.borderedProminent).tint(.cyan)
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
                            if session.running {
                                Button("Arrêter la session", role: .destructive) {
                                    touch = 0; session.stop(); showingMenu = false
                                }
                            }
                            DisclosureGroup("Détails de la session") {
                                Text(session.report).font(.footnote.monospaced()).textSelection(.enabled)
                                ShareLink(item: session.report) { Label("Partager le rapport", systemImage: "square.and.arrow.up") }
                            }.tint(.cyan)
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
        .onDisappear { touch = 0; session.stop() }
        .onChange(of: scenePhase) { _, phase in if phase != .active { touch = 0; session.stop() } }
#endif
    }
#if MAXPS4_FEX_APP
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

    private func control(_ name: String, label: String, bit: UInt32, size: CGFloat) -> some View {
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
