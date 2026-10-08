import Foundation

/// Capabilities required before a native shadPS4 backend may advertise game launch.
/// This is a compatibility contract, not a bundled shadPS4 port.
struct MaxPS4NativeCapabilities: Equatable {
    var ps4ExecutableLoader: Bool = false
    var x8664GuestExecution: Bool = false
    var ps4SystemLibraries: Bool = false
    var graphicsBackend: Bool = false

    var canLaunchPS4Game: Bool {
        ps4ExecutableLoader && x8664GuestExecution &&
        ps4SystemLibraries && graphicsBackend
    }

    var diagnostic: String {
        let requirements: [(String, Bool)] = [
            ("Chargeur SELF/ELF PS4", ps4ExecutableLoader),
            ("Exécution x86-64 sur ARM64", x8664GuestExecution),
            ("Services système PS4", ps4SystemLibraries),
            ("Rendu graphique iOS", graphicsBackend)
        ]
        return requirements.map { "\($0.1 ? "✓" : "✗") \($0.0)" }
            .joined(separator: "\n")
    }
}

/// A future native port must implement this interface to connect to MaxPS4.
/// No placeholder implementation claims to execute PS4 code.
@MainActor
protocol MaxPS4ShadPS4Backend: MaxPS4NativeEngine {
    var capabilities: MaxPS4NativeCapabilities { get }
}

// Linked native C ABI smoke probe. Only demonstrates the shadPS4 utility's
// C++ code is present; this is NOT an emulator readiness or game launch test.
@_silgen_name("maxps4_shadps4_utility_probe")
private func maxps4_shadps4_utility_probe() -> Int32

enum MaxPS4NativeLinkCheck {
    static var isUpstreamUtilityLinked: Bool {
        maxps4_shadps4_utility_probe() == 1
    }
}

@_silgen_name("maxps4_native_executable_signature")
private func maxps4_native_executable_signature(
    _ bytes: UnsafePointer<UInt8>?, _ count: Int
) -> Int32

extension MaxPS4NativeLinkCheck {
    static func executableSignature(_ data: Data) -> Int32 {
        data.withUnsafeBytes { raw in
            maxps4_native_executable_signature(
                raw.bindMemory(to: UInt8.self).baseAddress, data.count
            )
        }
    }

    static var executableSignatureSelfTest: Bool {
        var elf = Data(repeating: 0, count: 20)
        elf.replaceSubrange(0..<6, with: [0x7f, 0x45, 0x4c, 0x46, 2, 1])
        elf[18] = 0x3e
        let selfHeader = Data([0x4f, 0x15, 0x3d, 0x1d])
        return executableSignature(elf) == 1 &&
            executableSignature(selfHeader) == 2 &&
            executableSignature(Data([0x7f, 0x45])) == 0 &&
            executableSignature(Data()) == 0
    }
}

@_silgen_name("maxps4_native_elf_entry_point")
private func maxps4_native_elf_entry_point(
    _ bytes: UnsafePointer<UInt8>?, _ count: Int, _ entry: UnsafeMutablePointer<UInt64>?
) -> Int32

extension MaxPS4NativeLinkCheck {
    static func nativeELFEntryPoint(_ data: Data) -> UInt64? {
        var entry: UInt64 = 0
        let valid = data.withUnsafeBytes { raw in
            maxps4_native_elf_entry_point(
                raw.bindMemory(to: UInt8.self).baseAddress, data.count, &entry
            )
        }
        return valid == 1 ? entry : nil
    }

    static var nativeELFEntryPointSelfTest: Bool {
        var elf = Data(repeating: 0, count: 64)
        elf.replaceSubrange(0..<7, with: [0x7f, 0x45, 0x4c, 0x46, 2, 1, 1])
        elf[18] = 0x3e
        elf[24] = 0x78
        elf[25] = 0x56
        elf[26] = 0x34
        elf[27] = 0x12
        return nativeELFEntryPoint(elf) == 0x12345678 &&
            nativeELFEntryPoint(Data(elf.prefix(63))) == nil &&
            nativeELFEntryPoint(Data([0x4f, 0x15, 0x3d, 0x1d])) == nil
    }
}

@_silgen_name("maxps4_native_self_segment_count")
private func maxps4_native_self_segment_count(
    _ bytes: UnsafePointer<UInt8>?, _ count: Int, _ segments: UnsafeMutablePointer<UInt16>?
) -> Int32

extension MaxPS4NativeLinkCheck {
    static func nativeSELFSegmentCount(_ data: Data) -> UInt16? {
        var segments: UInt16 = 0
        let accepted = data.withUnsafeBytes { raw in
            maxps4_native_self_segment_count(
                raw.bindMemory(to: UInt8.self).baseAddress, data.count, &segments
            )
        }
        return accepted == 1 ? segments : nil
    }

    static var nativeSELFHeaderSelfTest: Bool {
        var sample = Data(repeating: 0, count: 64)
        sample.replaceSubrange(0..<4, with: [0x4F, 0x15, 0x3D, 0x1D])
        sample[6] = 1
        sample[24] = 1
        guard nativeSELFSegmentCount(sample) == 1 else { return false }
        guard nativeSELFSegmentCount(Data(sample.prefix(63))) == nil else { return false }
        sample[24] = 2
        guard nativeSELFSegmentCount(sample) == nil else { return false }
        sample[24] = 0
        guard nativeSELFSegmentCount(sample) == nil else { return false }
        sample[24] = 1
        sample[6] = 2
        return nativeSELFSegmentCount(sample) == nil
    }
}

@_silgen_name("maxps4_native_self_segment_flags")
private func maxps4_native_self_segment_flags(
    _ bytes: UnsafePointer<UInt8>?, _ count: Int,
    _ encrypted: UnsafeMutablePointer<UInt16>?,
    _ compressed: UnsafeMutablePointer<UInt16>?
) -> Int32

extension MaxPS4NativeLinkCheck {
    static func nativeSELFSegmentFlags(_ data: Data) -> (encrypted: UInt16, compressed: UInt16)? {
        var encrypted: UInt16 = 0
        var compressed: UInt16 = 0
        let accepted = data.withUnsafeBytes { raw in
            maxps4_native_self_segment_flags(
                raw.bindMemory(to: UInt8.self).baseAddress, data.count,
                &encrypted, &compressed
            )
        }
        return accepted == 1 ? (encrypted, compressed) : nil
    }

    static var nativeSELFSegmentFlagsSelfTest: Bool {
        var header = Data(repeating: 0, count: 96)
        header.replaceSubrange(0..<4, with: [0x4F, 0x15, 0x3D, 0x1D])
        header[6] = 1
        header[24] = 2
        header[32] = 0x02 // encrypted first segment
        header[64] = 0x0A // encrypted and compressed second segment
        guard let flags = nativeSELFSegmentFlags(header),
              flags.encrypted == 2, flags.compressed == 1 else { return false }
        return nativeSELFSegmentFlags(Data(header.prefix(95))) == nil
    }
}

@_silgen_name("maxps4_native_self_file_ranges_valid")
private func maxps4_native_self_file_ranges_valid(
    _ bytes: UnsafePointer<UInt8>?, _ count: Int
) -> Int32

extension MaxPS4NativeLinkCheck {
    static func nativeSELFFileRangesValid(_ data: Data) -> Bool {
        data.withUnsafeBytes { raw in
            maxps4_native_self_file_ranges_valid(
                raw.bindMemory(to: UInt8.self).baseAddress, data.count
            ) == 1
        }
    }

    static var nativeSELFFileRangesSelfTest: Bool {
        var sample = Data(repeating: 0, count: 100)
        sample.replaceSubrange(0..<4, with: [0x4F, 0x15, 0x3D, 0x1D])
        sample[6] = 1
        sample[24] = 1
        sample[40] = 96 // segment file_offset
        sample[48] = 4  // segment file_size
        guard nativeSELFFileRangesValid(sample) else { return false }
        sample[48] = 5
        guard !nativeSELFFileRangesValid(sample) else { return false }
        sample[48] = 4
        sample[40] = 0xFF
        return !nativeSELFFileRangesValid(sample) &&
            !nativeSELFFileRangesValid(Data(sample.prefix(63)))
    }
}

@_silgen_name("maxps4_native_self_memory_sizes_valid")
private func maxps4_native_self_memory_sizes_valid(
    _ bytes: UnsafePointer<UInt8>?, _ count: Int
) -> Int32

extension MaxPS4NativeLinkCheck {
    static func nativeSELFMemorySizesValid(_ data: Data) -> Bool {
        data.withUnsafeBytes { raw in
            maxps4_native_self_memory_sizes_valid(
                raw.bindMemory(to: UInt8.self).baseAddress, data.count
            ) == 1
        }
    }

    static var nativeSELFMemorySizesSelfTest: Bool {
        var header = Data(repeating: 0, count: 96)
        header.replaceSubrange(0..<4, with: [0x4F, 0x15, 0x3D, 0x1D])
        header[6] = 1
        header[24] = 1
        header[48] = 4 // file size
        header[56] = 8 // memory size
        guard nativeSELFMemorySizesValid(header) else { return false }
        header[56] = 3 // memory smaller than file
        guard !nativeSELFMemorySizesValid(header) else { return false }
        header[56] = 8
        header[59] = 0x20 // >256 MiB
        return !nativeSELFMemorySizesValid(header)
    }
}

@_silgen_name("maxps4_native_self_embedded_elf_entry")
private func maxps4_native_self_embedded_elf_entry(
    _ bytes: UnsafePointer<UInt8>?, _ count: Int, _ entry: UnsafeMutablePointer<UInt64>?
) -> Int32

extension MaxPS4NativeLinkCheck {
    static func nativeSELFEmbeddedELFEntry(_ data: Data) -> UInt64? {
        var entry: UInt64 = 0
        let ok = data.withUnsafeBytes { raw in
            maxps4_native_self_embedded_elf_entry(
                raw.bindMemory(to: UInt8.self).baseAddress, data.count, &entry
            )
        }
        return ok == 1 ? entry : nil
    }

    static var nativeSELFEmbeddedELFSelfTest: Bool {
        var sample = Data(repeating: 0, count: 128)
        sample.replaceSubrange(0..<4, with: [0x4F, 0x15, 0x3D, 0x1D])
        sample[6] = 1
        sample[24] = 1
        sample.replaceSubrange(64..<71, with: [0x7F, 0x45, 0x4C, 0x46, 2, 1, 1])
        sample[64 + 18] = 0x3E
        sample[64 + 24] = 0x78
        sample[64 + 25] = 0x56
        guard nativeSELFEmbeddedELFEntry(sample) == 0x5678 else { return false }
        guard nativeSELFEmbeddedELFEntry(Data(sample.prefix(127))) == nil else { return false }
        sample[64] = 0
        return nativeSELFEmbeddedELFEntry(sample) == nil
    }
}

@_silgen_name("maxps4_shadps4_alignment_probe")
private func maxps4_shadps4_alignment_probe() -> Int32

extension MaxPS4NativeLinkCheck {
    static var nativeUpstreamAlignmentSelfTest: Bool {
        maxps4_shadps4_alignment_probe() == 1
    }
}

@_silgen_name("maxps4_native_ps4_elf_type")
private func maxps4_native_ps4_elf_type(
    _ bytes: UnsafePointer<UInt8>?, _ count: Int,
    _ value: UnsafeMutablePointer<UInt16>?
) -> Int32

extension MaxPS4NativeLinkCheck {
    static func nativePS4ELFType(_ data: Data) -> UInt16? {
        var value: UInt16 = 0
        let accepted = data.withUnsafeBytes { raw in
            maxps4_native_ps4_elf_type(
                raw.bindMemory(to: UInt8.self).baseAddress, data.count, &value
            )
        }
        return accepted == 1 ? value : nil
    }

    static var nativePS4ELFTypeSelfTest: Bool {
        var elf = Data(repeating: 0, count: 64)
        elf.replaceSubrange(0..<7, with: [0x7F, 0x45, 0x4C, 0x46, 2, 1, 1])
        elf[18] = 0x3E
        for type in [UInt16(0xFE00), 0xFE0C, 0xFE10, 0xFE18] {
            elf[16] = UInt8(truncatingIfNeeded: type)
            elf[17] = UInt8(truncatingIfNeeded: type >> 8)
            guard nativePS4ELFType(elf) == type else { return false }
        }
        elf[16] = 2
        elf[17] = 0
        guard nativePS4ELFType(elf) == nil else { return false }
        return nativePS4ELFType(Data(elf.prefix(63))) == nil
    }
}

@_silgen_name("maxps4_native_guest_x86_run")
private func maxps4_native_guest_x86_run(
    _ code: UnsafePointer<UInt8>?, _ count: Int, _ budget: UInt32,
    _ result: UnsafeMutablePointer<UInt64>?
) -> Int32

extension MaxPS4NativeLinkCheck {
    static func runNativeSyntheticX86(_ code: Data, budget: UInt32 = 64) -> UInt64? {
        var output: UInt64 = 0
        let status = code.withUnsafeBytes { bytes in
            maxps4_native_guest_x86_run(
                bytes.bindMemory(to: UInt8.self).baseAddress,
                code.count, budget, &output
            )
        }
        return status == 1 ? output : nil
    }

    static var nativeSyntheticExecutionSelfTest: Bool {
        let program = Data([0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3])
        return runNativeSyntheticX86(program) == 42 &&
            runNativeSyntheticX86(program, budget: 1) == nil &&
            runNativeSyntheticX86(Data([0xB8, 2])) == nil &&
            runNativeSyntheticX86(Data([0x0F, 0x05])) == nil &&
            runNativeSyntheticX86(Data([0x90]), budget: 1) == nil
    }
}
