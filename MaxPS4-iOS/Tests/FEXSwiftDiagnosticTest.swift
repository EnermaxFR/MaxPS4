import Foundation
import Darwin

@main
struct FEXSwiftDiagnosticTest {
    static func main() {
        // Reproduce the documented PKG -> ELF error, even with an .elf suffix.
        // Use a sparse oversized container to verify refusal precedes ELF budgets.
        let container = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + ".elf")
        defer { try? FileManager.default.removeItem(at: container) }
        do {
            try Data([0x7f, 0x43, 0x4e, 0x54]).write(to: container)
            let writer = try FileHandle(forWritingTo: container)
            try writer.truncate(atOffset: 128 * 1024 * 1024)
            try writer.close()
        } catch { fatalError("Could not create synthetic CNT: \(error)") }
        let previousJIT = maxps4_jit_state()
        let missingM1 = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        let m1Refusal = MaxPS4FEXDiagnostic.testM1Collision(directory: missingM1)
        precondition(m1Refusal.contains("fixture_error="), m1Refusal)
        let experimentalRefusal = MaxPS4FEXDiagnostic.testEboot(url: missingM1, experimental: true)
        precondition(experimentalRefusal.contains("EBOOT.FILE") || experimentalRefusal.contains("unavailable"))
        let ebootRefusal = MaxPS4FEXDiagnostic.testEboot(url: missingM1)
        precondition(ebootRefusal.contains("EBOOT.FILE") || ebootRefusal.contains("unavailable"))
        let cRefusal = MaxPS4FEXDiagnostic.testCGameBoot(url: missingM1)
        precondition(cRefusal.contains("fixture_error="), cRefusal)
        precondition(maxps4_jit_state() == previousJIT)
        for report in [MaxPS4FEXDiagnostic.executeTestELF(url: container),
                       MaxPS4FEXDiagnostic.stagePS4(url: container)] {
            precondition(report.contains("[ELF.CONTAINER]") && report.contains("PKG/CNT détecté"), report)
            precondition(report.contains("Aucune initialisation JIT ni exécution tentée"), report)
        }
        precondition(maxps4_jit_state() == previousJIT)
        // The distinct V4 action requires an explicit ABI opt-in before opening
        // an engine or preparing JIT. Old manifests remain valid for the old API.
        let v4Directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: v4Directory) }
        do {
            try FileManager.default.createDirectory(at: v4Directory, withIntermediateDirectories: true)
            try Data([0x7f, 0x45, 0x4c, 0x46]).write(to: v4Directory.appendingPathComponent("eboot.bin"))
            var manifest: [String: Any] = ["root": "eboot.bin", "modules": [
                ["path": "eboot.bin", "loadBias": 262144, "dependencies": [String]()]
            ]]
            for abi in ["", "ps4-startup", "synthetic-hlt-v1"] {
                if !abi.isEmpty { manifest["v4ABI"] = abi }
                // Even the supported ABI is refused without an explicit entry.
                try JSONSerialization.data(withJSONObject: manifest).write(to: v4Directory.appendingPathComponent("maxps4.json"))
                let refusal = MaxPS4FEXDiagnostic.executeOrbisDirectory(url: v4Directory, translatedV4: true)
                precondition(refusal.contains("V4 refusé (-ENOTSUP)"), refusal)
                precondition(maxps4_jit_state() == previousJIT)
            }
        } catch { fatalError("V4 manifest fixture failed: \(error)") }
        precondition(MaxPS4FEXDiagnostic.pkgExecutionRefusal(prefix: Data([0x7f,0x45,0x4c,0x46])) == nil)
        precondition(MaxPS4FEXDiagnostic.pkgExecutionRefusal(prefix: Data([0x7f,0x43])) == nil)
        print("PASS imported CNT rejected before ELF allocation, engine creation and JIT; suffix ignored")
        let m2 = MaxPS4FEXDiagnostic.testM2LowVA()
        precondition(m2.contains("Validation M2 : réussie"), m2)
        precondition(m2.contains("JIT : 42") && m2.contains("TLS : 8"), m2)
        precondition(m2.contains("Collision préservée : 1 ; nettoyage : 1 ; code RX : 1 ; pile RW/NX : 1"), m2)
        precondition(m2.contains("Limites mappings : statut 0 ; refus vérifiés 16/16 ; mémoire préservée 1"), m2)
        precondition(m2.contains("Paires scalaires : exécutées 11/11 ; réussies 11/11 ; statut 0 ; exécution 0 ; vérification 0"), m2)
        precondition(m2.contains("Refus scalaires : statut 0 ; vérifiés 55/55 ; mémoire préservée 1"), m2)
        var counters = maxps4_m2_low_va_report()
        precondition(maxps4_fex_test_m2_low_va(&counters) == 0)
        precondition(MaxPS4FEXDiagnostic.m2Passed(0, counters))
        counters.scalar_passed = 10
        precondition(!MaxPS4FEXDiagnostic.m2Passed(0, counters))
        counters.scalar_passed = 11
        counters.scalar_executed = 10
        precondition(!MaxPS4FEXDiagnostic.m2Passed(0, counters))
        counters.scalar_executed = 11
        counters.scalar_verify_status = -ECANCELED
        precondition(!MaxPS4FEXDiagnostic.m2Passed(0, counters))
        var incomplete = maxps4_m2_low_va_report()
        incomplete.scalar_expected = 11
        incomplete.scalar_executed = 3
        incomplete.scalar_passed = 2
        incomplete.scalar_status = -EFAULT
        incomplete.scalar_execute_status = -EFAULT
        incomplete.scalar_verify_status = -ECANCELED
        let failed = MaxPS4FEXDiagnostic.scalarM2Details(incomplete)
        precondition(failed.contains("exécutées 3/11 ; réussies 2/11 ; statut \(-EFAULT) ; exécution \(-EFAULT) ; vérification \(-ECANCELED)"), failed)
        for _ in 0..<3 {
            let linked = MaxPS4FEXDiagnostic.testPS4Libkernel()
            precondition(linked.contains("Chargement ELF : réussi (0)"))
            precondition(linked.contains("NID recherché : aI+OeCz8xrQ") && linked.contains("Fonction résolue : scePthreadSelf"))
            precondition(linked.contains("FEXCore JIT ARM64 natif (2)"))
            precondition(linked.contains("Résultat appel libkernel [0x10108] : 1 (attendu 1)"))
            precondition(linked.contains("Retour au code invité : confirmé"))
            precondition(linked.contains("Valeur finale en mémoire invitée [0x10100] : 2 (attendu 2)"))
            precondition(linked.contains("Segments invités après retrait : 0") && linked.contains("Validation globale : réussie"))
            precondition(linked.contains("hôte macOS — aucune preuve iPhone"))
            precondition(linked.contains("Paires scalaires : exécutées 11/11 ; réussies 11/11 ; statut 0"), linked)
            precondition(linked.contains("Refus scalaires : statut 0 ; vérifiés 55/55 ; mémoire préservée 1"), linked)
            print(linked)
        }
        if CommandLine.arguments.count > 1 {
            let demo = URL(fileURLWithPath: CommandLine.arguments[1])
            let linked = MaxPS4FEXDiagnostic.executeTestELF(url: demo)
            precondition(linked.contains("Exécution FEX ARM64 : réussie"))
            precondition(linked.contains("RAX : 2") && linked.contains("appels libkernel : 1"))
            print("PASS Swift imported Orbis ELF -> native linker -> real FEX/libkernel -> RAX 2")
        }
        if CommandLine.arguments.count > 2 {
            let directory = URL(fileURLWithPath: CommandLine.arguments[2])
            let v4 = MaxPS4FEXDiagnostic.executeOrbisDirectory(url: directory.appendingPathComponent("v4-synthetic"), translatedV4: true)
            precondition(v4.contains("PRX synthétique V4 exécuté") && v4.contains("RAX=42"), v4)
            let result = MaxPS4FEXDiagnostic.executeOrbisDirectory(url: directory)
            precondition(result.contains("Exécution FEX ARM64 natif terminée : RAX = 42"), result)
            print("PASS Swift authorized manifest -> SCE ELF/PRX -> FEX native ARM64 -> RAX 42")
            let missing = MaxPS4FEXDiagnostic.executeOrbisDirectory(url: directory.appendingPathComponent("absent"))
            precondition(missing.contains("Import Orbis refusé"))
        }
        for iteration in 0..<8 {
            let report = autoreleasepool { MaxPS4FEXDiagnostic.run() }
            precondition(report.succeeded && report.result == 42)
            precondition(report.backend == "FEXCore JIT ARM64")
            precondition(report.details.contains("GenuineIntel"))
            precondition(report.details.contains("Debugger détecté"))
            precondition(report.details.contains("Point d’entrée ELF64 invité : 0x1000"))
            precondition(report.details.contains("RIP final et instructions terminées FEX : non exposés"))
            precondition(!report.details.contains("RIP invité après repli : 0x0"))
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


