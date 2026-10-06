import Foundation
import MaxPS4Core

@MainActor
final class MaxPS4Emulator: ObservableObject {
    @Published var status = "Prêt"
    private var bootGeneration: UInt = 0
    private var bootInProgress = false

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

        bootGeneration &+= 1
        let generation = bootGeneration
        bootInProgress = true
        status = "\(url.lastPathComponent) • exécution FEX lancée en arrière-plan…"
        let path = url.path
        let name = url.lastPathComponent

        Task.detached(priority: .userInitiated) { [weak self] in
            let result = path.withCString { maxps4_core_boot_game($0) }
            let diagnostic = String(cString: maxps4_core_jit_diagnostic())
            let backendDiagnostic = String(cString: maxps4_backend_diagnostic())
            await MainActor.run {
                guard let self, generation == self.bootGeneration else { return }
                self.bootInProgress = false
                switch result {
                case MAXPS4_CORE_OK:
                    self.status = "Démarrage : \(name)"
                case MAXPS4_CORE_JIT_UNAVAILABLE:
                    self.status = "JIT indisponible • \(diagnostic)"
                case MAXPS4_CORE_NOT_READY:
                    self.status = "JIT OK • \(backendDiagnostic)"
                case MAXPS4_CORE_INVALID_PATH:
                    self.status = "Exécutable PS4 invalide • \(backendDiagnostic)"
                default:
                    self.status = "Échec du cœur MaxPS4 (\(result.rawValue))"
                }
            }
        }

        Task { [weak self] in
            try? await Task.sleep(nanoseconds: 5_000_000_000)
            guard let self, self.bootInProgress, generation == self.bootGeneration else { return }
            self.status = "\(name) • FEX est toujours en cours après 5 s. L’interface reste active; aucun arrêt forcé du guest n’est tenté."
        }
    }


    func testBackend() {
        guard maxps4_backend_state() == MAXPS4_BACKEND_READY else {
            status = "Backend shadPS4/FEX non lié dans ce build"
            return
        }
        status = "Test du backend shadPS4/FEX en cours…"
        Task.detached(priority: .userInitiated) {
            let ok = maxps4_backend_self_test()
            let diagnostic = String(cString: maxps4_backend_diagnostic())
            await MainActor.run {
                self.status = ok ? "Backend shadPS4/FEX • OK • \(diagnostic)" : "Backend shadPS4/FEX • ÉCHEC • \(diagnostic)"
            }
        }
    }

    func stop() {
        maxps4_backend_stop()
        maxps4_core_stop()
        status = "Arrêté"
    }
}
