import SwiftUI
import MetalKit

private final class TriangleRenderer: NSObject, MTKViewDelegate {
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private let texturedPipeline: MTLRenderPipelineState
    private let checkerboard: MTLTexture
    private let alternateCheckerboard: MTLTexture
    private let quadVertices: MTLBuffer
    var renderMode = 0
    var touchOffset = SIMD2<Float>(0, 0)
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
        alternateCheckerboard = try MaxPS4MetalPipeline.makeCheckerboard(device: device, alternate: true)
        quadVertices = try MaxPS4MetalPipeline.makeQuadVertexBuffer(device: device)
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
        encoder.setRenderPipelineState(renderMode == 0 ? pipeline : texturedPipeline)
        if renderMode != 0 {
            encoder.setVertexBuffer(quadVertices, offset: 0, index: 1)
        }
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<SIMD3<Float>>.stride, index: 0)
        if renderMode == 0 {
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        } else {
            // Two separate objects, two texture bindings and two GPU draw commands.
            var left = SIMD2<Float>(-0.43 + touchOffset.x, touchOffset.y)
            encoder.setVertexBytes(&left, length: MemoryLayout<SIMD2<Float>>.stride, index: 2)
            encoder.setFragmentTexture(checkerboard, index: 0)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 6)
            var right = SIMD2<Float>(0.43, 0)
            encoder.setVertexBytes(&right, length: MemoryLayout<SIMD2<Float>>.stride, index: 2)
            encoder.setFragmentTexture(alternateCheckerboard, index: 0)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 6)
        }
        encoder.endEncoding()
        command.present(drawable)
        let mode = renderMode == 0 ? "Triangle" : "Deux rectangles texturés"
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
    let renderMode: Int
    let touchOffset: SIMD2<Float>
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
        context.coordinator.renderer?.renderMode = renderMode
        context.coordinator.renderer?.touchOffset = touchOffset
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
    @State private var renderMode = 0
    @State private var touchOffset = SIMD2<Float>(0, 0)
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
                        Picker("Démonstration", selection: $renderMode) {
                            Text("Triangle").tag(0)
                            Text("Deux textures").tag(1)
                            Text("Interactif").tag(2)
                        }
                        .pickerStyle(.segmented)
                        .onChange(of: renderMode) { _ in status = "Changement de mode Metal…" }
                        TriangleSurface(device: device,
                            active: running && visible && scenePhase == .active && !failed,
                            renderMode: renderMode, touchOffset: renderMode == 2 ? touchOffset : SIMD2<Float>(0, 0)) { message, error in
                                if running || error { status = message }
                                failed = error
                                if error { running = false }
                            }
                            .frame(height: 300)
                            .contentShape(Rectangle())
                            .gesture(DragGesture(minimumDistance: 0).onChanged { gesture in
                                guard renderMode == 2 else { return }
                                let delta = gesture.translation
                                touchOffset = SIMD2<Float>(
                                    max(-0.55, min(0.55, Float(delta.width) / 170)),
                                    max(-0.65, min(0.65, -Float(delta.height) / 170)))
                            })
                            .clipShape(RoundedRectangle(cornerRadius: 16))
                        if renderMode == 2 {
                            Text("Glisse ton doigt sur l’image pour déplacer le rectangle bleu.")
                            Button("Recentrer") { touchOffset = SIMD2<Float>(0, 0) }
                        }
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
