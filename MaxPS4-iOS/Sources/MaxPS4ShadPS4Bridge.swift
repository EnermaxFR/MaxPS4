import Foundation

/// Capabilities required before a native shadPS4 backend may advertise game launch.
/// This is a compatibility contract, not a bundled shadPS4 port.
struct MaxPS4NativeCapabilities: Equatable {
    var ps4ExecutableLoader: Bool = false
    var x8664GuestExecution: Bool = false
    var ps4SystemLibraries: Bool = false
    var graphicsBackend: Bool = false

    var canLaunchPS4Game: Bool {
        ps4ExecutableLoader && x8664GuestExecution &&
        ps4SystemLibraries && graphicsBackend
    }

    var diagnostic: String {
        let requirements: [(String, Bool)] = [
            ("Chargeur SELF/ELF PS4", ps4ExecutableLoader),
            ("Exécution x86-64 sur ARM64", x8664GuestExecution),
            ("Services système PS4", ps4SystemLibraries),
            ("Rendu graphique iOS", graphicsBackend)
        ]
        return requirements.map { "\($0.1 ? "✓" : "✗") \($0.0)" }
            .joined(separator: "\n")
    }
}

/// A future native port must implement this interface to connect to MaxPS4.
/// No placeholder implementation claims to execute PS4 code.
@MainActor
protocol MaxPS4ShadPS4Backend: MaxPS4NativeEngine {
    var capabilities: MaxPS4NativeCapabilities { get }
}
