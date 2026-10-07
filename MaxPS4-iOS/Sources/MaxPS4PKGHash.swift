import Foundation
import CryptoKit

/// Read-only, streaming file fingerprint. This does not authenticate PS4 content.
enum MaxPS4PKGHash {
    static func sha256(url: URL) async -> String {
        await Task.detached(priority: .utility) {
            do {
                let handle = try FileHandle(forReadingFrom: url)
                defer { try? handle.close() }
                var hasher = SHA256()
                var total: UInt64 = 0
                while true {
                    try Task.checkCancellation()
                    guard let chunk = try handle.read(upToCount: 1024 * 1024),
                          !chunk.isEmpty else { break }
                    total += UInt64(chunk.count)
                    hasher.update(data: chunk)
                }
                let fingerprint = hasher.finalize().map { String(format: "%02x", $0) }.joined()
                return "SHA-256 du fichier entier (" + String(total) + " octets) :\n" + fingerprint +
                    "\nEmpreinte calculée localement ; elle ne garantit pas l’authenticité du PKG."
            } catch {
                return "Calcul SHA-256 impossible : " + error.localizedDescription
            }
        }.value
    }
}
