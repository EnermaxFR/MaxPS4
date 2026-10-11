import SwiftUI
import MetalKit

private final class TriangleRenderer: NSObject, MTKViewDelegate {
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private let texturedPipeline: MTLRenderPipelineState
    private let checkerboard: MTLTexture
    var textured = false
    private var elapsed: TimeInterval = 0
    private var previousTime: TimeInterval?
    private var reportedFrame = false
    var report: (String, Bool) -> Void

    init(device: MTLDevice, report: @escaping (String, Bool) -> Void) throws {
        guard let queue = device.makeCommandQueue() else {
            throw NSError(domain: "MaxPS4.Metal", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "Création de la file GPU impossible"])
        }
        self.queue = queue
        pipeline = try MaxPS4MetalPipeline.make(device: device)
        texturedPipeline = try MaxPS4MetalPipeline.makeTextured(device: device)
        checkerboard = try MaxPS4MetalPipeline.makeCheckerboard(device: device)
        self.report = report
        super.init()
    }

    func pause() { previousTime = nil }
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}
    func draw(in view: MTKView) {
        guard view.drawableSize.width > 0, view.drawableSize.height > 0,
              let pass = view.currentRenderPassDescriptor, let drawable = view.currentDrawable else { return }
        guard let command = queue.makeCommandBuffer(), let encoder = command.makeRenderCommandEncoder(descriptor: pass) else {
            view.isPaused = true
            report("Erreur : création de la commande GPU impossible", true)
            return
        }
        let now = ProcessInfo.processInfo.systemUptime
        if let previousTime { elapsed += min(now - previousTime, 0.1) }
        previousTime = now
        var uniforms = MaxPS4MetalPipeline.uniforms(elapsed: elapsed,
            width: Double(view.drawableSize.width), height: Double(view.drawableSize.height))
        encoder.setRenderPipelineState(textured ? texturedPipeline : pipeline)
        if textured { encoder.setFragmentTexture(checkerboard, index: 0) }
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<SIMD3<Float>>.stride, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: textured ? 6 : 3)
        encoder.endEncoding()
        command.present(drawable)
        let mode = textured ? "Texture" : "Triangle"
        let first = !reportedFrame
        reportedFrame = true
        command.addCompletedHandler { [weak self] buffer in
            let error = buffer.error?.localizedDescription
            DispatchQueue.main.async { [weak self, weak view] in
                if let error {
                    view?.isPaused = true
                    self?.report("Erreur GPU : \(error)", true)
                } else if first { self?.report("\(mode) rendue — commande Metal terminée", false) }
            }
        }
        command.commit()
    }
}

private struct TriangleSurface: UIViewRepresentable {
    let device: MTLDevice
    let active: Bool
    let textured: Bool
    let report: (String, Bool) -> Void

    final class Coordinator {
        var renderer: TriangleRenderer?
    }
    func makeCoordinator() -> Coordinator { Coordinator() }
    func makeUIView(context: Context) -> MTKView {
        let view = MTKView(frame: .zero, device: device)
        view.colorPixelFormat = .bgra8Unorm
        view.clearColor = MTLClearColorMake(0.02, 0.03, 0.08, 1)
        view.preferredFramesPerSecond = 60
        view.isPaused = true
        do {
            context.coordinator.renderer = try TriangleRenderer(device: device, report: report)
            view.delegate = context.coordinator.renderer
        } catch {
            DispatchQueue.main.async { report("Erreur Metal : \(error.localizedDescription)", true) }
        }
        return view
    }
    func updateUIView(_ view: MTKView, context: Context) {
        context.coordinator.renderer?.report = report
        context.coordinator.renderer?.textured = textured
        view.isPaused = !active || context.coordinator.renderer == nil
        if !active { context.coordinator.renderer?.pause() }
    }
    static func dismantleUIView(_ view: MTKView, coordinator: Coordinator) {
        view.isPaused = true
        view.delegate = nil
        view.releaseDrawables()
        coordinator.renderer = nil
    }
}

struct MaxPS4MetalDiagnosticView: View {
    @Environment(\.dismiss) private var dismiss
    @Environment(\.scenePhase) private var scenePhase
    @State private var device = MTLCreateSystemDefaultDevice()
    @State private var running = false
    @State private var textured = false
    @State private var visible = false
    @State private var failed = false
    @State private var status = "Prêt — rendu arrêté"

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    Text("Prototype GPU Apple Metal").font(.headline)
                    Text("Test de rendu natif sur iPhone. Le triangle et la texture ne sont pas une émulation GPU PS4 : commandes GCN, traduction des shaders et mémoire graphique PS4 restent à développer.")
                    Text(device.map { "Metal disponible : \($0.name)" } ?? "Metal indisponible sur cet appareil")
                        .accessibilityIdentifier("maxps4.metalAvailability")
                    if let device {
                        Picker("Démonstration", selection: $textured) {
                            Text("Triangle").tag(false)
                            Text("Texture").tag(true)
                        }
                        .pickerStyle(.segmented)
                        .onChange(of: textured) { _ in status = "Changement de mode Metal…" }
                        TriangleSurface(device: device,
                            active: running && visible && scenePhase == .active && !failed,
                            textured: textured) { message, error in
                                if running || error { status = message }
                                failed = error
                                if error { running = false }
                            }
                            .frame(height: 300)
                            .clipShape(RoundedRectangle(cornerRadius: 16))
                        Button(running ? "Arrêter le rendu" : "Démarrer le rendu") {
                            running.toggle()
                            status = running ? "Soumission du rendu Metal…" : "Rendu arrêté"
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(failed)
                        .accessibilityIdentifier("maxps4.metalToggle")
                        Text(running && scenePhase != .active ? "Rendu suspendu en arrière-plan" : status)
                            .foregroundStyle(failed ? .orange : .primary)
                            .accessibilityIdentifier("maxps4.metalStatus")
                    } else {
                        Text("Rendu désactivé : aucun périphérique Metal. Aucun rendu PS4 de remplacement.")
                            .foregroundStyle(.orange)
                    }
                }.padding()
            }
            .navigationTitle("Diagnostic GPU")
            .toolbar { ToolbarItem(placement: .topBarTrailing) { Button("Fermer") { dismiss() } } }
            .onAppear { visible = true }
            .onDisappear { visible = false; running = false }
        }
    }
}
