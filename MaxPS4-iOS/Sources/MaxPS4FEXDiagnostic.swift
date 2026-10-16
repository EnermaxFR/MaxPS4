import Foundation
#if canImport(Darwin)
import Darwin
#endif

struct MaxPS4FEXDiagnostic: Sendable {
    let succeeded: Bool
    let result: UInt64?
    let backend: String
    let details: String
    let elfMemoryBridgePassed: Bool
    let elfControlFlowPassed: Bool
    var ps4IntegrationReport: String {
        let jit = succeeded && backend == "FEXCore JIT ARM64"
        return [
            "Intégration PS4 — état réel",
            "\(jit ? "✓" : "✗") CPU x86-64 FEXCore JIT : \(jit ? "test synthétique validé" : "non validé")",
            "\(elfMemoryBridgePassed ? "✓" : "✗") Contrôles ELF synthétique + mémoire invitée : \(elfMemoryBridgePassed ? "tests indépendants réussis" : "échec du test")",
            "\(elfControlFlowPassed ? "✓" : "✗") ELF → mémoire → CPU prototype : \(elfControlFlowPassed ? "exécution synthétique validée" : "non validé")",
            "◌ ELF SCE/PRX : liaison et exécution bornée via import de dossier ; SELF protégé refusé",
            "◌ Mémoire invitée PS4 : pages natives et CPU borné disponibles ; adressage PS4 général indisponible",
            "◌ Services libkernel disponibles ; démo ELF dynamique bornée à vérifier sur cet appareil",
            "✗ GPU / rendu PS4 : non connecté",
            "Lancement d’un jeu PS4 : indisponible",
            "Le succès du JIT ne valide pas un jeu PS4."
        ].joined(separator: "\n")
    }

    /// Imported plaintext file -> real engine-owned guest pages -> entry readback.
    /// Staging succeeds independently of JIT permission, and never dispatches CPU.
    static func stagePS4(url: URL) -> String {
#if MAXPS4_FEX_APP
        do {
            let handle = try FileHandle(forReadingFrom: url)
            defer { try? handle.close() }
            let size = try handle.seekToEnd()
            guard size >= 64, size <= 64 * 1024 * 1024 else {
                return "MaxPS4 : fichier hors budget ELF/SELF (64 Mio)"
            }
            try handle.seek(toOffset: 0)
            let file = try handle.read(upToCount: Int(size)) ?? Data()
            guard file.count == Int(size) else { return "MaxPS4 : lecture ELF/SELF incomplète" }
            var engine: OpaquePointer?
            let opened = maxps4_fex_engine_open(&engine)
            guard opened == 0, let engine else { return "FEXCore : ouverture impossible (\(opened))" }
            defer { maxps4_fex_engine_close(engine) }
            var entry: UInt64 = 0
            var segments: UInt32 = 0
            var blockers: UInt32 = 0
            let staged = file.withUnsafeBytes {
                maxps4_fex_engine_stage_ps4(engine, $0.bindMemory(to: UInt8.self).baseAddress,
                    $0.count, &entry, &segments, &blockers)
            }
            guard staged == 0 else { return "MaxPS4 : mappage ELF/SELF PS4 refusé (\(staged))" }
            var first = [UInt8](repeating: 0, count: 1)
            let read = first.withUnsafeMutableBufferPointer {
                maxps4_fex_engine_read_ps4(engine, entry, $0.baseAddress, $0.count)
            }
            guard read == 0 else { return "MaxPS4 : lecture du point d'entrée refusée (\(read))" }
            return [
                "MaxPS4 : ELF/SELF PS4 clair mappé dans la mémoire native du moteur FEXCore",
                "Segments : \(segments) ; entrée invitée : 0x" + String(entry, radix: 16),
                "Premier octet relu : 0x" + String(first[0], radix: 16) + " (non exécuté)",
                "Liaison/imports : " + (blockers & 1 != 0 ? "requis, non résolus" : "aucun indicateur dynamique détecté"),
                "TLS : " + (blockers & 2 != 0 ? "requis, non initialisé" : "aucun segment TLS détecté"),
                "Blocage CPU : adressage absolu PS4, admission des instructions et ABI non raccordés",
                "JIT iPhone : préparation RX/RW vérifiée requise ; aucune instruction PS4 exécutée par ce mappage",
                "Services PS4 et rendu Metal du jeu : non validés ; aucune jouabilité revendiquée"
            ].joined(separator: "\n")
        } catch { return "MaxPS4 : chargement impossible — \(error.localizedDescription)" }
#else
        return "MaxPS4 : moteur FEXCore natif absent de cette compilation"
#endif
    }

    /// Executes a user-selected bounded static ELF, using the same native loader
    /// for FEX and the no-JIT path. Arbitrary PS4 binaries remain rejected.
    static func executeTestELF(url: URL) -> String {
#if MAXPS4_FEX_APP
        do {
            let handle = try FileHandle(forReadingFrom: url)
            defer { try? handle.close() }
            let size = try handle.seekToEnd()
            guard size >= 64, size <= 32 * 1024 * 1024 else { return "ELF hors budget (32 Mio)" }
            try handle.seek(toOffset: 0)
            let file = try handle.read(upToCount: Int(size)) ?? Data()
            guard file.count == Int(size) else { return "Lecture ELF incomplète" }
            var engine: OpaquePointer?
            let opened = maxps4_fex_engine_open(&engine)
            guard opened == 0, let engine else { return "Ouverture FEXCore refusée : \(opened)" }
            defer { maxps4_fex_engine_close(engine) }
            // Orbis demonstration path has its own strict linker and real FEX
            // trampoline. It never falls through to the static interpreter.
            if file.count >= 18, file[7] == 9, file[17] == 0xfe {
                var entry: UInt64 = 0; var imports: UInt32 = 0
                let linked = file.withUnsafeBytes { bytes in
                    maxps4_fex_engine_link_ps4(engine, bytes.bindMemory(to: UInt8.self).baseAddress,
                        bytes.count, 65536, &entry, &imports)
                }
                guard linked == 0 else {
                    return "Démo PS4 : liaison refusée (\(linked)). Format dynamique, imports ou instructions hors du périmètre pris en charge."
                }
                let initialized = maxps4_fex_engine_initialize(engine)
                guard initialized == 0 else {
                    return "Démo PS4 liée (\(imports) imports) ; exécution FEX indisponible sur cet appareil (\(initialized))."
                }
                var value: UInt64 = 0; var backend: UInt32 = 0; var calls: UInt32 = 0
                let executed = maxps4_fex_engine_run_linked_ps4(engine, &value, &backend, &entry, &calls)
                let nativeFEX = executed == 0 && backend == UInt32(MAXPS4_CPU_FEX_ARM64_JIT)
                return [
                    "Démo PS4 : " + url.lastPathComponent,
                    "Exécution FEX ARM64 : " + (nativeFEX ? "réussie" : "non validée (\(executed))"),
                    "Imports résolus : \(imports) ; appels libkernel : \(calls)",
                    "RAX : \(value) ; entrée : 0x" + String(entry, radix: 16),
                    "La démo scePthreadSelf attend RAX = 2 après reprise de son exécution.",
                    "TLS, dépendances PRX complètes et lancement de jeu : indisponibles."
                ].joined(separator: "\n")
            }
            let jit = maxps4_fex_engine_initialize(engine)
            var value: UInt64 = 0; var entry: UInt64 = 0; var rip: UInt64 = 0
            var backend: UInt32 = 0; var count: UInt32 = 0
            var jitExecution = jit
            let status = file.withUnsafeBytes { bytes in
                let pointer = bytes.bindMemory(to: UInt8.self).baseAddress
                if jit == 0 {
                    jitExecution = maxps4_fex_engine_run_elf(engine, pointer, bytes.count, 65536,
                        &value, &backend, &entry)
                    if jitExecution == 0 { return jitExecution }
                }
                // Keep the FEX error if execution itself runs out of JIT memory.
                return maxps4_fex_engine_interpret_elf(engine, pointer, bytes.count, 65536,
                    &value, &backend, &entry, &rip, &count)
            }
            let name = backend == UInt32(MAXPS4_CPU_FEX_ARM64_JIT) ? "FEXCore JIT ARM64" :
                backend == UInt32(MAXPS4_CPU_RESTRICTED_INTERPRETER) ? "Interpréteur ELF sans JIT" : "Aucun"
            return [
                "ELF importé : " + url.lastPathComponent,
                "Statut : \(status) ; initialisation JIT : \(jit) ; exécution JIT : \(jitExecution) ; backend : \(name)",
                "Entrée invitée : 0x" + String(entry, radix: 16),
                "RAX : \(value)",
                backend == UInt32(MAXPS4_CPU_RESTRICTED_INTERPRETER) ?
                    "RIP invité : 0x" + String(rip, radix: 16) + " ; instructions terminées : \(count)" :
                    "RIP final et compteur FEX : non exposés",
                "Admission limitée : MOV EAX, ADD EAX, NOP, stores RIP-relatifs RW et HLT final.",
                "Imports, relocations, TLS et exécution de jeu PS4 : non pris en charge."
            ].joined(separator: "\n")
        } catch { return "Exécution ELF impossible : \(error.localizedDescription)" }
#else
        return "Moteur FEXCore natif absent de cette compilation"
#endif
    }

    /// Fixed M2 fixture only; no interpreter fallback and no BRK protocol.
    static func testM2LowVA() -> String {
#if MAXPS4_FEX_APP
        var report = maxps4_m2_low_va_report()
        let status = maxps4_fex_test_m2_low_va(&report)
        let executed = report.backend == UInt32(MAXPS4_CPU_FEX_ARM64_JIT) && report.execute_status == 0
        let passed = status == 0 && executed && report.rax == 42 && report.memory == 42 && report.tls == 8 &&
            report.collision_preserved == 1 && report.cleanup == 1 && report.code_rx == 1 && report.stack_rw_nx == 1 &&
            report.boundary_status == 0 && report.boundary_cases == 16 && report.boundary_preserved == 1
        let revision = Bundle.main.object(forInfoDictionaryKey: "MaxPS4SourceRevision") as? String ?? "non renseignée"
        return [
            "M2 low VA — traduction mémoire FEXCore",
            "Date UTC : " + ISO8601DateFormatter().string(from: Date()),
            "Révision source : " + revision,
            "Système : " + ProcessInfo.processInfo.operatingSystemVersionString,
            "Statut : \(status) ; initialisation : \(report.initialize_status) ; exécution : \(executed ? String(report.execute_status) : "non validée")",
            "Backend : \(report.backend) (2 requis pour JIT ARM64 natif)",
            "Entrée invitée : 0x" + String(report.entry, radix: 16),
            "JIT : \(executed ? String(report.rax) : "non mesuré") (attendu 42) ; mémoire : \(executed ? String(report.memory) : "non mesurée") (attendu 42)",
            "TLS : \(executed ? String(report.tls) : "non mesuré") (attendu 8)",
            "Collision préservée : \(report.collision_preserved) ; nettoyage : \(report.cleanup) ; code RX : \(report.code_rx) ; pile RW/NX : \(report.stack_rw_nx) (tous attendus 1)",
            "Fournisseur RX/RW : état \(maxps4_jit_state()), erreur \(maxps4_jit_error()), étape \(maxps4_jit_failure_stage()), Mach \(maxps4_jit_failure_mach()), région \(maxps4_jit_failure_region()) ; échec script \(maxps4_jit_script_failure())",
            "Limites mappings : statut \(report.boundary_status) ; refus vérifiés \(report.boundary_cases)/16 ; mémoire préservée \(report.boundary_preserved) (attendu 1)",
            "Validation M2 : " + (passed ? "réussie" : "échouée"),
            "Aucun repli interpréteur. Aucun BRK. La correction de -48 exige une validation sur votre iPhone."
        ].joined(separator: "\n")
#else
        return "M2 low VA : moteur FEXCore natif absent de cette compilation"
#endif
    }

    /// Built-in ELF bytes are shared with the native linked-execution tests.
    /// This path never invokes the restricted interpreter.
    static func testPS4Libkernel() -> String {
#if MAXPS4_FEX_APP
        var report = maxps4_ps4_elf_test_report()
        let status = maxps4_fex_test_ps4_libkernel(&report)
        let error = withUnsafeBytes(of: report.error) {
            String(decoding: $0.prefix(while: { $0 != 0 }), as: UTF8.self)
        }
        let backend: String
        switch report.backend {
        case UInt32(MAXPS4_CPU_FEX_ARM64_JIT): backend = "FEXCore JIT ARM64 natif (2)"
        case UInt32(MAXPS4_CPU_FEX_ARM64_SIMULATOR): backend = "FEXCore ARM64 simulé par VIXL (3)"
        default: backend = "Aucun — exécution non validée"
        }
#if targetEnvironment(simulator)
        let environment = "simulateur iOS — aucune preuve iPhone"
#elseif os(iOS)
        let environment = "appareil iOS physique"
#else
        let environment = "hôte macOS — aucune preuve iPhone"
#endif
        func step(_ value: Int32) -> String {
            value == -ECANCELED ? "non tenté" : value == 0 ? "réussi (0)" : "échoué (\(value))"
        }
        var m2 = maxps4_m2_low_va_report()
        let m2Status = maxps4_fex_test_m2_low_va(&m2)
        let observed = report.read_status == 0
        let revision = Bundle.main.object(forInfoDictionaryKey: "MaxPS4SourceRevision") as? String ?? "non renseignée"
        return [
            "Test ELF PS4 / libkernel synthétique v1",
            "Date UTC : " + ISO8601DateFormatter().string(from: Date()),
            "Révision source : " + revision,
            "Environnement : " + environment + " ; " + ProcessInfo.processInfo.operatingSystemVersionString,
            "Ouverture FEX : " + step(report.open_status),
            "Chargement ELF : " + step(report.load_status) + " ; segments PT_LOAD : \(report.segments)",
            "Point d’entrée invité : " + (report.load_status == 0 ? "0x" + String(report.entry, radix: 16) : "indisponible"),
            "NID recherché : aI+OeCz8xrQ (libkernel, version 1)",
            "Fonction résolue : " + (report.resolved_service != 0 ? "scePthreadSelf (service \(report.resolved_service))" : "aucune"),
            "Liaison NID / relocation JUMP_SLOT : " + step(report.link_status) + " ; imports : \(report.imports)",
            "Initialisation JIT : " + step(report.initialize_status),
            "Fournisseur RX/RW : état \(maxps4_jit_state()), erreur \(maxps4_jit_error())",
            "Préparation JIT : étape \(maxps4_jit_failure_stage()), code Mach \(maxps4_jit_failure_mach()), région \(maxps4_jit_failure_region())",
            "Backend d’exécution réel : " + backend,
            "Exécution : " + step(report.execute_status) + " ; appels libkernel observés : \(report.calls)",
            "Lecture mémoire invitée : " + step(report.read_status),
            "Résultat appel libkernel [0x10108] : " + (observed ? String(report.kernel_return) + " (attendu 1)" : "indisponible"),
            "Retour au code invité : " + (report.resumed == 1 ? "confirmé par CALL/RET → store du retour → ADD → store final → HLT" : "non confirmé"),
            "Valeur finale en mémoire invitée [0x10100] : " + (observed ? String(report.final_memory) + " (attendu 2)" : "indisponible"),
            "RAX final : " + (report.execute_status == 0 ? String(report.rax) : "indisponible"),
            "Vérification : " + step(report.verification_status),
            "Erreurs précises : " + (error.isEmpty ? "aucune" : error),
            "Segments invités après retrait : \(report.guest_segments_after)",
            "Régions RX/RW utilisées après nettoyage : \(report.jit_regions_after) (avant : \(report.jit_regions_before))",
            "Validation globale : " + (status == 0 ? "réussie" : "échouée (\(status))"),
            "M2 low-VA 0x400000 : statut \(m2Status), backend \(m2.backend), RAX \(m2.rax), mémoire \(m2.memory) (attendu 42), TLS \(m2.tls) (attendu 8)",
            "M2 collision hôte préservée : \(m2.collision_preserved), nettoyage : \(m2.cleanup), code RX : \(m2.code_rx), pile RW/NX : \(m2.stack_rw_nx)",
            "Ce diagnostic borné doit être exécuté sur iPhone ; aucune correction générale de -48 n’est annoncée.",
            "RIP final et instructions terminées FEX : non exposés par cette API.",
            "Les régions StikDebug préparées restent réservées jusqu’à leur libération explicite.",
            "Démo bornée avec tables DT_* standard ; adressage PS4 général, tables SCE, PRX et TLS restent indisponibles."
        ].joined(separator: "\n")
#else
        return "Test ELF PS4 / libkernel : moteur FEXCore natif absent de cette compilation"
#endif
    }

    // An authorized plaintext bundle has an explicit graph. A recognized PKG
    // or opaque PFS never supplies these bytes implicitly.
    static func executeOrbisDirectory(url: URL) -> String {
#if MAXPS4_FEX_APP
        struct Manifest: Decodable {
            struct Module: Decodable { let path: String; let loadBias: UInt64; let dependencies: [String] }
            let root: String
            let modules: [Module]
        }
        func basename(_ value: String) -> Bool {
            !value.isEmpty && value.utf8.count <= 255 && value != "." && value != ".." &&
                !value.contains("/") && !value.contains("\\") && !value.contains("\0")
        }
        do {
            let directory = url.resolvingSymlinksInPath().standardizedFileURL
            func read(_ name: String, limit: UInt64) throws -> Data {
                guard basename(name) else { throw CocoaError(.fileReadInvalidFileName) }
                let fileURL = directory.appendingPathComponent(name)
                guard fileURL.resolvingSymlinksInPath().deletingLastPathComponent() == directory else {
                    throw CocoaError(.fileReadNoPermission)
                }
                let handle = try FileHandle(forReadingFrom: fileURL)
                defer { try? handle.close() }
                let size = try handle.seekToEnd()
                guard size > 0, size <= limit else { throw CocoaError(.fileReadTooLarge) }
                try handle.seek(toOffset: 0)
                let bytes = try handle.read(upToCount: Int(size)) ?? Data()
                guard bytes.count == Int(size) else { throw CocoaError(.fileReadCorruptFile) }
                return bytes
            }
            let manifest = try JSONDecoder().decode(Manifest.self, from: read("maxps4.json", limit: 65536))
            guard basename(manifest.root), !manifest.modules.isEmpty, manifest.modules.count <= 128,
                  Set(manifest.modules.map(\.path)).count == manifest.modules.count,
                  manifest.modules.contains(where: { $0.path == manifest.root }),
                  manifest.modules.allSatisfy({ basename($0.path) && $0.dependencies.count <= 128 && $0.dependencies.allSatisfy(basename) }) else {
                return "Import Orbis refusé : manifeste invalide (racine, chemins ou dépendances)."
            }
            var storage: [NSData] = []
            var total = 0
            for module in manifest.modules {
                let bytes = try read(module.path, limit: 32 * 1024 * 1024)
                total += bytes.count
                guard total <= 64 * 1024 * 1024 else { return "Import Orbis refusé : budget total de 64 Mio dépassé." }
                storage.append(bytes as NSData)
            }
            var strings: [UnsafeMutablePointer<CChar>] = []
            defer { strings.forEach { free($0) } }
            func cstring(_ value: String) throws -> UnsafePointer<CChar> {
                guard let pointer = strdup(value) else { throw CocoaError(.fileReadUnknown) }
                strings.append(pointer); return UnsafePointer(pointer)
            }
            var dependencies: [UnsafeMutablePointer<UnsafePointer<CChar>?>] = []
            defer { dependencies.forEach { $0.deallocate() } }
            var modules: [maxps4_orbis_module] = []
            for (index, module) in manifest.modules.enumerated() {
                let pointers = UnsafeMutablePointer<UnsafePointer<CChar>?>.allocate(capacity: max(1, module.dependencies.count))
                dependencies.append(pointers)
                for (i, name) in module.dependencies.enumerated() { pointers[i] = try cstring(name) }
                modules.append(maxps4_orbis_module(path: try cstring(module.path),
                    bytes: storage[index].bytes.assumingMemoryBound(to: UInt8.self), size: storage[index].length,
                    load_bias: module.loadBias, dependencies: UnsafePointer(pointers), dependency_count: module.dependencies.count))
            }
            var engine: OpaquePointer?
            let opened = maxps4_fex_engine_open(&engine)
            guard opened == 0, let engine else { return "Ouverture FEX refusée (\(opened))." }
            defer { maxps4_fex_engine_close(engine) }
            var diagnostic = [CChar](repeating: 0, count: 1024)
            let root = try cstring(manifest.root)
            let prepared = withExtendedLifetime(storage) {
                modules.withUnsafeBufferPointer { modules in
                    diagnostic.withUnsafeMutableBufferPointer { text in
                        maxps4_fex_engine_prepare_orbis(engine, modules.baseAddress, modules.count,
                            root, 65536, text.baseAddress, text.count)
                    }
                }
            }
            let reason = diagnostic.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
            guard prepared == 0 else { return "Fichiers disponibles : \(modules.count). Préparation Orbis refusée (\(prepared)) : \(reason)" }
            let initialized = maxps4_fex_engine_initialize(engine)
            guard initialized == 0 else {
                return "Fichiers vérifiés, modules liés, mémoire préparée. Exécution non tentée : JIT FEX indisponible (\(initialized)). Activer StikDebug puis réessayer."
            }
            var value: UInt64 = 0; var entry: UInt64 = 0; var backend: UInt32 = 0
            let status = maxps4_fex_engine_run_orbis(engine, &value, &backend, &entry)
            guard status == 0 else { return "Processus Orbis préparé ; exécution FEX refusée (\(status))." }
            let cpu = backend == UInt32(MAXPS4_CPU_FEX_ARM64_JIT) ? "ARM64 natif" : "simulateur ARM64"
            return "Fichiers vérifiés : \(modules.count). SCE/NID/relocations résolus.\nExécution FEX \(cpu) terminée : RAX = \(value), entrée = 0x" + String(entry, radix: 16) +
                "\nAppels invités CALL/RET bornés ; arrêt HLT et nettoyage du processus.\nImage, commandes de jeu et audio : indisponibles. Aucun jeu jouable validé."
        } catch {
            return "Import Orbis refusé : \(error.localizedDescription). Fournir un dossier autorisé contenant maxps4.json et les ELF/PRX en clair. Un PKG à PFS opaque ne permet pas ce lancement."
        }
#else
        return "Import Orbis : moteur FEXCore absent de cette compilation."
#endif
    }

    static func run() -> Self {
#if MAXPS4_FEX_APP
        let elfMemoryBridgePassed = MaxPS4ELFLoader.importDemoSelfTest() &&
            MaxPS4GuestMemory.isolationSelfTest() &&
            MaxPS4GuestMemory.wordAccessSelfTest()
        let elfControlFlowPassed = MaxPS4ELFLoader.controlFlowIntegrationTest()
        var engine: OpaquePointer?
        let opened = maxps4_fex_engine_open(&engine)
        guard opened == 0, let engine else {
            return Self(succeeded: false, result: nil, backend: "Aucun",
                        details: "Création du contexte FEX : erreur \(opened).",
                        elfMemoryBridgePassed: elfMemoryBridgePassed,
                        elfControlFlowPassed: elfControlFlowPassed)
        }
        var needsClose = true
        defer { if needsClose { maxps4_fex_engine_close(engine) } }
        var cpuid = [UInt32](repeating: 0, count: 4)
        let cpuidStatus = cpuid.withUnsafeMutableBufferPointer {
            maxps4_fex_engine_cpuid(engine, 0, 0, $0.baseAddress)
        }
        let debugger = maxps4FEXDebuggerStatus()
        let vendorBytes = [cpuid[1], cpuid[3], cpuid[2]].flatMap { word in
            (0..<4).map { UInt8(truncatingIfNeeded: word >> ($0 * 8)) }
        }
        let vendor = String(decoding: vendorBytes, as: UTF8.self)
        let providerBefore = maxps4_jit_state()
        let initialized = maxps4_fex_engine_initialize(engine)
        let providerAfter = maxps4_jit_state()
        let providerError = maxps4_jit_error()
        let mailboxDiagnostic = Int(maxps4_jit_mailbox_diagnostic())
        let mailboxDescriptions = ["session inactive", "nonce absent", "nonce incorrect", "statut non publié", "confirmation rejetée", "confirmation reçue"]
        let mailboxDescription = mailboxDescriptions.indices.contains(mailboxDiagnostic) ? mailboxDescriptions[mailboxDiagnostic] : "inconnu"
        let jitGuidance: String
        if initialized == -Int32(EBUSY) && providerAfter == 1 {
            jitGuidance = "Débogueur encore attaché : canari ARM64 non tenté. Attendre le détachement réussi dans StikDebug, puis réessayer sans réactiver la session."
        } else if initialized == -Int32(EAGAIN) && providerAfter == 1 {
            jitGuidance = "Activation StikDebug en attente : MaxPS4 n’a pas reçu la confirmation des régions RX. Terminer l’activation dans StikDebug, revenir dans MaxPS4, puis relancer le test. Ne pas libérer la mémoire avant ce test."
        } else if initialized == -Int32(EACCES) && providerAfter == 0 {
            jitGuidance = "JIT non préparé : utiliser « Activer avec StikDebug » avant le test FEXCore."
        } else if providerAfter == 3 {
            jitGuidance = "Préparation mémoire JIT refusée (erreur \(providerError)). Consulter l’étape et le code Mach ci-dessous."
        } else if initialized == 0 {
            jitGuidance = "FEXCore JIT initialisé ; vérifier le backend réel du test ci-dessous."
        } else {
            jitGuidance = "Initialisation JIT non disponible (statut \(initialized))."
        }
        var value: UInt64 = 0
        var selected: UInt32 = 0
        var jitStatus = initialized
        var elfEntry: UInt64 = 0
        var guestRIP: UInt64 = 0
        var instructions: UInt32 = 0
        var fallbackAttempted = false
        var status: Int32
        if initialized == 0 {
            let file = MaxPS4SyntheticELFFEXBridge.syntheticELF()
            jitStatus = file.withUnsafeBytes {
                maxps4_fex_engine_run_elf(engine, $0.bindMemory(to: UInt8.self).baseAddress,
                                          $0.count, 3, &value, &selected, &elfEntry)
            }
            status = jitStatus
        } else { status = initialized }
        if status != 0 {
            fallbackAttempted = true
            // Preserve the actual JIT error separately from the interpreter result.
            let file = MaxPS4SyntheticELFFEXBridge.syntheticELF()
            status = file.withUnsafeBytes {
                maxps4_fex_engine_interpret_elf(engine, $0.bindMemory(to: UInt8.self).baseAddress,
                    $0.count, 3, &value, &selected, &elfEntry, &guestRIP, &instructions)
            }
        }
        let stage = maxps4_fex_engine_stage(engine)
        maxps4_fex_engine_close(engine)
        needsClose = false
        let leases = maxps4_jit_leases()
        let name: String
        switch selected {
        case UInt32(MAXPS4_CPU_RESTRICTED_INTERPRETER): name = "Interpréteur natif MaxPS4 (sans JIT FEX)"
        case UInt32(MAXPS4_CPU_FEX_ARM64_JIT): name = "FEXCore JIT ARM64"
        default: name = "Aucun"
        }
        let passed = status == 0 && cpuidStatus == 0 && value == 42 && selected != 0 && leases == 0
        let stageNames = ["inconnu", "mailbox-status", "rx-address-validation", "rx-address-overlap", "rx-protection-check", "vm-remap", "rw-current-protection", "rw-max-protection", "rx-max-protection", "alias-protection-check", "code-cache-guard-page", "canary-alias-coherence", "canary-execution"]
        let failureStage = Int(maxps4_jit_failure_stage())
        let failureName = stageNames.indices.contains(failureStage) ? stageNames[failureStage] : "inconnu"
        let message = """
        Système : \(ProcessInfo.processInfo.operatingSystemVersionString)
        Runtime FEX Darwin : contexte créé, stade \(stage)
        CPUID invité FEX : statut \(cpuidStatus), fournisseur \(vendor), feuille maximale \(cpuid[0])
        Debugger détecté : \(debugger == 1 ? "oui" : debugger == 0 ? "non" : "inconnu") (pas un droit JIT)
        Initialisation FEX JIT : statut \(initialized)
        État fournisseur avant/après : \(providerBefore) → \(providerAfter)
        Diagnostic JIT : \(jitGuidance)
        Confirmation StikDebug : \(mailboxDescription) (code \(mailboxDiagnostic))
        Échec script StikDebug : code \(maxps4_jit_script_failure()) (0 : aucun échec confirmé ; logs StikDebug requis)
        Exécution FEX JIT : statut \(jitStatus) (0 requis pour un succès JIT)
        Point d’entrée ELF64 invité : 0x\(String(elfEntry, radix: 16))
        \(selected == UInt32(MAXPS4_CPU_RESTRICTED_INTERPRETER) ? "RIP invité après repli : 0x" + String(guestRIP, radix: 16) + ", instructions terminées : \(instructions)" : fallbackAttempted ? "Repli interpréteur : échec avant mesure. RIP et compteur indisponibles." : "Repli interpréteur : non utilisé. RIP final et instructions terminées FEX : non exposés par cette API.")
        Fournisseur RX/RW : état \(maxps4_jit_state()), erreur \(maxps4_jit_error())
        Échec JIT précis : \(failureName) (étape \(failureStage))
        Code Mach : \(maxps4_jit_failure_mach()), région : \(maxps4_jit_failure_region())
        Régions encore utilisées après nettoyage : \(leases)
        Exécution x86-64 : statut \(status), résultat \(status == 0 ? String(value) : "absent")
        Backend : \(name)
        Validation : \(passed ? "réussie" : "échec")
        \(selected == UInt32(MAXPS4_CPU_FEX_ARM64_JIT) && status == 0 ? "FEX_ARM64_JIT a réellement exécuté le bloc invité." : "Le résultat de repli ne valide pas le JIT FEXCore.")
        Les régions préparées restent réservées jusqu’à « Libérer la mémoire JIT ».
        La signature SideStore et un debugger ne prouvent pas une autorisation JIT.
        Aucun résultat d'exécution de jeu PS4.
        """
        return Self(succeeded: passed, result: status == 0 ? value : nil,
                    backend: name, details: message,
                    elfMemoryBridgePassed: elfMemoryBridgePassed,
                    elfControlFlowPassed: elfControlFlowPassed)
#else
        return Self(succeeded: false, result: nil, backend: "Aucun",
                    details: "Cette ancienne configuration ne contient pas FEXCore. Utiliser la compilation MaxPS4 FEXCore SideStore.",
                    elfMemoryBridgePassed: false,
                    elfControlFlowPassed: false)
#endif
    }
}



