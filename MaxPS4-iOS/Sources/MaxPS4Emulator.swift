import Foundation
import MaxPS4Core

@MainActor
final class MaxPS4Emulator: ObservableObject {
    @Published var status = "Prêt"

    init() {
        let version = String(cString: maxps4_core_version())
        let jit = maxps4_core_jit_available()
        status = "\(version) • JIT \(jit ? "disponible" : "indisponible")"
    }

    func importGame(from url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        defer {
            if access { url.stopAccessingSecurityScopedResource() }
        }

        let result = url.path.withCString { maxps4_core_boot_game($0) }
        switch result {
        case MAXPS4_CORE_OK:
            status = "Démarrage : \(url.lastPathComponent)"
        case MAXPS4_CORE_JIT_UNAVAILABLE:
            status = "JIT indisponible — active StikDebug/LiveContainer JIT"
        case MAXPS4_CORE_NOT_READY:
            status = "Bridge prêt — runtime FEXCore/shadPS4 à connecter"
        case MAXPS4_CORE_INVALID_PATH:
            status = "Chemin du jeu invalide"
        default:
            status = "Échec du cœur MaxPS4 (\(result.rawValue))"
        }
    }

    func stop() {
        maxps4_core_stop()
        status = "Arrêté"
    }
}
