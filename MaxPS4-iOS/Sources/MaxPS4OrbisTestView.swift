import SwiftUI
import UniformTypeIdentifiers

struct MaxPS4OrbisTestView: View {
    @State private var importing = false
    @State private var busy = false
    @State private var inspection: MaxPS4OrbisTest.Inspection?
    @State private var execution = "execution=not_attempted"
    @State private var error: String?
    private let queue = DispatchQueue(label: "maxps4.orbis-test", qos: .userInitiated)

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    Text("ELF Orbis autorisé et non chiffré. Validation et admission CPU obligatoires avant toute exécution.")
                    Button("Importer un ELF autorisé") { importing = true }.disabled(busy)
                    Button("Charger la fixture légale (résultat attendu : 42)") {
                        inspect(MaxPS4OrbisTest.fixture())
                    }.disabled(busy)
                    NavigationLink("Diagnostic FEXCore / activation StikDebug") { MaxPS4FEXDiagnosticView() }
                        .accessibilityIdentifier("maxps4.testFEXCore")
                    Button("Préparer FEX/JIT et exécuter l’ELF validé") {
                        guard let selected = inspection, selected.ready else { return }
                        busy = true
                        queue.async {
                            let result = MaxPS4OrbisTest.run(selected)
                            DispatchQueue.main.async { execution = result; busy = false }
                        }
                    }.disabled(busy || inspection?.ready != true)
                    if busy { ProgressView("Vérification en cours…") }
                    if let error { Text(error).foregroundStyle(.orange) }
                    if let inspection {
                        Text(inspection.report).font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                        Text(execution).font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                        ShareLink("Exporter le diagnostic sans chemins ni contenu ELF",
                                  item: inspection.export + "\n" + execution)
                    }
                    Text("Un test FEX synthétique, un chargement ELF et une exécution Orbis sont des preuves distinctes. Aucun jeu PS4 validé.").font(.footnote)
                }.padding()
            }.navigationTitle("MaxPS4 Orbis Test")
        }.fileImporter(isPresented: $importing, allowedContentTypes: [.data], allowsMultipleSelection: false) { result in
            guard case .success(let urls) = result, let url = urls.first else {
                error = "Import annulé ou indisponible."; return
            }
            busy = true; inspection = nil; execution = "execution=not_attempted"; error = nil
            queue.async {
                let scoped = url.startAccessingSecurityScopedResource()
                defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                do {
                    let values = try url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
                    guard values.isRegularFile == true, values.isSymbolicLink != true else {
                        throw CocoaError(.fileReadUnsupportedScheme)
                    }
                    let handle = try FileHandle(forReadingFrom: url)
                    defer { try? handle.close() }
                    let size = try handle.seekToEnd()
                    guard size >= 64 && size <= 32*1024*1024 else { throw CocoaError(.fileReadTooLarge) }
                    try handle.seek(toOffset: 0)
                    let bytes = try handle.read(upToCount: Int(size)) ?? Data()
                    guard bytes.count == Int(size) else { throw CocoaError(.fileReadCorruptFile) }
                    let checked = MaxPS4OrbisTest.inspect(bytes)
                    DispatchQueue.main.async { inspection = checked; busy = false }
                } catch {
                    DispatchQueue.main.async { self.error = "Lecture refusée : fichier régulier de 64 octets à 32 Mio requis, source stable et autorisée."; busy = false }
                }
            }
        }
    }
    private func inspect(_ bytes: Data) {
        busy = true; error = nil; inspection = nil; execution = "execution=not_attempted"
        queue.async {
            let checked = MaxPS4OrbisTest.inspect(bytes)
            DispatchQueue.main.async { inspection = checked; busy = false }
        }
    }
}
