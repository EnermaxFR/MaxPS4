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
