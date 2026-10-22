import Combine
import Foundation

@_silgen_name("maxps4_native_abi_version")
private func maxps4NativeABIVersion() -> UInt32

@_silgen_name("maxps4_native_capabilities")
private func maxps4NativeCapabilities() -> UInt32

@_silgen_name("maxps4_native_aether_cpu_probe")
private func maxps4NativeAetherCPUProbe() -> Int32

@_silgen_name("maxps4_aether_cpu_state_regression")
private func maxps4AetherCPUStateRegression() -> Int32

@_silgen_name("maxps4_aether_multi_block_dispatch_probe")
private func maxps4AetherMultiBlockDispatchProbe() -> Int32


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
        if let shadPS4 = engine as? any MaxPS4ShadPS4Backend {
            guard shadPS4.capabilities.canLaunchPS4Game else {
                nativeEngine = nil
                status = "Port shadPS4 incomplet :\n" + shadPS4.capabilities.diagnostic
                return
            }
        }
        nativeEngine = engine
        status = engine.isReady ? "Moteur natif connecté" : "Moteur natif présent mais non prêt"
    }

    init() {
        loadLibrary()
    }

    var backendReady: Bool {
        nativeEngine?.isReady == true
    }

    var nativeABIVersion: UInt32 { maxps4NativeABIVersion() }

    var nativeDiagnosticReport: String {
        "MaxPS4 — diagnostic iOS\\n" +
        "ABI native : " + String(maxps4NativeABIVersion()) + "\\n" +
        "Capacités natives : " + String(maxps4NativeCapabilities()) + "\\n" +
        "Backend PS4 prêt : " + String(backendReady) + "\\n" +
        "JIT FEXCore : non opérationnel\\n" +
        "Jeux importés : " + String(games.count) + "\\n" +
        "Dernier résultat : " + status
    }

    func runNativeCPUCheck() {
        let result = maxps4NativeAetherCPUProbe()
        let states = maxps4AetherCPUStateRegression()
        let blocks = maxps4AetherMultiBlockDispatchProbe()
        status = "AetherPS4 — dispatch restreint : " +
            (result == 1 ? "PASS" : "FAIL (code " + String(result) + ")") +
            " ; conservation de 8 états CPU : " +
            (states == 1 ? "PASS" : "FAIL (code " + String(states) + ")") +
            " ; deux blocs x86 synthétiques et refus des RIP invalides : " +
            (blocks == 1 ? "PASS" : "FAIL (code " + String(blocks) + ")") +
            ". Tests limités au moteur de démonstration : pas de jeu PS4 ni JIT FEXCore."
    }

    func playGuessingDemo(guess: Int) {
        do {
            let target = 7
            let won = try MaxPS4CPUPrototype.guessNumber(target: target, guess: guess)
            status = won
                ? "Mini-jeu x86-64 : bravo, tu as trouvé le nombre \(target) !"
                : "Mini-jeu x86-64 : ce n'est pas \(guess). Essaie encore entre 1 et 20."
        } catch {
            status = "Mini-jeu x86-64 : \(error.localizedDescription)"
        }
    }

    func testShadPS4NativeUtility() {
        let linked = MaxPS4NativeLinkCheck.isUpstreamUtilityLinked &&
            MaxPS4NativeLinkCheck.executableSignatureSelfTest &&
            MaxPS4NativeLinkCheck.nativeELFEntryPointSelfTest &&
            MaxPS4NativeLinkCheck.nativeSELFHeaderSelfTest &&
            MaxPS4NativeLinkCheck.nativeSELFSegmentFlagsSelfTest &&
            MaxPS4NativeLinkCheck.nativeSELFFileRangesSelfTest &&
            MaxPS4NativeLinkCheck.nativeSELFMemorySizesSelfTest &&
            MaxPS4NativeLinkCheck.nativeSELFEmbeddedELFSelfTest &&
            MaxPS4NativeLinkCheck.nativeUpstreamAlignmentSelfTest &&
            MaxPS4NativeLinkCheck.nativePS4ELFTypeSelfTest &&
            MaxPS4NativeLinkCheck.nativeSyntheticExecutionSelfTest &&
            MaxPS4NativeLinkCheck.executionBackendSelfTest &&
            MaxPS4NativeLinkCheck.arm64TranslationPreviewSelfTest
        guard linked else {
            status = "Pont C++ : contrôle natif échoué. Aucun jeu ne peut être lancé."
            return
        }
        // Cross-check native ELF header parsing against the bounded Swift loader.
        // Only a generated test ELF is used; no PS4 game is executed.
        do {
            let url = try MaxPS4ELFLoader.createImportDemo()
            defer { try? FileManager.default.removeItem(at: url) }
            let file = try Data(contentsOf: url)
            let nativeEntry = MaxPS4NativeLinkCheck.nativeELFEntryPoint(file)
            let loaded = try MaxPS4ELFLoader.load(url: url)
            guard nativeEntry == loaded.entry else {
                status = "Pont C++ / chargeur Swift : points d'entrée ELF incohérents."
                return
            }
            status = "Pont C++ et chargeur Swift : en-tête ELF de test cohérent (entrée 0x" +
                String(loaded.entry, radix: 16) +
                "). Le moteur PS4 reste incomplet."
        } catch {
            status = "Diagnostic ELF natif/Swift échoué : \(error.localizedDescription)"
        }
    }

    func importGame(from url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        // A background PKG copy takes over the security scope and releases it when done.
        var scopeHandedOff = false
        defer {
            if access && !scopeHandedOff { url.stopAccessingSecurityScopedResource() }
        }

        let name = url.lastPathComponent.lowercased()
        guard name == "eboot.bin" || name.hasSuffix(".self") || name.hasSuffix(".elf") || name.hasSuffix(".pkg") else {
            status = "Format non pris en charge : sélectionnez PKG, eboot.bin, SELF ou ELF"
            return
        }

        let isPKG = name.hasSuffix(".pkg")
        // Prevent importing the same filename twice (case-insensitive), including in-flight copies.
        if games.contains(where: { $0.fileName.caseInsensitiveCompare(url.lastPathComponent) == .orderedSame }) ||
            pendingImports.contains(name) {
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
            if isPKG {
                // Multi-GB packages: copy off the main actor so the UI stays responsive.
                scopeHandedOff = true
                copyPKGInBackground(from: url, to: destination, key: name, scoped: access)
                return
            }
            try fileManager.copyItem(at: url, to: destination)
            try addImportedGame(at: destination, isPKG: false)
        } catch {
            status = "Import impossible : \(error.localizedDescription)"
        }
    }

    /// Names of PKG copies still running, lowercased, to reject a second import of the same file.
    private var pendingImports: Set<String> = []

    private func copyPKGInBackground(from url: URL, to destination: URL, key: String, scoped: Bool) {
        pendingImports.insert(key)
        let total = Int64((try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0)
        status = Self.copyProgress(copied: 0, total: total, name: destination.lastPathComponent)
        Task {
            // copyItem gives no callback; the destination size is a cheap progress signal.
            let progress = Task { [weak self] in
                while !Task.isCancelled {
                    try? await Task.sleep(nanoseconds: 500_000_000)
                    let attributes = try? FileManager.default.attributesOfItem(atPath: destination.path)
                    let copied = (attributes?[.size] as? NSNumber)?.int64Value ?? 0
                    if !Task.isCancelled {
                        self?.status = Self.copyProgress(copied: copied, total: total, name: destination.lastPathComponent)
                    }
                }
            }
            let result = await Task.detached(priority: .userInitiated) {
                Result { try FileManager.default.copyItem(at: url, to: destination) }
            }.value
            progress.cancel()
            if scoped { url.stopAccessingSecurityScopedResource() }
            pendingImports.remove(key)
            switch result {
            case .success:
                do { try addImportedGame(at: destination, isPKG: true) }
                catch { status = "Import impossible : \(error.localizedDescription)" }
            case .failure(let error):
                // Only our own fresh, partial destination is removed; the source is never touched.
                try? fileManager.removeItem(at: destination)
                status = "Import impossible : \(error.localizedDescription)"
            }
        }
    }

    private static func copyProgress(copied: Int64, total: Int64, name: String) -> String {
        func format(_ bytes: Int64) -> String { ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file) }
        guard total > 0 else { return "Copie du PKG en cours : \(name) (\(format(copied)))" }
        let percent = min(100, Int(Double(copied) * 100 / Double(total)))
        return "Copie du PKG en cours : \(name) — \(percent) % (\(format(copied)) / \(format(total)))"
    }

    private func addImportedGame(at destination: URL, isPKG: Bool) throws {
        let prettyName = destination
            .deletingPathExtension()
            .lastPathComponent
            .replacingOccurrences(of: "_", with: " ")
            .replacingOccurrences(of: "-", with: " ")

        let displayName = prettyName // Library label only; metadata comes from the checked report.
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
            let url = URL(fileURLWithPath: game.localPath)
            status = game.fileName.lowercased().hasSuffix(".pkg")
                ? try MaxPS4PKGInspector.inspect(url: url)
                : try MaxPS4ELFInspector.inspect(url: url)
        } catch {
            status = "Analyse impossible : \(error.localizedDescription)"
        }
    }

    /// Dedicated iOS action: bounded file I/O and parsing run off the main actor.
    /// Each request reopens the retained file; no cached report is trusted.
    func analyzePKG(_ game: MaxPS4Game) async -> String {
        guard game.fileName.lowercased().hasSuffix(".pkg") else {
            return "Analyse PKG : sélectionnez un paquet .pkg"
        }
        let url = URL(fileURLWithPath: game.localPath)
        let report = await Task.detached(priority: .userInitiated) {
            do { return try MaxPS4PKGInspector.inspect(url: url) }
            catch { return "Analyse PKG impossible : \(error.localizedDescription)" }
        }.value
        status = report
        return report
    }

    private func pkgEntryReport(_ url: URL) throws -> String {
        try MaxPS4PKGInspector.inspect(url: url)
    }

    // Compatibility actions use the same checked inventory, never raw PFS scans.
    func inspectPKGEntries(_ game: MaxPS4Game) { inspect(game) }
    func inspectPKGAssetCandidates(_ game: MaxPS4Game) { inspect(game) }
    func inspectPKGStructure(_ game: MaxPS4Game) { inspect(game) }
    func inspectPS4ExecutableReadiness(_ game: MaxPS4Game) { inspect(game) }
    func inspectPS4EntryPrefixes(_ game: MaxPS4Game) { inspect(game) }
    func diagnosePKGBoot(_ game: MaxPS4Game) { inspect(game) }

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

    /// Exercises a synthetic ELF64 -> guest memory -> x86-64 CPU pipeline.
    /// Uses no PS4 binaries, game assets or native machine-code execution.
    func runELFCPUBench() {
        let loaderOK = MaxPS4ELFLoader.selfTest()
        let cpuOK = MaxPS4CPUPrototype.selfTest()
        let integrationOK = MaxPS4ELFLoader.integrationTest()
        let controlFlowIntegrationOK = MaxPS4ELFLoader.controlFlowIntegrationTest()
        let controlFlowOK = MaxPS4CPUPrototype.callAndBranchSelfTest()
        let nearJumpOK = MaxPS4CPUPrototype.nearJumpSelfTest()
        let stackArithmeticOK = MaxPS4CPUPrototype.arithmeticAndStackSelfTest()
        let bitwiseAndOK = MaxPS4CPUPrototype.bitwiseAndSelfTest()
        let bitwiseOrOK = MaxPS4CPUPrototype.bitwiseOrSelfTest()
        let compareOK = MaxPS4CPUPrototype.compareRegistersSelfTest()
        let xorOK = MaxPS4CPUPrototype.xorRegisterSelfTest()
        let immediate32OK = MaxPS4CPUPrototype.immediate32AndControlFlowSelfTest()
        let nearConditionalOK = MaxPS4CPUPrototype.nearConditionalBranchSelfTest()
        let immediate8OK = MaxPS4CPUPrototype.immediate8AndBranchesSelfTest()
        status = [
            "Banc de test ELF64 / CPU x86-64",
            "Chargeur ELF et permissions mémoire : " + (loaderOK ? "OK ✅" : "ÉCHEC ❌"),
            "Interpréteur CPU synthétique : " + (cpuOK ? "OK ✅" : "ÉCHEC ❌"),
            "Chaîne ELF → mémoire → CPU : " + (integrationOK ? "OK ✅" : "ÉCHEC ❌"),
            "ELF avec calculs, sauts et écriture mémoire : " + (controlFlowIntegrationOK ? "OK ✅" : "ÉCHEC ❌"),
            "CALL/RET et branchements contrôlés : " + (controlFlowOK ? "OK ✅" : "ÉCHEC ❌"),
            "JMP rel32 et rejets hors limites : " + (nearJumpOK ? "OK ✅" : "ÉCHEC ❌"),
            "ADD/SUB, PUSH/POP et pile virtuelle : " + (stackArithmeticOK ? "OK ✅" : "ÉCHEC ❌"),
            "AND bit-à-bit et drapeau zéro : " + (bitwiseAndOK ? "OK ✅" : "ÉCHEC ❌"),
            "OR bit-à-bit et drapeau zéro : " + (bitwiseOrOK ? "OK ✅" : "ÉCHEC ❌"),
            "CMP registres et drapeau zéro : " + (compareOK ? "OK ✅" : "ÉCHEC ❌"),
            "XOR inverse et validation : " + (xorOK ? "OK ✅" : "ÉCHEC ❌"),
            "Arithmétique 64 bits et branchements : " + (immediate32OK ? "OK ✅" : "ÉCHEC ❌"),
            "JZ/JNZ rel32 et bornes mémoire : " + (nearConditionalOK ? "OK ✅" : "ÉCHEC ❌"),
            "ADD/SUB/CMP imm8 et branchements : " + (immediate8OK ? "OK ✅" : "ÉCHEC ❌"),
            "Résultat attendu du programme synthétique : RAX=42, stockage mémoire=42.",
            "Ce test n'exécute pas Sonic Mania ni du code PS4.",
            "Exécution réelle PS4 : indisponible."
        ].joined(separator: "\n")
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

    func testProcessMemoryIsolation() {
        status = MaxPS4VirtualRuntime.testProcessMemoryIsolation()
    }

    func testPS4VirtualMemoryService() {
        status = MaxPS4VirtualRuntime.testPS4VirtualMemoryService()
    }

    func createELFImportDemo() {
        do {
            let url = try MaxPS4ELFLoader.createImportDemo()
            defer { try? fileManager.removeItem(at: url) }
            importGame(from: url)
            if games.contains(where: { $0.fileName.lowercased() == url.lastPathComponent.lowercased() }) {
                status = "ELF64 de démonstration créé ✅ • ouvre Jeux → MaxPS4 Demo Imports → Inspecter les imports ELF64"
            }
        } catch {
            status = "Création du fichier ELF64 de test impossible : \(error.localizedDescription)"
        }
    }

    func diagnoseFirstPS4Launch(_ game: MaxPS4Game) {
        let name = game.fileName.lowercased()
        guard name.hasSuffix(".elf") || name.hasSuffix(".self") || name == "eboot.bin" else {
            status = "Premier lancement : sélectionner un ELF64 clair ou eboot.bin ; SELF protégé non pris en charge"
            return
        }
        status = MaxPS4FEXDiagnostic.stagePS4(url: URL(fileURLWithPath: game.localPath))
    }

    func inspectELFImports(_ game: MaxPS4Game) {
        let name = game.fileName.lowercased()
        guard name.hasSuffix(".elf") || name == "eboot.bin" else {
            status = "Importations ELF64 : sélectionnez un fichier .elf ou eboot.bin non chiffré"
            return
        }
        do {
            status = try MaxPS4ELFLoader.inspectImports(url: URL(fileURLWithPath: game.localPath))
        } catch {
            status = "Importations ELF64 : \(error.localizedDescription)"
        }
    }

    func runBatchDiagnostics() {
        status = MaxPS4VirtualRuntime.runBatchDiagnostics()
    }

    func testELFLibraryIntegration() {
        status = MaxPS4VirtualRuntime.testELFLibraryIntegration()
    }

    func testPS4LibraryResolver() {
        status = MaxPS4VirtualRuntime.testPS4LibraryResolver()
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
                status = try pkgEntryReport(url)
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
                    // Read the real imported ELF entry point; never execute these bytes.
                    let entryBytes = try loaded.memory.fetchInstructionBytes(at: loaded.entry, count: 16)
                    let hex = entryBytes.map { String(format: "%02X", $0) }.joined(separator: " ")
                    status = [
                        game.name + " • ELF64 importé : contrôle avant exécution",
                        "Fichier : " + game.fileName,
                        "Segments ELF chargés : " + String(loaded.segments),
                        "Point d’entrée x86-64 : 0x" + String(loaded.entry, radix: 16),
                        "16 octets au point d’entrée : " + hex,
                        "Source : exécutable importé, pas un programme synthétique",
                        "EXÉCUTION BLOQUÉE : backend CPU/HLE PS4 non opérationnel",
                        "Instructions réelles exécutées : 0"
                    ].joined(separator: "\n")
                }
            } else {
                status = "Essai moteur : format non pris en charge"
            }
        } catch {
            status = "Essai moteur impossible : \(error.localizedDescription)"
        }
    }

    // Reusable preflight for the game library and the launch action.
    // This never claims that an imported package is executable.
    func launchReadinessReport(for game: MaxPS4Game) -> String {
        let fileURL = URL(fileURLWithPath: game.localPath)
        guard fileManager.fileExists(atPath: fileURL.path) else {
            return "Fichier introuvable : \\(game.fileName). Vérifie les données importées."
        }
        if fileURL.pathExtension.lowercased() == "pkg" {
            return "\(game.name) : PKG reconnu et conservé. Lancement bloqué : eboot.bin, modules PRX et ressources du PFS ne sont pas accessibles par cet import. Fournir un dossier obtenu par une voie autorisée avec des ELF/PRX en clair et maxps4.json, puis utiliser « Importer et exécuter un dossier Orbis compatible » dans le diagnostic FEXCore. Aucun jeu lancé depuis ce PKG."
        }
        guard let nativeEngine else {
            let cpuProbeStatus = maxps4NativeAetherCPUProbe() == 1 ? "réussi" : "échoué"
            return "\\(game.name) : bibliothèque native ABI \\(maxps4NativeABIVersion()) liée ; lancement PS4 indisponible (capacités : \\(maxps4NativeCapabilities())) ; test CPU AetherPS4 : \\(cpuProbeStatus)."
        }
        guard nativeEngine.isReady else {
            return "\\(game.name) : moteur natif détecté, mais pas prêt à exécuter un jeu."
        }
        if let backend = nativeEngine as? any MaxPS4ShadPS4Backend,
           !backend.capabilities.canLaunchPS4Game {
            return "\\(game.name) : moteur PS4 incomplet. " + backend.capabilities.diagnostic
        }
        return "\\(game.name) : moteur déclaré prêt. Le lancement reste expérimental."
    }

    func launch(_ game: MaxPS4Game) {
        guard fileManager.fileExists(atPath: game.localPath) else {
            status = "Fichier introuvable : \(game.fileName)"
            return
        }

        if game.fileName.lowercased().hasSuffix(".pkg") {
            status = launchReadinessReport(for: game)
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
            // An absent JSON catalog should not hide existing imported files.
            if let folder = try? importFolder(),
               let files = try? fileManager.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil) {
                games = files.filter {
                    ["pkg", "elf", "self"].contains($0.pathExtension.lowercased())
                        || $0.lastPathComponent.lowercased() == "eboot.bin"
                }.map {
                    MaxPS4Game(id: UUID(), name: $0.deletingPathExtension().lastPathComponent,
                               fileName: $0.lastPathComponent, localPath: $0.path,
                               importedAt: Date())
                }
                try? saveLibrary()
            } else { games = [] }
            return
        }

        do {
            let data = try Data(contentsOf: libraryURL)
            let decoded = try JSONDecoder().decode([MaxPS4Game].self, from: data)
            // Container UUIDs may change after an iOS app update. Never trust
            // a persisted absolute sandbox path; resolve the saved filename
            // relative to our current Documents/MaxPS4Games directory instead.
            let folder = try importFolder()
            let recovered = decoded.compactMap { game -> MaxPS4Game? in
                let fileName = URL(fileURLWithPath: game.fileName).lastPathComponent
                guard fileName != ".", fileName != "..", !fileName.isEmpty else { return nil }
                let current = folder.appendingPathComponent(fileName)
                guard fileManager.fileExists(atPath: current.path) else { return nil }
                return MaxPS4Game(id: game.id, name: game.name, fileName: fileName,
                                  localPath: current.path, importedAt: game.importedAt)
            }
            // Also recover game files when the metadata JSON is stale or incomplete.
            let existing = Set(recovered.map { $0.fileName.lowercased() })
            let recognized = Set(["pkg", "elf", "self"])
            let files = (try? fileManager.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? []
            let extra = files.filter {
                let name = $0.lastPathComponent
                return (recognized.contains($0.pathExtension.lowercased()) || name.lowercased() == "eboot.bin")
                    && !existing.contains(name.lowercased())
            }.map { url in
                MaxPS4Game(id: UUID(),
                           name: url.deletingPathExtension().lastPathComponent,
                           fileName: url.lastPathComponent,
                           localPath: url.path, importedAt: Date())
            }
            games = recovered + extra
            try? saveLibrary()
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
