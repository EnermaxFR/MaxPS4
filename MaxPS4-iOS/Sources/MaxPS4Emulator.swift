import Foundation
import MaxPS4Core

@MainActor
final class MaxPS4Emulator: ObservableObject {
    @Published var status = "Prêt"

    init() {
        refreshStatus()
    }

    private func refreshStatus() {
        let version = String(cString: maxps4_core_version())
        let jit = maxps4_core_jit_available()
        let jitDiagnostic = String(cString: maxps4_core_jit_diagnostic())
        let backend = String(cString: maxps4_backend_name())
        let backendDiagnostic = String(cString: maxps4_backend_diagnostic())
        let state = maxps4_backend_state()
        let backendState: String

        switch state {
        case MAXPS4_BACKEND_READY:
            backendState = "READY"
        case MAXPS4_BACKEND_NOT_LINKED:
            backendState = "STAGING"
        default:
            backendState = "ERROR"
        }

        status = "\(version) • JIT \(jit ? "disponible" : "indisponible") • \(jitDiagnostic)\nBackend 0.5 • \(backend) • \(backendState) • \(backendDiagnostic)"
    }

    func importGame(from url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        defer {
            if access { url.stopAccessingSecurityScopedResource() }
        }

        guard maxps4_backend_state() == MAXPS4_BACKEND_READY else {
            let backend = String(cString: maxps4_backend_name())
            status = "Backend 0.5 • \(backend) • STAGING\nLe runtime shadPS4/FEXCore réel n'est pas encore lié."
            return
        }

        let result = url.path.withCString { maxps4_core_boot_game($0) }
        let diagnostic = String(cString: maxps4_core_jit_diagnostic())
        switch result {
        case MAXPS4_CORE_OK:
            status = "Démarrage : \(url.lastPathComponent)"
        case MAXPS4_CORE_JIT_UNAVAILABLE:
            status = "JIT indisponible • \(diagnostic)"
        case MAXPS4_CORE_NOT_READY:
            status = "JIT OK • runtime FEXCore/shadPS4 à connecter"
        case MAXPS4_CORE_INVALID_PATH:
            status = "Chemin du jeu invalide"
        default:
            status = "Échec du cœur MaxPS4 (\(result.rawValue))"
        }
    }

    func stop() {
        maxps4_backend_stop()
        maxps4_core_stop()
        status = "Arrêté"
    }
}
