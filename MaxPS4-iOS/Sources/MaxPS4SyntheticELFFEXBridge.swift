import Foundation

/// Controlled ELF fixture and admission tests. Native execution loads the ELF's
/// segments into owned mappings and translates its entry into a FEX guest pointer.
enum MaxPS4SyntheticELFFEXBridge {
    static let program = Data([0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xF4])
    static let entry: UInt64 = 0x1000
    enum BridgeError: Error {
        case unsupportedImage
        case nativeFailure(Int32)
        case unexpectedBackend
    }
    struct Result {
        let rax: UInt64
        let backend: UInt32
    }

    /// Returns bytes; the caller chooses a unique destination. No existing file is overwritten.
    static func syntheticELF() -> Data {
        var file = Data(repeating: 0, count: 120 + program.count)
        func put(_ value: UInt64, _ offset: Int, _ width: Int) {
            for i in 0..<width { file[offset + i] = UInt8(truncatingIfNeeded: value >> (8 * i)) }
        }
        file.replaceSubrange(0..<7, with: [0x7F, 0x45, 0x4C, 0x46, 2, 1, 1])
        put(2, 16, 2); put(62, 18, 2); put(1, 20, 4)
        put(entry, 24, 8); put(64, 32, 8); put(64, 52, 2)
        put(56, 54, 2); put(1, 56, 2)
        put(1, 64, 4); put(5, 68, 4) // PT_LOAD, PF_R | PF_X
        put(120, 72, 8); put(entry, 80, 8)
        put(UInt64(program.count), 96, 8); put(UInt64(program.count), 104, 8)
        put(1, 112, 8)
        file.replaceSubrange(120..<file.count, with: program)
        return file
    }

    /// The whitelist is deliberately exact: no imports, relocations, branches, RET,
    /// stores, syscalls or arbitrary file contents reach the native execution boundary.
    static func validate(memory: MaxPS4GuestMemory, entry: UInt64) throws {
        guard entry == Self.entry, memory.regionCount == 1,
              memory.allocatedBytes == program.count,
              try memory.read(at: entry, count: program.count) == program,
              try memory.fetchInstructionBytes(at: entry, count: program.count) == program
        else { throw BridgeError.unsupportedImage }
        var copy = memory
        do {
            try copy.write(Data([program[0]]), at: entry)
        } catch MaxPS4GuestMemory.MemoryError.accessDenied {
            return // Immutable RX guest region, including rejection of RWX.
        }
        throw BridgeError.unsupportedImage
    }

    /// Injectable boundary allows permission/admission tests without authorizing JIT.
    /// The closure must execute the fixed FEX probe, never the interpreter fallback.
    static func run(url: URL, probe: () -> (Int32, UInt64, UInt32)) throws -> Result {
        let loaded = try MaxPS4ELFLoader.load(url: url)
        try validate(memory: loaded.memory, entry: loaded.entry)
        let (status, value, backend) = probe()
        guard status == 0 else { throw BridgeError.nativeFailure(status) }
        guard backend == 2, value == 42 else { throw BridgeError.unexpectedBackend }
        return Result(rax: value, backend: backend)
    }

#if MAXPS4_FEX_APP
    /// Uses the established runtime lifecycle, authorized JIT provider and W^X policy.
    /// Native ELF validation is independent of the Swift diagnostic loader.
    static func run(url: URL) throws -> Result {
        let attributes = try FileManager.default.attributesOfItem(atPath: url.path)
        guard let size = attributes[.size] as? NSNumber,
              size.uint64Value <= 32 * 1024 * 1024 else { throw BridgeError.unsupportedImage }
        let file = try Data(contentsOf: url)
        return try run(file: file)
    }

    static func run(file: Data) throws -> Result {
        guard file.count <= 32 * 1024 * 1024 else { throw BridgeError.unsupportedImage }
        var engine: OpaquePointer?
        let opened = maxps4_fex_engine_open(&engine)
        guard opened == 0, let engine else { throw BridgeError.nativeFailure(opened == 0 ? -12 : opened) }
        defer { maxps4_fex_engine_close(engine) }
        let initialized = maxps4_fex_engine_initialize(engine)
        guard initialized == 0 else { throw BridgeError.nativeFailure(initialized) }
        var value: UInt64 = 0
        var backend: UInt32 = 0
        var entry: UInt64 = 0
        let status = file.withUnsafeBytes {
            maxps4_fex_engine_run_elf(engine, $0.bindMemory(to: UInt8.self).baseAddress,
                                      $0.count, 65536, &value, &backend, &entry)
        }
        guard status == 0 else { throw BridgeError.nativeFailure(status) }
        guard backend == UInt32(MAXPS4_CPU_FEX_ARM64_JIT) else { throw BridgeError.unexpectedBackend }
        return Result(rax: value, backend: backend)
    }
#endif
}
