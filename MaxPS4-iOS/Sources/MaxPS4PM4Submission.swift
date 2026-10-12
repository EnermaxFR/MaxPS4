#if MAXPS4_FEX_APP
import Foundation

// Restricted ELF/FEX diagnostic submission. This is not sceGnmSubmitCommandBuffers
// but bounded RIP-relative stores can generate a PM4 stream in guest RW data.
enum MaxPS4PM4Submission {
    struct Snapshot {
        let words: [UInt32]
        let rax: UInt64
        let entry: UInt64
        let backend: UInt32
    }
    static func run(file: Data, guestAddress: UInt64, wordCount: Int, budget: UInt32 = 3) throws -> Snapshot {
        guard wordCount > 0, wordCount <= 65536 else {
            throw NSError(domain: "MaxPS4.PM4", code: -22)
        }
        var engine: OpaquePointer?
        let opened = maxps4_fex_engine_open(&engine)
        guard opened == 0, let engine else {
            throw NSError(domain: "MaxPS4.PM4", code: Int(opened))
        }
        defer { maxps4_fex_engine_close(engine) }
        let initialized = maxps4_fex_engine_initialize(engine)
        guard initialized == 0 else {
            throw NSError(domain: "MaxPS4.PM4", code: Int(initialized))
        }
        var words = [UInt32](repeating: 0, count: wordCount)
        var written = 0
        var rax: UInt64 = 0, entry: UInt64 = 0
        var backend: UInt32 = 0
        let status = file.withUnsafeBytes { bytes in
            words.withUnsafeMutableBufferPointer { output in
                maxps4_fex_engine_run_elf_pm4(engine,
                    bytes.bindMemory(to: UInt8.self).baseAddress, bytes.count, budget,
                    guestAddress, wordCount, output.baseAddress, output.count,
                    &written, &rax, &backend, &entry)
            }
        }
        guard status == 0, written == wordCount, backend == UInt32(MAXPS4_CPU_FEX_ARM64_JIT) else {
            throw NSError(domain: "MaxPS4.PM4", code: status == 0 ? -5 : Int(status))
        }
        return Snapshot(words: words, rax: rax, entry: entry, backend: backend)
    }
}
#endif
