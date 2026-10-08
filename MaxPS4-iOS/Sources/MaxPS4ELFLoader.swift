import Foundation

/// Read-only ELF64 segment loader into simulated guest memory; never executes bytes.
enum MaxPS4ELFLoader {
    enum LoaderError: LocalizedError {
        case invalid
        var errorDescription: String? { "Fichier ELF64 incompatible avec le prototype" }
    }

    /// Creates an entirely synthetic ELF64 sample with one undefined symbol.
    /// It is not extracted from a PlayStation game.
    static func createImportDemo() throws -> URL {
        var bytes = [UInt8](repeating: 0, count: 0x240)
        func put(_ value: UInt64, at offset: Int, width: Int) {
            for i in 0..<width {
                bytes[offset + i] = UInt8(truncatingIfNeeded: value >> (i * 8))
            }
        }
        bytes[0...6] = [0x7F, 0x45, 0x4C, 0x46, 2, 1, 1]
        put(2, at: 16, width: 2) // ET_EXEC, x86-64
        put(62, at: 18, width: 2)
        put(1, at: 20, width: 4)
        put(0x1000, at: 24, width: 8)
        put(64, at: 32, width: 8) // program headers
        put(0x180, at: 40, width: 8) // section headers
        put(64, at: 52, width: 2)
        put(56, at: 54, width: 2)
        put(1, at: 56, width: 2)
        put(64, at: 58, width: 2)
        put(3, at: 60, width: 2)
        put(1, at: 64, width: 4) // PT_LOAD
        put(5, at: 68, width: 4) // read + execute
        put(0x100, at: 72, width: 8)
        put(0x1000, at: 80, width: 8)
        put(2, at: 96, width: 8)
        put(16, at: 104, width: 8)
        put(0x1000, at: 112, width: 8)
        bytes[0x100] = 0x90 // NOP
        bytes[0x101] = 0xC3 // RET
        // Two ELF64 dynsym entries: null and undefined import.
        put(1, at: 0x120 + 24, width: 4) // st_name in linked string table
        let name = [UInt8]("sceKernelGetProcessTime".utf8)
        bytes[0x150] = 0
        for (i, byte) in name.enumerated() { bytes[0x151 + i] = byte }
        // SHT_DYNSYM (index 1), linked to SHT_STRTAB (index 2).
        put(11, at: 0x1C0 + 4, width: 4)
        put(0x120, at: 0x1C0 + 24, width: 8)
        put(48, at: 0x1C0 + 32, width: 8)
        put(2, at: 0x1C0 + 40, width: 4)
        put(24, at: 0x1C0 + 56, width: 8)
        put(3, at: 0x200 + 4, width: 4)
        put(0x150, at: 0x200 + 24, width: 8)
        put(UInt64(name.count + 2), at: 0x200 + 32, width: 8)
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("MaxPS4_Demo_Imports.elf")
        try Data(bytes).write(to: url, options: .atomic)
        return url
    }

    /// End-to-end sample validation: parse an actual generated ELF64 file,
    /// ensure the missing-import diagnostic remains bounded and reject corruption.
    static func importDemoSelfTest() -> Bool {
        do {
            let url = try createImportDemo()
            defer { try? FileManager.default.removeItem(at: url) }
            let loaded = try load(url: url)
            let report = try inspectImports(url: url)
            guard loaded.entry == 0x1000, loaded.segments == 1,
                  report.contains("1 importations non définies"),
                  report.contains("sceKernelGetProcessTime") else { return false }

            var bytes = try Data(contentsOf: url)
            // Corrupt the dynamic symbol's string-table link so that it must
            // fail closed rather than read outside the ELF.
            bytes[0x1C0 + 40] = 0xFF
            try bytes.write(to: url, options: .atomic)
            do {
                _ = try inspectImports(url: url)
                return false
            } catch LoaderError.invalid {
                return true
            }
        } catch {
            return false
        }
    }

    /// Tests that malformed ELF section metadata is rejected without executing it.
    static func malformedImportSelfTest() -> Bool {
        do {
            let url = try createImportDemo()
            defer { try? FileManager.default.removeItem(at: url) }
            let original = try Data(contentsOf: url)
            func rejected(_ offset: Int, _ value: UInt8) throws -> Bool {
                var copy = original
                copy[offset] = value
                try copy.write(to: url, options: .atomic)
                do {
                    _ = try inspectImports(url: url)
                    return false
                } catch LoaderError.invalid {
                    return true
                }
            }
            // Missing linked section, illegal entry stride and truncated section table.
            return try rejected(0x1C0 + 40, 0xFF)
                && rejected(0x1C0 + 56, 0)
                && rejected(40, 0xFF)
        } catch {
            return false
        }
    }

    static func relocationSelfTest() -> Bool {
        do {
            let url = try createImportDemo()
            defer { try? FileManager.default.removeItem(at: url) }
            var bytes = [UInt8](try Data(contentsOf: url))
            bytes += [UInt8](repeating: 0, count: 0x100)
            func put(_ value: UInt64, _ offset: Int, _ width: Int) {
                for i in 0..<width { bytes[offset + i] = UInt8(truncatingIfNeeded: value >> (i * 8)) }
            }
            put(4, 60, 2) // four section headers
            put(4, 0x240 + 4, 4) // SHT_RELA
            put(0x280, 0x240 + 24, 8)
            put(24, 0x240 + 32, 8)
            put(24, 0x240 + 56, 8)
            try Data(bytes).write(to: url, options: .atomic)
            guard try inspectImports(url: url).contains("1 entrées de relocalisation") else { return false }
            put(7, 0x240 + 56, 8) // invalid record stride must be rejected
            try Data(bytes).write(to: url, options: .atomic)
            do { _ = try inspectImports(url: url); return false }
            catch LoaderError.invalid { return true }
        } catch {
            return false
        }
    }

    /// Bounds-checked ELF64 section-table import inventory (SHT_DYNSYM).
    /// Diagnostic only: no relocation application, SELF extraction or execution.
    static func inspectImports(url: URL) throws -> String {
        let file = try Data(contentsOf: url, options: [.mappedIfSafe])
        guard file.count >= 64, file.count <= 32 * 1024 * 1024 else { throw LoaderError.invalid }
        func n(_ pos: Int, _ width: Int) -> UInt64 {
            (0..<width).reduce(UInt64(0)) { $0 | (UInt64(file[pos + $1]) << ($1 * 8)) }
        }
        guard Array(file.prefix(4)) == [0x7F, 0x45, 0x4C, 0x46],
              file[4] == 2, file[5] == 1, n(18, 2) == 62 else { throw LoaderError.invalid }
        let sectionOffset = n(40, 8)
        let entrySize = n(58, 2)
        let count = n(60, 2)
        guard entrySize == 64, count <= 512, sectionOffset <= UInt64(file.count),
              count <= (UInt64(file.count) - sectionOffset) / entrySize else {
            return "ELF64 : table des sections absente ou invalide • importations non inspectables"
        }
        func section(_ index: Int) -> (type: UInt64, offset: UInt64, size: UInt64, link: UInt64, stride: UInt64) {
            let at = Int(sectionOffset + UInt64(index) * 64)
            return (n(at + 4, 4), n(at + 24, 8), n(at + 32, 8), n(at + 40, 4), n(at + 56, 8))
        }
        func valid(_ offset: UInt64, _ size: UInt64) -> Bool {
            offset <= UInt64(file.count) && size <= UInt64(file.count) - offset
        }
        var symbols: [String] = []
        var relocations = 0
        for i in 0..<Int(count) {
            let sec = section(i)
            if sec.type == 4 || sec.type == 9 {
                let requiredStride: UInt64 = sec.type == 4 ? 24 : 16
                guard valid(sec.offset, sec.size), sec.stride == requiredStride,
                      sec.size % requiredStride == 0,
                      sec.size / requiredStride <= 4096 else { throw LoaderError.invalid }
                relocations += Int(sec.size / requiredStride)
            }
            guard sec.type == 11 else { continue } // ELF SHT_DYNSYM
            guard sec.link < count, sec.stride == 24, valid(sec.offset, sec.size),
                  sec.size / sec.stride <= 4096 else { throw LoaderError.invalid }
            let names = section(Int(sec.link))
            guard names.type == 3, valid(names.offset, names.size) else { throw LoaderError.invalid }
            for j in 0..<Int(sec.size / sec.stride) {
                let at = Int(sec.offset) + j * 24
                let nameOffset = n(at, 4)
                let defined = n(at + 6, 2) != 0
                guard defined == false, nameOffset < names.size else { continue }
                let start = Int(names.offset + nameOffset)
                let limit = Int(names.offset + names.size)
                var end = start
                while end < limit && end - start < 128 && file[end] != 0 { end += 1 }
                guard end < limit, file[end] == 0, end > start,
                      let name = String(bytes: file[start..<end], encoding: .utf8) else { continue }
                symbols.append(name)
            }
        }
        let shown = symbols.prefix(12).joined(separator: ", ")
        return "ELF64 : \(symbols.count) importations non définies • \(relocations) entrées de relocalisation • " +
               (shown.isEmpty ? "aucun nom disponible" : shown) +
               " • inventaire uniquement, résolution et application des relocalisations non implémentées"
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

        // Preflight every loadable segment before allocating guest memory.
        var ranges: [(start: UInt64, end: UInt64)] = []
        var totalMemory: UInt64 = 0
        for i in 0..<Int(count) {
            let offset = Int(table + UInt64(i) * size)
            guard number(offset, 4) == 1 else { continue }
            let start = number(offset + 16, 8)
            let segmentMemory = number(offset + 40, 8)
            guard start <= UInt64.max - segmentMemory else { throw LoaderError.invalid }
            guard segmentMemory <= UInt64(MaxPS4GuestMemory.maximumBytes),
                  totalMemory <= UInt64(MaxPS4GuestMemory.maximumBytes) - segmentMemory else {
                throw LoaderError.invalid
            }
            totalMemory += segmentMemory
            guard segmentMemory > 0 else { continue }
            let end = start + segmentMemory
            guard ranges.allSatisfy({ end <= $0.start || start >= $0.end }) else {
                throw MaxPS4GuestMemory.MemoryError.overlappingRegion
            }
            ranges.append((start: start, end: end))
        }

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
            // Apply ELF flags after loading the file bytes.
            try memory.protect(
                at: address, size: Int(memorySize),
                permissions: { var p: MaxPS4GuestMemory.Permissions = []; if flags & 4 != 0 { p.insert(.read) }; if flags & 2 != 0 { p.insert(.write) }; if flags & 1 != 0 { p.insert(.execute) }; return p }()
            )
            loaded += 1
        }
        guard loaded > 0, entryCovered else { throw LoaderError.invalid }
        return (memory, entry, loaded)
    }

    /// Dedicated end-to-end diagnostic using only a small synthetic ELF64 image.
    static func integrationTest() -> Bool {
        var bytes = [UInt8](repeating: 0, count: 160)
        func put(_ value: UInt64, _ offset: Int, _ width: Int) {
            for i in 0..<width {
                bytes[offset + i] = UInt8(truncatingIfNeeded: value >> (i * 8))
            }
        }
        bytes[0...6] = [0x7F, 0x45, 0x4C, 0x46, 2, 1, 1]
        put(2, 16, 2)
        put(62, 18, 2)
        put(1, 20, 4)
        put(0x1000, 24, 8)
        put(64, 32, 8)
        put(56, 54, 2)
        put(1, 56, 2)
        put(1, 64, 4)
        put(7, 68, 4)
        put(120, 72, 8)
        put(0x1000, 80, 8)
        put(21, 96, 8)
        put(64, 104, 8)
        let program: [UInt8] = [
            0x48, 0xB8, 42, 0, 0, 0, 0, 0, 0, 0,
            0x48, 0xA3, 0x28, 0x10, 0, 0, 0, 0, 0, 0,
            0xC3
        ]
        bytes.replaceSubrange(120..<141, with: program)
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: url) }
        do {
            try Data(bytes).write(to: url)
            let loaded = try load(url: url)
            var cpu = MaxPS4CPUPrototype()
            try cpu.runLoadedTest(memory: loaded.memory, entry: loaded.entry, length: program.count)
            let writtenBytes = try cpu.guestMemory.read(at: 0x1028, count: 8)
            return loaded.segments == 1 && cpu.rax == 42 &&
                cpu.executedInstructions == 3 &&
                writtenBytes == Data([42, 0, 0, 0, 0, 0, 0, 0])
        } catch {
            return false
        }
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
            // ELF PF_R|PF_X must allow instruction fetch but reject writes.
            guard try result.memory.fetchInstructionBytes(at: 0x1000, count: 2) == Data([0x90, 0xC3]) else { return false }
            var protectedMemory = result.memory
            do {
                try protectedMemory.write(Data([0x90]), at: 0x1000)
                return false
            } catch MaxPS4GuestMemory.MemoryError.accessDenied {}


            // End-to-end test: ELF file -> PT_LOAD -> guest CPU -> guest memory.
            // A synthetic MOV RAX,42 / MOV [0x1028],RAX / RET program.
            var integrated = bytes + [UInt8](repeating: 0, count: 64)
            let instructions: [UInt8] = [
                0x48, 0xB8, 42, 0, 0, 0, 0, 0, 0, 0,
                0x48, 0xA3, 0x28, 0x10, 0, 0, 0, 0, 0, 0,
                0xC3
            ]
            integrated.replaceSubrange(120..<(120 + instructions.count), with: instructions)
            integrated[68] = 7 // Synthetic writable code/data region for integration test
            for index in 0..<8 {
                integrated[96 + index] = UInt8(truncatingIfNeeded: UInt64(instructions.count) >> (index * 8))
                integrated[104 + index] = UInt8(truncatingIfNeeded: UInt64(64) >> (index * 8))
            }
            try Data(integrated).write(to: url, options: .atomic)
            let integratedImage = try load(url: url)
            var integratedCPU = MaxPS4CPUPrototype()
            try integratedCPU.runLoadedTest(
                memory: integratedImage.memory, entry: integratedImage.entry,
                length: instructions.count
            )
            guard integratedCPU.rax == 42,
                  integratedCPU.executedInstructions == 3,
                  try integratedCPU.guestMemory.read(at: 0x1028, count: 8) ==
                      Data([42, 0, 0, 0, 0, 0, 0, 0]) else { return false }

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
