import Foundation

/// Host-side smoke tests for isolated prototypes, including ELF/CPU integration. No PS4 executable is run.
@main
struct PrototypeSmokeTest {
    static func main() {
        let checks: [(String, Bool)] = [
            ("Mémoire invitée", MaxPS4GuestMemory.selfTest()),
            ("CPU x86-64", MaxPS4CPUPrototype.selfTest()),
            ("Appels CALL/RET et branchements", MaxPS4CPUPrototype.callAndBranchSelfTest()),
            ("Sauts JMP rel32 bornés", MaxPS4CPUPrototype.nearJumpSelfTest()),
            ("ADD/SUB et pile invitée", MaxPS4CPUPrototype.arithmeticAndStackSelfTest()),
            ("AND bit-à-bit et drapeau zéro", MaxPS4CPUPrototype.bitwiseAndSelfTest()),
            ("OR bit-à-bit et drapeau zéro", MaxPS4CPUPrototype.bitwiseOrSelfTest()),
            ("CMP registres et drapeau zéro", MaxPS4CPUPrototype.compareRegistersSelfTest()),
            ("XOR inverse et validation", MaxPS4CPUPrototype.xorRegisterSelfTest()),
            ("Arithmétique 64 bits, CMP et branchements", MaxPS4CPUPrototype.immediate32AndControlFlowSelfTest()),
            ("Sauts conditionnels longs JZ/JNZ", MaxPS4CPUPrototype.nearConditionalBranchSelfTest()),
            ("Boucles x86-64 bornées et limite CPU", MaxPS4CPUPrototype.backwardLoopAndLimitSelfTest()),
            ("TEST EAX immédiat et branchement", MaxPS4CPUPrototype.testImmediateAndBranchSelfTest()),
            ("TEST AL et TEST registre immédiat", MaxPS4CPUPrototype.extendedTestOpcodeSelfTest()),
            ("Décalages SHL SHR SAR x86-64", MaxPS4CPUPrototype.shiftRegisterSelfTest()),
            ("Arithmétique immédiate 8 bits et branchements", MaxPS4CPUPrototype.immediate8AndBranchesSelfTest()),
            ("Chargeur ELF64", MaxPS4ELFLoader.selfTest()),
            ("Validation des segments ELF64", MaxPS4ELFLoader.segmentPreflightSelfTest()),
            ("Importations ELF64 synthétiques", MaxPS4ELFLoader.importDemoSelfTest()),
            ("Relocalisation relative x86-64", MaxPS4ELFLoader.relativeRelocationSelfTest()),
            ("Relocalisations groupées atomiques", MaxPS4ELFLoader.relocationBatchSelfTest()),
            ("Inventaire des relocalisations", MaxPS4ELFLoader.relocationSelfTest()),
            ("Intégration ELF-CPU-mémoire", MaxPS4ELFLoader.integrationTest()),
            ("ELF-CPU : branchement conditionnel et écriture mémoire", MaxPS4ELFLoader.controlFlowIntegrationTest())
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
