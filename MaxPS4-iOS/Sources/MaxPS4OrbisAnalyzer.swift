import Foundation

/// Inventory and transient native preparation of one standalone plaintext file.
/// Missing external PRX remain missing. Does not enter FEX or allocate JIT memory.
enum MaxPS4OrbisAnalyzer {
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
}
