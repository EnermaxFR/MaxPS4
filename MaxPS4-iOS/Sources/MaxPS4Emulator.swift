import Combine
import Foundation

struct MaxPS4Game: Identifiable, Codable, Equatable {
    let id: UUID
    let name: String
    let fileName: String
    let localPath: String
    let importedAt: Date
}

@MainActor
final class MaxPS4Emulator: ObservableObject {
    @Published var status = "Prêt"
    @Published private(set) var games: [MaxPS4Game] = []

    private let fileManager = FileManager.default
    private var nativeEngine: (any MaxPS4NativeEngine)?

    func connectNativeEngine(_ engine: any MaxPS4NativeEngine) {
        nativeEngine = engine
        status = "Moteur natif connecté"
    }

    init() {
        loadLibrary()
    }

    var backendReady: Bool {
        nativeEngine?.isReady == true
    }

    func importGame(from url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        defer {
            if access { url.stopAccessingSecurityScopedResource() }
        }

        let name = url.lastPathComponent.lowercased()
        guard name == "eboot.bin" || name.hasSuffix(".self") || name.hasSuffix(".elf") else {
            status = "Format non pris en charge : sélectionnez eboot.bin, SELF ou ELF"
            return
        }

        // ELF magic is a useful preliminary check, not proof of PS4 compatibility.
        guard let header = try? FileHandle(forReadingFrom: url) else {
            status = "Impossible de lire le fichier importé"
            return
        }
        defer { try? header.close() }
        guard let bytes = try? header.read(upToCount: 20),
              bytes.count >= 20,
              Array(bytes.prefix(4)) == [0x7F, 0x45, 0x4C, 0x46] else {
            status = "Fichier invalide : en-tête ELF absent"
            return
        }
        guard bytes[4] == 2, bytes[5] == 1 else {
            status = "Format ELF incompatible : 64 bits little-endian requis"
            return
        }
        let machine = UInt16(bytes[18]) | (UInt16(bytes[19]) << 8)
        guard machine == 0x3E else {
            status = "Architecture incompatible : exécutable x86-64 attendu"
            return
        }

        do {
            let folder = try importFolder()
            let destination = uniqueDestination(for: url.lastPathComponent, in: folder)
            try fileManager.copyItem(at: url, to: destination)

            let prettyName = destination
                .deletingPathExtension()
                .lastPathComponent
                .replacingOccurrences(of: "_", with: " ")
                .replacingOccurrences(of: "-", with: " ")

            let game = MaxPS4Game(
                id: UUID(),
                name: prettyName,
                fileName: destination.lastPathComponent,
                localPath: destination.path,
                importedAt: Date()
            )

            games.insert(game, at: 0)
            try saveLibrary()
            status = "Importé : \(destination.lastPathComponent)"
        } catch {
            status = "Import impossible : \(error.localizedDescription)"
        }
    }

    func inspect(_ game: MaxPS4Game) {
        do {
            status = try MaxPS4ELFInspector.inspect(url: URL(fileURLWithPath: game.localPath))
        } catch {
            status = "Analyse impossible : \(error.localizedDescription)"
        }
    }

    func rename(_ game: MaxPS4Game, to proposedName: String) {
        let name = proposedName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty else {
            status = "Le nom du jeu ne peut pas être vide"
            return
        }
        guard let index = games.firstIndex(where: { $0.id == game.id }) else { return }
        let previous = games[index]
        games[index] = MaxPS4Game(
            id: previous.id, name: name, fileName: previous.fileName,
            localPath: previous.localPath, importedAt: previous.importedAt
        )
        do {
            try saveLibrary()
            status = "Jeu renommé : \(name)"
        } catch {
            games[index] = previous
            status = "Renommage impossible : \(error.localizedDescription)"
        }
    }

    func remove(_ game: MaxPS4Game) {
        do {
            if fileManager.fileExists(atPath: game.localPath) {
                try fileManager.removeItem(atPath: game.localPath)
            }
            games.removeAll { $0.id == game.id }
            try saveLibrary()
            status = "Jeu supprimé"
        } catch {
            status = "Suppression impossible : \(error.localizedDescription)"
        }
    }

    func removeAll() {
        for game in games where fileManager.fileExists(atPath: game.localPath) {
            try? fileManager.removeItem(atPath: game.localPath)
        }
        games.removeAll()
        try? saveLibrary()
        status = "Bibliothèque vidée"
    }

    var deviceDiagnostic: String {
        let process = ProcessInfo.processInfo
        let memoryGB = Double(process.physicalMemory) / 1_073_741_824
        return "iOS \(process.operatingSystemVersionString) • \(process.processorCount) cœurs logiques • \(String(format: "%.1f", memoryGB)) Go RAM"
    }

    func testCPUPrototype() {
        status = MaxPS4CPUPrototype.selfTest()
            ? "CPU x86-64 : tests registres, mémoire, pile et branchements réussis"
            : "CPU x86-64 : échec de l’auto-test"
    }

    func testGuestMemory() {
        status = MaxPS4GuestMemory.selfTest()
            ? "Mémoire invitée : auto-test réussi (prototype, 16 Mio maximum)"
            : "Mémoire invitée : échec de l’auto-test"
    }

    func testBackend() {
        status = backendReady
            ? "Backend shadPS4/FEX prêt"
            : "Diagnostic : iPhone détecté • moteur shadPS4 non connecté"
    }

    func launch(_ game: MaxPS4Game) {
        guard fileManager.fileExists(atPath: game.localPath) else {
            status = "Fichier introuvable : \(game.fileName)"
            return
        }

        // Keep all native emulator execution behind this method. Once the
        // shadPS4/FEX bridge is linked, this is the single place the UI calls.
        guard let nativeEngine, nativeEngine.isReady else {
            status = "Jeu sélectionné : \(game.name) • moteur natif non connecté"
            return
        }
        do {
            try nativeEngine.launchGame(at: URL(fileURLWithPath: game.localPath))
            status = "Lancement demandé : \(game.name)"
        } catch {
            status = "Échec du lancement : \(error.localizedDescription)"
        }
    }

    private var libraryURL: URL {
        fileManager.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("maxps4-library.json")
    }

    private func importFolder() throws -> URL {
        let url = fileManager.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("MaxPS4Games", isDirectory: true)
        try fileManager.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    private func uniqueDestination(for fileName: String, in folder: URL) -> URL {
        let input = URL(fileURLWithPath: fileName)
        let base = input.deletingPathExtension().lastPathComponent
        let ext = input.pathExtension

        var candidate = folder.appendingPathComponent(fileName)
        var index = 2

        while fileManager.fileExists(atPath: candidate.path) {
            let nextName = ext.isEmpty ? "\(base)-\(index)" : "\(base)-\(index).\(ext)"
            candidate = folder.appendingPathComponent(nextName)
            index += 1
        }
        return candidate
    }

    private func loadLibrary() {
        guard fileManager.fileExists(atPath: libraryURL.path) else {
            games = []
            return
        }

        do {
            let data = try Data(contentsOf: libraryURL)
            let decoded = try JSONDecoder().decode([MaxPS4Game].self, from: data)
            games = decoded.filter { fileManager.fileExists(atPath: $0.localPath) }
        } catch {
            games = []
            status = "Bibliothèque réinitialisée"
        }
    }

    private func saveLibrary() throws {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data = try encoder.encode(games)
        try data.write(to: libraryURL, options: .atomic)
    }
}


// Interface entre la nouvelle UI MaxPS4 et un futur port natif shadPS4/FEX.
// La compilation standalone ne contient pas encore ce moteur.
@MainActor
protocol MaxPS4NativeEngine {
    var isReady: Bool { get }
    func launchGame(at url: URL) throws
}
