import SwiftUI
import UniformTypeIdentifiers
import UIKit

private enum MaxPS4Tab: Hashable {
    case home
    case games
    case tools
    case settings
}

struct MaxPS4HomeView: View {
    @EnvironmentObject private var emulator: MaxPS4Emulator

    @State private var selectedTab: MaxPS4Tab = .home
    @State private var importingGame = false
    @State private var gameSearch = ""
    @State private var gameFilter = 0
    @State private var gameToRename: MaxPS4Game?
    @State private var gameToDelete: MaxPS4Game?
    @State private var showingDuplicates = false
    @State private var comparisonReport: String?
    @State private var comparingPKGs = false
    @State private var renamedGameTitle = ""
    @State private var inspectionReport: String?
    @State private var selectedGameDetails: MaxPS4Game?
    @State private var detailsReport: String?
    @State private var jitStatusReport: String?

    @AppStorage("showFPS") private var showFPS = false
    @AppStorage("networkEnabled") private var networkEnabled = false

    private var filteredGames: [MaxPS4Game] {
        let query = gameSearch.trimmingCharacters(in: .whitespacesAndNewlines)
        let matching = emulator.games.filter { query.isEmpty || $0.name.localizedCaseInsensitiveContains(query) }
        return gameFilter == 1 ? matching.sorted { $0.importedAt > $1.importedAt } : matching
    }

    var body: some View {
        ZStack {
            MaxPS4Background()

            Group {
                switch selectedTab {
                case .home:
                    home
                case .games:
                    library
                case .tools:
                    toolsPage
                case .settings:
                    settings
                }
            }
        }
        .preferredColorScheme(.dark)
        .safeAreaInset(edge: .bottom) {
            tabBar
        }
        .alert("Supprimer ce fichier ?", isPresented: Binding(
            get: { gameToDelete != nil },
            set: { if !$0 { gameToDelete = nil } }
        )) {
            Button("Annuler", role: .cancel) { gameToDelete = nil }
            Button("Supprimer définitivement", role: .destructive) {
                if let game = gameToDelete { emulator.remove(game) }
                gameToDelete = nil
            }
        } message: {
            Text("La copie locale sera supprimée : " + (gameToDelete?.fileName ?? "") + ". Action irréversible.")
        }
        .alert("Renommer le jeu", isPresented: Binding(
            get: { gameToRename != nil },
            set: { if !$0 { gameToRename = nil } }
        )) {
            TextField("Nom du jeu", text: $renamedGameTitle)
            Button("Annuler", role: .cancel) { gameToRename = nil }
            Button("Enregistrer") {
                if let game = gameToRename {
                    emulator.rename(game, to: renamedGameTitle)
                }
                gameToRename = nil
            }
        } message: {
            Text("Le fichier original ne sera pas renommé.")
        }
        .alert("Résultat de l’analyse", isPresented: Binding(
            get: { inspectionReport != nil },
            set: { if !$0 { inspectionReport = nil } }
        )) {
            Button("Fermer") { inspectionReport = nil }
        } message: {
            Text(inspectionReport ?? "")
        }
        .sheet(isPresented: $showingDuplicates) {
            NavigationStack {
                List {
                    Section {
                        Button(comparingPKGs ? "Comparaison en cours…" : "Comparer les PKG par SHA-256") {
                            guard !comparingPKGs else { return }
                            comparingPKGs = true
                            comparisonReport = "Lecture des PKG en cours…" 
                            let files = emulator.games.filter { $0.fileName.lowercased().hasSuffix(".pkg") }
                            Task {
                                let message = await Task.detached(priority: .utility) { () -> String in
                                    var groups: [String: [String]] = [:]
                                    var errors: [String] = []
                                    var details: [String] = []
                                    var titleGroups: [String: [String]] = [:]
                                    var contentGroups: [String: [String]] = [:]
                                    for game in files {
                                        do {
                                            let fingerprint = try MaxPS4PKGHash.digest(url: URL(fileURLWithPath: game.localPath))
                                            groups[fingerprint, default: []].append(game.fileName)
                                            let handle = try FileHandle(forReadingFrom: URL(fileURLWithPath: game.localPath))
                                            defer { try? handle.close() }
                                            let header = try handle.read(upToCount: 128) ?? Data()
                                            let attributes = try FileManager.default.attributesOfItem(atPath: game.localPath)
                                            let length = (attributes[.size] as? NSNumber)?.int64Value ?? 0
                                            var contentID = "indisponible"
                                            if header.count >= 0x64 && Array(header.prefix(4)) == [0x7F, 0x43, 0x4E, 0x54] {
                                                let field = header.subdata(in: 0x40..<0x64)
                                                contentID = String(bytes: field.prefix(while: { $0 != 0 }), encoding: .ascii) ?? "indisponible"
                                            }
                                            let titleID = contentID.range(of: "CUSA[0-9]{5}", options: .regularExpression).map { String(contentID[$0]) } ?? "indisponible"
                                            if titleID != "indisponible" {
                                                titleGroups[titleID, default: []].append(game.fileName)
                                            }
                                            if contentID != "indisponible" {
                                                contentGroups[contentID, default: []].append(game.fileName)
                                            }
                                            details.append(game.fileName + "\nTaille : " + String(length) + " octets\nContent ID : " + contentID + "\nTitle ID : " + titleID + "\nSHA-256 : " + fingerprint)
                                        } catch { errors.append(game.fileName + " : " + error.localizedDescription) }
                                    }
                                    let duplicates = groups.values.filter { $0.count > 1 }
                                    var message = "PKG présents : " + String(files.count) + " • comparés : " + String(files.count - errors.count) + " • erreurs : " + String(errors.count) + "\n"
                                    message += duplicates.isEmpty ? "Aucun PKG strictement identique détecté." :
                                        duplicates.map { "Copies identiques : " + $0.sorted().joined(separator: " / ") }.joined(separator: "\n")
                                    let sameTitle = titleGroups.filter { $0.value.count > 1 }
                                    let sameContent = contentGroups.filter { $0.value.count > 1 }
                                    for (id, names) in sameTitle.sorted(by: { $0.key < $1.key }) {
                                        message += "\nMême Title ID " + id + " : " + names.sorted().joined(separator: " / ")
                                    }
                                    for (id, names) in sameContent.sorted(by: { $0.key < $1.key }) {
                                        message += "\nMême Content ID " + id + " : " + names.sorted().joined(separator: " / ")
                                    }
                                    message += "\nVersion du jeu : non déterminée par cet en-tête. Même identifiant ne signifie pas même version."
                                    if files.count < 2 { message += "\nIl faut au moins deux PKG présents pour comparer des copies." }
                                    if !errors.isEmpty { message += "\nFichiers non comparés : " + errors.joined(separator: " / ") }
                                    return message + "\n\nDétails des fichiers :\n" + details.joined(separator: "\n\n") + "\nAucune suppression automatique."
                                }.value
                                comparisonReport = message
                                comparingPKGs = false
                            }
                        }
                        .disabled(comparingPKGs)
                        if let comparisonReport { Text(comparisonReport).font(.footnote).textSelection(.enabled) }
                    }
                    if emulator.duplicateGroups.isEmpty {
                        Text("Aucun doublon probable dans la bibliothèque.")
                            .foregroundStyle(.secondary)
                    } else {
                        ForEach(Array(emulator.duplicateGroups.enumerated()), id: \.offset) { _, group in
                            Section(group.first?.name ?? "Copies possibles") {
                                ForEach(group) { game in
                                    HStack {
                                        VStack(alignment: .leading, spacing: 4) {
                                            Text(game.fileName).font(.subheadline)
                                            Text(game.importedAt.formatted(date: .abbreviated, time: .shortened))
                                                .font(.caption).foregroundStyle(.secondary)
                                        }
                                        Spacer()
                                        Button(role: .destructive) {
                                            gameToDelete = game
                                        } label: {
                                            Image(systemName: "trash")
                                        }
                                        .buttonStyle(.borderless)
                                    }
                                }
                            }
                        }
                    }
                    Text("Doublons probables selon le nom du fichier. Aucune suppression automatique.")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                .navigationTitle("Copies détectées")
                .toolbar {
                    ToolbarItem(placement: .topBarTrailing) {
                        Button("Fermer") { showingDuplicates = false }
                    }
                }
            }
        }
        .sheet(item: $selectedGameDetails, onDismiss: { detailsReport = nil }) { game in
            NavigationStack {
                ScrollView {
                    VStack(alignment: .leading, spacing: 16) {
                        Text(game.fileName.uppercased().contains("SONICMANIA") ? "Sonic Mania" : game.name)
                            .font(.largeTitle.bold())
                        ZStack {
                            RoundedRectangle(cornerRadius: 20)
                                .fill(LinearGradient(colors: [.blue, .purple, .black], startPoint: .topLeading, endPoint: .bottomTrailing))
                            VStack(spacing: 12) {
                                Image(systemName: "gamecontroller.fill").font(.system(size: 48))
                                Text(game.fileName.uppercased().contains("SONICMANIA") ? "SONIC MANIA" : game.name.uppercased())
                                    .font(.title2.bold())
                            }
                            .foregroundStyle(.white)
                        }
                        .frame(height: 180)
                        Text(game.fileName)
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                            .textSelection(.enabled)
                        Text(game.fileName.lowercased().hasSuffix(".pkg") ? "Format : PKG PS4" : "Format : ELF / SELF")
                        if game.fileName.uppercased().contains("SONICMANIA") {
                            Text("Identifiant PS4 observé : CUSA07023").foregroundStyle(.cyan)
                        }
                        if let attributes = try? FileManager.default.attributesOfItem(atPath: game.localPath),
                           let value = attributes[.size] as? NSNumber {
                            Text("Taille locale : " + ByteCountFormatter.string(fromByteCount: value.int64Value, countStyle: .file))
                        }
                        Text("Date d’import : " + game.importedAt.formatted(date: .abbreviated, time: .shortened))
                        Text("Exécution PS4 indisponible")
                            .foregroundStyle(.orange)
                        Button("Essayer le moteur (diagnostic)") {
                            emulator.tryGameEngine(game)
                            detailsReport = emulator.status
                        }
                        .buttonStyle(.borderedProminent)
                        Button("Analyser les métadonnées") {
                            emulator.inspect(game)
                            detailsReport = emulator.status
                        }
                        .buttonStyle(.borderedProminent)
                        if game.fileName.lowercased().hasSuffix(".elf") || game.fileName.lowercased() == "eboot.bin" {
                            Button("Inspecter les imports ELF64") {
                                emulator.inspectELFImports(game)
                                detailsReport = emulator.status
                            }
                            .buttonStyle(.bordered)
                        }
                        if game.fileName.lowercased().hasSuffix(".pkg") {
                            Button("Examiner les entrées du PKG") {
                                emulator.inspectPKGEntries(game)
                                detailsReport = emulator.status
                            }
                            .buttonStyle(.bordered)
                            Button("Scanner les exécutables PS4") {
                                emulator.inspectPS4EntryPrefixes(game)
                                detailsReport = emulator.status
                            }
                            .buttonStyle(.bordered)
                            Button("Vérifier le chargeur PS4 / SELF") {
                                emulator.inspectPS4ExecutableReadiness(game)
                                detailsReport = emulator.status
                            }
                            .buttonStyle(.bordered)
                            Button("Rechercher ressources Sonic Mania") {
                                emulator.inspectPKGAssetCandidates(game)
                                detailsReport = emulator.status
                            }
                            .buttonStyle(.bordered)
                            Button("Analyser la structure du PKG") {
                                emulator.inspectPKGStructure(game)
                                detailsReport = emulator.status
                            }
                            .buttonStyle(.bordered)
                            Button("Calculer SHA-256 du PKG") {
                                detailsReport = "Calcul de l’empreinte SHA-256 en cours…"
                                Task {
                                    detailsReport = await MaxPS4PKGHash.sha256(
                                        url: URL(fileURLWithPath: game.localPath)
                                    )
                                }
                            }
                            .buttonStyle(.bordered)
                        }
                        if let report = detailsReport {
                            Text(report)
                                .font(.subheadline)
                                .textSelection(.enabled)
                                .padding(16)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .background(.white.opacity(0.08), in: RoundedRectangle(cornerRadius: 14))
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding()
                }
                .navigationTitle("Fiche du jeu")
                .navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .topBarTrailing) {
                        Button("Fermer") {
                            selectedGameDetails = nil
                            detailsReport = nil
                        }
                    }
                }
            }
            .preferredColorScheme(.dark)
        }
        .fileImporter(
            isPresented: $importingGame,
            allowedContentTypes: [.data, .item],
            allowsMultipleSelection: false
        ) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                emulator.importGame(from: url)

            case .failure(let error):
                emulator.status = "Import impossible : \(error.localizedDescription)"
            }
        }
    }

    private var home: some View {
        ScrollView(showsIndicators: false) {
            VStack(alignment: .leading, spacing: 20) {
                header

                heroPanel

                homeQuickActions

                backendCard
                systemOverview

                primaryAction(
                    icon: "square.and.arrow.down.fill",
                    title: "Importer un PKG / eboot.bin / SELF",
                    subtitle: "PKG PS4 : identification uniquement, sans lancement"
                ) {
                    importingGame = true
                }

                secondaryAction(
                    icon: "cpu",
                    title: "Tester le backend shadPS4/FEX",
                    subtitle: "Vérifie l’état de l’intégration"
                ) {
                    emulator.testBackend()
                }

                libraryHeader

                if emulator.games.isEmpty {
                    emptyLibrary
                } else {
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 14) {
                            ForEach(emulator.games.prefix(6)) { game in
                                gameCard(game, fixedWidth: 158)
                            }
                        }
                        .padding(.vertical, 4)
                    }
                }

                statusCard
                legalNotice
                Spacer(minLength: 90)
            }
            .padding(.horizontal, 20)
            .padding(.top, 12)
        }
    }

    private var library: some View {
        ScrollView(showsIndicators: false) {
            VStack(alignment: .leading, spacing: 18) {
                HStack {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("BIBLIOTHÈQUE")
                            .font(.system(size: 36, weight: .black, design: .rounded))

                        Text("Votre bibliothèque locale")
                            .foregroundStyle(.white.opacity(0.58))
                    }

                    Spacer()

                    Button {
                        importingGame = true
                    } label: {
                        Image(systemName: "plus")
                            .font(.headline)
                            .frame(width: 44, height: 44)
                            .background(.blue)
                            .clipShape(Circle())
                            .foregroundStyle(.white)
                    }
                }

                HStack {
                    Image(systemName: "magnifyingglass")
                    TextField("Rechercher un jeu", text: $gameSearch)
                        .autocorrectionDisabled()
                }
                .padding(14)
                .background(.white.opacity(0.07), in: RoundedRectangle(cornerRadius: 16))
                Picker("Tri", selection: $gameFilter) {
                    Text("Tous").tag(0)
                    Text("Récents").tag(1)
                }
                .pickerStyle(.segmented)
                .tint(.cyan)

                Button {
                    showingDuplicates = true
                } label: {
                    Label("Gérer les doublons", systemImage: "square.on.square")
                        .font(.subheadline.weight(.medium))
                }
                .tint(.cyan)

                if emulator.games.isEmpty {
                    emptyLibrary.padding(.top, 24)
                } else if filteredGames.isEmpty {
                    Text("Aucun jeu correspondant")
                        .foregroundStyle(.white.opacity(0.65))
                        .padding(.vertical, 24)
                } else {
                    LazyVGrid(
                        columns: [
                            GridItem(.flexible(), spacing: 14),
                            GridItem(.flexible(), spacing: 14)
                        ],
                        spacing: 18
                    ) {
                        ForEach(filteredGames) { game in
                            gameCard(game, fixedWidth: nil)
                        }
                    }
                }

                statusCard
                Spacer(minLength: 90)
            }
            .padding(.horizontal, 20)
            .padding(.top, 12)
        }
    }

    private var settings: some View {
        ScrollView(showsIndicators: false) {
            VStack(alignment: .leading, spacing: 18) {
                Text("RÉGLAGES")
                    .font(.system(size: 36, weight: .black, design: .rounded))
                    .padding(.top, 12)

                settingsCard {
                    settingInfoRow(
                        icon: "cpu",
                        title: "Backend",
                        value: emulator.backendReady ? "Prêt" : "À connecter",
                        color: emulator.backendReady ? .green : .orange
                    )

                    Divider().overlay(.white.opacity(0.08))

                    Toggle(isOn: $showFPS) {
                        settingLabel(icon: "speedometer", title: "Préférence FPS (préparation)")
                    }
                    .tint(.blue)
                    .padding(.vertical, 14)

                    Divider().overlay(.white.opacity(0.08))

                    Toggle(isOn: $networkEnabled) {
                        settingLabel(icon: "network", title: "Préférence réseau (préparation)")
                    }
                    .tint(.blue)
                    .padding(.vertical, 14)
                }

                settingsCard {
                    settingInfoRow(
                        icon: "cpu",
                        title: "JIT ARM64",
                        value: MaxPS4NativeLinkCheck.isARM64JITReady ? "Prêt" : "Inactif",
                        color: MaxPS4NativeLinkCheck.isARM64JITReady ? .green : .orange
                    )
                    Divider().overlay(.white.opacity(0.08))
                    settingInfoRow(
                        icon: "ant.fill",
                        title: "Protocole StikDebug",
                        value: MaxPS4NativeLinkCheck.isStikDebugProtocolPresent ? "Intégré" : "Absent",
                        color: MaxPS4NativeLinkCheck.isStikDebugProtocolPresent ? .cyan : .orange
                    )
                    Divider().overlay(.white.opacity(0.08))
                    Button {
                        jitStatusReport = MaxPS4NativeLinkCheck.jitStatusReport
                    } label: {
                        settingButton(icon: "checkmark.shield.fill", title: "Vérifier l’état du JIT")
                    }
                    Divider().overlay(.white.opacity(0.08))
                    Button {
                        jitStatusReport = MaxPS4NativeLinkCheck.stikDebugSafePreflightReport
                    } label: {
                        settingButton(icon: "shield.lefthalf.filled", title: "Tester StikDebug sans risque")
                    }
                    Divider().overlay(.white.opacity(0.08))
                    Button {
                        jitStatusReport = MaxPS4NativeLinkCheck.arm64PipelineSelfTestReport
                    } label: {
                        settingButton(icon: "cpu.fill", title: "Tester le pipeline ARM64")
                    }
                    Divider().overlay(.white.opacity(0.08))
                    Button {
                        jitStatusReport = MaxPS4NativeLinkCheck.jitSafetyGateReport
                    } label: {
                        settingButton(icon: "lock.shield", title: "Contrôler la sécurité du JIT")
                    }
                    Divider().overlay(.white.opacity(0.08))
                    Button {
                        jitStatusReport = MaxPS4NativeLinkCheck.arm64VerifierAdversarialReport
                    } label: {
                        settingButton(icon: "checkmark.shield", title: "Tester les limites du code ARM64")
                    }
                    Divider().overlay(.white.opacity(0.08))
                    Button {
                        jitStatusReport = MaxPS4NativeLinkCheck.jitMemoryPreparationReport
                    } label: {
                        settingButton(icon: "memorychip", title: "Tester la mémoire du futur JIT")
                    }
                    Divider().overlay(.white.opacity(0.08))
                    Button {
                        jitStatusReport = MaxPS4NativeLinkCheck.jitARM64StagingReport
                    } label: {
                        settingButton(icon: "memorychip.fill", title: "Préparer un bloc ARM64 en mémoire")
                    }
                    Divider().overlay(.white.opacity(0.08))
                    Button {
                        jitStatusReport = MaxPS4NativeLinkCheck.jitBlockIntegrityReport
                    } label: {
                        settingButton(icon: "checkmark.seal", title: "Vérifier l’intégrité des blocs ARM64")
                    }
                    Divider().overlay(.white.opacity(0.08))
                    Button {
                        guard let bundleID = Bundle.main.bundleIdentifier,
                              let escaped = bundleID.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed),
                              let url = URL(string: "stikjit://enable-jit?bundle-id=" + escaped) else {
                            jitStatusReport = "Impossible de préparer le lien StikDebug : identifiant d’application absent."
                            return
                        }
                        UIApplication.shared.open(url, options: [:]) { opened in
                            if !opened {
                                jitStatusReport = "StikDebug n’a pas accepté le lien. Ouvre StikDebug manuellement et sélectionne MaxPS4."
                            }
                        }
                    } label: {
                        settingButton(icon: "arrow.up.right.square", title: "Demander le JIT dans StikDebug")
                    }
                    Text("Ouvre StikDebug pour MaxPS4. Le retour dans l’application ne prouve pas que le JIT natif fonctionne.")
                        .font(.caption)
                        .foregroundStyle(.white.opacity(0.56))
                    Divider().overlay(.white.opacity(0.08))
                    Button {
                        jitStatusReport = MaxPS4NativeLinkCheck.jitDualMappingReport
                    } label: {
                        settingButton(icon: "square.on.square", title: "Tester la double mémoire JIT (RW)")
                    }
                    Divider().overlay(.white.opacity(0.08))
                    Button {
                        jitStatusReport = MaxPS4NativeLinkCheck.aetherGuestBackendReport
                    } label: {
                        settingButton(icon: "cpu", title: "Tester le backend CPU AetherPS4")
                    }
                    Divider().overlay(.white.opacity(0.08))
                    Button {
                        jitStatusReport = MaxPS4NativeLinkCheck.aetherJITABIReport
                    } label: {
                        settingButton(icon: "memorychip", title: "Vérifier l’interface JIT AetherPS4")
                    }
                    Divider().overlay(.white.opacity(0.08))
                    Button {
                        jitStatusReport = MaxPS4NativeLinkCheck.arm64ExecutionBaselineReport
                    } label: {
                        settingButton(icon: "cpu.fill", title: "Tester l’exécution ARM64 native")
                    }
                    if let jitStatusReport {
                        Button {
                            UIPasteboard.general.string = jitStatusReport
                        } label: {
                            settingButton(icon: "doc.on.doc", title: "Copier le diagnostic JIT")
                        }
                        Divider().overlay(.white.opacity(0.08))
                        Text(jitStatusReport)
                            .font(.footnote.monospaced())
                            .foregroundStyle(.white.opacity(0.85))
                            .textSelection(.enabled)
                            .padding(.vertical, 10)
                    }
                }

                settingsCard {
                    Button {
                        emulator.testBackend()
                    } label: {
                        settingButton(icon: "checkmark.circle.fill", title: "Tester l’intégration")
                    }

                    Divider().overlay(.white.opacity(0.08))

                    Button(role: .destructive) {
                        emulator.removeAll()
                    } label: {
                        settingButton(icon: "trash.fill", title: "Vider la bibliothèque", color: .red)
                    }
                }

                Text("MaxPS4 n’inclut aucun jeu, firmware, clé ou contenu PlayStation. Utilisez uniquement des fichiers que vous êtes autorisé à utiliser.")
                    .font(.footnote)
                    .foregroundStyle(.white.opacity(0.48))
                    .fixedSize(horizontal: false, vertical: true)

                statusCard
                Spacer(minLength: 100)
            }
            .padding(.horizontal, 20)
        }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 5) {
            HStack(spacing: 0) {
                Text("Max")
                    .foregroundStyle(.white)
                Text("PS4")
                    .foregroundStyle(
                        LinearGradient(
                            colors: [.cyan, .purple, .blue],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        )
                    )
            }
            .font(.system(size: 42, weight: .black, design: .rounded))

            Text("iOS  •  ÉDITION NÉON")
                .font(.subheadline.weight(.medium))
                .foregroundStyle(.white.opacity(0.60))
        }
        .padding(.top, 8)
    }

    private var homeQuickActions: some View {
        HStack(spacing: 12) {
            Button {
                withAnimation(.easeInOut(duration: 0.22)) { selectedTab = .tools }
            } label: {
                HStack(spacing: 11) {
                    Image(systemName: "gamecontroller.fill")
                        .font(.title3)
                        .foregroundStyle(.cyan)
                    VStack(alignment: .leading, spacing: 3) {
                        Text("MAX MAZE").font(.subheadline.bold())
                        Text("Mini-jeu arcade").font(.caption2).foregroundStyle(.white.opacity(0.60))
                    }
                    Spacer(minLength: 0)
                    Image(systemName: "arrow.up.right").font(.caption.bold())
                        .foregroundStyle(.cyan)
                }
                .padding(15)
                .frame(maxWidth: .infinity)
                .background(Color.cyan.opacity(0.09), in: RoundedRectangle(cornerRadius: 18))
                .overlay(RoundedRectangle(cornerRadius: 18).stroke(.cyan.opacity(0.34)))
            }
            .buttonStyle(.plain)
            .accessibilityHint("Ouvre les outils où se trouve Max Maze")

            Button {
                importingGame = true
            } label: {
                VStack(spacing: 5) {
                    Image(systemName: "plus.square.on.square")
                        .font(.title3)
                    Text("Importer").font(.caption.bold())
                }
                .foregroundStyle(.white)
                .frame(width: 89, height: 62)
                .background(.white.opacity(0.09), in: RoundedRectangle(cornerRadius: 18))
                .overlay(RoundedRectangle(cornerRadius: 18).stroke(.white.opacity(0.13)))
            }
            .buttonStyle(.plain)
        }
    }

    private var systemOverview: some View {
        LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
            overviewTile(icon: "gamecontroller.fill", title: "Jeux", value: "\(emulator.games.count) importés")
            overviewTile(icon: "internaldrive", title: "Stockage", value: "Local")
            overviewTile(icon: "cpu", title: "CPU", value: emulator.backendReady ? "Connecté" : "Inactif")
            overviewTile(icon: "desktopcomputer", title: "GPU", value: emulator.backendReady ? "Moteur connecté" : "Inactif")
        }
    }

    private func overviewTile(icon: String, title: String, value: String) -> some View {
        HStack(spacing: 10) {
            Image(systemName: icon)
                .foregroundStyle(.cyan)
                .font(.title3)
                .frame(width: 30)
            VStack(alignment: .leading, spacing: 5) {
                Text(title).font(.subheadline.weight(.semibold))
                Text(value).font(.caption).foregroundStyle(.white.opacity(0.63))
            }
            Spacer(minLength: 0)
        }
        .padding(14)
        .background(Color(red: 0.055, green: 0.06, blue: 0.19), in: RoundedRectangle(cornerRadius: 18))
        .overlay(RoundedRectangle(cornerRadius: 18).stroke(.blue.opacity(0.28), lineWidth: 1))
    }

    private var backendCard: some View {
        HStack(spacing: 14) {
            Circle()
                .fill(emulator.backendReady ? .green : .orange)
                .frame(width: 14, height: 14)
                .shadow(
                    color: (emulator.backendReady ? Color.green : Color.orange).opacity(0.8),
                    radius: 9
                )

            VStack(alignment: .leading, spacing: 4) {
                Text(emulator.backendReady ? "Backend prêt" : "Frontend prêt")
                    .font(.headline)

                Text(
                    emulator.backendReady
                        ? "shadPS4 / FEXCore connecté"
                        : "Interface active • pont natif à connecter"
                )
                .font(.subheadline)
                .foregroundStyle(.white.opacity(0.62))
            }

            Spacer()

            Image(systemName: emulator.backendReady ? "checkmark.circle.fill" : "wrench.and.screwdriver.fill")
                .foregroundStyle(emulator.backendReady ? .green : .orange)
                .font(.title3)
        }
        .padding(18)
        .background(.white.opacity(0.055), in: RoundedRectangle(cornerRadius: 22))
        .overlay(
            RoundedRectangle(cornerRadius: 22)
                .stroke(.blue.opacity(0.28), lineWidth: 1)
        )
    }

    private func primaryAction(
        icon: String,
        title: String,
        subtitle: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            HStack(spacing: 14) {
                Image(systemName: icon)
                    .font(.title2)
                    .frame(width: 52, height: 52)
                    .background(.white.opacity(0.14), in: RoundedRectangle(cornerRadius: 15))

                VStack(alignment: .leading, spacing: 4) {
                    Text(title)
                        .font(.headline)
                    Text(subtitle)
                        .font(.subheadline)
                        .foregroundStyle(.white.opacity(0.76))
                }

                Spacer()
                Image(systemName: "chevron.right")
                    .font(.headline)
            }
            .foregroundStyle(.white)
            .padding(18)
            .background(
                LinearGradient(
                    colors: [
                        Color(red: 0.02, green: 0.31, blue: 0.95),
                        Color(red: 0.0, green: 0.56, blue: 1.0)
                    ],
                    startPoint: .topLeading,
                    endPoint: .bottomTrailing
                ),
                in: RoundedRectangle(cornerRadius: 24)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 24)
                    .stroke(.cyan.opacity(0.60), lineWidth: 1)
            )
            .shadow(color: .blue.opacity(0.28), radius: 18, y: 6)
        }
        .buttonStyle(.plain)
    }

    private func secondaryAction(
        icon: String,
        title: String,
        subtitle: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            HStack(spacing: 14) {
                ZStack {
                    RoundedRectangle(cornerRadius: 15)
                        .fill(Color.cyan.opacity(0.12))
                    Image(systemName: icon)
                        .font(.system(size: 22, weight: .semibold))
                        .foregroundStyle(.cyan)
                        .symbolRenderingMode(.hierarchical)
                    // Persistent visible fallback when an unavailable SF Symbol
                    // name is used on a particular iOS version.
                    Image(systemName: "square.grid.2x2.fill")
                        .font(.system(size: 13, weight: .medium))
                        .foregroundStyle(.cyan.opacity(0.52))
                        .offset(x: 15, y: 15)
                }
                .frame(width: 52, height: 52)

                VStack(alignment: .leading, spacing: 4) {
                    Text(title)
                        .font(.headline)
                    Text(subtitle)
                        .font(.subheadline)
                        .foregroundStyle(.white.opacity(0.60))
                }

                Spacer()
                Image(systemName: "chevron.right")
            }
            .foregroundStyle(.white)
            .padding(18)
            .background(.white.opacity(0.045), in: RoundedRectangle(cornerRadius: 24))
            .overlay(
                RoundedRectangle(cornerRadius: 24)
                    .stroke(.blue.opacity(0.22), lineWidth: 1)
            )
        }
        .buttonStyle(.plain)
    }

    private var libraryHeader: some View {
        HStack {
            VStack(alignment: .leading, spacing: 4) {
                HStack(spacing: 8) {
                    Image(systemName: "gamecontroller.fill")
                        .foregroundStyle(.cyan)
                    Text("Bibliothèque")
                        .font(.title2.bold())
                }

                Text(
                    emulator.games.isEmpty
                        ? "Aucun jeu importé"
                        : "\(emulator.games.count) élément\(emulator.games.count > 1 ? "s" : "")"
                )
                .font(.subheadline)
                .foregroundStyle(.white.opacity(0.58))
            }

            Spacer()

            if !emulator.games.isEmpty {
                Button {
                    selectedTab = .games
                } label: {
                    HStack(spacing: 4) {
                        Text("Tout voir")
                        Image(systemName: "chevron.right")
                    }
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(.cyan)
                }
            }
        }
        .padding(.top, 4)
    }

    private var emptyLibrary: some View {
        VStack(spacing: 14) {
            Image(systemName: "gamecontroller")
                .font(.system(size: 36))
                .foregroundStyle(.white.opacity(0.58))

            Text("Aucun jeu importé")
                .font(.headline)

            Text("Importez un eboot.bin / SELF ou un fichier de jeu que vous êtes autorisé à utiliser.")
                .font(.subheadline)
                .foregroundStyle(.white.opacity(0.58))
                .multilineTextAlignment(.center)

            Button("Importer") {
                importingGame = true
            }
            .buttonStyle(.borderedProminent)
            .tint(.blue)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 30)
        .padding(.horizontal, 16)
        .background(.white.opacity(0.025), in: RoundedRectangle(cornerRadius: 24))
        .overlay(
            RoundedRectangle(cornerRadius: 24)
                .stroke(
                    .blue.opacity(0.32),
                    style: StrokeStyle(lineWidth: 1, dash: [7, 6])
                )
        )
    }

    private func gameCard(_ game: MaxPS4Game, fixedWidth: CGFloat?) -> some View {
        let colors: [Color] = [.blue, .indigo, .purple, .cyan]
        let accent = colors[game.name.utf8.reduce(0) { ($0 + Int($1)) % colors.count }]
        let isPKG = game.fileName.lowercased().hasSuffix(".pkg")
        let displayedTitle = isPKG && game.fileName.uppercased().contains("SONICMANIA") ? "Sonic Mania" : game.name

        return VStack(alignment: .leading, spacing: 10) {
            Button {
                if isPKG {
                    selectedGameDetails = game
                } else {
                    emulator.launch(game)
                }
            } label: {
                ZStack(alignment: .bottomLeading) {
                    LinearGradient(
                        colors: [accent.opacity(0.95), .black],
                        startPoint: .topLeading,
                        endPoint: .bottomTrailing
                    )

                    Circle()
                        .fill(.white.opacity(0.10))
                        .frame(width: 120, height: 120)
                        .offset(x: 58, y: -78)

                    VStack(alignment: .leading, spacing: 6) {
                        Image(systemName: "gamecontroller.fill")
                            .font(.title2)
                            .foregroundStyle(.white.opacity(0.78))

                        Spacer()

                        Text(displayedTitle)
                            .font(.headline)
                            .lineLimit(2)
                            .multilineTextAlignment(.leading)

                        Text(isPKG ? "PKG PS4 • Analyse seule" : game.fileName)
                            .font(.caption2)
                            .lineLimit(1)
                            .foregroundStyle(.white.opacity(0.56))
                    }
                    .padding(14)
                }
                .frame(maxWidth: fixedWidth == nil ? .infinity : nil)
                .frame(width: fixedWidth, height: 220)
                .clipShape(RoundedRectangle(cornerRadius: 20))
                .overlay(
                    RoundedRectangle(cornerRadius: 20)
                        .stroke(.white.opacity(0.12), lineWidth: 1)
                )
            }
            .buttonStyle(.plain)

            HStack {
                Button {
                    if isPKG {
                        selectedGameDetails = game
                    } else {
                        emulator.launch(game)
                    }
                } label: {
                    Label(isPKG ? "Infos" : "Ouvrir", systemImage: isPKG ? "info.circle" : "play.fill")
                        .font(.caption.weight(.semibold))
                }

                Spacer()

                Menu {
                    Button {
                        renamedGameTitle = game.name
                        gameToRename = game
                    } label: {
                        Label("Renommer", systemImage: "pencil")
                    }
                    Button {
                        emulator.inspect(game)
                        inspectionReport = emulator.status
                    } label: {
                        Label(game.fileName.lowercased().hasSuffix(".pkg") ? "Analyser le PKG" : "Analyser l’exécutable", systemImage: "doc.text.magnifyingglass")
                    }
                    if game.fileName.lowercased().hasSuffix(".pkg") {
                        Button {
                            emulator.inspectPKGEntries(game)
                            inspectionReport = emulator.status
                        } label: {
                            Label("Examiner les entrées du PKG", systemImage: "list.bullet.rectangle")
                        }
                        Button {
                            emulator.inspectPKGStructure(game)
                            inspectionReport = emulator.status
                        } label: {
                            Label("Vérifier la structure PKG", systemImage: "checkmark.shield")
                        }
                        Button {
                            emulator.diagnosePKGBoot(game)
                            inspectionReport = emulator.status
                        } label: {
                            Label("Diagnostiquer le démarrage", systemImage: "stethoscope")
                        }
                    }
                    Button(role: .destructive) {
                        gameToDelete = game
                    } label: {
                        Label("Supprimer…", systemImage: "trash")
                    }
                } label: {
                    Image(systemName: "ellipsis.circle")
                        .font(.title3)
                }
            }
            .foregroundStyle(.white.opacity(0.82))
        }
        .frame(width: fixedWidth)
    }

    private var statusCard: some View {
        HStack(spacing: 10) {
            Image(systemName: "info.circle.fill")
                .foregroundStyle(.cyan)
            Text(emulator.status)
                .font(.footnote)
                .foregroundStyle(.white.opacity(0.72))
            Spacer()
        }
        .padding(14)
        .background(.white.opacity(0.04), in: RoundedRectangle(cornerRadius: 16))
    }

    private var legalNotice: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: "info.circle")
                .foregroundStyle(.white.opacity(0.55))

            Text("Utilisez uniquement des fichiers que vous êtes autorisé à utiliser. Aucun jeu, firmware ou contenu PlayStation n’est inclus dans l’app.")
                .font(.caption)
                .foregroundStyle(.white.opacity(0.46))
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var heroPanel: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Label("MAXPS4  /  CONTROL CENTER", systemImage: "sparkles.rectangle.stack.fill")
                    .font(.system(size: 11, weight: .bold, design: .monospaced))
                    .tracking(2)
                    .foregroundStyle(.cyan)
                Spacer()
                Text("MaxPS4")
                    .font(.system(size: 10, weight: .bold, design: .monospaced))
                    .foregroundStyle(.white.opacity(0.55))
            }
            Text("Ta console. Ton univers.")
                .font(.system(size: 34, weight: .black, design: .rounded))
                .foregroundStyle(.white)
            Text("Bibliothèque locale et laboratoire PS4 sur iPhone")
                .font(.subheadline)
                .foregroundStyle(.white.opacity(0.66))
            HStack(spacing: 10) {
                Label("\(emulator.games.count) JEUX", systemImage: "gamecontroller.fill")
                Spacer()
                Label("iOS", systemImage: "iphone.gen3")
            }
            .font(.system(size: 11, weight: .bold, design: .monospaced))
            .foregroundStyle(.cyan)
            HStack(spacing: 8) {
                Circle().fill(emulator.backendReady ? Color.green : Color.orange)
                    .frame(width: 7, height: 7)
                Text(emulator.backendReady ? "Backend connecté" : "Émulation PS4 en développement")
                    .font(.system(size: 11, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white.opacity(0.83))
                Spacer()
                Image(systemName: "waveform.path")
                    .foregroundStyle(.cyan.opacity(0.8))
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 10)
            .background(.black.opacity(0.24), in: RoundedRectangle(cornerRadius: 12))
        }
        .padding(22)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            LinearGradient(colors: [
                Color(red: 0.07, green: 0.12, blue: 0.28),
                Color(red: 0.19, green: 0.07, blue: 0.34),
                Color(red: 0.03, green: 0.06, blue: 0.17)
            ], startPoint: .topLeading, endPoint: .bottomTrailing),
            in: RoundedRectangle(cornerRadius: 28)
        )
        .overlay(RoundedRectangle(cornerRadius: 28)
            .stroke(LinearGradient(colors: [.cyan.opacity(0.8), .purple.opacity(0.65)], startPoint: .topLeading, endPoint: .bottomTrailing), lineWidth: 1.3))
        .shadow(color: .purple.opacity(0.22), radius: 20, y: 8)
    }

    private var toolsPage: some View {
        ScrollView(showsIndicators: false) {
            VStack(alignment: .leading, spacing: 20) {
                Text("OUTILS")
                    .font(.system(size: 36, weight: .black, design: .rounded))
                Text("CENTRE DE CONTRÔLE")
                    .font(.system(size: 12, weight: .bold, design: .monospaced))
                    .tracking(2).foregroundStyle(.cyan)
                VStack(alignment: .leading, spacing: 9) {
                    Text("APPAREIL iOS")
                        .font(.caption.weight(.bold))
                        .foregroundStyle(.cyan)
                    Text(emulator.deviceDiagnostic)
                        .font(.subheadline)
                        .foregroundStyle(.white.opacity(0.82))
                    Text("shadPS4 / FEX : " + (emulator.backendReady ? "connecté" : "non intégré"))
                        .font(.caption)
                        .foregroundStyle(emulator.backendReady ? Color.green : Color.orange)
                }
                .padding(18)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(.white.opacity(0.06), in: RoundedRectangle(cornerRadius: 18))

                Text("JEUX ET STOCKAGE")
                    .font(.system(size: 13, weight: .bold, design: .rounded))
                    .tracking(1.1)
                    .foregroundStyle(.cyan)
                    .padding(.top, 8)
                primaryAction(icon: "square.and.arrow.down.fill", title: "Importer un jeu", subtitle: "Sélectionner un fichier local") {
                    importingGame = true
                }
                secondaryAction(icon: "internaldrive", title: "Gestion du stockage", subtitle: "\(emulator.games.count) fichiers enregistrés localement") {
                    selectedTab = .games
                }
                Text("ÉTAT DU MOTEUR")
                    .font(.system(size: 13, weight: .bold, design: .rounded))
                    .tracking(1.1)
                    .foregroundStyle(.cyan)
                    .padding(.top, 8)
                secondaryAction(icon: "checkmark.shield", title: "Tester le pont C++ shadPS4", subtitle: "Vérifier le composant natif sur iPhone • pas de jeu") {
                    emulator.testShadPS4NativeUtility()
                }
                secondaryAction(icon: "cpu", title: "Diagnostic du moteur", subtitle: "Tester la connexion native") {
                    emulator.testBackend()
                }
                secondaryAction(icon: "check-check", title: "Diagnostic complet (11 tests)", subtitle: "ELF, imports, mémoire, processus, services et bibliothèques") {
                    emulator.runBatchDiagnostics()
                }
                secondaryAction(icon: "doc.text", title: "État du système", subtitle: "Afficher les informations de diagnostic") {
                    emulator.testBackend()
                }
                Text("CHARGEUR ET EXÉCUTION")
                    .font(.system(size: 13, weight: .bold, design: .rounded))
                    .tracking(1.1)
                    .foregroundStyle(.cyan)
                    .padding(.top, 8)
                secondaryAction(icon: "doc.zipper", title: "Tester le chargeur ELF64", subtitle: "Segments simulés • sans exécution PS4") {
                    emulator.testELFLoader()
                }
                secondaryAction(icon: "file-plus", title: "Créer ELF64 de démonstration", subtitle: "Générer un fichier test avec un import libkernel") {
                    emulator.createELFImportDemo()
                }
                secondaryAction(icon: "link", title: "Tester ELF64 + bibliothèques", subtitle: "Chargeur ELF64 et import libkernel simulé") {
                    emulator.testELFLibraryIntegration()
                }
                secondaryAction(icon: "shippingbox", title: "Tester chargement ELF64 → CPU", subtitle: "Segment, mémoire virtuelle, exécution d’un fichier de test") {
                    emulator.testELFGuestRuntime()
                }
                secondaryAction(icon: "memorychip", title: "Banc de test ELF64 + CPU", subtitle: "Exécuter un ELF synthétique en mémoire invitée") {
                    emulator.runELFCPUBench()
                }
                secondaryAction(icon: "play.rectangle.on.rectangle", title: "Démarrer l’environnement virtuel", subtitle: "CPU x86-64, mémoire et processus • programme de test") {
                    emulator.testVirtualRuntime()
                }
                secondaryAction(icon: "point.3.connected.trianglepath.dotted", title: "Tester l’intégration complète", subtitle: "ELF64 → CPU → mémoire • programme synthétique") {
                    emulator.testIntegration()
                }
                MaxPS4MazeGame()
                Text("PROTOTYPES CPU, MÉMOIRE ET SERVICES")
                    .font(.system(size: 13, weight: .bold, design: .rounded))
                    .tracking(1.1)
                    .foregroundStyle(.cyan)
                    .padding(.top, 8)
                secondaryAction(icon: "cpu", title: "Tester le CPU x86-64", subtitle: "Instructions expérimentales • sans jeu PS4") {
                    emulator.testCPUPrototype()
                }
                secondaryAction(icon: "waveform.path.ecg", title: "Tracer le CPU", subtitle: "3 instructions synthétiques • historique limité") {
                    emulator.traceCPUPrototype()
                }
                secondaryAction(icon: "square.stack.3d.up", title: "Carte mémoire invitée", subtitle: "Régions et permissions • simulation") {
                    emulator.showGuestMemoryMap()
                }
                secondaryAction(icon: "memorychip", title: "Tester la mémoire invitée", subtitle: "Prototype isolé • sans exécution PS4") {
                    emulator.testGuestMemory()
                }
                secondaryAction(icon: "memory-stick", title: "Tester mémoire virtuelle PS4 (prototype)", subtitle: "Allocation, lecture, protection et libération simulées") {
                    emulator.testPS4VirtualMemoryService()
                }
                secondaryAction(icon: "layers", title: "Tester mémoire et processus isolés", subtitle: "Deux processus, allocations indépendantes et libération") {
                    emulator.testProcessMemoryIsolation()
                }
                secondaryAction(icon: "library", title: "Tester bibliothèques système PS4", subtitle: "Résolution des symboles libkernel simulés") {
                    emulator.testPS4LibraryResolver()
                }
                secondaryAction(icon: "shield-check", title: "Tester compatibilité PS4 (base)", subtitle: "Symboles système et refus des appels non implémentés") {
                    emulator.testPS4CompatibilityScaffold()
                }
                secondaryAction(icon: "cpu", title: "Tester les services système simulés", subtitle: "Appels virtuels, mémoire et refus des appels inconnus") {
                    emulator.testSimulatedKernelServices()
                }
                statusCard
            }
            .padding(.horizontal, 20).padding(.top, 20)
        }
    }

    private var tabBar: some View {
        HStack(spacing: 0) {
            tabButton(.home, icon: "house.fill", title: "Accueil")
            tabButton(.games, icon: "square.grid.2x2.fill", title: "Jeux")
            tabButton(.tools, icon: "slider.horizontal.3", title: "Outils")
            tabButton(.settings, icon: "gearshape.fill", title: "Réglages")
        }
        .padding(.horizontal, 9)
        .padding(.vertical, 11)
        .background(Color(red: 0.035, green: 0.055, blue: 0.14).opacity(0.98),
                    in: RoundedRectangle(cornerRadius: 24))
        .overlay(RoundedRectangle(cornerRadius: 24)
                    .stroke(.cyan.opacity(0.35), lineWidth: 1))
        .shadow(color: .cyan.opacity(0.17), radius: 17, y: -3)
        .padding(.horizontal, 12)
        .padding(.bottom, 4)
    }

    private func tabButton(_ tab: MaxPS4Tab, icon: String, title: String) -> some View {
        Button {
            withAnimation(.easeInOut(duration: 0.22)) { selectedTab = tab }
        } label: {
            VStack(spacing: 6) {
                Image(systemName: icon)
                    .font(.system(size: 19, weight: .semibold))
                    .frame(height: 23)
                Text(title)
                    .font(.system(size: 10, weight: .bold, design: .rounded))
            }
            .foregroundStyle(selectedTab == tab ? Color.cyan : Color.white.opacity(0.52))
            .frame(maxWidth: .infinity)
            .padding(.vertical, 7)
            .background(selectedTab == tab ? Color.cyan.opacity(0.12) : Color.clear,
                        in: RoundedRectangle(cornerRadius: 16))
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(selectedTab == tab ? .isSelected : [])
    }

    private func settingsCard<Content: View>(
        @ViewBuilder content: () -> Content
    ) -> some View {
        VStack(spacing: 0) {
            content()
        }
        .padding(.horizontal, 16)
        .background(.white.opacity(0.05), in: RoundedRectangle(cornerRadius: 22))
        .overlay(
            RoundedRectangle(cornerRadius: 22)
                .stroke(.white.opacity(0.07), lineWidth: 1)
        )
    }

    private func settingLabel(icon: String, title: String) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .frame(width: 28)
                .foregroundStyle(.cyan)
            Text(title)
        }
    }

    private func settingInfoRow(
        icon: String,
        title: String,
        value: String,
        color: Color
    ) -> some View {
        HStack(spacing: 12) {
            settingLabel(icon: icon, title: title)
            Spacer()
            Text(value)
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(color)
        }
        .padding(.vertical, 16)
    }

    private func settingButton(
        icon: String,
        title: String,
        color: Color = .white
    ) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .frame(width: 28)
            Text(title)
            Spacer()
            Image(systemName: "chevron.right")
                .font(.caption)
                .foregroundStyle(.white.opacity(0.40))
        }
        .foregroundStyle(color)
        .padding(.vertical, 16)
    }
}

private struct MaxPS4Background: View {
    var body: some View {
        ZStack {
            LinearGradient(
                colors: [
                    Color(red: 0.025, green: 0.018, blue: 0.085),
                    Color(red: 0.055, green: 0.025, blue: 0.17),
                    .black
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .ignoresSafeArea()

            Circle()
                .fill(.purple.opacity(0.24))
                .frame(width: 360, height: 360)
                .blur(radius: 80)
                .offset(x: 180, y: -300)

            Circle()
                .fill(.cyan.opacity(0.08))
                .frame(width: 260, height: 260)
                .blur(radius: 70)
                .offset(x: -180, y: 280)
        }
    }
}
