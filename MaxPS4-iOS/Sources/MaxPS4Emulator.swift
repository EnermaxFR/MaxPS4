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
        guard name == "eboot.bin" || name.hasSuffix(".self") || name.hasSuffix(".elf") || name.hasSuffix(".pkg") else {
            status = "Format non pris en charge : sélectionnez PKG, eboot.bin, SELF ou ELF"
            return
        }

        let isPKG = name.hasSuffix(".pkg")
        // Prevent importing the same filename twice (case-insensitive).
        if games.contains(where: { $0.fileName.caseInsensitiveCompare(url.lastPathComponent) == .orderedSame }) {
            status = "Fichier déjà présent dans la bibliothèque : \(url.lastPathComponent)"
            return
        }
        // Identification only: PKG packages are not decrypted, extracted or launched.
        // ELF magic is a useful preliminary check, not proof of PS4 compatibility.
        guard let header = try? FileHandle(forReadingFrom: url) else {
            status = "Impossible de lire le fichier importé"
            return
        }
        defer { try? header.close() }
        guard let bytes = try? header.read(upToCount: 32), bytes.count >= 20 else {
            status = "Fichier invalide : en-tête incomplet"
            return
        }
        if isPKG {
            guard Array(bytes.prefix(4)) == [0x7F, 0x43, 0x4E, 0x54] else {
                status = "PKG invalide : signature PS4 absente"
                return
            }
        } else {
        guard Array(bytes.prefix(4)) == [0x7F, 0x45, 0x4C, 0x46] else {
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

            let displayName = isPKG && prettyName.uppercased().contains("SONICMANIA") ? "Sonic Mania" : prettyName
            let game = MaxPS4Game(
                id: UUID(),
                name: displayName,
                fileName: destination.lastPathComponent,
                localPath: destination.path,
                importedAt: Date()
            )

            games.insert(game, at: 0)
            try saveLibrary()
            status = isPKG ? "PKG PS4 importé (non exécutable) : \(destination.lastPathComponent)" : "Importé : \(destination.lastPathComponent)"
        } catch {
            status = "Import impossible : \(error.localizedDescription)"
        }
    }

    /// Report possible duplicate imports without deleting any user files.
    var duplicateGroups: [[MaxPS4Game]] {
        let groups = Dictionary(grouping: games) { game -> String in
            let filename = game.fileName.lowercased()
            let stem = (filename as NSString).deletingPathExtension
            let ext = (filename as NSString).pathExtension
            let normalized = stem.replacingOccurrences(
                of: "-[0-9]+$", with: "", options: .regularExpression
            )
            return normalized + "." + ext
        }
        return groups.values.filter { $0.count > 1 }
            .map { $0.sorted { $0.importedAt < $1.importedAt } }
            .sorted { ($0.first?.fileName ?? "") < ($1.first?.fileName ?? "") }
    }

    func inspectDuplicates() {
        let groups = Dictionary(grouping: games) { game -> String in
            let filename = game.fileName.lowercased()
            // Strip the numeric suffix assigned by uniqueDestination during import.
            let stem = (filename as NSString).deletingPathExtension
            let ext = (filename as NSString).pathExtension
            let normalized = stem.replacingOccurrences(
                of: "-[0-9]+$", with: "", options: .regularExpression
            )
            return normalized + "." + ext
        }
        let duplicates = groups.values.filter { $0.count > 1 }
        guard !duplicates.isEmpty else {
            status = "Bibliothèque : aucun doublon probable détecté"
            return
        }
        let fileCount = duplicates.reduce(0) { $0 + $1.count }
        let filenames = duplicates
            .flatMap { $0 }
            .map { "• " + $0.fileName }
            .sorted()
            .joined(separator: "\n")
        status = "Doublons possibles : \(fileCount) fichiers dans \(duplicates.count) groupes\n" +
            filenames + "\nAucun fichier supprimé. Utilisez ••• → Supprimer pour retirer manuellement une copie."
    }

    func inspect(_ game: MaxPS4Game) {
        do {
            if game.fileName.lowercased().hasSuffix(".pkg") {
                let fileURL = URL(fileURLWithPath: game.localPath)
                let attributes = try fileManager.attributesOfItem(atPath: fileURL.path)
                let byteCount = (attributes[.size] as? NSNumber)?.int64Value ?? 0
                let size = ByteCountFormatter.string(fromByteCount: byteCount, countStyle: .file)
                let handle = try FileHandle(forReadingFrom: fileURL)
                defer { try? handle.close() }
                let header = try handle.read(upToCount: 128) ?? Data()
                guard byteCount >= 128, header.count >= 128,
                      Array(header.prefix(4)) == [0x7F, 0x43, 0x4E, 0x54] else {
                    status = "PKG PS4 : en-tête incomplet ou invalide"
                    return
                }
                // Big-endian header fields: distinguish the advertised PKG length
                // from the actual number of bytes on disk, without reading payload data.
                func bigEndian64(_ offset: Int) -> UInt64 {
                    (0..<8).reduce(UInt64(0)) { value, i in
                        (value << 8) | UInt64(header[offset + i])
                    }
                }
                let declaredSize = bigEndian64(0x18)
                let sizeCheck: String
                if declaredSize == 0 {
                    sizeCheck = "taille déclarée absente"
                } else if declaredSize == UInt64(byteCount) {
                    sizeCheck = "taille déclarée cohérente"
                } else {
                    sizeCheck = "taille déclarée différente du fichier (vérification recommandée)"
                }
                // The PKG header holds a big-endian entry count at offset 0x10.
                // Report it only as a diagnostic: no extraction or trust decision.
                let entryCount = (0..<4).reduce(UInt32(0)) { value, i in
                    (value << 8) | UInt32(header[0x10 + i])
                }
                let secondaryCount = (0..<4).reduce(UInt32(0)) { value, i in
                    (value << 8) | UInt32(header[0x14 + i])
                }
                let tableCheck: String
                if entryCount == 0 {
                    tableCheck = "aucune entrée déclarée ; table non vérifiée"
                } else if entryCount > 100_000 {
                    tableCheck = "nombre d’entrées inhabituellement élevé ; table non vérifiée"
                } else if secondaryCount > entryCount {
                    tableCheck = "compteur secondaire supérieur au total ; en-tête suspect"
                } else {
                    tableCheck = "compteurs plausibles ; positions et contenu de la table non vérifiés"
                }
                let entryCheck = entryCount > 0 && entryCount <= 100_000
                    ? String(entryCount) + " entrées déclarées (non vérifiées)"
                    : "nombre absent ou inhabituel (non vérifié)"
                // PS4 PKG content_id is an ASCII field at 0x40 (36 bytes).
                // Never parse encrypted contents or infer a version from arbitrary bytes.
                let field = header.subdata(in: 0x40..<0x64)
                let contentID = String(bytes: field.prefix(while: { $0 != 0 }), encoding: .ascii)?
                    .trimmingCharacters(in: .whitespacesAndNewlines)
                let validID = contentID.flatMap { id -> String? in
                    guard id.count >= 16, id.count <= 36,
                          id.utf8.allSatisfy({ (48...57).contains($0) || (65...90).contains($0) || $0 == 45 || $0 == 95 }) else {
                        return nil
                    }
                    return id
                }
                let identifierSource = validID ?? game.fileName.uppercased()
                let titleID = identifierSource.range(of: "CUSA[0-9]{5}", options: .regularExpression)
                    .map { String(identifierSource[$0]) }
                let title = game.fileName.uppercased().contains("SONICMANIA") ? "Sonic Mania (nom du fichier)" : game.name
                let report = [
                    "Format : PKG PS4 (signature vérifiée)",
                    "Titre : " + title,
                    "Taille : " + size,
                    "Contrôle en-tête : " + sizeCheck,
                    "Table des entrées : " + entryCheck,
                    "Cohérence compteurs : " + tableCheck,
                    "Content ID : " + (validID ?? "indisponible"),
                    "Title ID : " + (titleID ?? "indisponible"),
                    "Version : non déterminée",
                    "Contenu : non extrait, non déchiffré",
                    "Exécution PS4 : indisponible"
                ]
                status = report.joined(separator: "\n")
            } else {
                status = try MaxPS4ELFInspector.inspect(url: URL(fileURLWithPath: game.localPath))
            }
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

    func traceCPUPrototype() {
        do {
            var cpu = MaxPS4CPUPrototype()
            try cpu.run([0x90, 0x90, 0xC3])
            status = "Trace CPU : \(cpu.executedInstructions) instructions • RIP \(cpu.rip) • offsets \(cpu.recentInstructionOffsets)"
        } catch {
            status = "Erreur trace CPU : \(error.localizedDescription)"
        }
    }

    func testCPUPrototype() {
        status = MaxPS4CPUPrototype.selfTest()
            ? "CPU x86-64 : tests registres, mémoire, pile et branchements réussis"
            : "CPU x86-64 : échec de l’auto-test"
    }

    func testIntegration() {
        status = MaxPS4ELFLoader.integrationTest()
            ? "Intégration réussie : ELF64 → CPU x86-64 → mémoire (valeur 42)"
            : "Échec intégration : vérifier le chargeur ELF64, le CPU ou la mémoire"
    }

    func testELFLoader() {
        status = MaxPS4ELFLoader.selfTest()
            ? "Chargeur ELF64 : auto-test réussi (mémoire simulée)"
            : "Chargeur ELF64 : échec de l’auto-test"
    }

    func showGuestMemoryMap() {
        do {
            var memory = MaxPS4GuestMemory()
            try memory.mapZeroFilled(at: 0x1000, size: 4096, permissions: [.read, .write])
            try memory.mapZeroFilled(at: 0x4000, size: 4096, permissions: [.read, .execute])
            status = "Plan mémoire invité : " + memory.memoryMapSummary
        } catch {
            status = "Diagnostic mémoire impossible : " + error.localizedDescription
        }
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

        if game.fileName.lowercased().hasSuffix(".pkg") {
            status = "PKG PS4 sélectionné : importation et identification uniquement • lancement indisponible"
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
