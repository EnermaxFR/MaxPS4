import SwiftUI
import GameController

// Observe the existing system controllers. Input routing remains in PlayableView.
struct MaxPS4ControllerView: View {
    @State private var controllers: [GCController] = []

    var body: some View {
        Form {
            Section {
                if controllers.isEmpty {
                    ContentUnavailableView {
                        Label("Aucune manette connectée", systemImage: "gamecontroller")
                    } description: {
                        Text("Associez une manette dans les réglages Bluetooth de l’iPhone ou de l’iPad, puis revenez ici.")
                    }
                } else {
                    ForEach(controllers, id: \.self) { controller in
                        HStack(spacing: 12) {
                            Image(systemName: "gamecontroller.fill").foregroundStyle(Color.accentColor)
                            VStack(alignment: .leading, spacing: 4) {
                                Text(controller.vendorName ?? "Manette iOS").font(.headline)
                                Text(controller.extendedGamepad != nil ? "Profil étendu" : controller.microGamepad != nil ? "Profil simplifié" : "Profil non pris en charge par la démo")
                                    .font(.caption).foregroundStyle(.secondary)
                            }
                            Spacer()
                            Image(systemName: "checkmark.circle.fill").foregroundStyle(.green)
                        }
                    }
                }
                Button("Actualiser les manettes", systemImage: "arrow.clockwise") { refresh() }
                    .accessibilityIdentifier("maxps4.controllers.refresh")
            } header: { Text("Manettes connectées") }
            Section("Commandes de la démo Orbis") {
                LabeledContent("Déplacement", value: "Croix directionnelle / stick gauche")
                LabeledContent("Action", value: "Bouton A")
                Text("Les commandes tactiles et les manettes restent disponibles dans « Première image jouable ». Ces commandes ne prouvent pas la compatibilité d’un jeu PS4.")
                    .font(.footnote).foregroundStyle(.secondary)
                Button("Modifier l’affectation des boutons — indisponible", systemImage: "slider.horizontal.3") {}
                    .disabled(true)
            }
        }
        .formStyle(.grouped)
        .onAppear { refresh() }
        .onReceive(NotificationCenter.default.publisher(for: .GCControllerDidConnect)) { _ in refresh() }
        .onReceive(NotificationCenter.default.publisher(for: .GCControllerDidDisconnect)) { _ in refresh() }
    }

    private func refresh() { controllers = GCController.controllers() }
}
