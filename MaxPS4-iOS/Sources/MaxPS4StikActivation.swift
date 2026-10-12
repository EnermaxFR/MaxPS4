import Foundation
import UIKit

@MainActor
enum MaxPS4StikActivation {
    static func begin() -> String {
#if MAXPS4_FEX_APP
        guard let installedURL = URL(string: "stikdebug://"), UIApplication.shared.canOpenURL(installedURL) else {
            return "StikDebug n’est pas disponible. Installer et configurer StikDebug, le pairing et le VPN selon ses instructions."
        }
        guard let bundle = Bundle.main.bundleIdentifier,
              let sourceURL = Bundle.main.url(forResource: "maxps4-stik", withExtension: "js") ??
                  Bundle.main.url(forResource: "maxps4-stik", withExtension: "js", subdirectory: "JIT"),
              let source = try? String(contentsOf: sourceURL, encoding: .utf8) else {
            return "Script MaxPS4 manquant : activation refusée."
        }
        var mailbox: UInt64 = 0, nonce: UInt64 = 0, page: UInt64 = 0
        let status = maxps4_jit_begin(&mailbox, &nonce, &page)
        guard status == 0 else {
            return "Demande JIT refusée (\(status)). Si la mémoire est prête, lancer le test ; si une ancienne session attend, terminer StikDebug ou redémarrer MaxPS4."
        }
        let prefix = "const MAXPS4_MAILBOX=0x\(String(mailbox, radix:16))n;const MAXPS4_NONCE=0x\(String(nonce, radix:16))n;const MAXPS4_SIZES=[16384,16777216,\(page)];\n"
        var request = URLComponents()
        request.scheme = "stikdebug"
        request.host = "enable-jit"
        // The one fixed custom adapter always requests region preparation.
        // We do not infer TXM absence or rely on a transient debugger flag.
        request.queryItems = [
            URLQueryItem(name: "bundle-id", value: bundle),
            URLQueryItem(name: "pid", value: String(getpid())),
            URLQueryItem(name: "script-data", value: Data((prefix + source).utf8).base64EncodedString())
        ]
        guard let url = request.url else { return "URL StikDebug invalide. Redémarrer MaxPS4 pour annuler la demande." }
        UIApplication.shared.open(url) // Acceptance is NOT a JIT success signal.
        return "StikDebug prépare trois régions RX pour ce processus. Revenir ici après le détachement, puis « Tester FEXCore ». Un debugger attaché seul ne suffit pas."
#else
        return "Cette configuration ne contient pas le fournisseur FEXCore."
#endif
    }
    static func release() -> String {
#if MAXPS4_FEX_APP
        let status = maxps4_jit_cancel()
        return status == 0 ? "Mémoire JIT libérée. Réactiver avec StikDebug avant un nouveau test JIT." :
            "Libération refusée (\(status)) : une session ou une exécution utilise encore la mémoire."
#else
        return "Fournisseur absent."
#endif
    }
}
