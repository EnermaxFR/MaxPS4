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
            "◌ Chargeur SELF/ELF PS4 : prototype d’analyse, pas de chargement exécutable complet",
            "◌ Mémoire invitée PS4 : mappage natif disponible par diagnostic de fichier ; dispatch CPU bloqué",
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
                "JIT iPhone : garde-fou ENOTSUP conservé ; aucune instruction PS4 exécutée",
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
            let status = file.withUnsafeBytes { bytes in
                let pointer = bytes.bindMemory(to: UInt8.self).baseAddress
                if jit == 0 {
                    return maxps4_fex_engine_run_elf(engine, pointer, bytes.count, 65536,
                        &value, &backend, &entry)
                }
                return maxps4_fex_engine_interpret_elf(engine, pointer, bytes.count, 65536,
                    &value, &backend, &entry, &rip, &count)
            }
            let name = backend == UInt32(MAXPS4_CPU_FEX_ARM64_JIT) ? "FEXCore JIT ARM64" :
                backend == UInt32(MAXPS4_CPU_RESTRICTED_INTERPRETER) ? "Interpréteur ELF sans JIT" : "Aucun"
            return [
                "ELF importé : " + url.lastPathComponent,
                "Statut : \(status) ; initialisation JIT : \(jit) ; backend : \(name)",
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
        if initialized == -45 && providerAfter == 1 {
            jitGuidance = "Activation StikDebug en attente : MaxPS4 n’a pas reçu la confirmation des régions RX. Terminer l’activation dans StikDebug, revenir dans MaxPS4, puis relancer le test. Ne pas libérer la mémoire avant ce test."
        } else if initialized == -45 && providerAfter == 0 {
            jitGuidance = "JIT non préparé : utiliser « Activer avec StikDebug » avant le test FEXCore."
        } else if providerAfter == 3 {
            jitGuidance = "Préparation mémoire JIT refusée (erreur \\(providerError)). Consulter l’étape et le code Mach ci-dessous."
        } else if initialized == 0 {
            jitGuidance = "FEXCore JIT initialisé ; vérifier le backend réel du test ci-dessous."
        } else {
            jitGuidance = "Initialisation JIT non disponible (statut \\(initialized))."
        }
        var value: UInt64 = 0
        var selected: UInt32 = 0
        var jitStatus = initialized
        var elfEntry: UInt64 = 0
        var guestRIP: UInt64 = 0
        var instructions: UInt32 = 0
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
        Exécution FEX JIT : statut \(jitStatus) (0 requis pour un succès JIT)
        Point d’entrée ELF64 invité : 0x\(String(elfEntry, radix: 16))
        RIP invité après repli : 0x\(String(guestRIP, radix: 16)), instructions terminées : \(instructions)
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

