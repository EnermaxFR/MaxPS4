import SwiftUI
import UniformTypeIdentifiers

struct MaxPS4FEXDiagnosticView: View {
    @Environment(\.dismiss) private var dismiss
    @Environment(\.scenePhase) private var scenePhase
    @State private var running = false
    @State private var report: MaxPS4FEXDiagnostic?
    @State private var activationMessage: String?
    @State private var activationRequested = false
    @State private var importingELF = false
    @State private var importingPKG = false
    @State private var pkgReport: String?
    @State private var importingOrbisDirectory = false
    @State private var importingM1Diagnostic = false
    @State private var importingEboot = false
    @State private var experimentalEboot = false
    @State private var importedReport: String?
    @State private var ps4LibkernelReport: String?
    @State private var m2Report: String?
    @State private var analyzingOrbis = false
    @State private var orbisReport: String?

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    NavigationLink("Première image jouable", destination: MaxPS4PlayableView())
                    Button("Diagnostic M1 — collisions / deux threads") { importingM1Diagnostic = true }
                        .disabled(running)
                        .accessibilityIdentifier("maxps4.m1Collision.imported")
                    Button("Diagnostic M1 — fixtures intégrées") {
                        running = true
                        Task {
                            importedReport = await withCheckedContinuation { continuation in
                                MaxPS4FEXMetalExecution.queue.async {
                                    continuation.resume(returning: MaxPS4FEXDiagnostic.testBundledM1Collision())
                                }
                            }
                            running = false
                        }
                    }
                    .disabled(running)
                    .accessibilityIdentifier("maxps4.m1Collision.bundled")
                    Button("Tester SELF clair — signature non vérifiée") {
                        experimentalEboot = true; importingEboot = true
                    }
                    .disabled(running)
                    .accessibilityIdentifier("maxps4.experimentalSELF")
                    Button("Tester eboot.bin — mode strict") { experimentalEboot = false; importingEboot = true }
                        .disabled(running)
                        .accessibilityIdentifier("maxps4.separateEboot")
                    Text("État du JIT FEXCore").font(.headline)
                    Button("Tester homebrew C — démarrage V4") {
                        running = true
                        Task {
                            importedReport = await withCheckedContinuation { continuation in
                                MaxPS4FEXMetalExecution.queue.async {
                                    continuation.resume(returning: MaxPS4FEXDiagnostic.testCGameBoot())
                                }
                            }
                            running = false
                        }
                    }
                    .disabled(running)
                    .accessibilityIdentifier("maxps4.cGameBootProbe")
                    Button("Tester homebrew C — données globales V4") {
                        running = true
                        Task {
                            importedReport = await withCheckedContinuation { continuation in
                                MaxPS4FEXMetalExecution.queue.async {
                                    continuation.resume(returning: MaxPS4FEXDiagnostic.testCGameBoot(globals: true))
                                }
                            }
                            running = false
                        }
                    }
                    .disabled(running)
                    .accessibilityIdentifier("maxps4.globalGameBootProbe")
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
                    Text("CS_DEBUGGED, MAP_JIT et RW/RW ne valident pas le fournisseur RX/RW FEXCore. Utiliser l’activation ci-dessous ; aucun test BRK n’est autorisé. Un simple attachement debugserver ou le script Universal JIT seul ne publie pas la mailbox MaxPS4.")
                        .font(.footnote)
                    Text("Activer avec StikDebug, attendre la fin de son script et son détachement, puis tester FEXCore. Le test vérifie les permissions RX/RW et l’exécution x86-64. Une confirmation de script seule ne valide pas le JIT.")
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
                    Button("Tester M2 low VA — JIT 42 / TLS 8") {
                        running = true
                        Task {
                            m2Report = await withCheckedContinuation { continuation in
                                MaxPS4FEXMetalExecution.queue.async {
                                    continuation.resume(returning: MaxPS4FEXDiagnostic.testM2LowVA())
                                }
                            }
                            running = false
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(running)
                    .accessibilityIdentifier("maxps4-m2-low-va-test")
                    if let m2Report {
                        Text(m2Report).font(.system(.body, design: .monospaced)).textSelection(.enabled)
                        ShareLink(item: m2Report) { Label("Partager le rapport M2", systemImage: "square.and.arrow.up") }
                    }
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
                    Button("Analyser un PKG PS4 (sans exécution)…") { importingPKG = true }
                        .disabled(running)
                        .accessibilityIdentifier("maxps4-pkg-analysis")
                    Text("Inventaire PKG/PFS en lecture seule ; aucun lancement ni déchiffrement.")
                        .font(.footnote)
                    if let pkgReport {
                        Text(pkgReport).font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                        ShareLink(item: pkgReport) {
                            Label("Partager l’analyse PKG/PFS", systemImage: "square.and.arrow.up")
                        }
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
                    Button("Importer et exécuter un dossier Orbis compatible…") { importingOrbisDirectory = true }
                        .disabled(running)
                    Text("Dossier autorisé avec maxps4.json, ELF et PRX en clair. Les fichiers, modules et instructions sont vérifiés avant l’exécution. Un PFS opaque reste bloqué. Pour la démo avec framebuffer et commandes, ouvrir « Première image jouable ». Audio de jeu indisponible.")
                        .font(.footnote)
                    if let importedReport {
                        Text(importedReport).font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                    }
                    Button("Activer avec StikDebug") {
                        report = nil
                        activationRequested = true
                        activationMessage = MaxPS4StikActivation.begin { message in
                            activationMessage = message
                        }
                    }
                    .disabled(running)
                    Button("Libérer la mémoire JIT") {
                        activationMessage = MaxPS4StikActivation.release()
                        report = nil
                        activationRequested = false
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
            .fileImporter(isPresented: $importingPKG, allowedContentTypes: [.data]) { selection in
                switch selection {
                case .success(let url):
                    let scoped = url.startAccessingSecurityScopedResource()
                    running = true
                    Task {
                        defer {
                            if scoped { url.stopAccessingSecurityScopedResource() }
                            running = false
                        }
                        pkgReport = await withCheckedContinuation { continuation in
                            MaxPS4FEXMetalExecution.queue.async {
                                do {
                                    continuation.resume(returning: try MaxPS4PKGInspector.inspect(url: url))
                                } catch {
                                    continuation.resume(returning: error.localizedDescription)
                                }
                            }
                        }
                    }
                case .failure(let error):
                    pkgReport = error.localizedDescription
                }
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
            .fileImporter(isPresented: $importingOrbisDirectory, allowedContentTypes: [.folder]) { selection in
                switch selection {
                case .success(let url):
                    let scoped = url.startAccessingSecurityScopedResource()
                    running = true
                    Task {
                        defer {
                            if scoped { url.stopAccessingSecurityScopedResource() }
                            running = false
                        }
                        importedReport = await withCheckedContinuation { continuation in
                            MaxPS4FEXMetalExecution.queue.async {
                                continuation.resume(returning: MaxPS4FEXDiagnostic.executeOrbisDirectory(url: url))
                            }
                        }
                    }
                case .failure(let error): importedReport = error.localizedDescription
                }
            }
            .fileImporter(isPresented: $importingM1Diagnostic, allowedContentTypes: [.folder]) { selection in
                switch selection {
                case .success(let url):
                    let scoped = url.startAccessingSecurityScopedResource()
                    running = true
                    Task {
                        defer { if scoped { url.stopAccessingSecurityScopedResource() }; running = false }
                        importedReport = await withCheckedContinuation { continuation in
                            MaxPS4FEXMetalExecution.queue.async {
                                continuation.resume(returning: MaxPS4FEXDiagnostic.testM1Collision(directory: url))
                            }
                        }
                    }
                case .failure(let error): importedReport = error.localizedDescription
                }
            }
            .fileImporter(isPresented: $importingEboot, allowedContentTypes: [.data]) { selection in
                switch selection {
                case .success(let url):
                    let chosenExperimental = experimentalEboot
                    let scoped = url.startAccessingSecurityScopedResource()
                    running = true
                    Task {
                        defer { if scoped { url.stopAccessingSecurityScopedResource() }; running = false }
                        importedReport = await withCheckedContinuation { continuation in
                            MaxPS4FEXMetalExecution.queue.async {
                                continuation.resume(returning: MaxPS4FEXDiagnostic.testEboot(url: url, experimental: chosenExperimental))
                            }
                        }
                    }
                case .failure(let error): importedReport = error.localizedDescription
                }
            }
            .navigationTitle("Diagnostic FEXCore")
            .onChange(of: scenePhase) { phase in
                if phase == .active && activationRequested {
                    activationMessage = MaxPS4StikActivation.confirmation()
                }
            }
            .toolbar { ToolbarItem(placement: .topBarTrailing) { Button("Fermer") { dismiss() } } }
        }
    }
}


