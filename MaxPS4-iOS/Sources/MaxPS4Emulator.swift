import Foundation
import MaxPS4Core

struct MaxPS4LibraryGame: Identifiable, Codable, Hashable {
    let id: UUID
    var name: String
    var executableRelativePath: String
    var importedAt: Date
    var lastPlayedAt: Date?
    var isFavorite: Bool
    var isHomebrew: Bool
}

@MainActor
final class MaxPS4Emulator: ObservableObject {
    @Published var status = "Prêt"
    @Published private(set) var libraryGames: [MaxPS4LibraryGame] = []
    @Published private(set) var isRunning = false
    @Published private(set) var currentGameName: String?
    @Published private(set) var runtimeDiagnostic = ""
    @Published private(set) var runtimeOutput = ""

    private let libraryDefaultsKey = "MaxPS4.LibraryGames.v1"
    private let libraryDirectoryName = "MaxPS4Library"

    private var bootGeneration: UInt = 0
    private var bootInProgress = false

    init() {
        loadLibrary()
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

        do {
            let validation = validateExecutable(at: url)
            guard validation.isValid else {
                status = "Import refusé • \(validation.diagnostic)"
                return
            }

            if isDuplicateImport(url) {
                status = "\(url.lastPathComponent) • déjà présent dans la bibliothèque"
                return
            }

            let id = UUID()
            let folderURL = try libraryDirectoryURL()
                .appendingPathComponent(id.uuidString, isDirectory: true)

            try FileManager.default.createDirectory(
                at: folderURL,
                withIntermediateDirectories: true
            )

            let destinationURL = folderURL.appendingPathComponent(url.lastPathComponent)
            try FileManager.default.copyItem(at: url, to: destinationURL)

            let rawName = url.lastPathComponent
            let parentName = url.deletingLastPathComponent().lastPathComponent
            let displayName: String

            if rawName.lowercased() == "eboot.bin", !parentName.isEmpty {
                displayName = parentName
            } else {
                displayName = url.deletingPathExtension().lastPathComponent
            }

            let game = MaxPS4LibraryGame(
                id: id,
                name: displayName.isEmpty ? rawName : displayName,
                executableRelativePath: id.uuidString + "/" + rawName,
                importedAt: Date(),
                lastPlayedAt: nil,
                isFavorite: false,
                isHomebrew: false
            )

            libraryGames.insert(game, at: 0)
            saveLibrary()

            if maxps4_backend_state() == MAXPS4_BACKEND_READY {
                launchGame(game.id)
            } else {
                let backend = String(cString: maxps4_backend_name())
                status = "\(game.name) • ajouté à la bibliothèque\nBackend 0.5 • \(backend) • STAGING"
            }
        } catch {
            status = "Import impossible: \(error.localizedDescription)"
        }
    }

    func launchGame(_ id: UUID) {
        guard !bootInProgress && !isRunning else {
            status = "Une session est déjà active. Arrête-la avant un nouveau lancement."
            return
        }

        guard let index = libraryGames.firstIndex(where: { $0.id == id }) else {
            status = "Jeu introuvable dans la bibliothèque"
            return
        }

        guard maxps4_backend_state() == MAXPS4_BACKEND_READY else {
            let backend = String(cString: maxps4_backend_name())
            status = "Backend 0.5 • \(backend) • STAGING\nLe runtime shadPS4/FEXCore réel n'est pas encore lié."
            return
        }

        let game = libraryGames[index]
        let executableURL = managedExecutableURL(for: game)

        guard FileManager.default.fileExists(atPath: executableURL.path) else {
            status = "\(game.name) • fichier importé introuvable"
            return
        }

        let validation = validateExecutable(at: executableURL)
        guard validation.isValid else {
            status = "Lancement refusé • \(validation.diagnostic)"
            return
        }

        guard executableURL.pathExtension.lowercased() != "pkg" else {
            status = "PKG reconnu, mais non exécutable directement. Importe un ELF/SELF homebrew autorisé."
            return
        }

        libraryGames[index].lastPlayedAt = Date()
        saveLibrary()

        bootGeneration &+= 1
        let generation = bootGeneration
        bootInProgress = true
        isRunning = true
        currentGameName = game.name
        runtimeDiagnostic = "Initialisation du guest…"
        runtimeOutput = ""
        status = "\(game.name) • exécution FEX lancée en arrière-plan…"

        startRuntimePolling(generation: generation)

        let path = executableURL.path
        let name = game.name

        Task.detached(priority: .userInitiated) { [weak self] in
            let result = path.withCString { maxps4_core_boot_game($0) }
            let diagnostic = String(cString: maxps4_core_jit_diagnostic())
            let backendDiagnostic = String(cString: maxps4_backend_diagnostic())

            await MainActor.run {
                guard let self, generation == self.bootGeneration else { return }
                self.bootInProgress = false

                switch result {
                case MAXPS4_CORE_OK:
                    self.isRunning = false
                    self.currentGameName = nil
                    self.status = "\(name) • session terminée avec succès"
                    self.refreshRuntimeSnapshot()
                case MAXPS4_CORE_JIT_UNAVAILABLE:
                    self.isRunning = false
                    self.currentGameName = nil
                    self.refreshRuntimeSnapshot()
                    self.status = "JIT indisponible • \(diagnostic)"
                case MAXPS4_CORE_NOT_READY:
                    self.isRunning = false
                    self.currentGameName = nil
                    self.refreshRuntimeSnapshot()
                    self.status = "JIT OK • \(backendDiagnostic)"
                case MAXPS4_CORE_INVALID_PATH:
                    self.isRunning = false
                    self.currentGameName = nil
                    self.refreshRuntimeSnapshot()
                    self.status = "Exécutable PS4 invalide • \(backendDiagnostic)"
                default:
                    self.isRunning = false
                    self.currentGameName = nil
                    self.refreshRuntimeSnapshot()
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

    func toggleFavorite(_ id: UUID) {
        guard let index = libraryGames.firstIndex(where: { $0.id == id }) else { return }
        libraryGames[index].isFavorite.toggle()
        saveLibrary()
    }

    func toggleHomebrew(_ id: UUID) {
        guard let index = libraryGames.firstIndex(where: { $0.id == id }) else { return }
        libraryGames[index].isHomebrew.toggle()
        saveLibrary()
    }

    func removeGame(_ id: UUID) {
        guard let game = libraryGames.first(where: { $0.id == id }) else { return }

        let folderURL = libraryDirectoryBaseURL()
            .appendingPathComponent(game.id.uuidString, isDirectory: true)

        try? FileManager.default.removeItem(at: folderURL)
        libraryGames.removeAll { $0.id == id }
        saveLibrary()
        status = "\(game.name) • retiré de la bibliothèque"
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
                self.status = ok
                    ? "Backend shadPS4/FEX • OK • \(diagnostic)"
                    : "Backend shadPS4/FEX • ÉCHEC • \(diagnostic)"
            }
        }
    }

    func stop() {
        bootGeneration &+= 1
        bootInProgress = false
        maxps4_backend_stop()
        maxps4_core_stop()
        isRunning = false
        currentGameName = nil
        refreshRuntimeSnapshot()
        status = "Arrêté"
    }

    private func startRuntimePolling(generation: UInt) {
        Task { [weak self] in
            guard let self else { return }

            while generation == self.bootGeneration && self.isRunning {
                self.refreshRuntimeSnapshot()
                try? await Task.sleep(nanoseconds: 500_000_000)
            }
        }
    }

    private func refreshRuntimeSnapshot() {
        var diagnostic = [CChar](repeating: 0, count: 1024)
        diagnostic.withUnsafeMutableBufferPointer { buffer in
            guard let baseAddress = buffer.baseAddress else { return }
            maxps4_backend_live_diagnostic(baseAddress, buffer.count)
        }

        var output = [CChar](repeating: 0, count: 4096)
        output.withUnsafeMutableBufferPointer { buffer in
            guard let baseAddress = buffer.baseAddress else { return }
            maxps4_backend_live_output(baseAddress, buffer.count)
        }

        runtimeDiagnostic = diagnostic.withUnsafeBufferPointer { buffer in
            guard let baseAddress = buffer.baseAddress else { return "" }
            return String(cString: baseAddress)
        }

        runtimeOutput = output.withUnsafeBufferPointer { buffer in
            guard let baseAddress = buffer.baseAddress else { return "" }
            return String(cString: baseAddress)
        }
    }

    private func validateExecutable(at url: URL) -> (isValid: Bool, diagnostic: String) {
        var diagnostic = [CChar](repeating: 0, count: 512)

        let isValid = url.path.withCString { path in
            diagnostic.withUnsafeMutableBufferPointer { buffer in
                guard let baseAddress = buffer.baseAddress else { return false }
                return maxps4_ps4_loader_validate(
                    path,
                    nil,
                    baseAddress,
                    UInt(buffer.count)
                )
            }
        }

        let message = diagnostic.withUnsafeBufferPointer { buffer -> String in
            guard let baseAddress = buffer.baseAddress else { return "format PS4 invalide" }
            let text = String(cString: baseAddress)
            return text.isEmpty ? "format PS4 invalide" : text
        }

        return (isValid, message)
    }

    private func isDuplicateImport(_ sourceURL: URL) -> Bool {
        let sourceName = sourceURL.lastPathComponent.lowercased()
        let sourceSize = (try? sourceURL.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? -1
        guard sourceSize >= 0 else { return false }

        return libraryGames.contains { game in
            let existingURL = managedExecutableURL(for: game)
            guard existingURL.lastPathComponent.lowercased() == sourceName else { return false }
            let existingSize = (try? existingURL.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? -2
            return existingSize == sourceSize
        }
    }

    private func loadLibrary() {
        guard
            let data = UserDefaults.standard.data(forKey: libraryDefaultsKey),
            let savedGames = try? JSONDecoder().decode([MaxPS4LibraryGame].self, from: data)
        else {
            libraryGames = []
            return
        }

        libraryGames = savedGames.filter {
            FileManager.default.fileExists(atPath: managedExecutableURL(for: $0).path)
        }

        if libraryGames.count != savedGames.count {
            saveLibrary()
        }
    }

    private func saveLibrary() {
        guard let data = try? JSONEncoder().encode(libraryGames) else { return }
        UserDefaults.standard.set(data, forKey: libraryDefaultsKey)
    }

    private func libraryDirectoryBaseURL() -> URL {
        let applicationSupport = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        ).first ?? FileManager.default.temporaryDirectory

        return applicationSupport.appendingPathComponent(
            libraryDirectoryName,
            isDirectory: true
        )
    }

    private func libraryDirectoryURL() throws -> URL {
        let url = libraryDirectoryBaseURL()

        try FileManager.default.createDirectory(
            at: url,
            withIntermediateDirectories: true
        )

        return url
    }

    private func managedExecutableURL(for game: MaxPS4LibraryGame) -> URL {
        libraryDirectoryBaseURL()
            .appendingPathComponent(game.executableRelativePath)
    }
}
