import Foundation
#if canImport(Darwin)
import Darwin
#endif

struct MaxPS4FEXDiagnostic: Sendable {
    let succeeded: Bool
    let result: UInt64?
    let backend: String
    let details: String

    static func run() -> Self {
#if MAXPS4_FEX_APP
        var engine: OpaquePointer?
        let opened = maxps4_fex_engine_open(&engine)
        guard opened == 0, let engine else {
            return Self(succeeded: false, result: nil, backend: "Aucun",
                        details: "Création du contexte FEX : erreur \(opened).")
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
        let initialized = maxps4_fex_engine_initialize(engine)
        var value: UInt64 = 0
        var selected: UInt32 = 0
        var jitStatus = initialized
        var status: Int32
        if initialized == 0 {
            jitStatus = maxps4_fex_engine_probe(engine, &value, &selected)
            status = jitStatus
        } else { status = initialized }
        if status != 0 {
            // Preserve the actual JIT error separately from the interpreter result.
            let code: [UInt8] = [0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3]
            status = code.withUnsafeBufferPointer {
                maxps4_fex_engine_run(engine, $0.baseAddress, $0.count, 32, &value, &selected)
            }
        }
        var additionalProbes = "Tests JIT supplémentaires : non exécutés (initialisation ou premier test échoué)"
        if initialized == 0 && jitStatus == 0 && selected == UInt32(MAXPS4_CPU_FEX_ARM64_JIT) {
            var lines: [String] = []
            for testCase in 1...3 {
                var probeValue: UInt64 = 0
                var probeBackend: UInt32 = 0
                let probeStatus = maxps4_fex_engine_probe_case(engine, UInt32(testCase), &probeValue, &probeBackend)
                let ok = probeStatus == 0 && probeValue == 42 && probeBackend == UInt32(MAXPS4_CPU_FEX_ARM64_JIT)
                let verdict = ok ? "PASS" : "ÉCHEC"
                lines.append("Bloc \(testCase + 1) : \(verdict) (statut \(probeStatus), résultat \(probeValue), backend \(probeBackend))")
            }
            additionalProbes = "Tests JIT supplémentaires :\\n" + lines.joined(separator: "\\n")
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
        Exécution FEX JIT : statut \(jitStatus) (0 requis pour un succès JIT)
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
                    backend: name, details: message)
#else
        return Self(succeeded: false, result: nil, backend: "Aucun",
                    details: "Cette ancienne configuration ne contient pas FEXCore. Utiliser la compilation MaxPS4 FEXCore SideStore.")
#endif
    }
}
