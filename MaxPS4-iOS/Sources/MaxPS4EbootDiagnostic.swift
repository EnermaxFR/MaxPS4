import Foundation
#if MAXPS4_FEX_APP
import CryptoKit
#endif

extension MaxPS4FEXDiagnostic {
    // The selected file is a bounded immutable snapshot for both native calls.
    // No extraction, key lookup, PKG access or admission based on its filename.
    static func testEboot(url: URL, experimental: Bool = false) -> String {
#if MAXPS4_FEX_APP
        var lines = ["diagnostic=separate-eboot-v2; experimental=\(experimental)", "experimental_function_ABI=true; commercial_game_not_validated",
            "source_fingerprint=\(Bundle.main.object(forInfoDictionaryKey: "MaxPS4SourceFingerprint") ?? "unknown")"]
        do {
            guard url.isFileURL else { return "EBOOT.INPUT: local regular file required" }
            let values = try url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
            guard values.isRegularFile == true && values.isSymbolicLink != true else {
                return "EBOOT.INPUT: regular file required; directories/symlinks refused"
            }
            let handle = try FileHandle(forReadingFrom: url)
            defer { try? handle.close() }
            let limit = 32 * 1024 * 1024
            var file = Data()
            while file.count <= limit {
                let chunk = try handle.read(upToCount: min(65536, limit + 1 - file.count)) ?? Data()
                if chunk.isEmpty { break }
                file.append(chunk)
            }
            guard file.count >= 64 && file.count <= limit else { return "EBOOT.INPUT: size outside 64 bytes..32 MiB" }
            lines.append("separate_file_read=true; bytes=\(file.count)")
            lines.append("sha256=\(SHA256.hash(data: file).map { String(format: "%02x", $0) }.joined())")
            return file.withUnsafeBytes { bytes in
                let base = bytes.bindMemory(to: UInt8.self).baseAddress
                var report = [CChar](repeating: 0, count: 128 * 1024)
                let status = report.withUnsafeMutableBufferPointer {
                    experimental ? maxps4_fex_eboot_experimental_preflight(base, bytes.count, $0.baseAddress, $0.count) :
                        maxps4_fex_eboot_preflight(base, bytes.count, $0.baseAddress, $0.count)
                }
                lines.append("preflight=\(status)\n\(String(cString: report))")
                guard status == 0 else { return (lines + ["launch_unavailable=true; no CPU execution"]).joined(separator: "\n") }
                var engine: OpaquePointer?
                let opened = maxps4_fex_engine_open(&engine)
                lines.append("engine_open=\(opened)")
                guard opened == 0, let engine else { return lines.joined(separator: "\n") }
                defer { maxps4_fex_engine_close(engine) }
                let initialized = maxps4_fex_engine_initialize(engine)
                lines.append("jit_initialize=\(initialized)")
                guard initialized == 0 else { return lines.joined(separator: "\n") }
                var result: UInt64 = 0; var backend: UInt32 = 0
                let executed = experimental ? maxps4_fex_engine_run_eboot_experimental(engine, base, bytes.count, &result, &backend) :
                    maxps4_fex_engine_run_eboot(engine, base, bytes.count, &result, &backend)
                lines.append("dispatch=\(executed) backend=\(backend) RAX=\(result)")
                lines.append("native_ARM64_return_observed=\(executed == 0 && backend == 2); physical_iPhone_confirmation_required")
                lines.append("PS4_game_boot=NOT_VALIDATED")
                return lines.joined(separator: "\n")
            }
        } catch { return (lines + ["EBOOT.FILE: \(error.localizedDescription)"]).joined(separator: "\n") }
#else
        return "Eboot diagnostic unavailable: native FEX runtime absent"
#endif
    }
}
