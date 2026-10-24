import Foundation
import UIKit

@MainActor
enum MaxPS4StikActivation {
    static func begin(completion: @escaping (String) -> Void) -> String {
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
            return "Demande JIT refusée (\(status)). Vérifie l’activation ; si une ancienne session attend, termine StikDebug ou redémarre MaxPS4."
        }
        guard let url = MaxPS4StikRequest.url(bundleID: bundle, pid: getpid(),
            mailbox: mailbox, nonce: nonce, pageSize: page, script: source) else {
            _ = maxps4_jit_launch_failed(nonce)
            return "URL StikDebug invalide : JIT indisponible."
        }
        UIApplication.shared.open(url, options: [:]) { opened in
            if !opened { _ = maxps4_jit_launch_failed(nonce) }
            completion(opened ? "Termine l’activation dans StikDebug, puis reviens dans MaxPS4." :
                "Ouverture de StikDebug refusée ; JIT indisponible. Réessayer l’activation.")
        }
        return "Termine l’activation dans StikDebug, puis reviens dans MaxPS4."
#else
        return "Cette configuration ne contient pas le fournisseur FEXCore."
#endif
    }
    static func confirmation() -> String {
#if MAXPS4_FEX_APP
        switch maxps4_jit_confirmation() {
        case 1: return "Activation confirmée. Vérification du JIT…"
        case 2: return "StikDebug a signalé un échec. Consulte les détails de l’activation."
        default: return "Aucune confirmation StikDebug pour cette demande. JIT indisponible. Vérifier la fin du script, le pairing, le VPN et la signature get-task-allow dans StikDebug. Si le script est ignoré, son option officielle de détection des scripts peut être nécessaire."
        }
#else
        return "Fournisseur absent."
#endif
    }
    static func release() -> String {
#if MAXPS4_FEX_APP
        let status = maxps4_jit_cancel()
        return status == 0 ? "Mémoire JIT libérée. Tu peux relancer l’activation." :
            "Libération refusée (\(status)) : une session ou une exécution utilise encore la mémoire."
#else
        return "Fournisseur absent."
#endif
    }
}
