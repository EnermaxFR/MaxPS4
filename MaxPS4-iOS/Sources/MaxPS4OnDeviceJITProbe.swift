import Foundation

@_silgen_name("maxps4_native_map_jit_allocation_probe")
private func maxps4NativeMapJITAllocationProbe() -> Int32

@_silgen_name("maxps4_native_map_jit_errno_probe")
private func maxps4NativeMapJITErrnoProbe(_ errorOut: UnsafeMutablePointer<Int32>?) -> Int32

@_silgen_name("maxps4_stikdualmap_port_present")
private func maxps4StikDualMapPortPresent() -> Int32

enum MaxPS4OnDeviceJITProbe {
    // Read-only preflight: does not issue BRK, map executable pages or call the allocator.
    static func stikAllocatorPreflightReport() -> String {
        let integrated = maxps4StikDualMapPortPresent() == 1
        let debugger = MaxPS4NativeLinkCheck.debuggerAttachmentDescription
        let debugSigning = MaxPS4NativeLinkCheck.codeSigningDebugStatus
        let protocolPresent = MaxPS4NativeLinkCheck.isStikDebugProtocolPresent
        return """
        Précontrôle allocateur AetherPS4 / StikDebug (sans BRK)
        Code dual-mapping compilé : \(integrated ? "Oui" : "Non")
        État signature : \(debugSigning)
        État du débogueur : \(debugger)
        Entrées du protocole intégrées : \(protocolPresent ? "Oui" : "Non")
        Protocole source StikDebug : BRK #0xf00d, commande x16=1, arguments x0=0 / x1=taille
        Compatibilité statique : Commande et paramètres concordants avec StikDebug/Scripts/universal.js
        Réponse du serveur debugserver : Non vérifiée (aucune commande envoyée)
        Universal JIT Script attaché : Non vérifiable par ce test
        Allocation RX via StikDebug : Non tentée
        Alias RW via vm_remap : Non tenté ici
        Exécution ARM64 générée : Non tentée
        Activation automatique : Désactivée

        Ce précontrôle ne prouve pas que StikDebug peut servir le protocole BRK. Ne pas activer l'allocateur tant que l'attachement et la gestion du trap ne sont pas validés.
        """
    }

    // CPU + JIT integration regression: actual x86 interpreter result is compared
    // with the ARM64 translator's validated output bytes (not executed).
    static func combinedEngineReport() -> String {
        let cases: [(String, Data, UInt64)] = [
            ("MOV 42 / RET", Data([0xB8, 42, 0, 0, 0, 0xC3]), 42),
            ("MOV 40 + ADD 2 / RET", Data([0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3]), 42),
            ("MOV 50 - SUB 8 / RET", Data([0xB8, 50, 0, 0, 0, 0x2D, 8, 0, 0, 0, 0xC3]), 42)
        ]
        var lines = ["MaxPS4 — tests CPU invité / traducteur ARM64"]
        var passed = 0
        for (name, code, expected) in cases {
            let guestResult = MaxPS4NativeLinkCheck.runNativeSyntheticX86(code)
            let translation = MaxPS4NativeLinkCheck.arm64TranslationPreview(code)
            let accepted = translation != nil &&
                MaxPS4NativeLinkCheck.verifyARM64Preview(translation!)
            let fallback = MaxPS4NativeLinkCheck.runSyntheticX86PreferJIT(code)
            let ok = guestResult == expected && accepted &&
                fallback?.result == expected && fallback?.usedJIT == false
            if ok { passed += 1 }
            lines.append("\(name) : \(ok ? "PASS" : "FAIL") — CPU \(guestResult.map(String.init) ?? "refusé"), ARM64 \(accepted ? "vérifié (données)" : "non validé"), JIT réellement utilisé : \(fallback?.usedJIT == true ? "oui" : "non")")
        }
        lines.append("Tests réussis : \(passed)/\(cases.count)")
        lines.append("Allocateur AetherPS4/StikDebug : \(maxps4StikDualMapPortPresent() == 1 ? "intégré (non activé)" : "indisponible")")
        lines.append("Mémoire exécutable et moteur FEXCore : non validés")
        lines.append("Ces programmes synthétiques ne constituent pas un jeu PS4.")
        return lines.joined(separator: "\n")
    }

    static func report() -> String {
        let mapStatus: String
        var errorCode: Int32 = 0
        let allocation = maxps4NativeMapJITErrnoProbe(&errorCode)
        switch allocation {
        case 1: mapStatus = "Allocation MAP_JIT : Autorisée (sans exécution)"
        case 0: mapStatus = "Allocation MAP_JIT : Refusée par iOS (errno \(errorCode): \(String(cString: strerror(errorCode))))"
        default: mapStatus = "Allocation MAP_JIT : Indisponible"
        }
        return """
        Diagnostic JIT iPhone
        Signature / CS_DEBUGGED : \(MaxPS4NativeLinkCheck.codeSigningDebugStatus)
        Débogueur / P_TRACED : \(MaxPS4NativeLinkCheck.debuggerAttachmentDescription)
        Protocole StikDebug intégré : \(MaxPS4NativeLinkCheck.isStikDebugProtocolPresent ? "Oui" : "Non")
        \(mapStatus)
        Entitlement get-task-allow : Non lu directement (vérifier dans StikDebug)\n        Méthode StikDebug : la confirmation de demande ne garantit pas le succès de mmap(MAP_JIT)
        Exécution ARM64 générée : Non testée par ce diagnostic
        Allocateur AetherPS4/StikDebug : \(maxps4StikDualMapPortPresent() == 1 ? "Code natif intégré, activation non validée" : "Absent")\n        FEXCore : Non validé

        Remarque : P_TRACED absent ne signifie pas à lui seul que StikDebug a échoué. MAP_JIT autorisé ne prouve pas une mémoire RX ni une exécution JIT réussie.
        """
    }
}

@_silgen_name("maxps4_native_generated_arm64_execute_probe")
private func maxps4_native_generated_arm64_execute_probe() -> Int32

extension MaxPS4OnDeviceJITProbe {
    static func generatedARM64ExecutionReport() -> String {
        switch maxps4_native_generated_arm64_execute_probe() {
        case 1:
            return "Instructions ARM64 générées dynamiquement : Exécutées (42). Test isolé réussi ; FEXCore et les jeux PS4 ne sont pas validés."
        case -1:
            return "Test interrompu : aucun débogueur attaché détecté par MaxPS4. Vérifie StikDebug. Aucune instruction générée exécutée."
        case -2:
            return "Test interrompu : allocation MAP_JIT refusée par iOS. Aucun code généré exécuté."
        case -3:
            return "Test interrompu : changement de permissions RW → RX refusé par iOS. Aucun code généré exécuté."
        case -4:
            return "Échec : le code ARM64 généré n’a pas renvoyé 42."
        default:
            return "Exécution ARM64 générée indisponible sur cet appareil."
        }
    }
}
