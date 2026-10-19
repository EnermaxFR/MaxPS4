import Foundation
#if MAXPS4_FEX_APP
import CryptoKit
#endif

extension MaxPS4FEXDiagnostic {
    static func testCGameBoot(url: URL? = nil, globals: Bool = false) -> String {
#if MAXPS4_FEX_APP
        let resource = globals ? "GlobalBootProbe/homebrew.elf" : "GameBootProbe/homebrew.elf"
        let expected: UInt64 = globals ? 4661 : 257
        var lines = ["diagnostic=orbis-v4-c-boot-v2; globals=\(globals)", "test_program_only=true; commercial_game_not_tested",
            "source_fingerprint=\(Bundle.main.object(forInfoDictionaryKey: "MaxPS4SourceFingerprint") ?? "unknown")"]
        do {
            guard let location = url ?? Bundle.main.resourceURL?.appendingPathComponent(resource) else {
                return "C homebrew resource unavailable"
            }
            let handle = try FileHandle(forReadingFrom: location)
            defer { try? handle.close() }
            let file = try handle.read(upToCount: 1024 * 1024 + 1) ?? Data()
            guard file.count >= 64 && file.count <= 1024 * 1024 else { return "C homebrew size invalid" }
            lines.append("sha256=\(SHA256.hash(data: file).map { String(format: "%02x", $0) }.joined())")
            return file.withUnsafeBytes { bytes in
                var request = maxps4_v4_prx_request(file: bytes.bindMemory(to: UInt8.self).baseAddress,
                    length: bytes.count, load_bias: 0, entry: 0x400000,
                    abi: UInt32(MAXPS4_V4_ABI_STARTUP_RETURN), instruction_budget: globals ? 4 : 3)
                var diagnostic = [CChar](repeating: 0, count: 256)
                let admission = diagnostic.withUnsafeMutableBufferPointer {
                    maxps4_fex_v4_preflight(&request, $0.baseAddress, $0.count)
                }
                lines.append("preflight=\(admission) \(String(cString: diagnostic))")
                guard admission == 0 else { return lines.joined(separator: "\n") }
                var engine: OpaquePointer?
                let opened = maxps4_fex_engine_open(&engine)
                lines.append("engine_open=\(opened)")
                guard opened == 0, let engine else { return lines.joined(separator: "\n") }
                defer { maxps4_fex_engine_close(engine) }
                let initialized = maxps4_fex_engine_initialize(engine)
                lines.append("jit_initialize=\(initialized)")
                guard initialized == 0 else { return lines.joined(separator: "\n") }
                for run in 0..<3 {
                    var value: UInt64 = 0; var backend: UInt32 = 0
                    let status = maxps4_fex_engine_run_v4_prx(engine, &request, &value, &backend)
                    lines.append("run=\(run) status=\(status) backend=\(backend) RAX=\(value)")
                    guard status == 0 && backend == 2 && value == expected else {
                        return (lines + ["native_iPhone_test=NOT_VERIFIED"]).joined(separator: "\n")
                    }
                }
                return (lines + ["native_ARM64_probe_values_verified=true; confirm physical iPhone", "PS4_game_boot=NOT_VALIDATED"]).joined(separator: "\n")
            }
        } catch { return (lines + ["fixture_error=\(error.localizedDescription)"]).joined(separator: "\n") }
#else
        return "C homebrew diagnostic unavailable: native FEX runtime absent"
#endif
    }
}
