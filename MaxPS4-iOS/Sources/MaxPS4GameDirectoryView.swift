import SwiftUI
import UniformTypeIdentifiers

struct MaxPS4GameDirectoryView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var importing = false
    @State private var directory: URL?
    @State private var busy = false
    @State private var inspectionCompleted = false
    @State private var report = "Sélectionnez un dossier contenant eboot.bin, les PRX et les données déjà accessibles par des moyens autorisés."
    @State private var launchReport = ""

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    Text("Précontrôle d’un dossier Orbis").font(.title2.bold())
                    Text("Lecture seule du dossier source. Aucun déchiffrement PKG/PFS, aucune clé ou licence utilisée.")
                        .font(.footnote).foregroundStyle(.secondary)
                    Button("Sélectionner le dossier accessible…") { importing = true }
                        .buttonStyle(.borderedProminent).disabled(busy)
                    if let directory {
                        Text(directory.lastPathComponent).font(.headline)
                        Button("Actualiser le précontrôle") { inspect(directory) }
                            .buttonStyle(.bordered).disabled(busy)
                        Button("Vérifier et tenter l’exécution bornée") { launch(directory) }
                            .buttonStyle(.bordered).disabled(busy || !inspectionCompleted)
                            .accessibilityIdentifier("maxps4.orbisDirectory.attempt")
                        Button("Tester le PRX traduit V4 (synthétique)") { launch(directory, translatedV4: true) }
                            .buttonStyle(.bordered).disabled(busy || !inspectionCompleted)
                            .accessibilityIdentifier("maxps4.orbisDirectory.v4PRX")
                        Text(inspectionCompleted
                             ? "Inventaire terminé ; admission CPU non établie. La tentative relit les fichiers et doit réussir la préparation native puis l’initialisation JIT avant toute exécution."
                             : "Tentative indisponible tant que l’inventaire n’est pas terminé.")
                            .font(.caption).foregroundStyle(.secondary)
                        Text("Le runtime exige maxps4.json avec un graphe PRX et des biais explicites. Les formats ou instructions non pris en charge sont refusés avant le dispatch. Un succès CPU ne prouve pas le démarrage du jeu.")
                            .font(.caption).foregroundStyle(.secondary)
                        Text("Le test V4 est distinct : un seul PRX autonome et un manifeste v4ABI=synthetic-hlt-v1 avec v4Entry explicite. TLS, initialisation de jeu et instructions hors du contrat sont refusés (-ENOTSUP), sans repli vers le lanceur existant.")
                            .font(.caption).foregroundStyle(.orange)
                    }
                    if busy { ProgressView("Diagnostic en cours…") }
                    ShareLink(item: report + (launchReport.isEmpty ? "" : "\n\n" + launchReport)) {
                        Label("Exporter le rapport texte", systemImage: "square.and.arrow.up")
                    }.disabled(busy)
                    Text(report).font(.system(.footnote, design: .monospaced)).textSelection(.enabled)
                    if !launchReport.isEmpty {
                        Text(launchReport).font(.system(.footnote, design: .monospaced)).textSelection(.enabled)
                    }
                }.padding().frame(maxWidth: .infinity, alignment: .leading)
            }
            .navigationTitle("Dossier de jeu")
            .toolbar { Button("Fermer") { dismiss() }.disabled(busy) }
        }
        .interactiveDismissDisabled(busy)
        .fileImporter(isPresented: $importing, allowedContentTypes: [.folder]) { result in
            switch result {
            case .success(let url):
                directory = url
                inspect(url)
            case .failure:
                report = "Sélection du dossier impossible ; aucune exécution."
                inspectionCompleted = false
            }
        }
    }

    private func inspect(_ url: URL) {
        busy = true
        inspectionCompleted = false
        launchReport = ""
        Task {
            let result = await Task.detached(priority: .utility) {
                let scoped = url.startAccessingSecurityScopedResource()
                defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                return MaxPS4OrbisAnalyzer.inspectDirectory(url: url)
            }.value
            report = result.text
            inspectionCompleted = result.inspectionCompleted
            busy = false
        }
    }

    private func launch(_ url: URL, translatedV4: Bool = false) {
        busy = true
        Task {
            let result = await Task.detached(priority: .utility) {
                let scoped = url.startAccessingSecurityScopedResource()
                defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                // Recheck before attempting the existing bounded runtime path.
                let preflight = MaxPS4OrbisAnalyzer.inspectDirectory(url: url)
                guard preflight.inspectionCompleted else { return preflight.text }
                return (translatedV4 ? "Tentative PRX traduit V4 — ABI synthétique explicite\n" : "Tentative Orbis bornée — fichiers du dossier sélectionné\n") +
                    MaxPS4FEXDiagnostic.executeOrbisDirectory(url: url, translatedV4: translatedV4)
            }.value
            launchReport = result
            busy = false
        }
    }
}
