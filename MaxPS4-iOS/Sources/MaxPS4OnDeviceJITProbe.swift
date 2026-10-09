import Foundation

@_silgen_name("maxps4_native_map_jit_allocation_probe")
private func maxps4NativeMapJITAllocationProbe() -> Int32

enum MaxPS4OnDeviceJITProbe {
    static func report() -> String {
        switch maxps4NativeMapJITAllocationProbe() {
        case 1:
            return "iPhone : allocation MAP_JIT autorisée. Cela ne prouve pas que le code ARM64 généré s’exécute, ni que FEXCore fonctionne."
        case 0:
            return "iPhone : allocation MAP_JIT refusée ou indisponible. Vérifie StikDebug et la signature de l’application. Aucun code généré n’a été exécuté."
        default:
            return "Test MAP_JIT non pris en charge sur cet appareil."
        }
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
