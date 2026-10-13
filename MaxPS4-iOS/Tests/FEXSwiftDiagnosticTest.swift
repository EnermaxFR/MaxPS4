import Foundation
import Darwin

@main
struct FEXSwiftDiagnosticTest {
    static func main() {
        if CommandLine.arguments.count > 1 {
            let demo = URL(fileURLWithPath: CommandLine.arguments[1])
            let linked = MaxPS4FEXDiagnostic.executeTestELF(url: demo)
            precondition(linked.contains("Exécution FEX ARM64 : réussie"))
            precondition(linked.contains("RAX : 2") && linked.contains("appels libkernel : 1"))
            print("PASS Swift imported Orbis ELF -> native linker -> real FEX/libkernel -> RAX 2")
        }
        for iteration in 0..<8 {
            let report = autoreleasepool { MaxPS4FEXDiagnostic.run() }
            precondition(report.succeeded && report.result == 42)
            precondition(report.backend == "FEXCore JIT ARM64")
            precondition(report.details.contains("GenuineIntel"))
            precondition(report.details.contains("Debugger détecté"))
            precondition(report.details.contains("Point d’entrée ELF64 invité : 0x1000"))
            print("PASS Swift opaque-C FEX diagnostic iteration=\(iteration) result=42 backend=\(report.backend)")
        }
        do {
            var engine: OpaquePointer?
            precondition(maxps4_fex_engine_open(&engine) == 0)
            guard let engine else { preconditionFailure("Missing engine") }
            defer { maxps4_fex_engine_close(engine) }
            var value: UInt64 = 0; var entry: UInt64 = 0; var rip: UInt64 = 0
            var backend: UInt32 = 0; var instructions: UInt32 = 0
            let file = MaxPS4SyntheticELFFEXBridge.syntheticELF()
            let status = file.withUnsafeBytes {
                maxps4_fex_engine_interpret_elf(engine, $0.bindMemory(to: UInt8.self).baseAddress,
                    $0.count, 3, &value, &backend, &entry, &rip, &instructions)
            }
            precondition(status == 0 && value == 42 && entry == 0x1000 && rip == 0x100b)
            precondition(backend == UInt32(MAXPS4_CPU_RESTRICTED_INTERPRETER) && instructions == 3)
            precondition(maxps4_fex_engine_stage(engine) == MAXPS4_FEX_CONTEXT_CREATED)
            print("PASS Swift/C loaded ELF interpreter, logical RIP, no initialized JIT dispatcher")
        }
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + ".elf")
        defer { try? FileManager.default.removeItem(at: url) }
        do {
            var file = MaxPS4SyntheticELFFEXBridge.syntheticELF()
            file[121] = 100; file[126] = 23
            try file.write(to: url)
            let imported = MaxPS4FEXDiagnostic.executeTestELF(url: url)
            precondition(imported.contains("Statut : 0") && imported.contains("RAX : 123"))
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
        do {
            var file = MaxPS4SyntheticELFFEXBridge.syntheticELF()
            file[7] = 9; file[16] = 0; file[17] = 0xfe
            // Real opaque engine staging API, independent of JIT initialization.
            var engine: OpaquePointer?
            precondition(maxps4_fex_engine_open(&engine) == 0)
            guard let engine else { preconditionFailure("Missing engine") }
            defer { maxps4_fex_engine_close(engine) }
            var entry: UInt64 = 99; var segments: UInt32 = 99; var blockers: UInt32 = 99
            let staged = file.withUnsafeBytes {
                maxps4_fex_engine_stage_ps4(engine, $0.bindMemory(to: UInt8.self).baseAddress,
                    $0.count, &entry, &segments, &blockers)
            }
            precondition(staged == 0 && entry == 0x1000 && segments == 1 && blockers == 4)
            precondition(maxps4_fex_engine_stage(engine) == MAXPS4_FEX_CONTEXT_CREATED)
            var first = [UInt8](repeating: 0, count: 5)
            precondition(first.withUnsafeMutableBufferPointer {
                maxps4_fex_engine_read_ps4(engine, entry, $0.baseAddress, $0.count)
            } == 0 && first[0] == 0xb8)
            let saved = first
            precondition(first.withUnsafeMutableBufferPointer {
                maxps4_fex_engine_read_ps4(engine, UInt64.max, $0.baseAddress, $0.count)
            } == -EFAULT && first == saved)
            try file.write(to: url)
            let report = MaxPS4FEXDiagnostic.stagePS4(url: url)
            precondition(report.contains("Premier octet relu : 0xb8") && report.contains("non exécuté"))
            file[7] = 0
            let rejected = file.withUnsafeBytes {
                maxps4_fex_engine_stage_ps4(engine, $0.bindMemory(to: UInt8.self).baseAddress,
                    $0.count, &entry, &segments, &blockers)
            }
            precondition(rejected == -ENOEXEC && entry == 0 && segments == 0 && blockers == 0)
            precondition(first.withUnsafeMutableBufferPointer {
                maxps4_fex_engine_read_ps4(engine, 0x1000, $0.baseAddress, $0.count)
            } == -EFAULT)
            print("PASS Swift/C PS4 staging + entry readback + unchanged dispatcher stage + atomic rejection")
        } catch { preconditionFailure("PS4 native staging failed: \(error)") }
        print("PASS Swift ELF file bytes -> native mappings -> real FEX JIT RAX=123; unsupported ELF rejected")
        print("PASS Swift lifecycle; macOS host execution only, no iPhone proof")
    }
}

