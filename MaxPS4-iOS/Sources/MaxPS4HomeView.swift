import SwiftUI
import UniformTypeIdentifiers

struct MaxPS4HomeView: View {
    @EnvironmentObject private var emulator: MaxPS4Emulator
    @State private var importingGame = false

    var body: some View {
        NavigationStack {
            VStack(spacing: 24) {
                Spacer()

                Image(systemName: "gamecontroller.fill")
                    .font(.system(size: 72))

                Text("MaxPS4")
                    .font(.largeTitle.bold())

                Text("PS4 emulator for iPhone")
                    .foregroundStyle(.secondary)

                Button {
                    importingGame = true
                } label: {
                    Label("Importer un jeu", systemImage: "plus.circle.fill")
                        .frame(maxWidth: .infinity)
                        .padding()
                }
                .buttonStyle(.borderedProminent)

                Text(emulator.status)
                    .font(.footnote)
                    .foregroundStyle(.secondary)

                Spacer()
            }
            .padding()
            .navigationTitle("MaxPS4")
            .fileImporter(
                isPresented: $importingGame,
                allowedContentTypes: [.data],
                allowsMultipleSelection: false
            ) { result in
                switch result {
                case .success(let urls):
                    guard let url = urls.first else { return }
                    emulator.importGame(from: url)
                case .failure(let error):
                    emulator.status = "Import impossible: \(error.localizedDescription)"
                }
            }
        }
    }
}
