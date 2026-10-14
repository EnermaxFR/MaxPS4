import Foundation

@main
struct PKGSwiftDiagnosticTest {
    static func main() throws {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + ".pkg")
        defer { try? FileManager.default.removeItem(at: url) }
        var data = Data(repeating: 0, count: 0x610)
        func put(_ at: Int, _ value: UInt64, _ width: Int) {
            for i in 0..<width { data[at + width - 1 - i] = UInt8(truncatingIfNeeded: value >> (i * 8)) }
        }
        data.replaceSubrange(0..<4, with: [0x7f, 0x43, 0x4e, 0x54])
        put(0x10, 1, 4); put(0x16, 1, 2); put(0x18, 0x5a0, 4)
        put(0x20, 0x600, 8); put(0x28, 16, 8); put(0x430, UInt64(data.count), 8)
        put(0x5a0, 0x1000, 4); put(0x5b0, 0x600, 4); put(0x5b4, 16, 4)
        data.replaceSubrange(0x600..<0x604, with: [0, 0x50, 0x53, 0x46])
        try data.write(to: url)
        let report = try MaxPS4PKGInspector.inspect(url: url)
        precondition(report.contains("MaxPS4") && report.contains("param.sfo"))
        precondition(report.contains("Octets copiés en mémoire : 16"))
        put(0x5a8, 0x80000000, 4)
        try data.write(to: url)
        let protected = try MaxPS4PKGInspector.inspect(url: url)
        precondition(protected.contains("chiffré (flag déclaré)"))
        precondition(protected.contains("Octets copiés en mémoire : 0"))
        // Independent synthetic PFS header: classification through the real ABI.
        data = Data(repeating: 0, count: 0x700)
        data.replaceSubrange(0..<4, with: [0x7f, 0x43, 0x4e, 0x54])
        put(0x10, 1, 4); put(0x16, 1, 2); put(0x18, 0x5a0, 4)
        put(0x430, UInt64(data.count), 8); put(0x410, 0x600, 8); put(0x418, 0x100, 8)
        put(0x5a0, 0x409, 4)
        func little(_ at: Int, _ value: UInt64, _ width: Int) {
            for i in 0..<width { data[at + i] = UInt8(truncatingIfNeeded: value >> (i * 8)) }
        }
        little(0x600, 1, 8); little(0x608, 20130315, 8); little(0x61c, 12, 2)
        try data.write(to: url)
        let encryptedPFS = try MaxPS4PKGInspector.inspect(url: url)
        precondition(encryptedPFS.contains("PFS chiffré déclaré : inaccessible"))
        precondition(encryptedPFS.contains("budget de lecture=72 octets"))
        little(0x61c, 8, 2); try data.write(to: url)
        let plainPFS = try MaxPS4PKGInspector.inspect(url: url)
        precondition(plainPFS.contains("PFS non chiffré déclaré : contenu non pris en charge"))
        precondition(plainPFS.contains("eboot.bin demeure inaccessible"))
        little(0x608, 20130305, 8); try data.write(to: url)
        let unknownPFS = try MaxPS4PKGInspector.inspect(url: url)
        precondition(unknownPFS.contains("chiffrement indéterminé"))
        try Data([0x7f, 0x43, 0x4e, 0x54]).write(to: url)
        let rejected = try MaxPS4PKGInspector.inspect(url: url)
        precondition(rejected.contains("Rejet"))
        try FileManager.default.removeItem(at: url)
        do {
            _ = try MaxPS4PKGInspector.inspect(url: url)
            fatalError("Missing source was accepted")
        } catch MaxPS4PKGInspector.ReportError.failed {}
        if CommandLine.arguments.count > 1 {
            let fixtures = URL(fileURLWithPath: CommandLine.arguments[1])
            let plain = fixtures.appendingPathComponent("pfs-plain.pkg")
            let file = try MaxPS4PKGInspector.readPFSFile(url: plain, selected: "eboot.bin")
            precondition(file == Data("hello".utf8))
            let plainReport = try MaxPS4PKGInspector.inspect(url: plain)
            precondition(plainReport.contains("PFS non chiffré accessible"))
            for (pkg, name) in [("pfs-plain.pkg", "../eboot.bin"), ("pfs-plain.pkg", "keystone"),
                                ("pfs-encrypted.pkg", "eboot.bin"), ("pfs-unknown.pkg", "eboot.bin")] {
                do {
                    _ = try MaxPS4PKGInspector.readPFSFile(url: fixtures.appendingPathComponent(pkg), selected: name)
                    fatalError("Unsafe or unsupported selection accepted")
                } catch MaxPS4PKGInspector.ReportError.failed {}
            }
        }
        print("PASS Swift/native PKG report, plaintext copy, encryption refusal, PFS header statuses and bounded extraction, truncation and missing source")
    }
}
