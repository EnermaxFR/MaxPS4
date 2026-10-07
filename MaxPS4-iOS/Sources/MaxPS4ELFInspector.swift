import Foundation

/// Read-only ELF64 program-header inspector. Does not map or execute guest code.
enum MaxPS4ELFInspector {
    enum InspectionError: LocalizedError {
        case invalid(String)
        var errorDescription: String? {
            switch self { case .invalid(let message): return message }
        }
    }

    static func inspect(url: URL) throws -> String {
        let attributes = try FileManager.default.attributesOfItem(atPath: url.path)
        guard let fileNumber = attributes[.size] as? NSNumber else {
            throw InspectionError.invalid("Taille inconnue")
        }
        let fileSize = fileNumber.uint64Value
        guard fileSize >= 64 else { throw InspectionError.invalid("En-tête ELF tronqué") }
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        func read(_ offset: UInt64, _ length: Int) throws -> Data {
            guard UInt64(length) <= fileSize, offset <= fileSize - UInt64(length) else {
                throw InspectionError.invalid("Lecture hors limites du fichier")
            }
            try handle.seek(toOffset: offset)
            guard let data = try handle.read(upToCount: length), data.count == length else {
                throw InspectionError.invalid("Lecture ELF incomplète")
            }
            return data
        }
        func u16(_ data: Data, _ offset: Int) -> UInt16 {
            UInt16(data[offset]) | (UInt16(data[offset + 1]) << 8)
        }
        func u32(_ data: Data, _ offset: Int) -> UInt32 {
            (0..<4).reduce(UInt32(0)) { $0 | (UInt32(data[offset + $1]) << ($1 * 8)) }
        }
        func u64(_ data: Data, _ offset: Int) -> UInt64 {
            (0..<8).reduce(UInt64(0)) { $0 | (UInt64(data[offset + $1]) << ($1 * 8)) }
        }
        let header = try read(0, 64)
        guard Array(header.prefix(4)) == [0x7f, 0x45, 0x4c, 0x46],
              header[4] == 2, header[5] == 1, header[6] == 1 else {
            throw InspectionError.invalid("ELF64 little-endian invalide")
        }
        guard u16(header, 18) == 0x3e else {
            throw InspectionError.invalid("Architecture non x86-64")
        }
        let tableOffset = u64(header, 32)
        let entrySize = Int(u16(header, 54))
        let count = Int(u16(header, 56))
        guard count <= 1024, entrySize == 56 else {
            throw InspectionError.invalid("Table de segments non prise en charge")
        }
        guard tableOffset <= fileSize,
              UInt64(count) <= (fileSize - tableOffset) / UInt64(entrySize) else {
            throw InspectionError.invalid("Table de segments hors limites")
        }
        var loadable = 0
        var executable = 0
        for index in 0..<count {
            let item = try read(tableOffset + UInt64(index * entrySize), entrySize)
            let type = u32(item, 0)
            let flags = u32(item, 4)
            let offset = u64(item, 8)
            let fileBytes = u64(item, 32)
            let memoryBytes = u64(item, 40)
            guard offset <= fileSize, fileBytes <= fileSize - offset else {
                throw InspectionError.invalid("Segment hors limites")
            }
            if type == 1 {
                guard memoryBytes >= fileBytes else {
                    throw InspectionError.invalid("Segment PT_LOAD incohérent")
                }
                loadable += 1
                if flags & 1 != 0 { executable += 1 }
            }
        }
        let entry = u64(header, 24)
        return "ELF64 x86-64 • entrée 0x\(String(entry, radix: 16)) • \(count) segments • \(loadable) PT_LOAD dont \(executable) exécutables • lecture seule"
    }
}
