import Foundation

/// Host-side smoke test for isolated prototypes. No PS4 executable is run.
@main
struct PrototypeSmokeTest {
    static func main() {
        let checks: [(String, Bool)] = [
            ("Mémoire invitée", MaxPS4GuestMemory.selfTest()),
            ("CPU x86-64", MaxPS4CPUPrototype.selfTest()),
            ("Chargeur ELF64", MaxPS4ELFLoader.selfTest())
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
