import SwiftUI

@main
struct MaxPS4App: App {
    @StateObject private var emulator = MaxPS4Emulator()

    var body: some Scene {
        WindowGroup {
            MaxPS4HomeView()
                .environmentObject(emulator)
        }
    }
}
