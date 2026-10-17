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
                if code == -Int32(POSIXErrorCode.ENOEXEC.rawValue) {
                    return partial + "\nValidation de structure PKG refusée (ENOEXEC, code \(code))."
                }
                return partial + "\nLecture du rapport PKG interrompue (code \(code))"
            }
        }
    }

    static func inspect(url: URL, requireValidPackage: Bool = false) throws -> String {
        // At most 4096 descriptors; bounded report storage independent of PKG size.
        // Includes descriptor names and the separate executable-reference list.
        var output = [CChar](repeating: 0, count: 4 * 1_048_576)
        let result = url.path.withCString { path in
            output.withUnsafeMutableBufferPointer { buffer in
                requireValidPackage
                    ? maxps4_pkg_validate_import(path, buffer.baseAddress, buffer.count)
                    : maxps4_pkg_inventory_report(path, buffer.baseAddress, buffer.count)
            }
        }
        let report = output.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
        guard result == 0 else { throw ReportError.failed(result, report) }
        return report
    }
    struct ImportError: LocalizedError {
        let stage: String
        let underlying: Error
        let cleanup: String
        var errorDescription: String? {
            let native = underlying as NSError
            return "[\(stage)] domaine=\(native.domain) code=\(native.code)\n" +
                underlying.localizedDescription + cleanup
        }
    }

    /// Own the staging directory, publish without replacing existing files, then
    /// commit the library atomically. A failed commit removes only this new copy.
    static func copyForLibrary(url: URL, destination: URL, validatePackage: Bool,
                               commit: () throws -> Void) throws {
        let manager = FileManager.default
        var stage = "IMPORT.PKG_VALIDATE"
        var staging: URL?
        var published = false
        do {
            if validatePackage { _ = try inspect(url: url, requireValidPackage: true) }
            stage = "IMPORT.STAGING"
            let directory = destination.deletingLastPathComponent()
                .appendingPathComponent(".import-" + UUID().uuidString, isDirectory: true)
            try manager.createDirectory(at: directory, withIntermediateDirectories: false)
            staging = directory
            let payload = directory.appendingPathComponent("payload")
            stage = "IMPORT.COPY"
            try manager.copyItem(at: url, to: payload)
            stage = "IMPORT.COPY_VALIDATE"
            if validatePackage { _ = try inspect(url: payload, requireValidPackage: true) }
            stage = "IMPORT.PUBLISH_COPY"
            try manager.moveItem(at: payload, to: destination)
            published = true
            try manager.removeItem(at: directory)
            staging = nil
            stage = "IMPORT.LIBRARY_COMMIT"
            try commit()
        } catch {
            var cleanup = ""
            let owned = [published ? destination : nil, staging].compactMap { $0 }
            for path in owned {
                do { try manager.removeItem(at: path) }
                catch { cleanup += "\n[IMPORT.ROLLBACK] Copie à retirer : \(path.lastPathComponent) ; \(error.localizedDescription)" }
            }
            throw ImportError(stage: stage, underlying: error, cleanup: cleanup)
        }
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
