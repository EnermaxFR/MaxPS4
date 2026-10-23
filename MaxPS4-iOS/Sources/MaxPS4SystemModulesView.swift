import SwiftUI

/// PS4 system modules (firmware libraries such as libc.prx) that the user
/// copied, decrypted, from their own console. MaxPS4 ships and decrypts none.
struct MaxPS4SystemModulesView: View {
    @State private var importing = false
    @State private var working = false
    @State private var message: String?
    @State private var report = ""

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("Les jeux PS4 s’appuient sur les bibliothèques du firmware (libc, libSceFios2…). Importez le dossier des modules système copiés, déchiffrés, depuis votre propre console : MaxPS4 ne fournit ni ne déchiffre aucun fichier de Sony.")
                    .font(.footnote)
                Button("Importer un dossier de modules…") { importing = true }
                    .buttonStyle(.borderedProminent)
                    .disabled(working)
                    .accessibilityIdentifier("maxps4-system-modules-import")
                Button("Supprimer les modules importés", role: .destructive) {
                    run {
                        do {
                            try MaxPS4OrbisAnalyzer.removeSystemModules()
                            return "Modules supprimés de l’appareil."
                        } catch {
                            return "Suppression impossible : \(error.localizedDescription)"
                        }
                    }
                }
                .disabled(working)
                if working { ProgressView() }
                if let message { Text(message).font(.footnote) }
                Text(report).font(.system(.footnote, design: .monospaced))
                    .textSelection(.enabled)
                if !report.isEmpty {
                    ShareLink(item: report) { Label("Partager l’inventaire", systemImage: "square.and.arrow.up") }
                }
            }
            .padding()
        }
        .navigationTitle("Modules système PS4")
        .onAppear { refresh() }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.folder]) { selection in
            switch selection {
            case .success(let folder):
                run {
                    let scoped = folder.startAccessingSecurityScopedResource()
                    defer { if scoped { folder.stopAccessingSecurityScopedResource() } }
                    do {
                        let copied = try MaxPS4OrbisAnalyzer.importSystemModules(from: folder)
                        return copied == 0 ? "Aucun fichier .prx ou .sprx dans ce dossier." :
                            "\(copied) fichier(s) importé(s)."
                    } catch {
                        return "Import impossible : \(error.localizedDescription)"
                    }
                }
            case .failure(let error):
                message = error.localizedDescription
            }
        }
    }

    /// Runs file work off the main thread, then refreshes the inventory.
    private func run(_ work: @escaping @Sendable () -> String) {
        working = true
        Task {
            let result = await Task.detached(priority: .userInitiated) { work() }.value
            message = result
            refresh()
            working = false
        }
    }

    private func refresh() {
        Task {
            report = await Task.detached(priority: .userInitiated) {
                MaxPS4OrbisAnalyzer.systemModulesReport()
            }.value
        }
    }
}
