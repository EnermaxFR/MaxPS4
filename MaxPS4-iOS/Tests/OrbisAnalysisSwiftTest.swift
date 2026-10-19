import Foundation

@main
struct OrbisAnalysisSwiftTest {
    static func main() throws {
        guard CommandLine.arguments.count == 2 else { fatalError("Synthetic fixture path required") }
        let url = URL(fileURLWithPath: CommandLine.arguments[1])
        let report = MaxPS4OrbisAnalyzer.inspect(url: url)
        precondition(report.contains("ET_SCE_DYNAMIC / PRX"))
        precondition(report.contains("Architecture : x86-64"))
        precondition(report.contains("NID=aI+OeCz8xrQ"))
        precondition(report.contains("résolus : 1"))
        precondition(report.contains("aucune exécution"))
        precondition(report.contains("Préparation runtime Orbis"))
        precondition(report.contains("Préparation : refusée"))
        precondition(report.contains("Exécution : impossible"))
        let invalid = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: invalid) }
        try Data([0]).write(to: invalid)
        precondition(MaxPS4OrbisAnalyzer.inspect(url: invalid).contains("Rejet"))
        precondition(MaxPS4OrbisAnalyzer.inspect(url: invalid.appendingPathExtension("missing")).contains("interrompue"))
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let missing = MaxPS4OrbisAnalyzer.inspectDirectory(url: directory)
        precondition(missing.inspectionCompleted && missing.text.contains("eboot.bin manquant"))
        precondition(missing.text.contains("[ORBIS.PREFLIGHT] execution_admitted=0"))
        try FileManager.default.copyItem(at: url, to: directory.appendingPathComponent("eboot.bin"))
        let inventory = MaxPS4OrbisAnalyzer.inspectDirectory(url: directory)
        precondition(inventory.inspectionCompleted && inventory.text.contains("NID=aI+OeCz8xrQ"))
        precondition(inventory.text.contains("[ORBIS.PREFLIGHT] execution_admitted=0"))
        precondition(inventory.text == MaxPS4OrbisAnalyzer.inspectDirectory(url: directory).text)
        precondition(inventory.text.contains("aucune exécution"))
        precondition(!MaxPS4OrbisAnalyzer.inspectDirectory(url: invalid).inspectionCompleted)
        // Mission 43: synthetic static SELF through the production Swift wrapper.
        var elf = [UInt8](repeating: 0, count: 128)
        func put(_ bytes: inout [UInt8], _ offset: Int, _ value: UInt64, _ width: Int = 8) {
            for i in 0..<width { bytes[offset+i] = UInt8(truncatingIfNeeded: value >> (8*i)) }
        }
        elf.replaceSubrange(0..<8, with: [0x7f, 69, 76, 70, 2, 1, 1, 9])
        put(&elf,16,0xfe00,2); put(&elf,18,62,2); put(&elf,20,1,4)
        put(&elf,24,0x1000); put(&elf,32,64); put(&elf,52,64,2)
        put(&elf,54,56,2); put(&elf,56,1,2); put(&elf,64,1,4)
        put(&elf,68,5,4); put(&elf,72,120); put(&elf,80,0x1000)
        put(&elf,96,8); put(&elf,104,16); put(&elf,112,1); elf[120] = 0xc3
        var clear = [UInt8](repeating: 0, count: 192)
        put(&clear,0,0x1d3d154f,4)
        clear[5]=1; clear[6]=1; clear[7]=0x12; clear[8]=1; clear[9]=1
        put(&clear,12,184,2); put(&clear,16,192,4); put(&clear,24,1,2)
        put(&clear,32,0x800); put(&clear,40,184); put(&clear,48,8); put(&clear,56,8)
        clear.replaceSubrange(64..<184, with: elf[0..<120])
        clear.replaceSubrange(184..<192, with: elf[120..<128])
        let eboot = directory.appendingPathComponent("eboot.bin")
        for bytes in [elf, clear] {
            try Data(bytes).write(to: eboot)
            precondition(!MaxPS4OrbisAnalyzer.inspect(url: eboot).contains("Analyse interrompue"))
            let result = MaxPS4OrbisAnalyzer.inspectDirectory(url: directory)
            precondition(result.inspectionCompleted && result.text.contains("execution_admitted=0"))
            precondition(result.text.contains("Proposition temporaire en mémoire"))
            let preserved = try Data(contentsOf: eboot)
            precondition(preserved == Data(bytes))
            precondition(!FileManager.default.fileExists(atPath: directory.appendingPathComponent("maxps4.json").path))
        }
        precondition(MaxPS4OrbisAnalyzer.inspect(url: eboot).contains("SELF en clair : profil statique strict validé"))
        var signedClear = clear
        put(&signedClear,32,0x804)
        try Data(signedClear).write(to: eboot)
        let signedInspection = MaxPS4OrbisAnalyzer.inspectDirectory(url: directory, inspectClearSelf: true)
        precondition(signedInspection.inspectionCompleted && signedInspection.text.contains("inspection non authentifiée"))
        precondition(signedInspection.text.contains("execution_admitted=0"))
        precondition(!signedInspection.text.contains("Proposition temporaire en mémoire"))
        let strictInspection = MaxPS4OrbisAnalyzer.inspectDirectory(url: directory)
        precondition(strictInspection.text.contains("Conteneur SELF non pris en charge"))
        for (flags, diagnostic) in [(UInt64(0x802), "SELF.ENCRYPTED"), (UInt64(0x808), "SELF.COMPRESSED_UNSUPPORTED")] {
            var protected = clear
            put(&protected,32,flags)
            try Data(protected).write(to: eboot)
            let result = MaxPS4OrbisAnalyzer.inspectDirectory(url: directory)
            precondition(result.inspectionCompleted && result.text.contains("execution_admitted=0"))
            precondition(result.text.contains(diagnostic) && !result.text.contains("Proposition temporaire en mémoire"))
            precondition(MaxPS4OrbisAnalyzer.inspect(url: eboot).contains(diagnostic))
            let clearInspection = MaxPS4OrbisAnalyzer.inspectDirectory(url: directory, inspectClearSelf: true)
            precondition(clearInspection.inspectionCompleted && clearInspection.text.contains(diagnostic))
            precondition(clearInspection.text.contains("execution_admitted=0"))
        }
        print("PASS Swift Orbis analysis bridge: SCE PRX metadata, qualified NID and rejected inputs; no execution")
    }
}
