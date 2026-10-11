import Foundation
import Darwin

@main
struct FEXSwiftDiagnosticTest {
    static func main() {
        for iteration in 0..<8 {
            let report = autoreleasepool { MaxPS4FEXDiagnostic.run() }
            precondition(report.succeeded && report.result == 42)
            precondition(report.backend == "FEXCore JIT ARM64")
            precondition(report.details.contains("GenuineIntel"))
            precondition(report.details.contains("Debugger détecté"))
            precondition(report.details.contains("Point d’entrée ELF64 invité : 0x1000"))
            print("PASS Swift opaque-C FEX diagnostic iteration=\(iteration) result=42 backend=\(report.backend)")
        }
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + ".elf")
        defer { try? FileManager.default.removeItem(at: url) }
        do {
            var file = MaxPS4SyntheticELFFEXBridge.syntheticELF()
            file[121] = 100; file[126] = 23
            try file.write(to: url)
            let result = try MaxPS4SyntheticELFFEXBridge.run(url: url)
            precondition(result.rax == 123 && result.backend == UInt32(MAXPS4_CPU_FEX_ARM64_JIT))
            file[130] = 0xC3
            try file.write(to: url)
            do {
                _ = try MaxPS4SyntheticELFFEXBridge.run(url: url)
                preconditionFailure("RET ELF must be rejected without interpreter fallback")
            } catch MaxPS4SyntheticELFFEXBridge.BridgeError.nativeFailure(let status) {
                precondition(status == -ENOTSUP)
            }
        } catch { preconditionFailure("Native Swift ELF execution failed: \(error)") }
        print("PASS Swift ELF file bytes -> native mappings -> real FEX JIT RAX=123; unsupported ELF rejected")
        print("PASS Swift lifecycle; macOS host execution only, no iPhone proof")
    }
}
