import SwiftUI

struct MaxPS4FEXDiagnosticView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var running = false
    @State private var report: MaxPS4FEXDiagnostic?

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    Text("Test x86-64 : 40 + 2").font(.headline)
                    Text("Le contexte FEX est interrogé réellement. Si son JIT est indisponible, le programme est exécuté par l’interpréteur natif existant.")
                    Button(running ? "Test en cours…" : "Tester FEXCore") {
                        running = true
                        Task {
                            let result = await Task.detached(priority: .userInitiated) {
                                MaxPS4FEXDiagnostic.run()
                            }.value
                            report = result
                            running = false
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(running)
                    if running { ProgressView() }
                    if let report {
                        Text(report.succeeded ? "Test réussi" : "Test en échec")
                            .foregroundStyle(report.succeeded ? .green : .orange)
                        Text(report.details).font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                    }
                }.padding()
            }
            .navigationTitle("Diagnostic FEXCore")
            .toolbar { ToolbarItem(placement: .topBarTrailing) { Button("Fermer") { dismiss() } } }
        }
    }
}
