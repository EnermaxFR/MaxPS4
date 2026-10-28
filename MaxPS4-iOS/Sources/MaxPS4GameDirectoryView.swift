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
// can be launched again without picking the folder each time, and game
// folders put in MaxPS4's own folder with the Files app, found by scanning.
@MainActor final class MaxPS4GameFolders: ObservableObject {
    struct Folder: Codable, Identifiable, Equatable {
        var id = UUID()
        var name: String
        var bookmark: Data
        var addedAt = Date()
    }
    // A folder with an eboot.bin under Documents ("Sur mon iPhone › MaxPS4").
    struct LocalGame: Identifiable, Equatable {
        var id: String { url.path }
        let url: URL
        let name: String
        let icon: URL?
    }
    static let shared = MaxPS4GameFolders()
    @Published private(set) var folders: [Folder] = []
    @Published private(set) var local: [LocalGame] = []
    private let key = "maxps4.library.folders"
    private var watchers: [DispatchSourceFileSystemObject] = []
    private var rescanPending = false

    private init() {
        if let data = UserDefaults.standard.data(forKey: key),
           let saved = try? JSONDecoder().decode([Folder].self, from: data) { folders = saved }
        scanDocuments()
    }

    // Looks up to three levels below Documents (its "Jeux" folder is made for
    // this) and watches the folders it walked, so a game copied in with the
    // Files app shows up without restarting MaxPS4.
    func scanDocuments() {
        let manager = FileManager.default
        let documents = manager.urls(for: .documentDirectory, in: .userDomainMask)[0]
        try? manager.createDirectory(at: documents.appendingPathComponent("Jeux", isDirectory: true),
                                     withIntermediateDirectories: true)
        var games: [LocalGame] = [], walked: [URL] = []
        func visit(_ folder: URL, depth: Int) {
            walked.append(folder)
            let children = (try? manager.contentsOfDirectory(at: folder, includingPropertiesForKeys: [.isDirectoryKey],
                                                             options: [.skipsHiddenFiles])) ?? []
            for child in children.sorted(by: { $0.lastPathComponent < $1.lastPathComponent })
            where (try? child.resourceValues(forKeys: [.isDirectoryKey]))?.isDirectory == true
                && child.lastPathComponent != "MaxPS4Games" {
                if manager.fileExists(atPath: child.appendingPathComponent("eboot.bin").path) {
                    let system = child.appendingPathComponent("sce_sys", isDirectory: true)
                    let icon = system.appendingPathComponent("icon0.png")
                    games.append(LocalGame(url: child,
                        name: Self.title(system.appendingPathComponent("param.sfo")) ?? child.lastPathComponent,
                        icon: manager.fileExists(atPath: icon.path) ? icon : nil))
                } else if depth < 3 { visit(child, depth: depth + 1) }
            }
        }
        visit(documents, depth: 1)
        if games != local { local = games }
        watch(walked.prefix(64))
    }
    private func watch(_ folders: ArraySlice<URL>) {
        watchers.forEach { $0.cancel() }
        watchers = folders.compactMap { folder in
            let descriptor = open(folder.path, O_EVTONLY)
            guard descriptor >= 0 else { return nil }
            let source = DispatchSource.makeFileSystemObjectSource(fileDescriptor: descriptor,
                                                                   eventMask: [.write, .rename, .delete], queue: .main)
            source.setEventHandler { MainActor.assumeIsolated { MaxPS4GameFolders.shared.scheduleRescan() } }
            source.setCancelHandler { close(descriptor) }
            source.resume()
            return source
        }
    }
    // A copy writes many entries: rescan once it settles.
    private func scheduleRescan() {
        guard !rescanPending else { return }
        rescanPending = true
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
            MainActor.assumeIsolated {
                MaxPS4GameFolders.shared.rescanPending = false
                MaxPS4GameFolders.shared.scanDocuments()
            }
        }
    }
    // TITLE from a PS4 param.sfo (PSF: key and data tables, 16-byte entries).
    private static func title(_ url: URL) -> String? {
        guard let data = try? Data(contentsOf: url), data.count >= 20,
              data.prefix(4) == Data([0, 0x50, 0x53, 0x46]) else { return nil }
        let bytes = [UInt8](data)
        func u32(_ at: Int) -> Int { at + 4 <= bytes.count ? Int(bytes[at]) | Int(bytes[at+1]) << 8 | Int(bytes[at+2]) << 16 | Int(bytes[at+3]) << 24 : 0 }
        func u16(_ at: Int) -> Int { at + 2 <= bytes.count ? Int(bytes[at]) | Int(bytes[at+1]) << 8 : 0 }
        func string(_ at: Int, _ limit: Int) -> String? {
            guard at >= 0, at < bytes.count else { return nil }
            let end = bytes[at..<min(bytes.count, at + limit)].firstIndex(of: 0) ?? min(bytes.count, at + limit)
            return String(bytes: bytes[at..<end], encoding: .utf8)
        }
        let keys = u32(8), values = u32(12), count = min(u32(16), 256)
        for index in 0..<count {
            let entry = 20 + index * 16
            guard string(keys + u16(entry), 64) == "TITLE" else { continue }
            let title = string(values + u32(entry + 12), u32(entry + 4))?.trimmingCharacters(in: .whitespacesAndNewlines)
            return title?.isEmpty == false ? title : nil
        }
        return nil
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
