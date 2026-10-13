import SwiftUI
import GameController
import UniformTypeIdentifiers

struct MaxPS4PlayableView: View {
    @Environment(\.scenePhase) private var scenePhase
    @StateObject private var session = MaxPS4PlayableSession()
    @State private var importing = false
    @State private var touchVisible = true
    @State private var touch: UInt32 = 0
    @State private var selected: URL?
    private let poll = Timer.publish(every: 1.0/30, on: .main, in: .common).autoconnect()
    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                if let image = session.image {
                    Image(uiImage: image).resizable().interpolation(.none).aspectRatio(contentMode: .fit)
                        .frame(maxHeight: 360).accessibilityLabel("Framebuffer produit par le programme invité")
                } else { Text("Aucune image invitée").frame(height: 180) }
                Text(session.report).font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                Button(session.running ? "Arrêter" : "Lancer / relancer") {
                    touch = 0
                    if session.running { session.stop() }
                    else if let url = selected ?? Bundle.main.url(forResource: "PlayableDemo", withExtension: nil) { session.start(url: url) }
                }.buttonStyle(.borderedProminent)
                Button("Importer une démo Orbis…") { importing = true }.disabled(session.running)
                Toggle("Commandes tactiles", isOn: $touchVisible)
                    .onChange(of: touchVisible) { _, visible in if !visible { touch = 0; session.input(0) } }
                if touchVisible {
                    VStack {
                        control("Haut", bit: 4)
                        HStack { control("Gauche", bit: 1); control("Action", bit: 16); control("Droite", bit: 2) }
                        control("Bas", bit: 8)
                    }.disabled(!session.running)
                }
                Text("Maintenir une direction pour déplacer le carré ; Action change sa couleur. Une manette iOS connectée utilise le pavé directionnel, le stick gauche et A. Le JIT doit être activé avec StikDebug.")
                    .font(.footnote)
                ShareLink(item: session.report) { Label("Partager la preuve de l’image courante", systemImage: "square.and.arrow.up") }
            }.padding()
        }
        .navigationTitle("Première image jouable")
        .fileImporter(isPresented: $importing, allowedContentTypes: [.folder]) { result in
            if case .success(let url) = result { selected = url; touch = 0; session.start(url: url) }
        }
        .onReceive(poll) { _ in
            var buttons = touchVisible ? touch : 0
            for controller in GCController.controllers() {
                if let pad = controller.extendedGamepad {
                    if pad.dpad.left.isPressed || pad.leftThumbstick.xAxis.value < -0.3 { buttons |= 1 }
                    if pad.dpad.right.isPressed || pad.leftThumbstick.xAxis.value > 0.3 { buttons |= 2 }
                    if pad.dpad.up.isPressed || pad.leftThumbstick.yAxis.value > 0.3 { buttons |= 4 }
                    if pad.dpad.down.isPressed || pad.leftThumbstick.yAxis.value < -0.3 { buttons |= 8 }
                    if pad.buttonA.isPressed { buttons |= 16 }
                } else if let pad = controller.microGamepad {
                    if pad.dpad.xAxis.value < -0.3 { buttons |= 1 }
                    if pad.dpad.xAxis.value > 0.3 { buttons |= 2 }
                    if pad.dpad.yAxis.value > 0.3 { buttons |= 4 }
                    if pad.dpad.yAxis.value < -0.3 { buttons |= 8 }
                    if pad.buttonA.isPressed { buttons |= 16 }
                }
            }
            session.input(buttons)
        }
        .onChange(of: scenePhase) { _, phase in if phase != .active { touch = 0; session.stop() } }
        .onDisappear { touch = 0; session.stop() }
    }
    private func control(_ name: String, bit: UInt32) -> some View {
        Text(name).frame(minWidth: 75, minHeight: 48).background(touch & bit != 0 ? Color.blue : Color.gray.opacity(0.2))
            .clipShape(RoundedRectangle(cornerRadius: 8))
            .accessibilityLabel(name).accessibilityAddTraits(.isButton)
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { _ in touch |= bit }
                .onEnded { _ in touch &= ~bit })
    }
}
