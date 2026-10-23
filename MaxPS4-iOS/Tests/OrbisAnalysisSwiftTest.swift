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
        try FileManager.default.copyItem(at: url, to: directory.appendingPathComponent("eboot.bin"))
        let inventory = MaxPS4OrbisAnalyzer.inspectDirectory(url: directory)
        precondition(inventory.inspectionCompleted && inventory.text.contains("NID=aI+OeCz8xrQ"))
        precondition(inventory.text == MaxPS4OrbisAnalyzer.inspectDirectory(url: directory).text)
        precondition(inventory.text.contains("aucune exécution"))
        precondition(!MaxPS4OrbisAnalyzer.inspectDirectory(url: invalid).inspectionCompleted)
        // System modules: only top-level .prx/.sprx files are copied, then
        // inventoried; removal leaves an empty inventory.
        let modules = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: modules) }
        precondition(MaxPS4OrbisAnalyzer.systemModulesReport(directory: modules).contains("Aucun module"))
        try FileManager.default.copyItem(at: url, to: directory.appendingPathComponent("libSynthetic.sprx"))
        try Data([0]).write(to: directory.appendingPathComponent("notes.txt"))
        let copied1 = try MaxPS4OrbisAnalyzer.importSystemModules(from: directory, to: modules)
        precondition(copied1 == 1)
        let modulesReport = MaxPS4OrbisAnalyzer.systemModulesReport(directory: modules)
        precondition(modulesReport.contains("en clair : 1") && modulesReport.contains("libSynthetic.sprx"))
        let copied2 = try MaxPS4OrbisAnalyzer.importSystemModules(from: directory, to: modules)
        precondition(copied2 == 1)
        try MaxPS4OrbisAnalyzer.removeSystemModules(directory: modules)
        precondition(MaxPS4OrbisAnalyzer.systemModulesReport(directory: modules).contains("Aucun module"))
        print("PASS Swift Orbis analysis bridge: SCE PRX metadata, qualified NID and rejected inputs; no execution")
    }
}
