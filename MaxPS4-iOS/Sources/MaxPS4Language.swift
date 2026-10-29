import SwiftUI

// The language chosen in Réglages ("" follows the iPhone). Text views follow
// it through the root view's locale; runtime messages look it up in
// Bundle.maxps4, so a change applies at once without relaunching.
enum MaxPS4Language {
    static let key = "maxps4.language"
    static let choices: [(code: String, name: String)] = [
        ("fr", "Français"), ("en", "English"), ("es", "Español"),
        ("de", "Deutsch"), ("it", "Italiano"), ("pt-BR", "Português (Brasil)"),
    ]
    static var current: String { UserDefaults.standard.string(forKey: key) ?? "" }
    // The PS4 system language games are told (sceSystemServiceParamGetInt):
    // the app's language, else the iPhone's; English (US) for any other.
    static var ps4LanguageID: Int32 {
        let code = (current.isEmpty ? Locale.preferredLanguages.first ?? "en" : current).lowercased()
        if code.hasPrefix("pt") { return code.contains("br") ? 17 : 7 }
        if code.hasPrefix("en") { return code.contains("gb") ? 18 : 1 }
        if code.hasPrefix("zh") { return code.contains("hant") || code.contains("tw") || code.contains("hk") ? 10 : 11 }
        let table: [String: Int32] = ["ja": 0, "fr": 2, "es": 3, "de": 4, "it": 5, "nl": 6, "ru": 8, "ko": 9,
                                      "fi": 12, "sv": 13, "da": 14, "nb": 15, "no": 15, "pl": 16, "tr": 19]
        return table[String(code.prefix(2))] ?? 1
    }
    // The locale for the root view: the chosen language, or the iPhone's.
    static func locale(for code: String) -> Locale { code.isEmpty ? .autoupdatingCurrent : Locale(identifier: code) }
    // Also what iOS itself uses for this app (file pickers, share sheets)
    // from its next launch.
    static func apply(_ code: String) {
        if code.isEmpty { UserDefaults.standard.removeObject(forKey: "AppleLanguages") }
        else { UserDefaults.standard.set([code], forKey: "AppleLanguages") }
    }
}

extension Bundle {
    static var maxps4: Bundle {
        let code = MaxPS4Language.current
        guard !code.isEmpty, let path = Bundle.main.path(forResource: code, ofType: "lproj"),
              let bundle = Bundle(path: path) else { return .main }
        return bundle
    }
}
