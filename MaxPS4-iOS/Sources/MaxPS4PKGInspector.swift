import Foundation

/// Bounded native inventory report. Does not export, decrypt or launch a game.
enum MaxPS4PKGInspector {
    enum ReportError: LocalizedError {
        case failed(Int32, String)
        var errorDescription: String? {
            switch self {
            case .failed(let code, let partial):
                return partial + "\nLecture du rapport PKG interrompue (code \(code))"
            }
        }
    }

    static func inspect(url: URL) throws -> String {
        // At most 4096 descriptors; bounded report storage independent of PKG size.
        var output = [CChar](repeating: 0, count: 2 * 1_048_576)
        let result = url.path.withCString { path in
            output.withUnsafeMutableBufferPointer { buffer in
                maxps4_pkg_inventory_report(path, buffer.baseAddress, buffer.count)
            }
        }
        let report = output.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
        guard result == 0 else { throw ReportError.failed(result, report) }
        return report
    }
}
