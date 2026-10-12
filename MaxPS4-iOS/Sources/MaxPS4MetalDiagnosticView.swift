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
    var objectOffset = SIMD2<Float>(-0.43, 0)
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
        encoder.setRenderPipelineState((renderMode == 0 || renderMode == 4) ? pipeline : texturedPipeline)
        if renderMode != 0 && renderMode != 4 {
            encoder.setVertexBuffer(quadVertices, offset: 0, index: 1)
        }
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<SIMD3<Float>>.stride, index: 0)
        if renderMode == 0 {
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        } else if renderMode == 4 {
            // Synthetic PM4 fixture with native shaders; decoding lives in C++ only.
            #if MAXPS4_FEX_APP
            let pm4: [UInt32] = [(3 << 30) | (1 << 16) | (0x2d << 8), 3, 2]
            var drawn = 0
            let status = pm4.withUnsafeBufferPointer { words in
                maxps4_pm4_encode_triangle(words.baseAddress, words.count,
                                          encoder, pipeline, &drawn)
            }
            if status != 0 || drawn != 1 {
                encoder.endEncoding()
                view.isPaused = true
                report("PM4 rejeté par le décodeur C++ (code \(status))", true)
                return
            }
            #else
            encoder.endEncoding()
            view.isPaused = true
            report("Pont PM4 natif absent de ce build", true)
            return
            #endif
        } else {
            // Two separate objects, two texture bindings and two GPU draw commands.
            var left = objectOffset
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
        let mode = renderMode == 0 ? "Triangle" : (renderMode == 4 ? "Triangle PM4 → Metal" : "Deux rectangles texturés")
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
    let objectOffset: SIMD2<Float>
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
        context.coordinator.renderer?.objectOffset = objectOffset
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
    @State private var cpuState = MaxPS4FEXMetalState()
    @State private var cpuBusy = false
    @State private var cpuRequest = UUID()
    @State private var cpuStatus = "Aucun résultat FEX appliqué"
    @State private var cpuInitial = 750
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
                            Text("FEX → Metal").tag(3)
                            Text("PM4 → Metal").tag(4)
                        }
                        .pickerStyle(.segmented)
                        .onChange(of: renderMode) { _ in
                            status = "Changement de mode Metal…"
                            invalidateCPU()
                        }
                        TriangleSurface(device: device,
                            active: running && visible && scenePhase == .active && !failed,
                            renderMode: renderMode, objectOffset: MaxPS4FEXMetalState.objectOffset(
                                mode: renderMode, touch: touchOffset, cpu: cpuState.cpuOffset)) { message, error in
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
                        if renderMode == 4 {
                            Text("Expérimental : paquet PM4 type 3 DRAW_INDEX_AUTO (3 sommets) décodé dans le rendu, puis traduit en draw Metal. Shaders natifs ; pas encore de shaders PS4 ni de mémoire GPU PS4.")
                                .accessibilityIdentifier("maxps4.pm4MetalDescription")
                        }
                        if renderMode == 3 {
                            Text("ELF64 x86-64 : MOV EAX,n ; ADD EAX,250 ; HLT. Seul un résultat du backend FEXCore JIT déplace le rectangle bleu.")
                            Picker("Entrée CPU", selection: $cpuInitial) {
                                Text("Gauche").tag(0)
                                Text("Centre").tag(750)
                                Text("Droite").tag(1500)
                            }.pickerStyle(.segmented)
                            Button(cpuBusy ? "Exécution FEX…" : "Exécuter ELF → Metal") { executeCPU() }
                                .disabled(cpuBusy || scenePhase != .active)
                                .accessibilityIdentifier("maxps4.fexMetalExecute")
                            Button("Recentrer") { invalidateCPU(); cpuState.reset(); cpuStatus = "Recentré — aucun résultat CPU appliqué" }
                            Text(cpuStatus).accessibilityIdentifier("maxps4.fexMetalStatus")
                            Text("JIT autorisé via StikDebug requis sur iPhone. Aucun jeu PS4 ni GPU PS4 n’est exécuté.")
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
            .onDisappear { visible = false; running = false; invalidateCPU() }
            .onChange(of: scenePhase) { phase in
                if phase != .active { invalidateCPU() }
            }
        }
    }

    private func invalidateCPU() {
        cpuRequest = UUID()
        cpuBusy = false
    }

    private func executeCPU() {
        let request = UUID()
        cpuRequest = request
        cpuBusy = true
        cpuStatus = "Exécution ELF64 par FEXCore…"
        let initial = UInt32(cpuInitial)
        MaxPS4FEXMetalExecution.queue.async {
            let result = Result { try MaxPS4FEXMetalExecution.run(initial: initial) }
            DispatchQueue.main.async {
                // Discard completion after mode switch, background, reset or dismissal.
                guard cpuRequest == request, visible, scenePhase == .active, renderMode == 3 else { return }
                cpuBusy = false
                do {
                    let value = try result.get()
                    try cpuState.accept(value)
                    cpuStatus = "FEXCore JIT ARM64 — RAX=\(value.rax), x=\(cpuState.cpuOffset.x) — résultat appliqué à Metal"
                } catch {
                    cpuState.reset()
                    cpuStatus = "Échec ELF/FEX : \(error). Aucun résultat CPU appliqué. Vérifier l’autorisation JIT."
                }
            }
        }
    }

}

