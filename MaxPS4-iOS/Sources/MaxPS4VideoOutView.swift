import SwiftUI
import MetalKit

#if MAXPS4_FEX_APP
private final class VideoOutRenderer: NSObject, MTKViewDelegate {
    private let device: MTLDevice
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private var source: MaxPS4VideoOutSource?
    private var presentation: MaxPS4VideoOutPresentation?
    private var activeID: UUID?
    private var lastTexture: MTLTexture?
    var report: (String) -> Void

    init(device: MTLDevice, report: @escaping (String) -> Void) throws {
        self.device = device
        guard let queue = device.makeCommandQueue() else { throw NSError(domain: "MaxPS4.VideoOut", code: -12) }
        self.queue = queue; self.pipeline = try MaxPS4VideoOutMetal.make(device: device)
        self.report = report
        super.init()
    }
    func use(_ source: MaxPS4VideoOutSource?) {
        if self.source !== source {
            cancel(); self.source = source; lastTexture = nil
        }
    }
    func cancel() { presentation?.cancel(); presentation = nil; activeID = nil }
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}
    func draw(in view: MTKView) {
        guard view.drawableSize.width > 0, view.drawableSize.height > 0 else { return }
        // Do not acquire another drawable while a guest presentation is in flight.
        guard activeID == nil else { return }
        var frame: MaxPS4VideoOutSource.Frame?
        do { frame = try source?.snapshot() }
        catch { report(error.localizedDescription); return }
        guard let pass = view.currentRenderPassDescriptor, let drawable = view.currentDrawable else { return }
        do {
            var texture = lastTexture
            if let frame {
                texture = try MaxPS4VideoOutMetal.texture(device: device, bytes: frame.bytes,
                    width: frame.info.width, height: frame.info.height,
                    pitch: frame.info.pitch_in_pixels, format: frame.info.pixel_format)
            }
            guard let command = queue.makeCommandBuffer(), let encoder = command.makeRenderCommandEncoder(descriptor: pass) else {
                throw NSError(domain: "MaxPS4.VideoOut", code: -12)
            }
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
                        self.report(accepted ? "Drawable présenté, flip invité confirmé." : "Présentation annulée ou échouée ; flip non confirmé.")
                    }
                }
                presentation = gate
                drawable.addPresentedHandler { gate.drawablePresented(at: $0.presentedTime) }
                command.addCompletedHandler { gate.gpuFinished($0.status == .completed && $0.error == nil) }
                // A hidden/dropped drawable may never produce a positive presentation.
                DispatchQueue.global().asyncAfter(deadline: .now() + 3) { gate.cancel() }
            }
            lastTexture = texture
            command.present(drawable)
            command.commit()
        } catch {
            if let frame { source?.complete(ticket: frame.info.ticket, presented: false) }
            report("Erreur Metal : \(error.localizedDescription)")
        }
    }
}

private struct VideoOutSurface: UIViewRepresentable {
    let device: MTLDevice
    let source: MaxPS4VideoOutSource?
    let active: Bool
    let report: (String) -> Void
    final class Coordinator { var renderer: VideoOutRenderer? }
    func makeCoordinator() -> Coordinator { Coordinator() }
    func makeUIView(context: Context) -> MTKView {
        let view = MTKView(frame: .zero, device: device)
        view.colorPixelFormat = .bgra8Unorm_srgb
        view.clearColor = MTLClearColorMake(0, 0, 0, 1)
        view.preferredFramesPerSecond = 60; view.isPaused = true
        do {
            context.coordinator.renderer = try VideoOutRenderer(device: device, report: report)
            view.delegate = context.coordinator.renderer
        } catch { DispatchQueue.main.async { report("Metal indisponible : \(error.localizedDescription)") } }
        return view
    }
    func updateUIView(_ view: MTKView, context: Context) {
        context.coordinator.renderer?.report = report
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

struct MaxPS4VideoOutView: View {
    @Environment(\.scenePhase) private var scenePhase
#if MAXPS4_FEX_APP
    @StateObject private var session = MaxPS4VideoOutSession()
    @State private var device = MTLCreateSystemDefaultDevice()
#endif
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                Text("Deux pixels écrits par un programme x86-64 intégré, agrandis avec Metal. La confirmation de flip attend la présentation du drawable. Ce test ne lance pas de jeu PS4.")
                Text("Activer le JIT avec StikDebug avant de démarrer.").font(.footnote)
#if MAXPS4_FEX_APP
                if let device {
                    VideoOutSurface(device: device, source: session.source,
                        active: session.running && scenePhase == .active, report: session.rendererReport)
                        .frame(height: 260)
                        .accessibilityIdentifier("maxps4.videoout.surface")
                    Button(session.running ? "Arrêter" : "Tester VideoOut → Metal") {
                        if session.running { session.stop() } else { session.start() }
                    }
                    .buttonStyle(.borderedProminent)
                    .accessibilityIdentifier("maxps4.videoout.start")
                } else { Text("Metal indisponible sur cet appareil.") }
                Text(session.report).textSelection(.enabled)
                ShareLink(item: session.report) { Label("Partager le résultat", systemImage: "square.and.arrow.up") }
#else
                Text("Le runtime FEX n’est pas inclus dans cette compilation.")
#endif
            }.padding()
        }
        .navigationTitle("VideoOut → Metal")
#if MAXPS4_FEX_APP
        .onDisappear { session.stop() }
        .onChange(of: scenePhase) { _, phase in if phase != .active { session.stop() } }
#endif
    }
}
