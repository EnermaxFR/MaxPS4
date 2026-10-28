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
            case .success(let url): directory = url; importError = nil; MaxPS4GameFolders.shared.add(url)
            case .failure(let error): importError = "Sélection impossible : \(error.localizedDescription)"
            }
        }
        .sheet(isPresented: $showingJIT) { MaxPS4JITSetupView() }
        .fullScreenCover(isPresented: $playing) {
            if let directory { MaxPS4GameView(directory: directory) }
        }
    }
}

// Game folders chosen with "Jouer", kept in the library as bookmarks so they
// can be launched again without picking the folder each time.
@MainActor final class MaxPS4GameFolders: ObservableObject {
    struct Folder: Codable, Identifiable, Equatable {
        var id = UUID()
        var name: String
        var bookmark: Data
        var addedAt = Date()
    }
    static let shared = MaxPS4GameFolders()
    @Published private(set) var folders: [Folder] = []
    private let key = "maxps4.library.folders"

    private init() {
        if let data = UserDefaults.standard.data(forKey: key),
           let saved = try? JSONDecoder().decode([Folder].self, from: data) { folders = saved }
    }
    func add(_ url: URL) {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        guard let bookmark = try? url.bookmarkData() else { return }
        // The same folder chosen again moves to the front instead of repeating.
        let target = url.standardizedFileURL
        folders.removeAll { Self.location($0)?.standardizedFileURL == target }
        folders.insert(Folder(name: url.lastPathComponent, bookmark: bookmark), at: 0)
        save()
    }
    // The folder's current URL, or nil once it was moved or deleted.
    func resolve(_ folder: Folder) -> URL? {
        var stale = false
        guard let url = try? URL(resolvingBookmarkData: folder.bookmark, bookmarkDataIsStale: &stale) else { return nil }
        if stale, let index = folders.firstIndex(where: { $0.id == folder.id }) {
            let scoped = url.startAccessingSecurityScopedResource()
            if let fresh = try? url.bookmarkData() { folders[index].bookmark = fresh; save() }
            if scoped { url.stopAccessingSecurityScopedResource() }
        }
        return url
    }
    private static func location(_ folder: Folder) -> URL? {
        var stale = false
        return try? URL(resolvingBookmarkData: folder.bookmark, bookmarkDataIsStale: &stale)
    }
    func remove(_ folder: Folder) {
        folders.removeAll { $0.id == folder.id }
        save()
    }
    private func save() {
        if let data = try? JSONEncoder().encode(folders) { UserDefaults.standard.set(data, forKey: key) }
    }
}
