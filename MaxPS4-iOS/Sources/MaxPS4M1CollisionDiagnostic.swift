import Foundation
#if MAXPS4_FEX_APP
import CryptoKit
#endif

extension MaxPS4FEXDiagnostic {
    static func testBundledM1Collision() -> String {
        guard let root = Bundle.main.resourceURL else { return "M1 fixture bundle unavailable" }
        return testM1Collision(directory: root.appendingPathComponent("M1CollisionFixtures", isDirectory: true))
    }
    /// Selected original M1 fixture folder only. All native calls run on the CPU queue.
    static func testM1Collision(directory: URL) -> String {
#if MAXPS4_FEX_APP
        var lines = ["diagnostic=m1-translated-collision-21-v1",
            "source_head=\(Bundle.main.object(forInfoDictionaryKey: "MaxPS4SourceRevision") ?? "unknown")",
            "app_build=\(Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") ?? "unknown")",
            "source_fingerprint=\(Bundle.main.object(forInfoDictionaryKey: "MaxPS4SourceFingerprint") ?? "unknown")",
            "source_dirty=\(Bundle.main.object(forInfoDictionaryKey: "MaxPS4SourceDirty") ?? "unknown")",
            "os=\(ProcessInfo.processInfo.operatingSystemVersionString)",
            "production_memory=translated", "physical_iPhone_validation_required=true"]
        do {
            let names = ["m1-two-threads-three-tls-modules.elf", "tls-83.prx", "tls-129.prx"]
            var files = [Data]()
            for name in names {
                let url = directory.appendingPathComponent(name)
                let size = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
                guard size >= 64, size <= 1024 * 1024 else { return (lines + ["fixture_size_invalid=\(name)"]).joined(separator: "\n") }
                let handle = try FileHandle(forReadingFrom: url)
                defer { try? handle.close() }
                let data = try handle.read(upToCount: size + 1) ?? Data()
                guard data.count == size else { return (lines + ["fixture_changed=\(name)"]).joined(separator: "\n") }
                files.append(data)
                lines.append("sha256[\(name)]=\(SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined())")
            }
            let file = files[0], moduleA = files[1], moduleB = files[2]
            guard file.prefix(4) == Data([0x7f, 0x45, 0x4c, 0x46]) else { return "Plaintext ELF fixture required" }
            let entry = (0..<8).reduce(UInt64(0)) { $0 | UInt64(file[24 + $1]) << (8 * $1) }
            guard entry <= UInt64.max - 0xc100 else { return "Fixture entry overflow" }
            let snapshot = entry + 0xc100 // Original finite OrbisM1Fixture layout only.
            lines.append("entry=0x\(String(entry, radix: 16)) snapshot=0x\(String(snapshot, radix: 16))")
            var reservation = maxps4_m1_collision_report()
            let probe = maxps4_fex_m1_collision_diagnostic(&reservation)
            lines.append("reservation_probe=\(probe) candidate=0x\(String(reservation.candidate_guest, radix: 16)) page=\(reservation.page_size)")
            lines.append("candidate_direct=\(reservation.candidate_status) mach_status=\(reservation.candidate_host_status)")
            lines.append("owned_collision_direct=\(reservation.controlled_direct_status) translated=\(reservation.translated_status) preserved=\(reservation.marker_preserved) recreated=\(reservation.recreated)")
            lines.append("reservation_probe_has_no_CPU_execution=true")
            var engine: OpaquePointer?
            let opened = maxps4_fex_engine_open(&engine)
            lines.append("engine_open=\(opened)")
            guard opened == 0, let engine else { return lines.joined(separator: "\n") }
            defer { maxps4_fex_engine_close(engine) }
            let initialized = maxps4_fex_engine_initialize(engine)
            lines.append("jit_initialize=\(initialized)")
            guard initialized == 0 else { return (lines + ["CPU_NOT_EXECUTED=JIT_unavailable"]).joined(separator: "\n") }
            for run in 0..<2 {
                var reports = [maxps4_m1_thread_report](repeating: .init(), count: 2)
                let status = file.withUnsafeBytes { bytes in
                    moduleA.withUnsafeBytes { a in
                        moduleB.withUnsafeBytes { b in
                            let inputs = [maxps4_m1_tls_input(bytes: a.bindMemory(to: UInt8.self).baseAddress, size: a.count),
                                          maxps4_m1_tls_input(bytes: b.bindMemory(to: UInt8.self).baseAddress, size: b.count)]
                            return inputs.withUnsafeBufferPointer { modules in
                                reports.withUnsafeMutableBufferPointer { output in
                                    maxps4_fex_engine_run_m1_threads(engine, bytes.bindMemory(to: UInt8.self).baseAddress,
                                        bytes.count, 200, snapshot, modules.baseAddress, 2, 2, output.baseAddress, 2)
                                }
                            }
                        }
                    }
                }
                lines.append("run=\(run) m1_thread_api_status=\(status)")
                guard status == 0 else { return (lines + ["m1_threads=BLOCKED; capture native reservation stderr; do not infer collision owner"]).joined(separator: "\n") }
                var verified = true
                for i in 0..<2 {
                    let t = reports[i]
                    let passed = t.joined == 1 && t.id == UInt64(i + 1) && t.cpu.backend == 2 &&
                        t.cpu.rax == 131 && t.cpu.memory == 131 && t.cpu.kernel_return == UInt64(i + 1) &&
                        t.cpu.tls_word == 22 && t.cpu.heap_word == 22 && t.cpu.zero_memory == 0 &&
                        t.modules.0 == 84 && t.modules.1 == 131 && t.zeros.0 == 0 && t.zeros.1 == 0 &&
                        t.lock_result == 0 && t.unlock_result == 0 && t.cpu.calls == 4
                    verified = verified && passed
                    lines.append("thread=\(t.id) backend=\(t.cpu.backend) joined=\(t.joined) rax=\(t.cpu.rax) fs=0x\(String(t.cpu.fs_base, radix: 16)) verified=\(passed)")
                }
                verified = verified && reports[0].cpu.rsp != reports[1].cpu.rsp && reports[0].cpu.fs_base != reports[1].cpu.fs_base
                lines.append("native_ARM64_values_verified=\(verified); physical device and original input comparison still required")
            }
        } catch { lines.append("fixture_error=\(error.localizedDescription)") }
        return lines.joined(separator: "\n")
#else
        return "M1 collision diagnostic unavailable: native FEX runtime absent"
#endif
    }
}
