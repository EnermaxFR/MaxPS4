import Foundation

/// Bounded PKG/PFS inspection and selected plaintext reads; no decryption or launch.
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
    static func readPFSFile(url: URL, selected: String) throws -> Data {
        guard !selected.utf8.contains(0), !selected.isEmpty, selected.utf8.count <= 4096 else {
            throw ReportError.failed(-Int32(POSIXErrorCode.EINVAL.rawValue), "Sélection invalide")
        }
        var length = 0
        let query = url.path.withCString { path in
            selected.withCString { name in
                maxps4_pkg_pfs_read_file(path, name, nil, 0, &length)
            }
        }
        guard (query == 0 || query == -Int32(POSIXErrorCode.ENOSPC.rawValue)), length <= 64 * 1_048_576 else {
            throw ReportError.failed(query, "Extraction PFS refusée : consulter le diagnostic PKG/PFS. Aucun déchiffrement ; clés et licences exclues.")
        }
        var data = Data(count: length)
        let result = data.withUnsafeMutableBytes { buffer in
            url.path.withCString { path in
                selected.withCString { name in
                    maxps4_pkg_pfs_read_file(path, name, buffer.bindMemory(to: UInt8.self).baseAddress, buffer.count, &length)
                }
            }
        }
        guard result == 0, length == data.count else {
            throw ReportError.failed(result == 0 ? -Int32(POSIXErrorCode.EIO.rawValue) : result,
                                     "Lecture PFS interrompue ou taille modifiée ; sélectionner une source stable puis réessayer.")
        }
        return data.prefix(length)
    }


    static func artwork(url: URL, entryID: UInt32) -> Data? {
        var length = 0
        let query = url.path.withCString {
            maxps4_pkg_read_artwork($0, entryID, nil, 0, &length)
        }
        guard query == -Int32(POSIXErrorCode.ENOSPC.rawValue),
              length >= 24, length <= 1_048_576 else { return nil }
        var bytes = Data(count: length)
        let result = bytes.withUnsafeMutableBytes { buffer in
            url.path.withCString {
                maxps4_pkg_read_artwork($0, entryID,
                    buffer.bindMemory(to: UInt8.self).baseAddress, buffer.count, &length)
            }
        }
        guard result == 0, length == bytes.count else { return nil }
        return bytes
    }

}
