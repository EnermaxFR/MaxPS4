import Foundation

/// Inventory and transient native preparation of one standalone plaintext file.
/// Missing external PRX remain missing. Does not enter FEX or allocate JIT memory.
enum MaxPS4OrbisAnalyzer {
    struct DirectoryReport: Sendable {
        let inspectionCompleted: Bool
        let text: String
    }

    static func inspectDirectory(url: URL) -> DirectoryReport {
        var output = [CChar](repeating: 0, count: 2 * 1_048_576)
        let status = url.path.withCString { path in
            output.withUnsafeMutableBufferPointer { buffer in
                maxps4_game_directory_report(path, buffer.baseAddress, buffer.count)
            }
        }
        let report = output.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
        return DirectoryReport(inspectionCompleted: status == 0,
            text: report + (status == 0 ? "" : "\nInspection interrompue (code \(status))"))
    }

    static func inspect(url: URL) -> String {
        var output = [CChar](repeating: 0, count: 2 * 1_048_576)
        let status = url.path.withCString { path in
            output.withUnsafeMutableBufferPointer { buffer in
                maxps4_orbis_analysis_report(path, buffer.baseAddress, buffer.count)
            }
        }
        let report = output.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
        return status == 0 ? report : report + "\nAnalyse interrompue (code \(status))"
    }

    /// PS4 system modules the user copied, decrypted, from their own console.
    /// Kept in Application Support, excluded from backups; never shipped.
    static var systemModulesDirectory: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("SystemModules", isDirectory: true)
    }

    /// Copies the top-level .prx/.sprx regular files of `folder` (symlinks
    /// skipped) into `destination`, replacing files of the same name.
    /// Returns how many were copied.
    static func importSystemModules(from folder: URL, to destination: URL = systemModulesDirectory) throws -> Int {
        let manager = FileManager.default
        try manager.createDirectory(at: destination, withIntermediateDirectories: true)
        var excluded = URLResourceValues()
        excluded.isExcludedFromBackup = true
        var target = destination
        try? target.setResourceValues(excluded)
        let keys: [URLResourceKey] = [.isRegularFileKey, .isSymbolicLinkKey]
        var copied = 0
        for item in try manager.contentsOfDirectory(at: folder, includingPropertiesForKeys: keys, options: [.skipsHiddenFiles]) {
            let suffix = item.pathExtension.lowercased()
            guard suffix == "prx" || suffix == "sprx" else { continue }
            let values = try item.resourceValues(forKeys: Set(keys))
            guard values.isRegularFile == true, values.isSymbolicLink != true else { continue }
            let copy = destination.appendingPathComponent(item.lastPathComponent)
            if manager.fileExists(atPath: copy.path) { try manager.removeItem(at: copy) }
            try manager.copyItem(at: item, to: copy)
            copied += 1
        }
        return copied
    }

    static func systemModulesReport(directory: URL = systemModulesDirectory) -> String {
        var output = [CChar](repeating: 0, count: 1_048_576)
        let status = directory.path.withCString { path in
            output.withUnsafeMutableBufferPointer { buffer in
                maxps4_system_modules_report(path, buffer.baseAddress, buffer.count)
            }
        }
        let report = output.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
        return status == 0 ? report : report + "\nInventaire interrompu (code \(status))"
    }

    static func removeSystemModules(directory: URL = systemModulesDirectory) throws {
        if FileManager.default.fileExists(atPath: directory.path) {
            try FileManager.default.removeItem(at: directory)
        }
    }
}
