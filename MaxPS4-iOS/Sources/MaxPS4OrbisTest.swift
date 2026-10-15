import Foundation
import CryptoKit

/// One immutable ELF snapshot is inspected and executed. No URL, filename or
/// executable contents enter the exportable diagnostic. Never falls back to an interpreter.
enum MaxPS4OrbisTest {
    struct Inspection {
        let bytes: Data
        let report: String
        let ready: Bool
        let export: String
    }
    static func fixture(initial: UInt32 = 40) -> Data {
        var bytes = [UInt8](repeating: 0, count: 131)
        func put(_ value: UInt64, _ at: Int, _ width: Int) {
            for i in 0..<width { bytes[at+i] = UInt8(truncatingIfNeeded: value >> (8*i)) }
        }
        bytes.replaceSubrange(0..<9, with: [0x7f, 69, 76, 70, 2, 1, 1, 9, 0])
        put(0xfe00,16,2); put(62,18,2); put(1,20,4); put(0x1000,24,8)
        put(64,32,8); put(64,52,2); put(56,54,2); put(1,56,2)
        put(1,64,4); put(5,68,4); put(120,72,8); put(0x1000,80,8)
        put(11,96,8); put(32,104,8); put(1,112,8)
        bytes.replaceSubrange(120..<131, with: [0xb8,40,0,0,0,0x05,2,0,0,0,0xf4])
        put(UInt64(initial),121,4)
        return Data(bytes)
    }
    static func inspect(_ bytes: Data) -> Inspection {
        var analysis = [CChar](repeating: 0, count: 4*1024*1024)
        var preparation = [CChar](repeating: 0, count: 16384)
        var image: OpaquePointer?
        let statuses = bytes.withUnsafeBytes { raw -> (Int32, Int32) in
            let data = raw.bindMemory(to: UInt8.self)
            let inspected = analysis.withUnsafeMutableBufferPointer {
                maxps4_orbis_bytes_report(data.baseAddress, data.count, $0.baseAddress, $0.count)
            }
            let linked = "test.elf".withCString { path in
                var module = maxps4_orbis_module(path: path, bytes: data.baseAddress,
                    size: data.count, load_bias: 0, dependencies: nil, dependency_count: 0)
                return preparation.withUnsafeMutableBufferPointer {
                    maxps4_orbis_link_with_report(&module, 1, 32*1024*1024, &image, $0.baseAddress, $0.count)
                }
            }
            return (inspected, linked)
        }
        maxps4_orbis_close(image)
        let digest = SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined()
        let revision = Bundle.main.object(forInfoDictionaryKey: "MaxPS4SourceRevision") as? String ?? "unknown"
        let exported = "MaxPS4 Orbis Test v3\nsource=\(revision)\nsha256=\(digest)\nbytes=\(bytes.count)\nanalysis=\(statuses.0)\nlink=\(statuses.1)\nexecution=not_attempted"
        return Inspection(bytes: bytes,
            report: String(cString: analysis) + "\n\n" + String(cString: preparation),
            ready: statuses.0 == 0 && statuses.1 == 0, export: exported)
    }
    static func autotests() -> String {
        var results = ["autotests=orbis-v3", "scope=current_host"]
        for initial in [UInt32(40), 7, 40, 7] {
            results.append("expected=\(initial + 2)\n" + run(inspect(fixture(initial: initial))))
        }
        var corrupt = fixture(); corrupt[68] = 7
        results.append("wx_refused=\(!inspect(corrupt).ready)")
        results.append("truncated_refused=\(!inspect(Data(fixture().prefix(63))).ready)")
        return results.joined(separator: "\n")
    }
    // Exact native-test fixtures; all pointers remain alive until the ABI returns.
    static func m1Autotests(bundleDirectory: URL? = nil) -> String {
        let revision = Bundle.main.object(forInfoDictionaryKey: "MaxPS4SourceRevision") as? String ?? "unknown"
        let buildRun = Bundle.main.object(forInfoDictionaryKey: "MaxPS4BuildRun") as? String ?? "unknown"
        let head = Bundle.main.object(forInfoDictionaryKey: "MaxPS4PRHeadRevision") as? String ?? "unknown"
        var lines = ["autotests=orbis-m1", "source=\(revision)", "pr_head=\(head)",
                     "build_run=\(buildRun)", "scope=current_host", "fixture=m1_two_threads_three_tls_modules_8gib",
                     "scheduler=deterministic_run_to_completion", "tls_abi=fixture_fs_offsets",
                     "m1_general_memory_translation=SKIP", "sce_dynamic_tls_abi=SKIP"]
#if MAXPS4_FEX_APP
        let directory = bundleDirectory ?? Bundle.main.resourceURL?.appendingPathComponent("OrbisM1")
        guard let directory,
              let elf = try? Data(contentsOf: directory.appendingPathComponent("m1-two-threads-three-tls-modules.elf")),
              let moduleA = try? Data(contentsOf: directory.appendingPathComponent("tls-83.prx")),
              let moduleB = try? Data(contentsOf: directory.appendingPathComponent("tls-129.prx")) else {
            return (lines + ["m1_threads=BLOCKED", "reason=dedicated_fixture_missing"]).joined(separator: "\n")
        }
        for (name, bytes) in [("elf", elf), ("tls83", moduleA), ("tls129", moduleB)] {
            let digest = SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined()
            lines.append("\(name)_sha256=\(digest)")
        }
        var engine: OpaquePointer?
        let opened = maxps4_fex_engine_open(&engine)
        lines.append("engine_open=\(opened)")
        guard opened == 0, let engine else {
            return (lines + ["m1_threads=BLOCKED", "reason=engine_open_failed"]).joined(separator: "\n")
        }
        defer { maxps4_fex_engine_close(engine) }
        let initialized = maxps4_fex_engine_initialize(engine)
        lines.append("jit_initialize=\(initialized)")
        guard initialized == 0 else {
            return (lines + ["m1_threads=BLOCKED", "reason=jit_initialization_failed"]).joined(separator: "\n")
        }
        let snapshot: UInt64 = 0x200010100
        func execute(_ bytes: Data, budget: UInt32 = 200) -> (Int32, [maxps4_m1_thread_report]) {
            var reports = [maxps4_m1_thread_report](repeating: maxps4_m1_thread_report(), count: 2)
            let status = bytes.withUnsafeBytes { raw in
                moduleA.withUnsafeBytes { a in
                    moduleB.withUnsafeBytes { b in
                        let inputs = [maxps4_m1_tls_input(bytes: a.bindMemory(to: UInt8.self).baseAddress, size: a.count),
                                      maxps4_m1_tls_input(bytes: b.bindMemory(to: UInt8.self).baseAddress, size: b.count)]
                        return inputs.withUnsafeBufferPointer { modules in
                            reports.withUnsafeMutableBufferPointer { output in
                                maxps4_fex_engine_run_m1_threads(engine,
                                    raw.bindMemory(to: UInt8.self).baseAddress, raw.count,
                                    budget, snapshot, modules.baseAddress, modules.count, 2, output.baseAddress, output.count)
                            }
                        }
                    }
                }
            }
            return (status, reports)
        }
        var passed = true
        for run in 1...2 {
            let (status, reports) = execute(elf)
            lines.append("run=\(run) m1_thread_api_status=\(status)")
            guard status == 0 else {
                let reason: String
                switch status {
                case -48, -98: reason = "direct_mapping_collision"
                case -12: reason = "vm_or_allocation_refused"
                case -13, -1: reason = "vm_or_execution_permission_denied"
                case -45, -95: reason = "unsupported_mapping_instruction_or_tls"
                case -22: reason = "invalid_fixture_or_api_contract"
                default: reason = "runtime_refused_status_\(status)"
                }
                return (lines + ["m1_threads=BLOCKED", "m1_multi_module_tls=SKIP", "reason=\(reason)"]).joined(separator: "\n")
            }
            for (i, t) in reports.enumerated() {
                let c = t.cpu
                let ok = t.joined == 1 && t.id == UInt64(i+1) && c.kernel_return == t.id &&
                    c.rax == 131 && c.memory == 131 && c.tls_word == 22 && c.heap_word == 22 &&
                    c.zero_memory == 0 && t.modules.0 == 84 && t.modules.1 == 131 &&
                    t.zeros.0 == 0 && t.zeros.1 == 0 && t.lock_result == 0 && t.unlock_result == 0 &&
                    c.calls == 4 && c.entry == 0x200004000 && (c.rsp & 15) == 0 &&
                    c.backend == UInt32(MAXPS4_CPU_FEX_ARM64_JIT)
                passed = passed && ok
                lines.append("thread=\(t.id) result=\(ok ? "PASS" : "FAIL") joined=\(t.joined) backend=\(c.backend) rax=\(c.rax) memory=\(c.memory) tls=\(c.tls_word) modules=\(t.modules.0)/\(t.modules.1) zeros=\(t.zeros.0)/\(t.zeros.1)/\(c.zero_memory) self=\(c.kernel_return) calls=\(c.calls) mutex=\(t.lock_result)/\(t.unlock_result) rsp=\(c.rsp) fs=\(c.fs_base)")
            }
            let isolated = reports[0].cpu.rsp != reports[1].cpu.rsp && reports[0].cpu.fs_base != reports[1].cpu.fs_base
            passed = passed && isolated
            lines.append("distinct_stacks_tls=\(isolated ? "PASS" : "FAIL")")
        }
        let (budgetStatus, rejected) = execute(elf, budget: 1)
        let refused = budgetStatus == -7 && rejected.allSatisfy { $0.joined == 0 && $0.cpu.backend == 0 }
        lines.append("budget_refusal=\(refused ? "PASS" : "FAIL") status=\(budgetStatus)")
        passed = passed && refused
        var wx = elf; wx[68] = 7
        let (wxStatus, wxReports) = execute(wx)
        let wxRefused = wxStatus < 0 && wxReports.allSatisfy { $0.joined == 0 && $0.cpu.backend == 0 }
        passed = passed && wxRefused
        lines.append("wx_refusal=\(wxRefused ? "PASS" : "FAIL") status=\(wxStatus)")
        lines.append("m1_threads=\(passed ? "PASS" : "FAIL")")
        lines.append("m1_multi_module_tls=\(passed ? "PASS" : "FAIL")")
#else
        lines += ["m1_threads=BLOCKED", "reason=runtime_not_compiled"]
#endif
        return lines.joined(separator: "\n")
    }

    static func run(_ inspection: Inspection) -> String {
#if MAXPS4_FEX_APP
        guard inspection.ready else { return "execution=refused_validation" }
        var engine: OpaquePointer?
        let opened = maxps4_fex_engine_open(&engine)
        guard opened == 0, let engine else { return "context=\(opened)\nexecution=not_attempted" }
        defer { maxps4_fex_engine_close(engine) }
        var diagnostic = [CChar](repeating: 0, count: 4096)
        let prepared = inspection.bytes.withUnsafeBytes { raw in
            "test.elf".withCString { path in
                var module = maxps4_orbis_module(path: path, bytes: raw.bindMemory(to: UInt8.self).baseAddress,
                    size: raw.count, load_bias: 0, dependencies: nil, dependency_count: 0)
                return diagnostic.withUnsafeMutableBufferPointer {
                    maxps4_fex_engine_prepare_orbis(engine, &module, 1, path, 65536, $0.baseAddress, $0.count)
                }
            }
        }
        // Detailed admission diagnostics may contain input-controlled module text;
        // export only numeric statuses and a fixed explanation.
        guard prepared == 0 else { return "context=0\nmap_and_cpu_admission=\(prepared)\nexecution=not_attempted\nInstructions, TLS ou dépendances non pris en charge." }
        let initialized = maxps4_fex_engine_initialize(engine)
        guard initialized == 0 else { return "context=0\nmap_and_cpu_admission=0\njit=\(initialized)\nexecution=not_attempted\nActiver StikDebug puis réessayer." }
        var result: UInt64 = 0; var entry: UInt64 = 0; var backend: UInt32 = 0
        let executed = maxps4_fex_engine_run_orbis(engine, &result, &backend, &entry)
        let native = executed == 0 && backend == UInt32(MAXPS4_CPU_FEX_ARM64_JIT)
        let expected: UInt64? = inspection.bytes == fixture() ? 42 :
            (inspection.bytes == fixture(initial: 7) ? 9 : nil)
        let isFixture = expected != nil
        var cpu = maxps4_fex_cpu_report()
        let cpuStatus = maxps4_fex_engine_last_cpu_report(engine, &cpu)
        let stateVerified = cpuStatus == 0 && cpu.completed == 1 && cpu.rax == result &&
            cpu.entry == entry && cpu.rsp_alignment == 0 && cpu.zero_integer_registers == 1 &&
            cpu.entry_matches == 1 && cpu.argc == 0 && cpu.argv_env_null == 1 && cpu.auxv_valid == 1
        let verified = native && isFixture && result == expected && entry == 0x1000 && stateVerified
        return "context=0\nmap_and_cpu_admission=0\njit=0\nexecution_status=\(executed)\nbackend=\(backend)\nnative_fex=\(native)\nentry=\(entry)\nrax=\(result)\nfixture=\(isFixture)\nfixture_verified=\(verified)\ncpu_state_verified=\(stateVerified)\n" +
            (verified ? "ELF Orbis synthétique exécuté et résultat \(result) vérifié." : "Résultat observé ; exécution de la fixture non vérifiée.") +
            "\nAucune preuve d’exécution d’un jeu PS4."
#else
        return "execution=not_attempted\nruntime=not_compiled"
#endif
    }
}
