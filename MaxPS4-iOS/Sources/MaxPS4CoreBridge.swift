import Foundation

/// Boundary between the MaxPS4 application and the external PS4 emulation core.
/// Keep all core-specific calls here so the MaxPS4 UI remains fully independent.
enum MaxPS4CoreBridge {
    static var isLinked: Bool {
        // The GitHub build will replace this probe with calls to the exported
        // shadps4_ios API once the static library and headers are linked.
        false
    }
}
