import SwiftUI
import UniformTypeIdentifiers

struct MaxPS4GameDirectoryView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var importing = false
    @State private var playing = false
    @State private var showingJIT = false
    @State private var directory: URL?
    @State private var importError: String?

    var body: some View {
        NavigationStack {
            ZStack {
                MaxPS4Background()
                VStack(alignment: .leading, spacing: 24) {
                    Image(systemName: "gamecontroller.fill").font(.system(size: 44)).foregroundStyle(.cyan)
                    Text("À toi de jouer").font(.system(size: 32, weight: .black, design: .rounded))
                    Text("Choisis le dossier complet du jeu, avec eboot.bin, sce_module et ses données.")
                        .foregroundStyle(.white.opacity(0.65))
                    if let directory {
                        Label(directory.lastPathComponent, systemImage: "folder.fill")
                            .font(.headline).foregroundStyle(.cyan)
                            .padding(18).frame(maxWidth: .infinity, alignment: .leading)
                            .background(.white.opacity(0.06), in: RoundedRectangle(cornerRadius: 20))
                        Button { playing = true } label: {
                            Label("Continuer", systemImage: "play.fill").frame(maxWidth: .infinity).padding(.vertical, 8)
                        }.buttonStyle(.borderedProminent).tint(.blue)
                    }
                    Button(directory == nil ? "Choisir le dossier du jeu" : "Changer de dossier") { importing = true }
                        .buttonStyle(.bordered).tint(.cyan)
                    if let importError { Text(importError).font(.footnote).foregroundStyle(.orange) }
                    Spacer(minLength: 20)
                    Button { showingJIT = true } label: { Label("Activer le JIT avec StikDebug", systemImage: "bolt.fill") }
                        .font(.footnote).tint(.cyan)
                }.padding(24).frame(maxWidth: .infinity, alignment: .leading)
            }
            .navigationTitle("Jouer").navigationBarTitleDisplayMode(.inline)
            .toolbar { Button("Fermer") { dismiss() } }
        }
        .preferredColorScheme(.dark)
        .fileImporter(isPresented: $importing, allowedContentTypes: [.folder]) { result in
            switch result {
            case .success(let url): directory = url; importError = nil
            case .failure(let error): importError = "Sélection impossible : \(error.localizedDescription)"
            }
        }
        .sheet(isPresented: $showingJIT) { MaxPS4JITSetupView() }
        .fullScreenCover(isPresented: $playing) {
            if let directory { MaxPS4GameView(directory: directory) }
        }
    }
}
