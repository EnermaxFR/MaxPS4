@_silgen_name("maxps4_aether_restricted_cpu_dispatch_probe")
private func aetherRestrictedDispatchProbe() -> Int32

@_silgen_name("maxps4_aether_multi_block_dispatch_probe")
private func aetherMultiBlockDispatchProbe() -> Int32

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

@_silgen_name("maxps4_native_elf_load_layout")
private func maxps4_native_elf_load_layout(
    _ data: UnsafePointer<UInt8>?, _ size: Int,
    _ segments: UnsafeMutablePointer<UInt32>?,
    _ bytes: UnsafeMutablePointer<UInt64>?
) -> Int32

extension MaxPS4NativeLinkCheck {
    static func nativeELFLoadLayout(_ data: Data) -> (segments: UInt32, bytes: UInt64)? {
        var segments: UInt32 = 0
        var bytes: UInt64 = 0
        let accepted = data.withUnsafeBytes { raw in
            maxps4_native_elf_load_layout(
                raw.bindMemory(to: UInt8.self).baseAddress, data.count, &segments, &bytes
            )
        }
        return accepted == 1 ? (segments, bytes) : nil
    }

    static var nativeELFLoadLayoutSelfTest: Bool {
        var elf = Data(repeating: 0, count: 128)
        elf.replaceSubrange(0..<7, with: [0x7f, 0x45, 0x4c, 0x46, 2, 1, 1])
        elf[18] = 0x3e // AMD64
        elf[32] = 64   // e_phoff
        elf[54] = 56   // e_phentsize
        elf[56] = 1    // e_phnum
        elf[64] = 1    // PT_LOAD
        elf[64 + 40] = 64 // p_memsz
        guard let layout = nativeELFLoadLayout(elf),
              layout.segments == 1, layout.bytes == 64 else { return false }
        var bad = elf
        bad[64 + 32] = 65 // p_filesz > p_memsz
        guard nativeELFLoadLayout(bad) == nil else { return false }
        return nativeELFLoadLayout(Data(elf.prefix(119))) == nil &&
               nativeELFLoadLayout(Data()) == nil
    }
}

@_silgen_name("maxps4_native_elf_executable_entry")
private func maxps4_native_elf_executable_entry(
    _ data: UnsafePointer<UInt8>?, _ size: Int,
    _ entry: UnsafeMutablePointer<UInt64>?,
    _ offset: UnsafeMutablePointer<UInt64>?
) -> Int32

extension MaxPS4NativeLinkCheck {
    static func nativeELFExecutableEntry(_ data: Data) -> (entry: UInt64, offset: UInt64)? {
        var entry: UInt64 = 0
        var offset: UInt64 = 0
        let ok = data.withUnsafeBytes { raw in
            maxps4_native_elf_executable_entry(
                raw.bindMemory(to: UInt8.self).baseAddress, data.count, &entry, &offset
            )
        }
        return ok == 1 ? (entry, offset) : nil
    }

    static var nativeELFExecutableEntrySelfTest: Bool {
        var elf = Data(repeating: 0, count: 128)
        elf.replaceSubrange(0..<7, with: [0x7f, 0x45, 0x4c, 0x46, 2, 1, 1])
        elf[18] = 0x3e // x86-64
        elf[24] = 0x04 // Entry VA 4
        elf[32] = 64 // e_phoff
        elf[54] = 56
        elf[56] = 1 // phnum
        elf[64] = 1 // PT_LOAD
        elf[68] = 5 // PF_R | PF_X
        elf[64 + 32] = 16 // p_filesz
        elf[64 + 40] = 16 // p_memsz
        guard let valid = nativeELFExecutableEntry(elf),
              valid.entry == 4, valid.offset == 4 else { return false }
        elf[68] = 4 // PF_R only: not executable
        guard nativeELFExecutableEntry(elf) == nil else { return false }
        elf[68] = 5
        elf[24] = 20 // Entry outside file-backed segment
        return nativeELFExecutableEntry(elf) == nil
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

@_silgen_name("maxps4_native_guest_run_with_backend")
private func maxps4_native_guest_run_with_backend(
    _ code: UnsafePointer<UInt8>?, _ count: Int, _ budget: UInt32,
    _ requestedMode: Int32, _ usedMode: UnsafeMutablePointer<Int32>?,
    _ output: UnsafeMutablePointer<UInt64>?
) -> Int32

@_silgen_name("maxps4_arm64_preview_cache_stats")
private func maxps4_arm64_preview_cache_stats(
    _ hits: UnsafeMutablePointer<UInt64>?,
    _ misses: UnsafeMutablePointer<UInt64>?
)

@_silgen_name("maxps4_native_arm64_preflight")
private func maxps4_native_arm64_preflight(
    _ code: UnsafePointer<UInt8>?, _ size: Int
) -> Int32

@_silgen_name("maxps4_native_jit_arm64_block_checksum")
private func maxps4_native_jit_arm64_block_checksum(
    _ code: UnsafePointer<UInt8>?, _ size: Int,
    _ checksum: UnsafeMutablePointer<UInt64>?,
    _ bytes: UnsafeMutablePointer<Int>?
) -> Int32

@_silgen_name("maxps4_native_arm64_static_execute_probe")
private func maxps4_native_arm64_static_execute_probe() -> Int32

@_silgen_name("maxps4_aether_jit_allocator_abi_probe")
private func maxps4_aether_jit_allocator_abi_probe() -> Int32

@_silgen_name("maxps4_aether_guest_backend_probe")
private func maxps4_aether_guest_backend_probe() -> Int32

@_silgen_name("maxps4_native_jit_rw_alias_probe")
private func maxps4_native_jit_rw_alias_probe(
    _ bytes: UnsafeMutablePointer<Int>?
) -> Int32

@_silgen_name("maxps4_native_jit_stage_arm64")
private func maxps4_native_jit_stage_arm64(
    _ bytes: UnsafePointer<UInt8>?, _ length: Int,
    _ stagedBytes: UnsafeMutablePointer<Int>?
) -> Int32

@_silgen_name("maxps4_native_jit_writable_page_probe")
private func maxps4_native_jit_writable_page_probe(
    _ pageSize: UnsafeMutablePointer<Int>?
) -> Int32

// Inspired by AetherPS4-iOS/Sources/JITSupport.swift (GPL-2.0-or-later).
// CS_DEBUGGED is an iOS code-signing status, NOT evidence that a JIT ran.
@_silgen_name("csops")
private func maxps4_ios_csops(
    _ pid: Int32, _ operation: Int32,
    _ buffer: UnsafeMutableRawPointer?, _ size: Int32
) -> Int32

@_silgen_name("maxps4_native_debugger_attached")
private func maxps4_native_debugger_attached() -> Int32

@_silgen_name("maxps4_stikdebug_jit26_protocol_available")
private func maxps4_stikdebug_jit26_protocol_available() -> Int32

@_silgen_name("maxps4_native_arm64_jit_ready")
private func maxps4_native_arm64_jit_ready() -> Int32

extension MaxPS4NativeLinkCheck {
    static var isARM64JITReady: Bool { maxps4_native_arm64_jit_ready() == 1 }
    static var codeSigningDebugStatus: String {
        var flags: UInt32 = 0
        let rc = withUnsafeMutablePointer(to: &flags) { pointer in
            maxps4_ios_csops(getpid(), 0, UnsafeMutableRawPointer(pointer),
                            Int32(MemoryLayout<UInt32>.size))
        }
        guard rc == 0 else { return "Indéterminé (csops indisponible)" }
        return (flags & 0x10000000) != 0
            ? "CS_DEBUGGED activé (JIT non prouvé)"
            : "CS_DEBUGGED absent"
    }

    static var debuggerAttachmentDescription: String {
        switch maxps4_native_debugger_attached() {
        case 1: return "Débogueur détecté (identité non vérifiée)"
        case 0: return "Aucun débogueur détecté"
        default: return "État indéterminé"
        }
    }

    static var isStikDebugProtocolPresent: Bool {
        maxps4_stikdebug_jit26_protocol_available() == 1
    }

    /// Safe preflight only: does not execute BRK, map executable pages, or
    /// assume a StikDebug debugserver is attached.
    static var stikDebugSafePreflightReport: String {
        let sample = Data([0xB8, 42, 0, 0, 0, 0xC3])
        let supported = isARM64TranslationSupported(sample)
        let words = arm64TranslationPreview(sample)
        let fallback = runSyntheticX86PreferJIT(sample)
        return """
        Passerelle StikDebug : \(isStikDebugProtocolPresent ? "Intégrée" : "Indisponible")
        Traduction ARM64 : \(supported && words != nil ? "Acceptée (" + String(words!.count) + " mots)" : "Refusée")
        Exécution du test : \(fallback?.result == 42 ? (fallback!.usedJIT ? "JIT" : "Interpréteur") : "Échec")
        Attachement : \(debuggerAttachmentDescription)\n        Signature iOS : \(codeSigningDebugStatus)\n        Connexion StikDebug : Non vérifiée par MaxPS4
        Mémoire exécutable : Non testée

        Ce test n’émet aucune interruption BRK et ne valide pas encore l’exécution JIT sur cet iPhone.
        """
    }

    /// End-to-end compiler pipeline smoke test. The generated ARM64 words
    /// remain inert data; results are produced by the x86 interpreter.
    /// Explicit safety gate for future JIT experiments. A debugger hint alone
    /// cannot authorize executable memory, BRK requests or generated-code calls.
    /// A real iOS VM allocation test, but no executable/JIT permission test.
    static var jitBlockIntegrityReport: String {
        let guest = Data([0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3])
        var first: UInt64 = 0
        var second: UInt64 = 0
        var sizeA = 0
        var sizeB = 0
        let firstOK = guest.withUnsafeBytes { raw in
            maxps4_native_jit_arm64_block_checksum(
                raw.bindMemory(to: UInt8.self).baseAddress, guest.count, &first, &sizeA
            ) == 1
        }
        let secondOK = guest.withUnsafeBytes { raw in
            maxps4_native_jit_arm64_block_checksum(
                raw.bindMemory(to: UInt8.self).baseAddress, guest.count, &second, &sizeB
            ) == 1
        }
        let consistent = firstOK && secondOK && first == second && sizeA == sizeB
        return """
        Intégrité des blocs ARM64 : \(consistent ? "Validée" : "Échec")
        Taille du bloc : \(sizeA) octets
        Empreinte de diagnostic : \(firstOK ? String(first, radix: 16) : "Indisponible")
        JIT natif : Inactif
        Aucun code généré n’a été exécuté.
        """
    }

    static var arm64ExecutionBaselineReport: String {
        let works = maxps4_native_arm64_static_execute_probe() == 1
        return """
        Exécution ARM64 native statique : \(works ? "Réussie (42)" : "Échec")
        Instructions ARM64 générées dynamiquement : Non exécutées
        Mémoire JIT RW/RX : Non activée
        FEXCore : Non connecté
        JIT ARM64 : Inactif

        Ce test exécute une instruction compilée et signée dans MaxPS4,
        pas une instruction produite par le traducteur JIT.
        """
    }

    static var aetherJITABIReport: String {
        let compatible = maxps4_aether_jit_allocator_abi_probe() == 1
        return """
        Interface mémoire AetherPS4 : \(compatible ? "Compatible ARM64" : "Non compatible")
        Structure DualMappedRegion : \(compatible ? "Validée" : "Échec")
        Allocation réelle RX : Non testée
        Exécution JIT : Inactive
        """
    }

    // The linked upstream CPU interface runs synthetic guest blocks only.
    static var aetherGuestBackendReport: String {
        let supported = maxps4_aether_guest_backend_probe() == 1
        let dispatch = aetherRestrictedDispatchProbe() == 1
        let blocks = aetherMultiBlockDispatchProbe() == 1
        return """
        Backend CPU d’AetherPS4 : \(supported ? "Relié et testé" : "Échec du test")
        Dispatch CPU synthétique : \(dispatch ? "Réussi" : "Échec")
        Deux blocs invités synthétiques : \(blocks ? "Réussis" : "Échec")
        Interface GuestCpuBackend : \(supported ? "Accessible" : "Non disponible")
        FEXCore JIT : Non encore relié
        Jeux PS4 : Pas encore exécutables
        """
    }

    static var jitDualMappingReport: String {
        var bytes = 0
        let status = maxps4_native_jit_rw_alias_probe(&bytes)
        return """
        Double vue mémoire iOS (vm_remap) : \(status == 1 ? "Réussie" : "Échec")
        Page partagée : \(bytes) octets
        Cohérence des écritures entre deux adresses : \(status == 1 ? "Validée" : "Non validée")
        Permissions : RW uniquement
        StikDebug : Aucun appel
        Exécution ARM64 générée : Non testée
        JIT : Inactif
        """
    }

    static var jitARM64StagingReport: String {
        let guest = Data([0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3])
        var bytes = 0
        let result = guest.withUnsafeBytes { raw in
            maxps4_native_jit_stage_arm64(
                raw.bindMemory(to: UInt8.self).baseAddress, guest.count, &bytes
            )
        }
        return """
        Stockage du bloc ARM64 en mémoire RW : \(result == 1 ? "Réussi" : "Échec")
        Taille du bloc : \(bytes) octets
        Vérification des instructions : \(result == 1 ? "Validée" : "Non validée")
        Mémoire exécutable : Non demandée
        StikDebug : Aucune commande envoyée
        JIT natif : Inactif

        Le code ARM64 est seulement écrit puis relu comme des données.
        """
    }

    static var jitMemoryPreparationReport: String {
        var bytes: Int = 0
        let ok = maxps4_native_jit_writable_page_probe(&bytes) == 1
        return """
        Allocation mémoire iOS (RW) : \(ok ? "Réussie" : "Échec")
        Taille de page : \(ok ? String(bytes) + " octets" : "Indéterminée")
        Permission d’exécution (RX) : Non testée
        Préparation StikDebug : Non demandée
        JIT ARM64 : \(isARM64JITReady ? "Prêt" : "Inactif")

        Une allocation RW réussie ne garantit pas que du code ARM64 généré pourra être exécuté.
        """
    }

    static var jitSafetyGateReport: String {
        let debugger = maxps4_native_debugger_attached()
        let protocolPresent = isStikDebugProtocolPresent
        let backendReady = isARM64JITReady
        let reasons: [String] = [
            "Débogueur : " + debuggerAttachmentDescription,
            "Protocole universel : " + (protocolPresent ? "Présent" : "Absent"),
            "Backend ARM64 exécutable : " + (backendReady ? "Disponible" : "Non implémenté"),
            "Permission de mémoire exécutable : Non attestée",
            "Handshake StikDebug : Non implémenté"
        ]
        // Never treat P_TRACED or protocol availability as an authorization.
        let canSafelyExecuteGeneratedCode = false
        let result = canSafelyExecuteGeneratedCode ? "Autorisé" : "Bloqué — interpréteur uniquement"
        _ = debugger
        return (["Sécurité JIT : " + result] + reasons).joined(separator: "\n")
    }

    static var arm64PipelineSelfTestReport: String {
        let programs: [(String, [UInt8], UInt64)] = [
            ("MOV / ADD", [0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3], 42),
            ("SUB / JNZ", [0xB8, 3, 0, 0, 0, 0x2D, 1, 0, 0, 0,
                           0x75, 0xF9, 0xC3], 0),
            ("CMP / JZ", [0xB8, 7, 0, 0, 0, 0x3D, 7, 0, 0, 0,
                          0x74, 0x05, 0xB8, 0, 0, 0, 0, 0xC3], 7)
        ]
        var summary: [String] = []
        var passing = 0
        for (name, bytes, expected) in programs {
            let code = Data(bytes)
            let translated = arm64TranslationPreview(code)
            let fallback = runSyntheticX86PreferJIT(code, budget: 64)
            let ok = translated.map { verifyARM64Preview($0) } == true &&
                     fallback?.result == expected &&
                     fallback?.usedJIT == false
            summary.append("\(ok ? "✓" : "✗") \(name): \(translated?.count ?? 0) mots ARM64, " +
                           "sortie \(fallback.map { String($0.result) } ?? "échec")")
            if ok { passing += 1 }
        }
        summary.insert("Pipeline ARM64 : \(passing)/\(programs.count) tests réussis", at: 0)
        summary.append("Exécution des mots ARM64 : non effectuée (JIT inactif)")
        return summary.joined(separator: "\n")
    }

    static var jitStatusReport: String {
        let protocolStatus = isStikDebugProtocolPresent ? "Passerelle intégrée" : "Passerelle absente"
        let translation = isARM64TranslationSupported(
            Data([0xB8, 42, 0, 0, 0, 0xC3])
        ) ? "Disponible (données uniquement)" : "Non disponible"
        let execution = isARM64JITReady ? "Prêt (backend)" : "Inactif"
        let probe = Data([0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3])
        let probeResult = runSyntheticX86PreferJIT(probe)
        let executionTest: String
        if let probeResult {
            executionTest = probeResult.result == 42
                ? (probeResult.usedJIT ? "42 • code JIT exécuté" : "42 • interpréteur utilisé")
                : "Résultat inattendu : \(probeResult.result)"
        } else {
            executionTest = "Échec du test d’exécution"
        }
        return """
        JIT natif : \(execution)
        Protocole StikDebug : \(protocolStatus)\n        Attachement : \(debuggerAttachmentDescription)
        Traduction x86 → ARM64 : \(translation)
        Test réel du backend : \(executionTest)

        Attention : la présence du protocole StikDebug ou de code traduit ne prouve pas que StikDebug est attaché ou que des instructions JIT s’exécutent.
        """
    }


    /// Validates and caches an ARM64 translation as non-executable data only.
    static func isARM64TranslationSupported(_ code: Data) -> Bool {
        code.withUnsafeBytes { raw in
            maxps4_native_arm64_preflight(
                raw.bindMemory(to: UInt8.self).baseAddress, code.count
            ) == 1
        }
    }

    /// Requests JIT; the native dispatcher reports whether it actually used it.
    /// Fallback always remains available for synthetic tests.
    static func runSyntheticX86PreferJIT(_ code: Data, budget: UInt32 = 64) -> (result: UInt64, usedJIT: Bool)? {
        var output: UInt64 = 0
        var usedMode: Int32 = -1
        let accepted = code.withUnsafeBytes { raw in
            maxps4_native_guest_run_with_backend(
                raw.bindMemory(to: UInt8.self).baseAddress, code.count, budget,
                1, &usedMode, &output
            )
        }
        return accepted == 1 ? (output, usedMode == 1) : nil
    }

    static var arm64CacheSelfTest: Bool {
        let program = Data([0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3])
        var hitsBefore: UInt64 = 0
        var missesBefore: UInt64 = 0
        maxps4_arm64_preview_cache_stats(&hitsBefore, &missesBefore)
        guard let first = runSyntheticX86PreferJIT(program), first.result == 42,
              let second = runSyntheticX86PreferJIT(program), second.result == 42,
              !first.usedJIT, !second.usedJIT else { return false }
        var hitsAfter: UInt64 = 0
        var missesAfter: UInt64 = 0
        maxps4_arm64_preview_cache_stats(&hitsAfter, &missesAfter)
        return hitsAfter >= hitsBefore + 1 &&
               missesAfter >= missesBefore &&
               !isARM64JITReady
    }

    static var executionBackendSelfTest: Bool {
        let program = Data([0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3])
        guard let output = runSyntheticX86PreferJIT(program) else { return false }
        guard isARM64TranslationSupported(program),
              !isARM64TranslationSupported(Data([0x0F, 0x05])),
              !isARM64TranslationSupported(Data()) else { return false }
        // A terminating three-iteration loop must stay in interpreter mode.
        let loop = Data([0xB8, 3, 0, 0, 0, 0x2D, 1, 0, 0, 0,
                         0x75, 0xF9, 0xC3])
        guard let loopResult = runSyntheticX86PreferJIT(loop, budget: 12),
              loopResult.result == 0, !loopResult.usedJIT,
              runSyntheticX86PreferJIT(loop, budget: 5) == nil else { return false }
        // A nonterminating branch must be bounded by the execution budget.
        let infinite = Data([0xB8, 1, 0, 0, 0, 0x3D, 1, 0, 0, 0,
                             0x74, 0xFE, 0xC3])
        guard runSyntheticX86PreferJIT(infinite, budget: 12) == nil else { return false }
        return output.result == 42 && !output.usedJIT && !isARM64JITReady && arm64CacheSelfTest
    }
}

@_silgen_name("maxps4_arm64_verify_preview")
private func maxps4_arm64_verify_preview(
    _ words: UnsafePointer<UInt32>?, _ count: Int
) -> Int32

@_silgen_name("maxps4_arm64_translate_preview")
private func maxps4_arm64_translate_preview(
    _ guest: UnsafePointer<UInt8>?, _ count: Int,
    _ words: UnsafeMutablePointer<UInt32>?, _ capacity: Int,
    _ emitted: UnsafeMutablePointer<Int>?
) -> Int32

extension MaxPS4NativeLinkCheck {
    /// Disassembles a tiny x86 subset into ARM64 machine words as inert data.
    /// These bytes are never executed and do not make the backend JIT-ready.
    static func arm64TranslationPreview(_ guest: Data) -> [UInt32]? {
        var words = [UInt32](repeating: 0, count: 128)
        var emitted = 0
        let accepted = guest.withUnsafeBytes { raw in
            words.withUnsafeMutableBufferPointer { buffer in
                maxps4_arm64_translate_preview(
                    raw.bindMemory(to: UInt8.self).baseAddress, guest.count,
                    buffer.baseAddress, buffer.count, &emitted
                )
            }
        }
        guard accepted == 1, emitted > 0, emitted <= words.count else { return nil }
        return Array(words.prefix(emitted))
    }

    static func verifyARM64Preview(_ words: [UInt32]) -> Bool {
        words.withUnsafeBufferPointer { buffer in
            maxps4_arm64_verify_preview(buffer.baseAddress, buffer.count) == 1
        }
    }

    static var arm64VerifierAdversarialReport: String {
        let samples: [(String, [UInt32], Bool)] = [
            ("RET isolé", [0xD65F03C0], true),
            ("opcode interdit", [0xFFFFFFFF, 0xD65F03C0], false),
            ("RET au milieu", [0xD65F03C0, 0xD65F03C0], false),
            ("branche hors limites", [0x54000040, 0xD65F03C0], false),
            ("branchement valide", [0x54000020, 0xD503201F, 0xD65F03C0], true)
        ]
        let lines = samples.map { name, words, expected in
            let actual = verifyARM64Preview(words)
            return "\(actual == expected ? "✓" : "✗") \(name)"
        }
        return (["Validation structurelle ARM64 (sans exécution)"] + lines)
            .joined(separator: "\n")
    }

    static var arm64VerifierSelfTest: Bool {
        let guest = Data([0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3])
        guard let words = arm64TranslationPreview(guest),
              verifyARM64Preview(words) else { return false }
        return !verifyARM64Preview([]) &&
               !verifyARM64Preview([0xFFFFFFFF, 0xD65F03C0]) &&
               !verifyARM64Preview([0xD65F03C0, 0xD65F03C0])
    }

    static var arm64TranslationPreviewSelfTest: Bool {
        let program = Data([0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3])
        guard arm64TranslationPreview(program) == [
            0x52800500, 0x72A00000, 0x31000800, 0xD65F03C0
        ] else { return false }
        // ADD EAX, 0x12345678 should become MOVZ/MOVK W1 + ADD W0,W0,W1.
        let wide = Data([0xB8, 1, 0, 0, 0, 0x05, 0x78, 0x56, 0x34, 0x12, 0xC3])
        guard arm64TranslationPreview(wide) == [
            0x52800020, 0x72A00000, 0x528ACF01, 0x72A24681,
            0x2B010000, 0xD65F03C0
        ] else { return false }
        // Boundary regression: ARM64 ADD immediate covers 0...4095, not 4096.
        let boundary = Data([0xB8, 0xFF, 0xFF, 0xFF, 0xFF,
                             0x05, 0xFF, 0x0F, 0, 0, 0xC3])
        guard arm64TranslationPreview(boundary) == [
            0x529FFFE0, 0x72BFFFE0, 0x313FFC00, 0xD65F03C0
        ] else { return false }
        let beyond = Data([0xB8, 0, 0, 0, 0,
                           0x05, 0, 0x10, 0, 0, 0xC3])
        guard arm64TranslationPreview(beyond) == [
            0x52800000, 0x72A00000, 0x52820001,
            0x72A00001, 0x2B010000, 0xD65F03C0
        ] else { return false }
        // ADD EAX, -1 must wrap at 32 bits; preview must preserve all bits.
        let negative = Data([0xB8, 1, 0, 0, 0,
                             0x05, 0xFF, 0xFF, 0xFF, 0xFF, 0xC3])
        guard runNativeSyntheticX86(negative) == 0,
              arm64TranslationPreview(negative) == [
                0x52800020, 0x72A00000, 0x529FFFE1,
                0x72BFFFE1, 0x2B010000, 0xD65F03C0
              ] else { return false }
        // SUB immediate has the same 32-bit wraparound as x86 EAX.
        let subSmall = Data([0xB8, 2, 0, 0, 0, 0x2D, 3, 0, 0, 0, 0xC3])
        guard runNativeSyntheticX86(subSmall) == 0xFFFF_FFFF,
              arm64TranslationPreview(subSmall) == [
                0x52800040, 0x72A00000, 0x71000C00, 0xD65F03C0
              ] else { return false }
        let subWide = Data([0xB8, 0, 0, 0, 0, 0x2D,
                            0x78, 0x56, 0x34, 0x12, 0xC3])
        guard runNativeSyntheticX86(subWide) == 0xEDCB_A988,
              arm64TranslationPreview(subWide) == [
                0x52800000, 0x72A00000, 0x528ACF01,
                0x72A24681, 0x6B010000, 0xD65F03C0
              ] else { return false }
        // ADD wraps at 32 bits and must set ZF for the following JZ.
        let addWrapJZ = Data([0xB8, 0xFF, 0xFF, 0xFF, 0xFF,
                             0x05, 1, 0, 0, 0, 0x74, 5,
                             0xB8, 99, 0, 0, 0, 0xC3])
        guard runNativeSyntheticX86(addWrapJZ) == 0,
              arm64TranslationPreview(addWrapJZ) == [
                0x529FFFE0, 0x72BFFFE0, 0x31000400,
                0x54000060, 0x52800C60, 0x72A00000, 0xD65F03C0
              ] else { return false }
        // SUB 1 from 1 sets ZF;  JZ skips the next MOV.
        let subZeroJZ = Data([0xB8, 1, 0, 0, 0,
                             0x2D, 1, 0, 0, 0, 0x74, 5,
                             0xB8, 99, 0, 0, 0, 0xC3])
        guard runNativeSyntheticX86(subZeroJZ) == 0,
              arm64TranslationPreview(subZeroJZ) == [
                0x52800020, 0x72A00000, 0x71000400,
                0x54000060, 0x52800C60, 0x72A00000, 0xD65F03C0
              ] else { return false }
        // XOR EAX, imm32: compare native interpreter with inert ARM64 EOR emission.
        let xorProgram = Data([0xB8, 0x78, 0x56, 0x34, 0x12,
                               0x35, 0xFF, 0, 0, 0, 0xC3])
        guard runNativeSyntheticX86(xorProgram) == 0x12345687,
              arm64TranslationPreview(xorProgram) == [
                0x528ACF00, 0x72A24680, 0x52801FE1,
                0x72A00001, 0x4A010000, 0x6A00001F, 0xD65F03C0
              ] else { return false }
        // AND EAX, imm32: native result and ARM64 AND W0,W0,W1 encoding.
        let andProgram = Data([0xB8, 0x78, 0x56, 0x34, 0x12,
                               0x25, 0xFF, 0, 0, 0, 0xC3])
        guard runNativeSyntheticX86(andProgram) == 0x78,
              arm64TranslationPreview(andProgram) == [
                0x528ACF00, 0x72A24680, 0x52801FE1,
                0x72A00001, 0x0A010000, 0x6A00001F, 0xD65F03C0
              ] else { return false }
        // OR EAX, imm32: materialize operand then ORR W0,W0,W1.
        let orProgram = Data([0xB8, 0x78, 0x56, 0x34, 0x12,
                              0x0D, 0x80, 0, 0, 0, 0xC3])
        guard runNativeSyntheticX86(orProgram) == 0x123456F8,
              arm64TranslationPreview(orProgram) == [
                0x528ACF00, 0x72A24680, 0x52801001,
                0x72A00001, 0x2A010000, 0x6A00001F, 0xD65F03C0
              ] else { return false }
        // OR EAX,0 produces ZF=1 when EAX=0; JZ must observe that flag.
        let orAndJZ = Data([0xB8, 0, 0, 0, 0, 0x0D, 0, 0, 0, 0,
                            0x74, 5, 0xB8, 99, 0, 0, 0, 0xC3])
        guard runNativeSyntheticX86(orAndJZ) == 0,
              arm64TranslationPreview(orAndJZ) == [
                0x52800000, 0x72A00000, 0x52800001, 0x72A00001,
                0x2A010000, 0x6A00001F, 0x54000060,
                0x52800C60, 0x72A00000, 0xD65F03C0
              ] else { return false }
        guard arm64TranslationPreview(Data([0x0D, 1])) == nil else { return false }
        // XOR EAX,EAX-style zeroing via XOR immediate drives JZ.
        let xorZeroJZ = Data([0xB8, 1, 0, 0, 0, 0x35, 1, 0, 0, 0,
                              0x74, 5, 0xB8, 99, 0, 0, 0, 0xC3])
        guard runNativeSyntheticX86(xorZeroJZ) == 0,
              arm64TranslationPreview(xorZeroJZ) == [
                0x52800020, 0x72A00000, 0x52800021, 0x72A00001,
                0x4A010000, 0x6A00001F, 0x54000060,
                0x52800C60, 0x72A00000, 0xD65F03C0
              ] else { return false }
        let andZeroJZ = Data([0xB8, 42, 0, 0, 0, 0x25, 0, 0, 0, 0,
                              0x74, 5, 0xB8, 99, 0, 0, 0, 0xC3])
        guard runNativeSyntheticX86(andZeroJZ) == 0,
              arm64TranslationPreview(andZeroJZ) == [
                0x52800540, 0x72A00000, 0x52800001, 0x72A00001,
                0x0A010000, 0x6A00001F, 0x54000060,
                0x52800C60, 0x72A00000, 0xD65F03C0
              ] else { return false }
        // CMP EAX, imm32 does not overwrite EAX; its ARM64 form sets NZCV.
        let compare = Data([0xB8, 0x2A, 0, 0, 0,
                            0x3D, 0x2A, 0, 0, 0, 0xC3])
        guard runNativeSyntheticX86(compare) == 42,
              arm64TranslationPreview(compare) == [
                0x52800540, 0x72A00000, 0x52800541,
                0x72A00001, 0x6B01001F, 0xD65F03C0
              ] else { return false }
        // JZ/JNZ rel8=0: conditional fallthrough after CMP, encoded as B.cond.
        let jz = Data([0xB8, 42, 0, 0, 0, 0x3D, 42, 0, 0, 0,
                       0x74, 0, 0xC3])
        guard runNativeSyntheticX86(jz) == 42,
              arm64TranslationPreview(jz) == [
                0x52800540, 0x72A00000, 0x52800541,
                0x72A00001, 0x6B01001F, 0x54000020, 0xD65F03C0
              ] else { return false }
        let jnz = Data([0xB8, 42, 0, 0, 0, 0x3D, 41, 0, 0, 0,
                        0x75, 0, 0xC3])
        guard runNativeSyntheticX86(jnz) == 42,
              arm64TranslationPreview(jnz) == [
                0x52800540, 0x72A00000, 0x52800521,
                0x72A00001, 0x6B01001F, 0x54000021, 0xD65F03C0
              ] else { return false }
        // A forward branch must skip whole guest instructions, not raw ARM64 words.
        let forwardJZ = Data([0xB8, 42, 0, 0, 0, 0x3D, 42, 0, 0, 0,
                              0x74, 5, 0xB8, 99, 0, 0, 0, 0xC3])
        guard runNativeSyntheticX86(forwardJZ) == 42,
              arm64TranslationPreview(forwardJZ) == [
                0x52800540, 0x72A00000, 0x52800541, 0x72A00001,
                0x6B01001F, 0x54000060, 0x52800C60, 0x72A00000,
                0xD65F03C0
              ] else { return false }
        let forwardJNZ = Data([0xB8, 42, 0, 0, 0, 0x3D, 41, 0, 0, 0,
                               0x75, 5, 0xB8, 99, 0, 0, 0, 0xC3])
        guard runNativeSyntheticX86(forwardJNZ) == 42,
              arm64TranslationPreview(forwardJNZ) == [
                0x52800540, 0x72A00000, 0x52800521, 0x72A00001,
                0x6B01001F, 0x54000061, 0x52800C60, 0x72A00000,
                0xD65F03C0
              ] else { return false }
        // Backward JNZ returns to CMP, refreshing NZCV each iteration.
        let backwardToCMP = Data([0xB8, 42, 0, 0, 0, 0x3D, 42, 0, 0, 0,
                                  0x75, 0xF9, 0xC3])
        guard runNativeSyntheticX86(backwardToCMP) == 42,
              arm64TranslationPreview(backwardToCMP) == [
                0x52800540, 0x72A00000, 0x52800541,
                0x72A00001, 0x6B01001F, 0x54FFFFA1, 0xD65F03C0
              ] else { return false }
        // TEST changes ZF without changing EAX; JZ/JNZ consume the zero test.
        let testJZ = Data([0xB8, 42, 0, 0, 0, 0xA9, 2, 0, 0, 0,
                           0x74, 0, 0xC3])
        guard runNativeSyntheticX86(testJZ) == 42,
              arm64TranslationPreview(testJZ) == [
                0x52800540, 0x72A00000, 0x52800041,
                0x72A00001, 0x6A01001F, 0x54000020, 0xD65F03C0
              ] else { return false }
        let testBack = Data([0xB8, 42, 0, 0, 0, 0xA9, 1, 0, 0, 0,
                             0x75, 0xF9, 0xC3])
        guard runNativeSyntheticX86(testBack) == 42,
              arm64TranslationPreview(testBack) == [
                0x52800540, 0x72A00000, 0x52800021,
                0x72A00001, 0x6A01001F, 0x54FFFFA1, 0xD65F03C0
              ] else { return false }
        guard arm64TranslationPreview(Data([0xA9, 1])) == nil else { return false }
        // x86 MOV leaves ZF unchanged; a branch after MOV must retain CMP flags.
        // In ARM64 the MOVZ/MOVK pair must also preserve NZCV.
        let flagsAcrossMOV = Data([0xB8, 42, 0, 0, 0,
                                   0x3D, 42, 0, 0, 0,
                                   0xB8, 99, 0, 0, 0,
                                   0x74, 0, 0xC3])
        guard runNativeSyntheticX86(flagsAcrossMOV) == 99,
              arm64TranslationPreview(flagsAcrossMOV) == [
                0x52800540, 0x72A00000, 0x52800541, 0x72A00001,
                0x6B01001F, 0x52800C60, 0x72A00000,
                0x54000020, 0xD65F03C0
              ] else { return false }
        // Three iterations of SUB EAX,1 / JNZ back to SUB, then RET.
        // This is a finite arithmetic loop with ZF recomputed each iteration.
        let decrementLoop = Data([0xB8, 3, 0, 0, 0,
                                  0x2D, 1, 0, 0, 0,
                                  0x75, 0xF9, 0xC3])
        guard runNativeSyntheticX86(decrementLoop) == 0,
              arm64TranslationPreview(decrementLoop) == [
                0x52800060, 0x72A00000, 0x71000400,
                0x54FFFFE1, 0xD65F03C0
              ] else { return false }
        // Reject a jump directly into another conditional branch: NZCV might
        // be stale if a prior CMP or arithmetic instruction was skipped.
        let branchToBranch = Data([0xB8, 1, 0, 0, 0,
                                   0x3D, 1, 0, 0, 0,
                                   0x74, 2, 0x90, 0x90,
                                   0x75, 0, 0xC3])
        guard arm64TranslationPreview(branchToBranch) == nil else { return false }
        // Invalid instruction targets and loops without fresh CMP are rejected.
        let badTarget = Data([0xB8, 42, 0, 0, 0, 0x3D, 42, 0, 0, 0,
                              0x74, 1, 0xB8, 99, 0, 0, 0, 0xC3])
        let backward = Data([0xB8, 42, 0, 0, 0, 0x3D, 42, 0, 0, 0,
                             0x74, 0xFE, 0xC3])
        guard arm64TranslationPreview(badTarget) == nil,
              arm64TranslationPreview(backward) == nil else { return false }
        // The ARM64 preview refuses other offsets and branches without CMP.
        guard arm64TranslationPreview(Data([0x74, 0, 0xC3])) == nil,
              arm64TranslationPreview(Data([0x75, 1, 0xC3])) == nil else { return false }
        guard arm64TranslationPreview(Data([0x3D, 1])) == nil else { return false }
        guard arm64TranslationPreview(Data([0x25, 1])) == nil else { return false }
        guard arm64TranslationPreview(Data([0x35, 1])) == nil else { return false }
        guard arm64TranslationPreview(Data([0x2D, 1])) == nil else { return false }
        return arm64TranslationPreview(Data([0x0F, 0x05])) == nil &&
            arm64TranslationPreview(Data([0xB8, 1])) == nil &&
            arm64TranslationPreview(Data([0x90])) == nil &&
            arm64TranslationPreview(Data([0xC3, 0x90])) == nil
    }
}
