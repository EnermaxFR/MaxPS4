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
                // PS4 PKG: 0x18 is the 32-bit entry-table offset, not package length.
                let tableOffset = (0..<4).reduce(UInt32(0)) { ($0 << 8) | UInt32(header[0x18 + $1]) }
                let declaredBodySize = bigEndian64(0x28)
                let sizeCheck: String
                sizeCheck = declaredBodySize <= UInt64(byteCount)
                    ? "taille du corps plausible (champ 0x28, non équivalente au fichier entier)"
                    : "taille du corps supérieure à la taille réelle"
                // The PKG header holds a big-endian entry count at offset 0x10.
                // Report it only as a diagnostic: no extraction or trust decision.
                let entryCount = (0..<4).reduce(UInt32(0)) { value, i in
                    (value << 8) | UInt32(header[0x10 + i])
                }
                let secondaryCount = UInt32(header[0x16]) << 8 | UInt32(header[0x17])
                let tableCheck: String
                if entryCount == 0 {
                    tableCheck = "aucune entrée déclarée ; table non vérifiée"
                } else if entryCount > 100_000 {
                    tableCheck = "nombre d’entrées inhabituellement élevé ; table non vérifiée"
                } else if secondaryCount != entryCount {
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
                    "Décalage table : 0x" + String(tableOffset, radix: 16),
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

    /// Inspect only the public PKG header and the declared entry-table bounds.
    /// Entries may be encrypted; no content is extracted or executed.
    func inspectPKGStructure(_ game: MaxPS4Game) {
        guard game.fileName.lowercased().hasSuffix(".pkg") else {
            status = "Structure PKG : sélectionnez un fichier .pkg"
            return
        }
        do {
            let url = URL(fileURLWithPath: game.localPath)
            let attributes = try fileManager.attributesOfItem(atPath: url.path)
            guard let size = (attributes[.size] as? NSNumber)?.uint64Value, size >= 128 else {
                status = "Structure PKG : fichier trop court"
                return
            }
            let handle = try FileHandle(forReadingFrom: url)
            defer { try? handle.close() }
            let header = try handle.read(upToCount: 128) ?? Data()
            guard header.count == 128,
                  Array(header.prefix(4)) == [0x7F, 0x43, 0x4E, 0x54] else {
                status = "Structure PKG : signature invalide"
                return
            }
            func be32(_ offset: Int) -> UInt32 {
                (0..<4).reduce(UInt32(0)) { ($0 << 8) | UInt32(header[offset + $1]) }
            }
            func be64(_ offset: Int) -> UInt64 {
                (0..<8).reduce(UInt64(0)) { ($0 << 8) | UInt64(header[offset + $1]) }
            }
            let count = UInt64(be32(0x10))
            let scEntryCount = UInt64(UInt16(header[0x14]) << 8 | UInt16(header[0x15]))
            let secondCount = UInt64(UInt16(header[0x16]) << 8 | UInt16(header[0x17]))
            let tableOffset = UInt64(be32(0x18))
            let bodyOffset = be64(0x20)
            let bodySize = be64(0x28)
            let entryBytes = count * 32
            let tableValid = count > 0 && count <= 100_000 && tableOffset <= size && entryBytes <= size - tableOffset
            // Header offsets are displayed as diagnostics only; layout is not
            // trusted as an authenticated or decrypted PKG index.
            let countCheck = secondCount == count && scEntryCount <= count
                ? "compteurs cohérents"
                : "compteurs à vérifier"
            let message = [
                "Structure PKG • lecture seule",
                "Taille réelle : \(size) octets",
                "Corps PKG : offset \(bodyOffset), taille \(bodySize) octets",
                "Entrées déclarées : \(count)",
                "Compteur sécurisé : \(scEntryCount)",
                "Compteur secondaire : \(secondCount)",
                "Offset table : 0x\(String(tableOffset, radix: 16))",
                "Table (\(entryBytes) octets) : \(tableValid ? "bornes valides" : "bornes invalides")",
                "Contrôle : \(countCheck)",
                "Entrées internes : non déchiffrées ni extraites",
                "Déchiffrement et exécution : non disponibles"
            ]
            status = message.joined(separator: "\n")
        } catch {
            status = "Analyse de structure impossible : \(error.localizedDescription)"
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

    func testPS4CompatibilityScaffold() {
        status = MaxPS4VirtualRuntime.testPS4CompatibilityScaffold()
    }

    func testSimulatedKernelServices() {
        status = MaxPS4VirtualRuntime.testSimulatedKernelServices()
    }

    func testELFGuestRuntime() {
        status = MaxPS4VirtualRuntime.bootELFIntegrationTest()
    }

    func testVirtualRuntime() {
        status = MaxPS4VirtualRuntime.bootSelfTest()
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

    /// Read-only game launch preflight. This does not decrypt a PKG or execute PS4 code.
    func tryGameEngine(_ game: MaxPS4Game) {
        guard fileManager.fileExists(atPath: game.localPath) else {
            status = "Essai moteur : fichier introuvable"
            return
        }
        let name = game.fileName.lowercased()
        let url = URL(fileURLWithPath: game.localPath)
        do {
            if name.hasSuffix(".pkg") {
                let handle = try FileHandle(forReadingFrom: url)
                defer { try? handle.close() }
                let header = try handle.read(upToCount: 4) ?? Data()
                guard header == Data([0x7F, 0x43, 0x4E, 0x54]) else {
                    status = "Essai moteur : signature PKG invalide"
                    return
                }
                status = "Essai moteur : PKG PS4 reconnu ✅ • extraction/déchiffrement absents • exécution impossible sans moteur PS4 natif"
            } else if name.hasSuffix(".elf") || name == "eboot.bin" || name.hasSuffix(".self") {
                let handle = try FileHandle(forReadingFrom: url)
                defer { try? handle.close() }
                let magic = try handle.read(upToCount: 4) ?? Data()
                // SCE SELF is a container, not a directly executable ELF.
                // Report this explicitly rather than treating it as a corrupt ELF.
                if magic == Data([0x4F, 0x15, 0x3D, 0x1D]) {
                    // Read only the fixed SELF header; never treat encrypted payload
                    // as executable instructions or read arbitrary file offsets.
                    let headerTail = try handle.read(upToCount: 28) ?? Data()
                    guard headerTail.count == 28 else {
                        status = "Chargeur SELF : en-tête tronqué (32 octets requis)"
                        return
                    }
                    let header = Data(magic) + headerTail
                    func be16(_ at: Int) -> UInt16 {
                        (UInt16(header[at]) << 8) | UInt16(header[at + 1])
                    }
                    func be32(_ at: Int) -> UInt32 {
                        (0..<4).reduce(UInt32(0)) { ($0 << 8) | UInt32(header[at + $1]) }
                    }
                    let version = be16(4)
                    let mode = header[6]
                    let segmentCount = be16(0x18)
                    let metaSize = be32(0x10)
                    status = [
                        "Chargeur SELF : conteneur PlayStation reconnu ✅",
                        "Version en-tête : 0x" + String(version, radix: 16),
                        "Mode (champ brut) : 0x" + String(mode, radix: 16),
                        "Champ 0x18 (brut) : " + String(segmentCount),
                        "Champ 0x10 (brut) : " + String(metaSize),
                        "Limite : en-tête uniquement • segments non extraits",
                        "Déchiffrement / chargement du jeu / exécution PS4 : indisponibles"
                    ].joined(separator: "\n")
                    return
                }
                guard magic == Data([0x7F, 0x45, 0x4C, 0x46]) else {
                    status = "Essai moteur : signature d'exécutable inconnue • ELF64 x86-64 ou SELF attendu"
                    return
                }
                let loaded = try MaxPS4ELFLoader.load(url: url)
                // Only a strictly recognized 11-byte synthetic fixture may run.
                // Unknown and real PS4 executables remain diagnostic-only.
                let prefix = try loaded.memory.fetchInstructionBytes(at: loaded.entry, count: 11)
                let bytes = Array(prefix)
                if bytes.count == 11, bytes[0] == 0x48, bytes[1] == 0xB8,
                   bytes[10] == 0xC3 {
                    var cpu = MaxPS4CPUPrototype()
                    try cpu.runLoadedTest(memory: loaded.memory, entry: loaded.entry, length: 11)
                    status = "ELF64 de test exécuté ✅ • \(loaded.segments) segments • RAX=\(cpu.rax) • 2 instructions • environnement PS4 réel absent"
                } else {
                    status = "ELF64 chargé ✅ • \(loaded.segments) segments • entrée 0x\(String(loaded.entry, radix: 16)) • instructions non reconnues par le prototype : exécution refusée"
                }
            } else {
                status = "Essai moteur : format non pris en charge"
            }
        } catch {
            status = "Essai moteur impossible : \(error.localizedDescription)"
        }
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
