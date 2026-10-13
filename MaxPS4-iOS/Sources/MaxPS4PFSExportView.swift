import SwiftUI
import UniformTypeIdentifiers

private struct MaxPS4PFSFileDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.data] }
    var data: Data
    init(data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws {
        guard let bytes = configuration.file.regularFileContents else { throw CocoaError(.fileReadCorruptFile) }
        data = bytes
    }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: data)
    }
}

struct MaxPS4PFSExportView: View {
    let source: URL
    @State private var selected = "eboot.bin"
    @State private var busy = false
    @State private var exporting = false
    @State private var document: MaxPS4PFSFileDocument?
    @State private var message = "Choisissez un chemin exact figurant dans le rapport PFS uroot. Seul le PFS non chiffré D32 contigu est pris en charge."

    var body: some View {
        Form {
            Text(message).textSelection(.enabled)
            TextField("Chemin relatif dans uroot", text: $selected)
                .textInputAutocapitalization(.never).autocorrectionDisabled().disabled(busy)
            Button("Lire et exporter le fichier sélectionné") { readSelection() }
                .disabled(busy || selected.isEmpty)
            if busy { ProgressView("Lecture du fichier…") }
            Text("64 Mio maximum. Aucun lancement, déchiffrement ou export de clés/licences. La destination est choisie dans Fichiers ; les chemins du PFS ne créent aucun dossier automatiquement.")
                .font(.footnote)
            Text("Pour le précontrôle, regroupez les fichiers accessibles dans un dossier avec eboot.bin à la racine et les PRX dans leurs sous-dossiers, puis utilisez Import → Dossier de jeu accessible. Un fichier exporté ne prouve pas que le jeu démarre.")
                .font(.footnote)
        }
        .navigationTitle("Fichier PFS")
        .fileExporter(isPresented: $exporting, document: document, contentType: .data,
                      defaultFilename: String(selected.split(separator: "/").last ?? "fichier.bin")) { result in
            switch result {
            case .success: message = "Fichier sélectionné exporté. Source PKG inchangée ; aucune exécution."
            case .failure: message = "Export annulé ou impossible."
            }
            document = nil
        }
    }

    private func readSelection() {
        busy = true
        let path = selected
        Task {
            let result = await Task.detached(priority: .utility) {
                let scoped = source.startAccessingSecurityScopedResource()
                defer { if scoped { source.stopAccessingSecurityScopedResource() } }
                return Result { try MaxPS4PKGInspector.readPFSFile(url: source, selected: path) }
            }.value
            switch result {
            case .success(let data):
                document = MaxPS4PFSFileDocument(data: data)
                exporting = true
            case .failure(let error): message = error.localizedDescription
            }
            busy = false
        }
    }
}
