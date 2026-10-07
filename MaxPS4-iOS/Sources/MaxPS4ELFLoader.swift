import Foundation

/// Read-only ELF64 segment loader into simulated guest memory; never executes bytes.
enum MaxPS4ELFLoader {
    enum LoaderError: LocalizedError {
        case invalid
        var errorDescription: String? { "Fichier ELF64 incompatible avec le prototype" }
    }

    static func load(url: URL) throws -> (memory: MaxPS4GuestMemory, entry: UInt64, segments: Int) {
        // Keep this diagnostic loader bounded even for unexpectedly large inputs.
        let attributes = try FileManager.default.attributesOfItem(atPath: url.path)
        guard let fileSizeAttribute = attributes[.size] as? NSNumber,
              fileSizeAttribute.uint64Value <= 32 * 1024 * 1024 else {
            throw LoaderError.invalid
        }
        let file = try Data(contentsOf: url, options: [.mappedIfSafe])
        guard file.count >= 64, file.count <= 32 * 1024 * 1024 else { throw LoaderError.invalid }
        func number(_ offset: Int, _ width: Int) -> UInt64 {
            (0..<width).reduce(UInt64(0)) { $0 | (UInt64(file[offset + $1]) << ($1 * 8)) }
        }
        guard file.count >= 64, Array(file.prefix(4)) == [0x7F, 0x45, 0x4C, 0x46],
              file[4] == 2, file[5] == 1, file[6] == 1,
              (number(16, 2) == 2 || number(16, 2) == 3),
              number(18, 2) == 62, number(20, 4) == 1 else {
            throw LoaderError.invalid
        }
        let table = number(32, 8)
        let size = number(54, 2)
        let count = number(56, 2)
        guard size == 56, count <= 64, table <= UInt64(file.count),
              count <= (UInt64(file.count) - table) / size else { throw LoaderError.invalid }

        var memory = MaxPS4GuestMemory()
        var loaded = 0
        var entryCovered = false
        let entry = number(24, 8)
        for i in 0..<Int(count) {
            let offset = Int(table + UInt64(i) * size)
            guard number(offset, 4) == 1 else { continue }
            let flags = number(offset + 4, 4)
            let source = number(offset + 8, 8)
            let address = number(offset + 16, 8)
            let fileSize = number(offset + 32, 8)
            let memorySize = number(offset + 40, 8)
            guard memorySize >= fileSize, memorySize <= UInt64(MaxPS4GuestMemory.maximumBytes),
                  address <= UInt64.max - memorySize,
                  flags & ~UInt64(7) == 0,
                  source <= UInt64(file.count),
                  fileSize <= UInt64(file.count) - source else { throw LoaderError.invalid }
            if memorySize == 0 { continue }
            if flags & 1 != 0 && entry >= address && entry < address + memorySize {
                entryCovered = true
            }
            try memory.mapZeroFilled(at: address, size: Int(memorySize))
            if fileSize > 0 {
                let start = Int(source)
                try memory.write(file.subdata(in: start..<(start + Int(fileSize))), at: address)
            }
            loaded += 1
        }
        guard loaded > 0, entryCovered else { throw LoaderError.invalid }
        return (memory, entry, loaded)
    }

    static func selfTest() -> Bool {
        // A synthetic minimal ELF64 file; never reads external game content.
        var bytes = [UInt8](repeating: 0, count: 128)
        func put(_ value: UInt64, at index: Int, width: Int) {
            for i in 0..<width { bytes[index + i] = UInt8(truncatingIfNeeded: value >> (i * 8)) }
        }
        bytes[0...5] = [0x7F, 0x45, 0x4C, 0x46, 2, 1]
        bytes[6] = 1
        put(2, at: 16, width: 2)
        put(62, at: 18, width: 2)
        put(1, at: 20, width: 4)
        put(0x1000, at: 24, width: 8)
        put(64, at: 32, width: 8)
        put(56, at: 54, width: 2)
        put(1, at: 56, width: 2)
        put(1, at: 64, width: 4)
        put(5, at: 68, width: 4)
        put(120, at: 72, width: 8)
        put(0x1000, at: 80, width: 8)
        put(4, at: 96, width: 8)
        put(16, at: 104, width: 8)
        bytes[120...123] = [0x90, 0xC3, 0x90, 0x90]
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: url) }
        do {
            try Data(bytes).write(to: url)
            let result = try load(url: url)
            guard result.segments == 1, result.entry == 0x1000,
                  try result.memory.read(at: 0x1000, count: 5) == Data([0x90, 0xC3, 0x90, 0x90, 0]) else {
                return false
            }
            var cpu = MaxPS4CPUPrototype()
            try cpu.runLoadedTest(memory: result.memory, entry: result.entry, length: 2)
            guard cpu.rip == 2 else { return false }

            // An executable entry point outside PT_LOAD must be rejected.
            put(0x2000, at: 24, width: 8)
            try Data(bytes).write(to: url, options: .atomic)
            do {
                _ = try load(url: url)
                return false
            } catch LoaderError.invalid {}

            // A PT_LOAD segment whose memory size is smaller than file size is invalid.
            put(0x1000, at: 24, width: 8)
            put(2, at: 104, width: 8)
            try Data(bytes).write(to: url, options: .atomic)
            do {
                _ = try load(url: url)
                return false
            } catch LoaderError.invalid {}

            // Two overlapping PT_LOAD regions must fail with a memory error.
            var overlapping = [UInt8](repeating: 0, count: 184)
            overlapping.replaceSubrange(0..<120, with: bytes[0..<120])
            overlapping[120...123] = [1, 0, 0, 0]
            overlapping[124...127] = [5, 0, 0, 0]
            overlapping[176...179] = [0x90, 0xC3, 0x90, 0x90]
            // Update the two 56-byte headers to point at the new file payload.
            func setValue(_ value: UInt64, at index: Int, width: Int) {
                for i in 0..<width {
                    overlapping[index + i] = UInt8(truncatingIfNeeded: value >> (i * 8))
                }
            }
            setValue(2, at: 56, width: 2)
            setValue(16, at: 104, width: 8)
            setValue(176, at: 72, width: 8)
            setValue(176, at: 128, width: 8)
            setValue(0x1008, at: 136, width: 8)
            setValue(4, at: 152, width: 8)
            setValue(16, at: 160, width: 8)
            try Data(overlapping).write(to: url, options: .atomic)
            do {
                _ = try load(url: url)
                return false
            } catch MaxPS4GuestMemory.MemoryError.overlappingRegion {}

            // An invalid program-header table must be rejected before reading entries.
            setValue(UInt64.max, at: 32, width: 8)
            try Data(overlapping).write(to: url, options: .atomic)
            do {
                _ = try load(url: url)
                return false
            } catch LoaderError.invalid {}

            // Memory plans exceeding the prototype's budget must be rejected.
            setValue(64, at: 32, width: 8)
            setValue(1, at: 56, width: 2)
            setValue(UInt64(MaxPS4GuestMemory.maximumBytes) + 1, at: 104, width: 8)
            try Data(overlapping).write(to: url, options: .atomic)
            do {
                _ = try load(url: url)
                return false
            } catch LoaderError.invalid {}
            return true
        } catch { return false }
    }
}
