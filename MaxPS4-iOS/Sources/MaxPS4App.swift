import SwiftUI

@main
struct MaxPS4App: App {
    @StateObject private var emulator = MaxPS4Emulator()
    @AppStorage(MaxPS4Language.key) private var language = ""

    var body: some Scene {
        WindowGroup {
            MaxPS4HomeView()
                .environmentObject(emulator)
                .environment(\.locale, MaxPS4Language.locale(for: language))
        }
    }
}
