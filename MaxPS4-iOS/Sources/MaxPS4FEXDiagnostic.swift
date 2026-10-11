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
        defer { maxps4_fex_engine_close(engine) }
        var cpuid = [UInt32](repeating: 0, count: 4)
        let cpuidStatus = cpuid.withUnsafeMutableBufferPointer {
            maxps4_fex_engine_cpuid(engine, 0, 0, $0.baseAddress)
        }
        let initialized = maxps4_fex_engine_initialize(engine)
        var value: UInt64 = 0
        var selected: UInt32 = 0
        let status: Int32
        if initialized == 0 {
            status = maxps4_fex_engine_probe(engine, &value, &selected)
        } else if initialized == -Int32(ENOTSUP) {
            // Real bounded interpreter from the existing native bridge, not FEX JIT.
            let code: [UInt8] = [0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3]
            status = code.withUnsafeBufferPointer {
                maxps4_fex_engine_run(engine, $0.baseAddress, $0.count, 32, &value, &selected)
            }
        } else {
            status = initialized // Unexpected initialization failures are never hidden.
        }
        let name: String
        switch selected {
        case UInt32(MAXPS4_CPU_RESTRICTED_INTERPRETER): name = "Interpréteur natif MaxPS4 (sans JIT FEX)"
        case UInt32(MAXPS4_CPU_FEX_ARM64_JIT): name = "FEXCore JIT ARM64"
        default: name = "Aucun"
        }
        let passed = status == 0 && cpuidStatus == 0 && value == 42 && selected != 0
        let message = """
        Système : \(ProcessInfo.processInfo.operatingSystemVersionString)
        Runtime FEX Darwin : contexte créé, stade \(maxps4_fex_engine_stage(engine))
        CPUID FEX : statut \(cpuidStatus), feuille maximale \(cpuid[0])
        Initialisation JIT : \(initialized == -Int32(ENOTSUP) ? "indisponible sur iOS (ENOTSUP)" : "statut \(initialized)")
        Exécution x86-64 : statut \(status), résultat \(status == 0 ? String(value) : "absent")
        Backend : \(name)
        Validation : \(passed ? "réussie" : "échec")
        Le JIT iOS reste verrouillé : aucun fournisseur de mémoire exécutable conforme n'est intégré.
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
