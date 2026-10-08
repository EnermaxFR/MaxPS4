import Foundation

/// Host-side smoke test for isolated prototypes. No PS4 executable is run.
@main
struct PrototypeSmokeTest {
    static func main() {
        let checks: [(String, Bool)] = [
            ("Mémoire invitée", MaxPS4GuestMemory.selfTest()),
            ("CPU x86-64", MaxPS4CPUPrototype.selfTest()),
            ("Appels CALL/RET et branchements", MaxPS4CPUPrototype.callAndBranchSelfTest()),
            ("Sauts JMP rel32 bornés", MaxPS4CPUPrototype.nearJumpSelfTest()),
            ("Chargeur ELF64", MaxPS4ELFLoader.selfTest()),
            ("Importations ELF64 synthétiques", MaxPS4ELFLoader.importDemoSelfTest()),
            ("Relocalisation relative x86-64", MaxPS4ELFLoader.relativeRelocationSelfTest()),
            ("Relocalisations groupées atomiques", MaxPS4ELFLoader.relocationBatchSelfTest()),
            ("Inventaire des relocalisations", MaxPS4ELFLoader.relocationSelfTest()),
            ("Intégration ELF-CPU-mémoire", MaxPS4ELFLoader.integrationTest())
        ]
        for (name, passed) in checks {
            print("\(passed ? "PASS" : "FAIL") — \(name)")
        }
        guard checks.allSatisfy({ $0.1 }) else {
            exit(EXIT_FAILURE)
        }
        print("Tous les auto-tests du prototype sont réussis.")
    }
}
