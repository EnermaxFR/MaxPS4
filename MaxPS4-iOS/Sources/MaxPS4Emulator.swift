import Foundation

@MainActor
final class MaxPS4Emulator: ObservableObject {
    @Published var status = "Prêt"

    func importGame(from url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        defer {
            if access { url.stopAccessingSecurityScopedResource() }
        }

        status = "Jeu sélectionné : \(url.lastPathComponent)"
        // The independent MaxPS4 frontend intentionally talks to the emulator
        // through this layer. The shadps4_ios C API will be wired here rather
        // than importing the upstream AetherPS4 Swift application.
    }
}
