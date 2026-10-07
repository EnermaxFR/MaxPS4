import Foundation

/// Small, isolated guest-memory prototype for testing address translation.
/// No executable permissions, JIT, host memory mapping or PS4 runtime.
struct MaxPS4GuestMemory {
    enum MemoryError: LocalizedError {
        case invalidRange
        case outOfBounds
        case overlappingRegion
        case accessDenied
        var errorDescription: String? {
            switch self {
            case .invalidRange: return "Adresse ou taille invalide"
            case .outOfBounds: return "Accès mémoire invité hors limites"
            case .overlappingRegion: return "Régions mémoire chevauchantes"
            case .accessDenied: return "Accès refusé par les permissions mémoire invitées"
            }
        }
    }

    struct Permissions: OptionSet {
        let rawValue: UInt8
        static let read = Permissions(rawValue: 1)
        static let write = Permissions(rawValue: 2)
        static let execute = Permissions(rawValue: 4)
        static let readWrite: Permissions = [.read, .write]
    }

    private struct Region {
        let base: UInt64
        var bytes: Data
        var permissions: Permissions
        var end: UInt64 { base + UInt64(bytes.count) }
    }

    private var regions: [Region] = []
    private(set) var allocatedBytes = 0
    static let maximumBytes = 16 * 1024 * 1024

    var regionCount: Int { regions.count }

    var memoryMapSummary: String {
        let entries = regions.sorted { $0.base < $1.base }.map { region in
            let flags = String([
                region.permissions.contains(.read) ? "r" : "-",
                region.permissions.contains(.write) ? "w" : "-",
                region.permissions.contains(.execute) ? "x" : "-"
            ].joined())
            return "0x" + String(region.base, radix: 16) + " : " +
                String(region.bytes.count) + " octets [" + flags + "]"
        }
        return entries.isEmpty ? "Aucune région invitée" : entries.joined(separator: " • ")
    }

    mutating func mapZeroFilled(at address: UInt64, size: Int, permissions: Permissions = .readWrite) throws {
        guard size > 0, size <= Self.maximumBytes - allocatedBytes,
              address <= UInt64.max - UInt64(size) else {
            throw MemoryError.invalidRange
        }
        let end = address + UInt64(size)
        guard regions.allSatisfy({ end <= $0.base || address >= $0.end }) else {
            throw MemoryError.overlappingRegion
        }
        regions.append(Region(base: address, bytes: Data(count: size), permissions: permissions))
        allocatedBytes += size
    }

    func read(at address: UInt64, count: Int) throws -> Data {
        guard let index = regionIndex(at: address, count: count) else {
            throw MemoryError.outOfBounds
        }
        guard regions[index].permissions.contains(.read) else { throw MemoryError.accessDenied }
        let offset = Int(address - regions[index].base)
        return regions[index].bytes.subdata(in: offset..<(offset + count))
    }

    mutating func write(_ data: Data, at address: UInt64) throws {
        guard let index = regionIndex(at: address, count: data.count) else {
            throw MemoryError.outOfBounds
        }
        guard regions[index].permissions.contains(.write) else { throw MemoryError.accessDenied }
        let offset = Int(address - regions[index].base)
        regions[index].bytes.replaceSubrange(offset..<(offset + data.count), with: data)
    }

    mutating func protect(at address: UInt64, size: Int, permissions: Permissions) throws {
        guard let index = regionIndex(at: address, count: size),
              address == regions[index].base,
              size == regions[index].bytes.count else {
            throw MemoryError.outOfBounds
        }
        regions[index].permissions = permissions
    }

    /// Remove an entire guest region and release its simulated allocation.
    /// Partial unmapping is intentionally unsupported in this prototype.
    mutating func unmap(at address: UInt64, size: Int) throws {
        guard size > 0,
              let index = regionIndex(at: address, count: size),
              regions[index].base == address,
              regions[index].bytes.count == size else {
            throw MemoryError.outOfBounds
        }
        allocatedBytes -= regions[index].bytes.count
        regions.remove(at: index)
    }

    func fetchInstructionBytes(at address: UInt64, count: Int) throws -> Data {
        guard let index = regionIndex(at: address, count: count) else { throw MemoryError.outOfBounds }
        guard regions[index].permissions.contains(.execute) else { throw MemoryError.accessDenied }
        let offset = Int(address - regions[index].base)
        return regions[index].bytes.subdata(in: offset..<(offset + count))
    }

    private func regionIndex(at address: UInt64, count: Int) -> Int? {
        guard count >= 0, address <= UInt64.max - UInt64(count) else { return nil }
        let end = address + UInt64(count)
        return regions.firstIndex { address >= $0.base && end <= $0.end }
    }

    static func selfTest() -> Bool {
        do {
            var memory = Self()
            try memory.mapZeroFilled(at: 0x1000, size: 4096)
            try memory.write(Data([0x50, 0x53, 0x34]), at: 0x1010)
            guard try memory.read(at: 0x1010, count: 3) == Data([0x50, 0x53, 0x34]) else {
                return false
            }
            do {
                _ = try memory.read(at: 0x1FFF, count: 2)
                return false
            } catch MemoryError.outOfBounds {}
            do {
                try memory.mapZeroFilled(at: 0x1800, size: 4096)
                return false
            } catch MemoryError.overlappingRegion {}
            try memory.protect(at: 0x1000, size: 4096, permissions: [.read, .execute])
            guard try memory.read(at: 0x1010, count: 3) == Data([0x50, 0x53, 0x34]) else { return false }
            do {
                try memory.write(Data([0]), at: 0x1010)
                return false
            } catch MemoryError.accessDenied {}
            guard try memory.fetchInstructionBytes(at: 0x1010, count: 3) == Data([0x50, 0x53, 0x34]) else { return false }
            // Unmapping makes guest addresses inaccessible and frees the budget.
            try memory.unmap(at: 0x1000, size: 4096)
            guard memory.allocatedBytes == 0 else { return false }
            do {
                _ = try memory.read(at: 0x1010, count: 1)
                return false
            } catch MemoryError.outOfBounds {}
            try memory.mapZeroFilled(at: 0x1000, size: 4096)
            guard try memory.read(at: 0x1010, count: 1) == Data([0]) else { return false }
            do {
                try memory.unmap(at: 0x1000, size: 100)
                return false
            } catch MemoryError.outOfBounds {}
            return true
        } catch {
            return false
        }
    }
}
