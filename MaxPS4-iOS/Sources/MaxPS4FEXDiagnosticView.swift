import SwiftUI
import UniformTypeIdentifiers

struct MaxPS4FEXDiagnosticView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var running = false
    @State private var report: MaxPS4FEXDiagnostic?
    @State private var activationMessage: String?
    @State private var importingELF = false
    @State private var importedReport: String?
    @State private var ps4LibkernelReport: String?
    @State private var analyzingOrbis = false
    @State private var orbisReport: String?

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    Text("État du JIT FEXCore").font(.headline)
                    if let report {
                        let jitActive = report.succeeded && report.backend == "FEXCore JIT ARM64"
                        Label(
                            jitActive ? "JIT actif" :
                                report.succeeded && report.backend.contains("Interpréteur") ? "Interpréteur utilisé — JIT indisponible" :
                                "JIT indisponible",
                            systemImage: jitActive ? "checkmark.shield.fill" : "exclamationmark.shield.fill"
                        )
                        .font(.headline)
                        .foregroundStyle(jitActive ? .green : .orange)
                        .accessibilityIdentifier("maxps4-jit-status")
                    } else {
                        Label("JIT non testé sur cet appareil", systemImage: "questionmark.shield")
                            .foregroundStyle(.secondary)
                    }
                    Text("Le JIT nécessite une autorisation effective d’exécution de code sur cet iPhone. SideStore ou un débogueur détecté ne suffisent pas à la prouver. Si l’initialisation renvoie -45 (ENOTSUP), consultez le diagnostic et utilisez l’interpréteur de repli.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                    Text("Test x86-64 : 40 + 2").font(.headline)
                    Text("Le contexte FEX est interrogé réellement. Si son JIT est indisponible, le programme est exécuté par l’interpréteur natif existant.")
                    Button(running ? "Test en cours…" : "Tester FEXCore") {
                        running = true
                        Task {
                            let result = await withCheckedContinuation { continuation in
                                MaxPS4FEXMetalExecution.queue.async {
                                    continuation.resume(returning: MaxPS4FEXDiagnostic.run())
                                }
                            }
                            report = result
                            running = false
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(running)
                    Button("Tester ELF PS4 / libkernel") {
                        running = true
                        Task {
                            ps4LibkernelReport = await withCheckedContinuation { continuation in
                                MaxPS4FEXMetalExecution.queue.async {
                                    continuation.resume(returning: MaxPS4FEXDiagnostic.testPS4Libkernel())
                                }
                            }
                            running = false
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(running)
                    .accessibilityIdentifier("maxps4-ps4-libkernel-test")
                    if let ps4LibkernelReport {
                        Text(ps4LibkernelReport).font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                        ShareLink(item: ps4LibkernelReport) { Label("Partager le rapport ELF PS4", systemImage: "square.and.arrow.up") }
                    }
                    Button("Analyser un exécutable Orbis") { analyzingOrbis = true; importingELF = true }
                        .disabled(running)
                        .accessibilityIdentifier("maxps4-orbis-analysis")
                    Text("Sélectionnez un ELF ou PRX non chiffré. Analyse seule, sans exécution automatique.")
                        .font(.footnote)
                    if let orbisReport {
                        Text(orbisReport).font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                        ShareLink(item: orbisReport) { Label("Partager l’analyse Orbis", systemImage: "square.and.arrow.up") }
                    }
                    Button("Exécuter un ELF de test ou une démo PS4…") { analyzingOrbis = false; importingELF = true }
                        .disabled(running)
                    if let importedReport {
                        Text(importedReport).font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                    }
                    Button("Activer avec StikDebug") {
                        activationMessage = MaxPS4StikActivation.begin()
                    }
                    .disabled(running)
                    Button("Libérer la mémoire JIT") {
                        activationMessage = MaxPS4StikActivation.release()
                    }
                    .disabled(running)
                    if let activationMessage { Text(activationMessage).font(.footnote) }
                    if running { ProgressView() }
                    if let report {
                        Text(report.succeeded ? (report.backend == "FEXCore JIT ARM64" ? "JIT FEXCore réussi" : "Interpréteur réussi — JIT non validé") : "Test en échec")
                            .foregroundStyle(report.succeeded ? .green : .orange)
                        Text(report.details).font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                        Divider()
                        Text(report.ps4IntegrationReport)
                            .font(.system(.footnote, design: .monospaced))
                            .textSelection(.enabled)
                    }
                }.padding()
            }
            .fileImporter(isPresented: $importingELF, allowedContentTypes: [.data]) { selection in
                switch selection {
                case .success(let url):
                    let analysisOnly = analyzingOrbis
                    let scoped = url.startAccessingSecurityScopedResource()
                    running = true
                    Task {
                        defer {
                            if scoped { url.stopAccessingSecurityScopedResource() }
                            running = false
                        }
                        let result: String = await withCheckedContinuation { continuation in
                            MaxPS4FEXMetalExecution.queue.async {
                                if analysisOnly {
                                    continuation.resume(returning: MaxPS4OrbisAnalyzer.inspect(url: url))
                                } else {
                                    continuation.resume(returning: MaxPS4FEXDiagnostic.executeTestELF(url: url))
                                }
                            }
                        }
                        if analysisOnly { orbisReport = result } else { importedReport = result }
                    }
                case .failure(let error):
                    if analyzingOrbis { orbisReport = error.localizedDescription }
                    else { importedReport = error.localizedDescription }
                }
            }
            .navigationTitle("Diagnostic FEXCore")
            .toolbar { ToolbarItem(placement: .topBarTrailing) { Button("Fermer") { dismiss() } } }
        }
    }
}

