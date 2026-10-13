import Foundation

/// Bounded native inventory report. Does not export, decrypt or launch a game.
enum MaxPS4PKGInspector {
    enum ReportError: LocalizedError {
        case failed(Int32, String)
        var errorDescription: String? {
            switch self {
            case .failed(let code, let partial):
                if code == -Int32(POSIXErrorCode.ENOSPC.rawValue) {
                    return partial + "\nRapport tronqué : limite de stockage de 4 Mio atteinte."
                }
                return partial + "\nLecture du rapport PKG interrompue (code \(code))"
            }
        }
    }

    static func inspect(url: URL) throws -> String {
        // At most 4096 descriptors; bounded report storage independent of PKG size.
        // Includes descriptor names and the separate executable-reference list.
        var output = [CChar](repeating: 0, count: 4 * 1_048_576)
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
