import Foundation
import Combine

@MainActor
final class MaxPS4Emulator: ObservableObject {
    @Published var status = "Prêt"
    @Published var isRunning = false

    // The standalone IPA frontend is intentionally buildable before the
    // shadPS4 core bridge is linked into the Xcode target. The direct core
    // build is validated separately; linking it into this app is the next
    // integration stage.
    private var initialized = false

    func initializeCoreIfNeeded() {
        guard !initialized else { return }
        initialized = true
        status = "Interface MaxPS4 prête"
    }

    func importGame(from url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }

        initializeCoreIfNeeded()
        status = "Jeu sélectionné : \(url.lastPathComponent)"
    }

    func stop() {
        guard isRunning else { return }
        isRunning = false
        status = "Jeu arrêté"
    }

    func togglePause() {
        guard isRunning else { return }
        status = status == "En pause" ? "Jeu en cours" : "En pause"
    }
}
