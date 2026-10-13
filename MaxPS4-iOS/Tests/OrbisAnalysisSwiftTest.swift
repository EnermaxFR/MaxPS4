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
        let invalid = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: invalid) }
        try Data([0]).write(to: invalid)
        precondition(MaxPS4OrbisAnalyzer.inspect(url: invalid).contains("Rejet"))
        precondition(MaxPS4OrbisAnalyzer.inspect(url: invalid.appendingPathExtension("missing")).contains("interrompue"))
        print("PASS Swift Orbis analysis bridge: SCE PRX metadata, qualified NID and rejected inputs; no execution")
    }
}
