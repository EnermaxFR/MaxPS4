import SwiftUI
import UIKit

// A game folder's sheet: its artwork and param.sfo identity, where its saves
// are, and a Jouer button.
struct MaxPS4GameDetailsView: View {
    let url: URL
    let play: () -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var info: [(label: LocalizedStringKey, value: String)] = []
    @State private var artwork: UIImage?
    @State private var title = ""
    @State private var saves: URL?
    @State private var savesSize: Int64 = 0
    @State private var update: (url: URL, version: String?)?
    @State private var useUpdate = true

    var body: some View {
        NavigationStack {
            ZStack {
                MaxPS4Background()
                ScrollView {
                    VStack(alignment: .leading, spacing: 18) {
                        if let artwork {
                            Image(uiImage: artwork).resizable().scaledToFill()
                                .frame(maxWidth: .infinity).frame(height: 200).clipped()
                                .clipShape(RoundedRectangle(cornerRadius: 20))
                        }
                        Text(verbatim: title).font(.title2.bold())
                        Button {
                            dismiss(); play()
                        } label: {
                            Label("Jouer", systemImage: "play.fill").frame(maxWidth: .infinity).padding(.vertical, 6)
                        }.buttonStyle(.borderedProminent).tint(.cyan)
                        VStack(alignment: .leading, spacing: 8) {
                            ForEach(info.indices, id: \.self) { index in
                                HStack(alignment: .firstTextBaseline) {
                                    Text(info[index].label).foregroundStyle(.secondary)
                                    Spacer()
                                    Text(verbatim: info[index].value).multilineTextAlignment(.trailing).textSelection(.enabled)
                                }.font(.subheadline)
                            }
                        }
                        .padding(14).frame(maxWidth: .infinity, alignment: .leading)
                        .background(.white.opacity(0.06), in: RoundedRectangle(cornerRadius: 14))
                        if let update {
                            VStack(alignment: .leading, spacing: 8) {
                                Label("Mise à jour", systemImage: "arrow.down.circle").font(.headline)
                                Text(verbatim: [update.version.map { "v" + $0 }, update.url.lastPathComponent].compactMap { $0 }.joined(separator: " · "))
                                    .font(.subheadline).foregroundStyle(.secondary)
                                Toggle("Utiliser la mise à jour", isOn: $useUpdate).tint(.cyan)
                                    .onChange(of: useUpdate) { _, on in
                                        UserDefaults.standard.set(on, forKey: "maxps4.update." + MaxPS4Saves.gameID(for: url))
                                    }
                            }
                            .padding(14).frame(maxWidth: .infinity, alignment: .leading)
                            .background(.white.opacity(0.06), in: RoundedRectangle(cornerRadius: 14))
                        }
                        VStack(alignment: .leading, spacing: 8) {
                            Label("Sauvegardes", systemImage: "externaldrive").font(.headline)
                            if savesSize > 0 {
                                Text(verbatim: ByteCountFormatter.string(fromByteCount: savesSize, countStyle: .file))
                                    .font(.subheadline).foregroundStyle(.secondary)
                            } else {
                                Text("Aucune sauvegarde").font(.subheadline).foregroundStyle(.secondary)
                            }
                            // Files opens a folder of an app's Documents through shareddocuments://.
                            if let saves, let link = URL(string: "shareddocuments://" + saves.path) {
                                Button("Voir dans Fichiers") { UIApplication.shared.open(link) }.tint(.cyan)
                            }
                        }
                        .padding(14).frame(maxWidth: .infinity, alignment: .leading)
                        .background(.white.opacity(0.06), in: RoundedRectangle(cornerRadius: 14))
                    }.padding(20)
                }
            }
            .navigationTitle("Fiche du jeu").navigationBarTitleDisplayMode(.inline)
            .toolbar { Button("Fermer") { dismiss() } }
        }
        .preferredColorScheme(.dark)
        .task(id: url) { load() }
    }

    private func load() {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        let system = url.appendingPathComponent("sce_sys", isDirectory: true)
        let files = (try? FileManager.default.contentsOfDirectory(at: system, includingPropertiesForKeys: nil)) ?? []
        func named(_ name: String) -> URL? { files.first { $0.lastPathComponent.lowercased() == name } }
        let sfo = named("param.sfo")
        func field(_ key: String) -> String? { sfo.flatMap { MaxPS4GameFolders.sfoString($0, key: key) } }
        title = field("TITLE") ?? url.lastPathComponent
        artwork = [named("pic1.png"), named("icon0.png")].compactMap { $0 }.lazy
            .compactMap { UIImage(contentsOfFile: $0.path) }.first
        var rows: [(label: LocalizedStringKey, value: String)] = []
        if let id = field("TITLE_ID") { rows.append(("Identifiant", id)) }
        if let version = field("APP_VER") { rows.append(("Version de l’application", version)) }
        if let version = field("VERSION") { rows.append(("Version du contenu", version)) }
        if let content = field("CONTENT_ID") { rows.append(("Content ID", content)) }
        rows.append(("Dossier", url.lastPathComponent))
        info = rows
        update = MaxPS4GameFolders.shared.update(for: url)
        useUpdate = MaxPS4GameFolders.useUpdate(for: url)
        saves = MaxPS4Saves.root(for: url)
        savesSize = saves.map { Self.size(of: $0) } ?? 0
    }

    private static func size(of folder: URL) -> Int64 {
        let keys: Set<URLResourceKey> = [.fileSizeKey, .isRegularFileKey]
        guard let files = FileManager.default.enumerator(at: folder, includingPropertiesForKeys: Array(keys)) else { return 0 }
        var total: Int64 = 0
        for case let file as URL in files {
            if let values = try? file.resourceValues(forKeys: keys), values.isRegularFile == true { total += Int64(values.fileSize ?? 0) }
        }
        return total
    }
}
