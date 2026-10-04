import Foundation
import Combine

@MainActor
final class MaxPS4Emulator: ObservableObject {
    @Published var status = "Prêt"
    @Published var isRunning = false

    private var initialized = false

    func initializeCoreIfNeeded() {
        guard !initialized else { return }
        let userDir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        try? FileManager.default.createDirectory(at: userDir, withIntermediateDirectories: true)

        let result = userDir.path.withCString { path in
            var options = ShadPS4Options(user_dir: path, show_fps: 0, fullscreen: 1, network_enabled: 1)
            return shadps4_init(&options)
        }
        initialized = result == 0
        status = initialized ? "Noyau MaxPS4 prêt" : "Échec initialisation noyau (\(result))"
    }

    func importGame(from url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }

        initializeCoreIfNeeded()
        guard initialized else { return }

        status = "Préparation : \(url.lastPathComponent)"
        let result = url.path.withCString { shadps4_prepare_window($0) }
        guard result == 0 else {
            status = "Échec préparation du jeu (\(result))"
            return
        }

        isRunning = true
        status = "Jeu en cours"
        Thread.detachNewThread { [weak self] in
            let exitCode = shadps4_run_loop()
            Task { @MainActor in
                self?.isRunning = false
                self?.status = exitCode == 0 ? "Jeu arrêté" : "Erreur moteur (\(exitCode))"
            }
        }
    }

    func stop() {
        guard isRunning else { return }
        shadps4_stop()
        status = "Arrêt en cours…"
    }

    func togglePause() {
        guard isRunning else { return }
        shadps4_toggle_pause()
        status = shadps4_is_paused() != 0 ? "En pause" : "Jeu en cours"
    }
}
