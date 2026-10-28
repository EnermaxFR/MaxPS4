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
