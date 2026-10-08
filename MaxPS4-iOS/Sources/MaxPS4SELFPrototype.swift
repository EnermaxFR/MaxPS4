import Foundation

/// Synthetic SELF segment memory staging, not a PS4 executable loader.
/// Segment header layout follows shadPS4 src/core/loader/elf.h (GPL-2.0-or-later).
enum MaxPS4SELFPrototype {
    enum LoadError: Error { case invalid, protectedSegment }
    private static func word(_ bytes: Data, _ start: Int) -> UInt64 {
        (0..<8).reduce(UInt64(0)) { $0 | (UInt64(bytes[start + $1]) << (8 * $1)) }
    }
    static func mapPlainSegments(_ bytes: Data) throws -> MaxPS4GuestMemory {
        guard bytes.count >= 64,
              Array(bytes.prefix(4)) == [0x4f, 0x15, 0x3d, 0x1d],
              bytes[6] == 1 else { throw LoadError.invalid }
        let count = Int(bytes[24]) | (Int(bytes[25]) << 8)
        guard count > 0, count <= 16, count <= (bytes.count - 32) / 32
        else { throw LoadError.invalid }

        var descriptions: [(offset: Int, fileSize: Int, size: Int, address: UInt64)] = []
        var allocated = 0
        for i in 0..<count {
            let base = 32 + i * 32
            let flags = word(bytes, base)
            guard flags & 0x0e == 0 else { throw LoadError.protectedSegment }
            let offset = word(bytes, base + 8)
            let fileSize = word(bytes, base + 16)
            let size = word(bytes, base + 24)
            guard fileSize <= size, offset <= UInt64(bytes.count),
                  fileSize <= UInt64(bytes.count) - offset,
                  size > 0, size <= UInt64(MaxPS4GuestMemory.maximumBytes - allocated)
            else { throw LoadError.invalid }
            allocated += Int(size)
            // Diagnostic synthetic addresses; real SELF mapping is not implemented.
            let address = UInt64(0x1000) + UInt64(i) * 0x100000
            let end = address + size
            guard descriptions.allSatisfy({
                end <= $0.address || address >= $0.address + UInt64($0.size)
            }) else { throw LoadError.invalid }
            descriptions.append((Int(offset), Int(fileSize), Int(size), address))
        }
        var memory = MaxPS4GuestMemory()
        for segment in descriptions {
            try memory.mapZeroFilled(at: segment.address, size: segment.size)
            if segment.fileSize > 0 {
                try memory.write(bytes.subdata(in: segment.offset..<(segment.offset + segment.fileSize)),
                                 at: segment.address)
            }
            try memory.protect(at: segment.address, size: segment.size, permissions: [.read])
        }
        return memory
    }
    static func selfTest() -> Bool {
        do {
            var example = Data(repeating: 0, count: 72)
            example.replaceSubrange(0..<4, with: [0x4f, 0x15, 0x3d, 0x1d])
            example[6] = 1
            example[24] = 1
            example[40] = 64
            example[48] = 4
            example[56] = 8
            example.replaceSubrange(64..<68, with: [1, 2, 3, 4])
            let mapped = try mapPlainSegments(example)
            guard try mapped.read(at: 0x1000, count: 8) ==
                Data([1, 2, 3, 4, 0, 0, 0, 0]) else { return false }
            var writable = mapped
            do { try writable.write(Data([8]), at: 0x1000); return false }
            catch MaxPS4GuestMemory.MemoryError.accessDenied {}
            example[32] = 2
            do { _ = try mapPlainSegments(example); return false }
            catch LoadError.protectedSegment {}
            example[32] = 0
            example[48] = 10
            do { _ = try mapPlainSegments(example); return false }
            catch LoadError.invalid {}
            // Second synthetic mapping starts at 0x101000. First must not cross it.
            var overlapping = Data(repeating: 0, count: 96)
            overlapping.replaceSubrange(0..<4, with: [0x4f, 0x15, 0x3d, 0x1d])
            overlapping[6] = 1
            overlapping[24] = 2
            overlapping[58] = 0x11 // first segment memory size = 0x110000
            overlapping[88] = 8    // second segment memory size = 8
            do { _ = try mapPlainSegments(overlapping); return false }
            catch LoadError.invalid {}
            return true
        } catch { return false }
    }
}
