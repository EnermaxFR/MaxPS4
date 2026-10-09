import Foundation

@_silgen_name("maxps4_native_map_jit_allocation_probe")
private func maxps4NativeMapJITAllocationProbe() -> Int32

enum MaxPS4OnDeviceJITProbe {
    static func report() -> String {
        let mapStatus: String
        switch maxps4NativeMapJITAllocationProbe() {
        case 1: mapStatus = "Allocation MAP_JIT : Autorisée (sans exécution)"
        case 0: mapStatus = "Allocation MAP_JIT : Refusée par iOS"
        default: mapStatus = "Allocation MAP_JIT : Indisponible"
        }
        return """
        Diagnostic JIT iPhone
        Signature / CS_DEBUGGED : \(MaxPS4NativeLinkCheck.codeSigningDebugStatus)
        Débogueur / P_TRACED : \(MaxPS4NativeLinkCheck.debuggerAttachmentDescription)
        Protocole StikDebug intégré : \(MaxPS4NativeLinkCheck.isStikDebugProtocolPresent ? "Oui" : "Non")
        \(mapStatus)
        Entitlement get-task-allow : Non lu directement (vérifier dans StikDebug)
        Exécution ARM64 générée : Non testée par ce diagnostic
        FEXCore : Non validé

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
