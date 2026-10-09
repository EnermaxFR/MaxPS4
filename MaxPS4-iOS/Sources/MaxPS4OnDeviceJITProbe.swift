import Foundation

@_silgen_name("maxps4_native_map_jit_allocation_probe")
private func maxps4NativeMapJITAllocationProbe() -> Int32

@_silgen_name("maxps4_native_arm64_rx_staging_probe")
private func maxps4NativeARM64RXStagingProbe(_ errorOut: UnsafeMutablePointer<Int32>?) -> Int32

@_silgen_name("maxps4_native_rw_to_rx_permission_probe")
private func maxps4NativeRWtoRXProbe(_ errorOut: UnsafeMutablePointer<Int32>?) -> Int32

@_silgen_name("maxps4_native_map_jit_errno_probe")
private func maxps4NativeMapJITErrnoProbe(_ errorOut: UnsafeMutablePointer<Int32>?) -> Int32

@_silgen_name("maxps4_stikdualmap_port_present")
private func maxps4StikDualMapPortPresent() -> Int32

@_silgen_name("maxps4_stikdualmap_arm64_branch_suite")
private func maxps4StikDualMapARM64BranchSuite(_ passed: UnsafeMutablePointer<Int32>?) -> Int32

@_silgen_name("maxps4_stikdualmap_arm64_execute_suite")
private func maxps4StikDualMapARM64ExecuteSuite(_ passed: UnsafeMutablePointer<Int32>?) -> Int32

@_silgen_name("maxps4_stikdualmap_arm64_execute_42")
private func maxps4StikDualMapARM64Execute42() -> Int32

@_silgen_name("maxps4_stikdualmap_debugger_preflight")
private func maxps4StikDualMapDebuggerPreflight() -> Int32

@_silgen_name("maxps4_native_jit_rw_alias_probe")
private func maxps4StikRWMemoryAliasProbe(_ pageSize: UnsafeMutablePointer<Int>?) -> Int32

@_silgen_name("maxps4_native_pkg_header")
private func maxps4NativePKGHeader(
    _ data: UnsafePointer<UInt8>?, _ length: Int,
    _ contentID: UnsafeMutablePointer<CChar>?, _ capacity: Int,
    _ revision: UnsafeMutablePointer<UInt32>?
) -> Int32

enum MaxPS4OnDeviceJITProbe {
    static func arm64RXStagingReport() -> String {
        var errorCode: Int32 = 0
        let status = maxps4NativeARM64RXStagingProbe(&errorCode)
        let result: String
        switch status {
        case 1: result = "PASS : MOV W0, #42 / RET écrits et vérifiés sur page RX"
        case 0: result = "REFUS : allocation ou passage RX impossible (errno \(errorCode))"
        case -2: result = "ÉCHEC : octets ARM64 modifiés ou illisibles après passage RX"
        default: result = "Test non disponible sur cet appareil"
        }
        return """
        Laboratoire MaxPS4 — staging ARM64 en mémoire RX
        \(result)
        Instructions ARM64 : 0x52800540, 0xD65F03C0
        Aucun branchement vers le code généré ; aucune exécution JIT.
        Aucun BRK StikDebug envoyé ; processus isolé non disponible.
        """
    }

    // Permission test only: no instruction execution and no StikDebug BRK.
    static func executablePermissionReport() -> String {
        var errorCode: Int32 = 0
        let status = maxps4NativeRWtoRXProbe(&errorCode)
        let outcome: String
        switch status {
        case 1:
            outcome = "Transition RW → RX acceptée par mprotect (exécution NON testée)"
        case 0:
            outcome = "Transition RW → RX refusée (errno \(errorCode))"
        default:
            outcome = "Test indisponible sur cette architecture"
        }
        return """
        Laboratoire MaxPS4 — permissions mémoire JIT
        \(outcome)
        CS_DEBUGGED : \(MaxPS4NativeLinkCheck.codeSigningDebugStatus)
        Aucun BRK StikDebug envoyé.
        Aucun code ARM64 généré exécuté.
        Cette expérience ne prouve pas l'accès au JIT ou à FEXCore.
        """
    }

    // Read-only preflight: does not issue BRK, map executable pages or call the allocator.
    static func stikAllocatorPreflightReport() -> String {
        let integrated = maxps4StikDualMapPortPresent() == 1
        let debugger = MaxPS4NativeLinkCheck.debuggerAttachmentDescription
        let debugSigning = MaxPS4NativeLinkCheck.codeSigningDebugStatus
        let protocolPresent = MaxPS4NativeLinkCheck.isStikDebugProtocolPresent
        var allocationError: Int32 = 0
        let mapResult = maxps4NativeMapJITErrnoProbe(&allocationError)
        let mapDescription: String
        switch mapResult {
        case 1:
            mapDescription = "Allocation MAP_JIT autorisée (aucune exécution)"
        case 0:
            mapDescription = "Refus MAP_JIT par iOS (errno \(allocationError))"
        default:
            mapDescription = "Indisponible / non pris en charge"
        }
        // Exercise RW-to-RW aliasing only; never allocate RX or emit BRK.
        var aliasPageSize = 0
        let rwAliasWorks = maxps4StikRWMemoryAliasProbe(&aliasPageSize) == 1
        let aliasDescription = rwAliasWorks
            ? "RW/RW validé sur \(aliasPageSize) octets (sans droits exécutables)"
            : "Échec ou indisponible (aucune page exécutable testée)"
        let attachObserved = maxps4StikDualMapDebuggerPreflight() == 1
        let safeNextStep = attachObserved
            ? "P_TRACED observé, mais le script universal.js reste à confirmer avant tout BRK"
            : "Vérifier la connexion debugserver à MaxPS4 dans StikDebug; ne pas lancer BRK"
        return """
        Précontrôle allocateur AetherPS4 / StikDebug (sans BRK)
        Code dual-mapping compilé : \(integrated ? "Oui" : "Non")
        État signature : \(debugSigning)
        État du débogueur : \(debugger)
        Entrées du protocole intégrées : \(protocolPresent ? "Oui" : "Non")
        Allocation mémoire iOS : \(mapDescription)
        Double mapping sans JIT : \(aliasDescription)
        Étape de vérification : \(safeNextStep)
        Précondition native débogueur : \(maxps4StikDualMapDebuggerPreflight() == 1 ? "Présent (script non confirmé)" : "Absente — test exécutable interdit")
        Test ARM64 via double mapping : Codé en natif, non exposé tant que le BRK reste dangereux
        Protocole source StikDebug : BRK #0xf00d, commande x16=1, arguments x0=0 / x1=taille
        Compatibilité statique : Commande et paramètres concordants avec StikDebug/Scripts/universal.js
        Réponse du serveur debugserver : Non vérifiée (aucune commande envoyée)
        Universal JIT Script attaché : Non vérifiable par ce test
        Allocation RX via StikDebug : Non tentée
        Alias RW via vm_remap : \(rwAliasWorks ? "Validé en RW/RW (sans RX)" : "Non validé")
        Exécution ARM64 générée : Non tentée
        Activation automatique : Désactivée

        Ce précontrôle ne prouve pas que StikDebug peut servir le protocole BRK. Ne pas activer l'allocateur tant que l'attachement et la gestion du trap ne sont pas validés.
        """
    }

    // CPU + JIT integration regression: actual x86 interpreter result is compared
    // with the ARM64 translator's validated output bytes (not executed).
    static func combinedEngineReport() -> String {
        let cases: [(String, Data, UInt64)] = [
            ("MOV 42 / RET", Data([0xB8, 42, 0, 0, 0, 0xC3]), 42),
            ("MOV 40 + ADD 2 / RET", Data([0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3]), 42),
            ("MOV 50 - SUB 8 / RET", Data([0xB8, 50, 0, 0, 0, 0x2D, 8, 0, 0, 0, 0xC3]), 42)
        ]
        var lines = ["MaxPS4 — tests CPU invité / traducteur ARM64"]
        var passed = 0
        for (name, code, expected) in cases {
            let guestResult = MaxPS4NativeLinkCheck.runNativeSyntheticX86(code)
            let translation = MaxPS4NativeLinkCheck.arm64TranslationPreview(code)
            let accepted = translation != nil &&
                MaxPS4NativeLinkCheck.verifyARM64Preview(translation!)
            let fallback = MaxPS4NativeLinkCheck.runSyntheticX86PreferJIT(code)
            let ok = guestResult == expected && accepted &&
                fallback?.result == expected && fallback?.usedJIT == false
            if ok { passed += 1 }
            lines.append("\(name) : \(ok ? "PASS" : "FAIL") — CPU \(guestResult.map(String.init) ?? "refusé"), ARM64 \(accepted ? "vérifié (données)" : "non validé"), JIT réellement utilisé : \(fallback?.usedJIT == true ? "oui" : "non")")
        }
        lines.append("Tests réussis : \(passed)/\(cases.count)")
        lines.append("Allocateur AetherPS4/StikDebug : \(maxps4StikDualMapPortPresent() == 1 ? "intégré (non activé)" : "indisponible")")
        lines.append("Mémoire exécutable et moteur FEXCore : non validés")
        lines.append("Ces programmes synthétiques ne constituent pas un jeu PS4.")
        return lines.joined(separator: "\n")
    }

    // Expanded deterministic translator coverage. JIT execution is not claimed.
    static func expandedARM64TranslationReport() -> String {
        let cases: [(String, [UInt8], UInt64)] = [
            ("MOV 42", [0xB8, 42, 0, 0, 0, 0xC3], 42),
            ("ADD 2", [0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3], 42),
            ("SUB 8", [0xB8, 50, 0, 0, 0, 0x2D, 8, 0, 0, 0, 0xC3], 42),
            ("XOR", [0xB8, 40, 0, 0, 0, 0x35, 2, 0, 0, 0, 0xC3], 42),
            ("AND", [0xB8, 43, 0, 0, 0, 0x25, 42, 0, 0, 0, 0xC3], 42),
            ("OR", [0xB8, 40, 0, 0, 0, 0x0D, 2, 0, 0, 0, 0xC3], 42),
            ("NOP", [0x90, 0xB8, 42, 0, 0, 0, 0xC3], 42)
        ]
        var lines = ["Laboratoire MaxPS4 — couverture x86-64 → ARM64 (données uniquement)"]
        var passed = 0
        for (name, bytes, expected) in cases {
            let code = Data(bytes)
            let actual = MaxPS4NativeLinkCheck.runNativeSyntheticX86(code)
            let words = MaxPS4NativeLinkCheck.arm64TranslationPreview(code)
            let accepted = words.map { MaxPS4NativeLinkCheck.verifyARM64Preview($0) } ?? false
            let ok = actual == expected && accepted
            if ok { passed += 1 }
            lines.append("\(name) : \(ok ? "PASS" : "FAIL") — interpréteur \(actual.map(String.init) ?? "échec"), traduction \(accepted ? "validée" : "refusée")")
        }
        lines.append("Total : \(passed)/\(cases.count)")
        lines.append("Aucune instruction ARM64 générée n’a été exécutée. JIT et FEXCore non validés.")
        return lines.joined(separator: "\n")
    }

    // Deterministic synthetic ELF64 x86-64 test: a data-only loader exercise,
    // not a real PS4 homebrew or SELF and not a functional PS4 runtime.
    static func minimalELFLoaderReport() -> String {
        var elf = Data(repeating: 0, count: 134)
        elf.replaceSubrange(0..<7, with: [0x7f, 0x45, 0x4c, 0x46, 2, 1, 1])
        elf[18] = 0x3e // e_machine = x86_64
        elf[24] = 0x00; elf[25] = 0x10 // entry = 0x1000
        elf[32] = 64 // ELF program header table
        elf[54] = 56 // ELF64 program header size
        elf[56] = 1
        elf[64] = 1 // PT_LOAD
        elf[68] = 5 // PF_R | PF_X
        elf[72] = 128 // segment file offset
        elf[80] = 0x00; elf[81] = 0x10 // virtual address 0x1000
        elf[96] = 6  // filesz
        elf[104] = 6 // memsz
        elf.replaceSubrange(128..<134, with: [0xB8, 42, 0, 0, 0, 0xC3])
        let signature = MaxPS4NativeLinkCheck.executableSignature(elf) == 1
        let layout = MaxPS4NativeLinkCheck.nativeELFLoadLayout(elf)
        let entry = MaxPS4NativeLinkCheck.nativeELFExecutableEntry(elf)
        let boundsOK = entry?.entry == 0x1000 && entry?.offset == 128
            && layout?.segments == 1 && layout?.bytes == 6
        let code = boundsOK ? elf.subdata(in: 128..<134) : Data()
        let result = MaxPS4NativeLinkCheck.runNativeSyntheticX86(code)
        var invalid = elf
        invalid[68] = 4 // remove execute permission
        let rejectsNotExecutable = MaxPS4NativeLinkCheck.nativeELFExecutableEntry(invalid) == nil
        let rejectsTruncated = MaxPS4NativeLinkCheck.nativeELFLoadLayout(Data(elf.prefix(133))) == nil
        let good = signature && boundsOK && result == 42 &&
            rejectsNotExecutable && rejectsTruncated
        return """
        Test ELF64 x86-64 minimal (fichier synthétique)
        En-tête ELF reconnu : \(signature ? "PASS" : "FAIL")
        Segment PT_LOAD et point d'entrée : \(boundsOK ? "PASS" : "FAIL")
        Exécution des 6 octets à l'entrée par l'interpréteur : \(result == 42 ? "PASS — retour 42" : "FAIL")
        Permissions d'exécution refusées : \(rejectsNotExecutable ? "PASS" : "FAIL")
        Fichier tronqué refusé : \(rejectsTruncated ? "PASS" : "FAIL")
        Résultat global : \(good ? "PASS" : "FAIL")
        JIT utilisé : Non
        Chargeur SELF PS4 et services PS4 : Non opérationnels

        Ce test construit un ELF artificiel en mémoire et extrait son code pour
        l'interpréteur limité. Il ne lance ni homebrew PS4 ni jeu commercial.
        """
    }

    // Test fixture uses the first 0x70 bytes of the user-supplied Kero Blaster PKG
    // (not the complete package). It proves parsing only, not game installation.
    // Import through Files: read only a bounded header using a security-scoped URL.
    // No game installation, decryption, or whole-PKG allocation.
    static func inspectImportedPKG(_ url: URL) -> String {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        do {
            let handle = try FileHandle(forReadingFrom: url)
            defer { try? handle.close() }
            let data = try handle.read(upToCount: 0x70) ?? Data()
            guard data.count >= 0x70 else {
                return "PKG : en-tête incomplet (moins de 112 octets)."
            }
            var output = [CChar](repeating: 0, count: 37)
            var revision: UInt32 = 0
            let valid = data.withUnsafeBytes { raw in
                output.withUnsafeMutableBufferPointer { dst in
                    maxps4NativePKGHeader(
                        raw.bindMemory(to: UInt8.self).baseAddress, data.count,
                        dst.baseAddress, dst.count, &revision
                    )
                }
            } == 1
            guard valid else {
                return "PKG : signature ou identifiant non reconnu. Aucun contenu exécuté."
            }
            let contentID = String(cString: output)
            let fileSize = (try? FileManager.default.attributesOfItem(atPath: url.path)[.size] as? NSNumber)?.int64Value
            // Standard PS4 PKG: BE entry count at 0x10, table offset at 0x18.
            // Each table record occupies 32 bytes. Limit reads to 128 KiB.
            func be32(_ index: Int) -> UInt32 {
                data[index..<(index + 4)].reduce(UInt32(0)) {
                    ($0 << 8) | UInt32($1)
                }
            }
            let entries = Int(be32(0x10))
            let tableOffset = UInt64(be32(0x18))
            let tableBytes = entries <= 4096 ? UInt64(entries) * 32 : 0
            var entryReport = "Entrées : table invalide ou limites dépassées"
            if entries > 0, tableBytes > 0, tableBytes <= 131072,
               let total = fileSize, total >= 0,
               tableOffset <= UInt64(total),
               tableBytes <= UInt64(total) - tableOffset {
                try handle.seek(toOffset: tableOffset)
                let table = try handle.read(upToCount: Int(tableBytes)) ?? Data()
                if table.count == Int(tableBytes) {
                    var preview: [String] = []
                    for index in 0..<min(entries, 8) {
                        let position = index * 32
                        let ident = table[position..<(position + 4)].reduce(UInt32(0)) {
                            ($0 << 8) | UInt32($1)
                        }
                        preview.append(String(format: "%08X", ident))
                    }
                    var invalidRanges = 0
                    var encryptedFlags = 0
                    var encryptedEntries: [String] = []
                    var clearDataBytes: UInt64 = 0
                    var protectedDataBytes: UInt64 = 0
                    for index in 0..<entries {
                        let p = index * 32
                        func beTable32(_ at: Int) -> UInt32 {
                            table[(p + at)..<(p + at + 4)].reduce(UInt32(0)) {
                                ($0 << 8) | UInt32($1)
                            }
                        }
                        let flags = beTable32(8) // field +4 is filename offset, +8 is flags
                        let offset = UInt64(beTable32(16))
                        let size = UInt64(beTable32(20))
                        if offset > UInt64(total) ||
                           size > UInt64(total) - min(offset, UInt64(total)) {
                            invalidRanges += 1
                        }
                        if flags & 0x80000000 != 0 {
                            encryptedFlags += 1
                            protectedDataBytes += size
                            encryptedEntries.append(String(format: "%08X", beTable32(0)) +
                                " (" + String(size) + " octets)")
                        } else {
                            clearDataBytes += size
                        }
                    }
                    // Merge validated ranges to avoid double-counting overlapping entries.
                    var ranges: [(UInt64, UInt64)] = []
                    for index in 0..<entries {
                        let p = index * 32
                        func beAt(_ at: Int) -> UInt64 {
                            UInt64(table[(p + at)..<(p + at + 4)].reduce(UInt32(0)) {
                                ($0 << 8) | UInt32($1)
                            })
                        }
                        let start = beAt(16), size = beAt(20)
                        if size > 0 && start <= UInt64(total) &&
                           size <= UInt64(total) - start {
                            ranges.append((start, start + size))
                        }
                    }
                    ranges.sort { $0.0 < $1.0 }
                    var cursor: UInt64 = 0
                    var covered: UInt64 = 0
                    var gaps: [(UInt64, UInt64)] = []
                    for (start, end) in ranges {
                        if start > cursor { gaps.append((cursor, start)) }
                        if end > cursor {
                            covered += end - max(cursor, start)
                            cursor = end
                        }
                    }
                    if cursor < UInt64(total) { gaps.append((cursor, UInt64(total))) }
                    let largestGaps = gaps.sorted { ($0.1 - $0.0) > ($1.1 - $1.0) }
                        .prefix(3).map {
                            String(format: "0x%llX–0x%llX (%llu octets)", $0.0, $0.1, $0.1 - $0.0)
                        }
                    let unreferencedBytes = UInt64(total) - covered
                    let largestGapDescription: String
                    if largestGaps.isEmpty {
                        largestGapDescription = "Aucun"
                    } else {
                        largestGapDescription = largestGaps.joined(separator: "; ")
                    }
                    let gapReport = "Zones référencées uniques : \(covered) octets; " +
                        "zones non référencées : \(unreferencedBytes) octets; " +
                        "plus grands intervalles : \(largestGapDescription)"
                    // Read-only sampling of the largest unreferenced region.
                    var gapSamples: [String] = []
                    if let largest = gaps.max(by: { ($0.1 - $0.0) < ($1.1 - $1.0) }) {
                        let start = largest.0, end = largest.1
                        let length = end - start
                        if length >= 64 {
                            let middle = start + length / 2
                            let tail = end - 64
                            for position in [start, middle, tail] {
                                try handle.seek(toOffset: position)
                                let bytes = try handle.read(upToCount: 64) ?? Data()
                                guard bytes.count == 64 else { continue }
                                let nonzero = bytes.filter { $0 != 0 }.count
                                let unique = Set(bytes).count
                                let hex = bytes.prefix(8).map { String(format: "%02X", $0) }
                                    .joined(separator: " ")
                                gapSamples.append(String(format: "0x%llX", position) +
                                    " : " + hex + " ; non-nuls " + String(nonzero) +
                                    "/64 ; valeurs distinctes " + String(unique))
                            }
                        }
                    }
                    let gapSampleReport = gapSamples.isEmpty ? "Aucun" :
                        gapSamples.joined(separator: "; ")
                    // Entry 0x200 is the bounded null-terminated filename table.
                    var names = Data()
                    for index in 0..<entries {
                        let position = index * 32
                        let id = table[position..<(position + 4)].reduce(UInt32(0)) {
                            ($0 << 8) | UInt32($1)
                        }
                        guard id == 0x200 else { continue }
                        func be32At(_ at: Int) -> UInt32 {
                            table[(position + at)..<(position + at + 4)].reduce(UInt32(0)) {
                                ($0 << 8) | UInt32($1)
                            }
                        }
                        let off = UInt64(be32At(16)), length = UInt64(be32At(20))
                        if length > 0 && length <= 16384 && off <= UInt64(total) &&
                           length <= UInt64(total) - off {
                            try handle.seek(toOffset: off)
                            names = try handle.read(upToCount: Int(length)) ?? Data()
                        }
                    }
                    var resolved: [String] = []
                    for index in 0..<entries {
                        let position = index * 32
                        func be32At(_ at: Int) -> UInt32 {
                            table[(position + at)..<(position + at + 4)].reduce(UInt32(0)) {
                                ($0 << 8) | UInt32($1)
                            }
                        }
                        let id = be32At(0), nameOffset = Int(be32At(4))
                        guard id >= 0x1000, nameOffset > 0,
                              nameOffset < names.count else { continue }
                        let bytes = names[nameOffset..<names.count].prefix(while: { $0 != 0 })
                        guard bytes.count > 0 && bytes.count <= 256,
                              let path = String(bytes: bytes, encoding: .utf8) else { continue }
                        resolved.append(String(format: "%08X", id) + " : " + path)
                    }
                    var signatures: [String] = []
                    var psfDetails: [String] = []
                    for index in 0..<entries {
                        let p = index * 32
                        func word(_ at: Int) -> UInt32 {
                            table[(p + at)..<(p + at + 4)].reduce(UInt32(0)) {
                                ($0 << 8) | UInt32($1)
                            }
                        }
                        let ident = word(0)
                        let offset = UInt64(word(16))
                        let length = UInt64(word(20))
                        guard length >= 4, offset <= UInt64(total),
                              length <= UInt64(total) - offset else { continue }
                        try handle.seek(toOffset: offset)
                        let signature = try handle.read(upToCount: 8) ?? Data()
                        let label: String?
                        if signature.starts(with: [0, 0x50, 0x53, 0x46]) {
                            label = "PSF (probable param.sfo)"
                            // Strictly bounded metadata preview; no encrypted contents touched.
                            if length >= 20 && length <= 65536 {
                                try handle.seek(toOffset: offset)
                                let psf = try handle.read(upToCount: Int(length)) ?? Data()
                                if psf.count >= 20 {
                                    func le32(_ i: Int) -> Int {
                                        Int(psf[i]) | (Int(psf[i + 1]) << 8) |
                                        (Int(psf[i + 2]) << 16) | (Int(psf[i + 3]) << 24)
                                    }
                                    let keyStart = le32(8), dataStart = le32(12), count = le32(16)
                                    if keyStart >= 20 && keyStart < psf.count &&
                                       dataStart >= keyStart && dataStart < psf.count &&
                                       count >= 0 && count <= 128 && count <= (psf.count - 20) / 16 {
                                        for j in 0..<count {
                                            let base = 20 + j * 16
                                            let keyOffset = Int(psf[base]) | (Int(psf[base + 1]) << 8)
                                            let format = Int(psf[base + 2]) | (Int(psf[base + 3]) << 8)
                                            let size = le32(base + 4)
                                            let valueOffset = le32(base + 12)
                                            guard keyOffset <= psf.count - keyStart - 1,
                                                  valueOffset <= psf.count - dataStart,
                                                  size >= 0 && size <= psf.count - dataStart - valueOffset else { continue }
                                            let keyBytes = psf[(keyStart + keyOffset)..<psf.count].prefix(while: { $0 != 0 })
                                            guard keyBytes.count <= 64,
                                                  let key = String(bytes: keyBytes, encoding: .utf8),
                                                  ["TITLE", "TITLE_ID", "CONTENT_ID", "APP_VER"].contains(key) else { continue }
                                            if format == 0x0204 {
                                                let valueBytes = psf[(dataStart + valueOffset)..<(dataStart + valueOffset + size)].prefix(while: { $0 != 0 })
                                                if let value = String(bytes: valueBytes, encoding: .utf8), value.count <= 128 {
                                                    psfDetails.append(key + " : " + value)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        } else if signature.starts(with: [0x89, 0x50, 0x4E, 0x47]) {
                            label = "PNG"
                        } else if signature.starts(with: [0x7F, 0x45, 0x4C, 0x46]) {
                            label = "ELF"
                        } else {
                            label = nil
                        }
                        if let label {
                            signatures.append(String(format: "%08X", ident) + " : " + label)
                        }
                    }
                    entryReport = "Entrées de table lues : \(entries) (IDs initiaux : \(preview.joined(separator: ", ")))\nPlages hors fichier : \(invalidRanges)\nEntrées marquées chiffrées (flags +8) : \(encryptedFlags)\nEntrées protégées (ID et taille) : \(encryptedEntries.isEmpty ? "Aucune" : encryptedEntries.joined(separator: "; "))\nTailles déclarées (non dédupliquées) : accessibles \(clearDataBytes) octets; protégées \(protectedDataBytes) octets\n\(gapReport)\nÉchantillons de zone non référencée (lecture seule) : \(gapSampleReport)\nSignatures internes reconnues : \(signatures.isEmpty ? "Aucune" : signatures.joined(separator: "; "))\nNoms résolus : \(resolved.isEmpty ? "Aucun" : resolved.joined(separator: "; "))\nMétadonnées PSF : \(psfDetails.isEmpty ? "Aucune valeur décodée (format à examiner)" : psfDetails.joined(separator: "; "))"

                } else {
                    entryReport = "Entrées : lecture incomplète"
                }
            }
            return """
            PKG PS4 importé depuis Fichiers
            Nom : \(url.lastPathComponent)
            Taille : \(fileSize.map { String($0) + " octets" } ?? "Inconnue")
            Signature : Reconnue
            Content ID : \(contentID)
            Révision en-tête : \(revision)
            Lecture : en-tête de 112 octets + table bornée si valide
            \(entryReport)
            eboot.bin : Non identifié dans les entrées nommées du PKG
            Déchiffrement / exécution PS4 : Non disponibles
            JIT : Non utilisé
            """
        } catch {
            return "Impossible d’ouvrir le PKG : \(error.localizedDescription)"
        }
    }

    static func keroPKGHeaderReport() -> String {
        var header = [UInt8](repeating: 0, count: 0x70)
        header[0] = 0x7f; header[1] = 0x43; header[2] = 0x4e; header[3] = 0x54
        header[7] = 1
        let id = Array("UP0969-CUSA06015_00-KEROBLASTERUS000".utf8)
        for (index, byte) in id.enumerated() { header[0x40 + index] = byte }
        var output = [CChar](repeating: 0, count: 37)
        var revision: UInt32 = 0
        let ok = header.withUnsafeBufferPointer { data in
            output.withUnsafeMutableBufferPointer { dst in
                maxps4NativePKGHeader(data.baseAddress, data.count,
                    dst.baseAddress, dst.count, &revision)
            }
        } == 1
        let parsed = String(cString: output)
        var corrupt = header
        corrupt[0] = 0
        var unused = [CChar](repeating: 0, count: 37)
        var badRevision: UInt32 = 0
        let rejected = corrupt.withUnsafeBufferPointer { data in
            unused.withUnsafeMutableBufferPointer { dst in
                maxps4NativePKGHeader(data.baseAddress, data.count,
                    dst.baseAddress, dst.count, &badRevision)
            }
        } == 0
        return """
        Lecteur PKG PS4 — test Kero Blaster (en-tête témoin)
        Signature PKG reconnue : \(ok ? "PASS" : "FAIL")
        Content ID : \(parsed)
        Révision de conteneur : \(revision)
        Signature corrompue refusée : \(rejected ? "PASS" : "FAIL")
        Fichier PKG entier analysé sur cet iPhone : Non
        Entrées internes / eboot.bin : Non analysés
        Déchiffrement et exécution : Non disponibles
        JIT : Non activé

        Test basé sur les octets de l’en-tête fourni, sans charger le PKG entier.
        """
    }

    static func report() -> String {
        let mapStatus: String
        var errorCode: Int32 = 0
        let allocation = maxps4NativeMapJITErrnoProbe(&errorCode)
        switch allocation {
        case 1: mapStatus = "Allocation MAP_JIT : Autorisée (sans exécution)"
        case 0: mapStatus = "Allocation MAP_JIT : Refusée par iOS (errno \(errorCode): \(String(cString: strerror(errorCode))))"
        default: mapStatus = "Allocation MAP_JIT : Indisponible"
        }
        return """
        Diagnostic JIT iPhone
        Signature / CS_DEBUGGED : \(MaxPS4NativeLinkCheck.codeSigningDebugStatus)
        Débogueur / P_TRACED : \(MaxPS4NativeLinkCheck.debuggerAttachmentDescription)
        Protocole StikDebug intégré : \(MaxPS4NativeLinkCheck.isStikDebugProtocolPresent ? "Oui" : "Non")
        \(mapStatus)
        Entitlement get-task-allow : Non lu directement (vérifier dans StikDebug)\n        Méthode StikDebug : la confirmation de demande ne garantit pas le succès de mmap(MAP_JIT)
        Exécution ARM64 générée : Non testée par ce diagnostic
        Allocateur AetherPS4/StikDebug : \(maxps4StikDualMapPortPresent() == 1 ? "Code natif intégré, activation non validée" : "Absent")\n        FEXCore : Non validé

        Remarque : P_TRACED absent ne signifie pas à lui seul que StikDebug a échoué. MAP_JIT autorisé ne prouve pas une mémoire RX ni une exécution JIT réussie.
        """
    }
}

@_silgen_name("maxps4_native_generated_arm64_execute_probe")
private func maxps4_native_generated_arm64_execute_probe() -> Int32

extension MaxPS4OnDeviceJITProbe {
    // A standard iOS app has no safe fork-based crash sandbox for generated code.
    // Show the precise readiness gates without branching into RX memory.
    // User-initiated experimental BRK path. The debugger trap can terminate the app.
    // A marker survives a crash/relaunch, without implying that the trap was handled.
    static func manuallyExecuteBranchSuite() -> String {
        guard maxps4StikDualMapDebuggerPreflight() == 1 else {
            return "Branchements JIT non lancés : P_TRACED absent ; aucun BRK envoyé."
        }
        UserDefaults.standard.set("suite branchements JIT démarrée — résultat inconnu si fermeture", forKey: "maxps4StikDebugAttempt")
        var passed: Int32 = 0
        let status = maxps4StikDualMapARM64BranchSuite(&passed)
        let details: String
        if status == 1 {
            details = "PASS : \(passed)/3 tests natifs ARM64 (CMP/JZ, TEST/JNZ, boucle SUB/JNZ)"
        } else {
            details = "ÉCHEC : \(passed)/3 réussis, code \(status). Ne prouve pas un backend PS4 complet."
        }
        UserDefaults.standard.set(details, forKey: "maxps4StikDebugAttempt")
        return "Laboratoire — branchements et boucle JIT\n" + details
    }

    static func manuallyExecuteStikDebugSuite() -> String {
        guard maxps4StikDualMapDebuggerPreflight() == 1 else {
            return "Suite ARM64 non lancée : P_TRACED absent ; aucun BRK envoyé."
        }
        UserDefaults.standard.set("suite MOV/ADD/SUB commencée — issue inconnue en cas de fermeture", forKey: "maxps4StikDebugAttempt")
        var passed: Int32 = 0
        let status = maxps4StikDualMapARM64ExecuteSuite(&passed)
        let detail: String
        switch status {
        case 1: detail = "PASS : \(passed)/3 programmes exécutés en ARM64 natif ; MOV 42, MOV 40 + ADD 2, MOV 50 - SUB 8"
        case -3: detail = "Allocation StikDebug impossible (BRK non confirmé)"
        case -4: detail = "Échec de résultat pour le programme \(passed + 1)"
        case -5: detail = "Traduction/vérification refusée pour le programme \(passed + 1)"
        case -1: detail = "Débogueur absent au démarrage"
        default: detail = "Test indisponible (code \(status))"
        }
        UserDefaults.standard.set(detail, forKey: "maxps4StikDebugAttempt")
        return "Laboratoire — suite JIT ARM64 réelle\n" + detail + "\nCette suite synthétique ne valide pas encore FEXCore ou Kero Blaster."
    }

    static func manuallyExecuteStikDebug42() -> String {
        guard maxps4StikDualMapDebuggerPreflight() == 1 else {
            return "Test non lancé : P_TRACED absent. Aucune commande BRK envoyée."
        }
        UserDefaults.standard.set("tentative démarrée — résultat inconnu si MaxPS4 se ferme", forKey: "maxps4StikDebugAttempt")
        let result = maxps4StikDualMapARM64Execute42()
        let text: String
        switch result {
        case 1: text = "PASS : code ARM64 généré exécuté et résultat 42 obtenu via StikDebug"
        case -1: text = "Refus : attachement du débogueur non observé"
        case -2: text = "Refus : taille de page incompatible"
        case -3: text = "Échec : allocation RX/RW via StikDebug non obtenue"
        case -4: text = "Échec : la fonction ARM64 n’a pas renvoyé 42"
        case -5: text = "Échec : traduction ou vérification ARM64"
        default: text = "Test non pris en charge"
        }
        UserDefaults.standard.set(text, forKey: "maxps4StikDebugAttempt")
        return "Laboratoire — test manuel StikDebug (BRK)\n" + text
    }

    static func lastStikDebugAttemptReport() -> String {
        "Dernière tentative StikDebug : " + (UserDefaults.standard.string(forKey: "maxps4StikDebugAttempt") ?? "aucune")
    }

    static func executionIsolationReadinessReport() -> String {
        var jitError: Int32 = 0
        let jit = maxps4NativeMapJITErrnoProbe(&jitError)
        var rxError: Int32 = 0
        let rx = maxps4NativeRWtoRXProbe(&rxError)
        let debugger = maxps4StikDualMapDebuggerPreflight() == 1
        let jitText = jit == 1 ? "disponible" : (jit == 0 ? "refusé (errno \(jitError))" : "indisponible")
        let rxText = rx == 1 ? "autorisée" : (rx == 0 ? "refusée (errno \(rxError))" : "indisponible")
        return """
        Laboratoire MaxPS4 — préparation d'exécution native ARM64
        MAP_JIT : \(jitText)
        Transition RW → RX : \(rxText)
        État de signature : \(MaxPS4NativeLinkCheck.codeSigningDebugStatus)
        Indice P_TRACED : \(debugger ? "présent" : "absent (non concluant pour StikDebug)")
        Service universal.js : non confirmé
        Processus de test isolé : non disponible dans cette application iOS
        Exécution dynamique ARM64 : NON déclenchée
        Étape suivante : ajouter une cible auxiliaire réellement isolée et signée, puis valider la gestion des erreurs iOS avant tout branchement.
        """
    }

    static func generatedARM64ExecutionReport() -> String {
        // Fail closed before entering generated-code execution. This check is
        // intentionally independent of CS_DEBUGGED: its presence is not proof
        // that an executable mapping is permitted on this iPhone.
        guard maxps4StikDualMapPortPresent() == 1 else {
            return "Test ARM64 arrêté : backend natif non disponible."
        }
        var allocationError: Int32 = 0
        let mapStatus = maxps4NativeMapJITErrnoProbe(&allocationError)
        if mapStatus == 0 {
            return "Test ARM64 arrêté avant exécution : iOS refuse MAP_JIT (errno \(allocationError)). Aucun code généré exécuté. Le statut CS_DEBUGGED seul ne suffit pas."
        }
        if mapStatus != 1 {
            return "Test ARM64 arrêté : état MAP_JIT indisponible. Aucun code généré exécuté."
        }
        guard maxps4StikDualMapDebuggerPreflight() == 1 else {
            return "Test ARM64 arrêté : aucun débogueur détecté. MAP_JIT accessible ne prouve pas que le script StikDebug est attaché."
        }
        // Even if these gates pass, the native probe can fail; its return
        // value is the only success evidence. No BRK is emitted by this path.
        switch maxps4_native_generated_arm64_execute_probe() {
        case 1:
            return "Instructions ARM64 générées dynamiquement : Exécutées (42). Test exécuté dans le processus MaxPS4, NON isolé ; FEXCore et les jeux PS4 ne sont pas validés."
        case -1:
            return "Test ARM64 généré arrêté : P_TRACED absent selon iOS. Cela ne démontre pas à lui seul un échec de StikDebug. Aucune instruction ARM64 générée exécutée. Vérifier la session debugserver et les permissions de mémoire."
        case -2:
            return "Test interrompu : allocation MAP_JIT refusée par iOS. Aucun code généré exécuté."
        case -3:
            return "Test interrompu : changement de permissions RW → RX refusé par iOS. Aucun code généré exécuté."
        case -4:
            return "Échec : le code ARM64 généré n’a pas renvoyé 42."
        default:
            return "Exécution ARM64 générée indisponible sur cet appareil."
        }
    }
}
