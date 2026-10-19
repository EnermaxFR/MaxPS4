import SwiftUI
import UniformTypeIdentifiers
import UIKit

private enum MaxPS4Tab: String, CaseIterable, Identifiable, Hashable {
    case home, games, tools, developer, controllers, settings
    var id: String { rawValue }
    var title: String {
        switch self {
        case .home: return "Accueil"
        case .games: return "Bibliothèque"
        case .tools: return "Import et outils"
        case .developer: return "Diagnostics"
        case .controllers: return "Manettes"
        case .settings: return "Paramètres"
        }
    }
    var symbol: String {
        switch self {
        case .home: return "house"
        case .games: return "square.grid.2x2"
        case .tools: return "square.and.arrow.down"
        case .developer: return "waveform.path.ecg"
        case .controllers: return "gamecontroller"
        case .settings: return "gearshape"
        }
    }
}

struct MaxPS4HomeView: View {
    @EnvironmentObject private var emulator: MaxPS4Emulator

    @State private var selectedTab: MaxPS4Tab? = .games
    @State private var columnVisibility: NavigationSplitViewVisibility = .automatic
    @State private var confirmingLibraryRemoval = false
    @State private var importingGame = false
    @State private var gameSearch = ""
    @AppStorage("maxps4.library.layout") private var libraryLayout: MaxPS4LibraryLayout = .grid
    @AppStorage("maxps4.library.sort") private var librarySort: MaxPS4LibrarySort = .name
    @AppStorage("maxps4.appearance") private var appearance: MaxPS4Appearance = .system
    @State private var gameToRename: MaxPS4Game?
    @State private var gameToDelete: MaxPS4Game?
    @State private var showingDuplicates = false
    @State private var comparisonReport: String?
    @State private var comparingPKGs = false
    @State private var renamedGameTitle = ""
    @State private var inspectionReport: String?
    @State private var selectedGameDetails: MaxPS4Game?
    @State private var detailsReport: String?
    @State private var detailsLaunchSummary: String?
    @State private var jitStatusReport: String?
    @State private var showingFEXDiagnostic = false
    @State private var showingMetalDiagnostic = false
    @State private var showingGameDirectory = false

    @AppStorage("showFPS") private var showFPS = false
    @AppStorage("networkEnabled") private var networkEnabled = false

    private var filteredGames: [MaxPS4Game] {
        MaxPS4LibraryPresentation.results(emulator.games, query: gameSearch, sort: librarySort,
                                         title: \.name, fileName: \.fileName, importedAt: \.importedAt)
    }

    var body: some View {
        analysisAlerts
            .sheet(isPresented: $showingFEXDiagnostic) { MaxPS4FEXDiagnosticView() }
            .sheet(isPresented: $showingMetalDiagnostic) { MaxPS4MetalDiagnosticView() }
            .sheet(isPresented: $showingGameDirectory) { MaxPS4GameDirectoryView() }
            .sheet(isPresented: $showingDuplicates) { duplicatesPanel }
            .sheet(item: $selectedGameDetails, onDismiss: { detailsReport = nil; detailsLaunchSummary = nil }) { game in
                gameDetails(game)
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

    private var navigationShell: some View {
        NavigationSplitView(columnVisibility: $columnVisibility) {
            sidebar
                .navigationSplitViewColumnWidth(min: 220, ideal: 250, max: 300)
        } detail: {
            NavigationStack {
                Group {
                    switch selectedTab ?? .games {
                    case .home: home
                    case .games: library
                    case .tools: toolsPage
                    case .developer: developerPage
                    case .controllers: MaxPS4ControllerView()
                    case .settings: settings
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(MaxPS4Design.background)
                .navigationTitle((selectedTab ?? .games).title)
                .navigationBarTitleDisplayMode(.large)
                .toolbar {
                    if selectedTab != .settings && selectedTab != .controllers {
                        ToolbarItem(placement: .topBarTrailing) {
                            Button("Importer un fichier", systemImage: "plus") { importingGame = true }
                                .accessibilityIdentifier("maxps4.import")
                        }
                    }
                }
            }
        }
        .navigationSplitViewStyle(.balanced)
        .tint(.accentColor)
        .preferredColorScheme(appearance.colorScheme)
    }

    private var clearLibraryAlert: some View {
        navigationShell
        .alert("Vider la bibliothèque ?", isPresented: $confirmingLibraryRemoval) {
            Button("Annuler", role: .cancel) {}
            Button("Supprimer toutes les copies locales", role: .destructive) { emulator.removeAll() }
        } message: {
            Text("Tous les fichiers importés dans MaxPS4 seront supprimés. Cette action est irréversible.")
        }
    }

    private var deleteGameAlert: some View {
        clearLibraryAlert
        .alert("Supprimer ce fichier ?", isPresented: Binding<Bool>(
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
    }

    private var renameGameAlert: some View {
        deleteGameAlert
        .alert("Renommer le jeu", isPresented: Binding<Bool>(
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
    }

    private var analysisAlerts: some View {
        renameGameAlert
        .alert("Résultat de l’analyse", isPresented: Binding<Bool>(
            get: { inspectionReport != nil },
            set: { if !$0 { inspectionReport = nil } }
        )) {
            Button("Fermer") { inspectionReport = nil }
        } message: {
            Text(inspectionReport ?? "")
        }
    }

    private var duplicatesPanel: some View {
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

    private func gameDetails(_ game: MaxPS4Game) -> some View {
            NavigationStack {
                ScrollView {
                    VStack(alignment: .leading, spacing: 16) {
                        gameMetadata(game)
                        gameDetailActions(game)
                        gameDetailReportContent()
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding()
                }
                .task(id: game.id) {
                    detailsLaunchSummary = nil
                    if game.fileName.lowercased().hasSuffix(".pkg") {
                        let url = URL(fileURLWithPath: game.localPath)
                        let summary = await Task.detached(priority: .userInitiated) {
                            MaxPS4PKGInspector.launchSummary(url: url)
                        }.value
                        guard !Task.isCancelled else { return }
                        detailsLaunchSummary = summary
                    }
                }
                .navigationTitle(game.name)
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
            .preferredColorScheme(appearance.colorScheme)
    }

    private func gameMetadata(_ game: MaxPS4Game) -> some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(game.name)
                .font(.title2.bold())
                .lineLimit(2)
                .minimumScaleFactor(0.75)
            ZStack {
                RoundedRectangle(cornerRadius: 20)
                    .fill(LinearGradient(colors: [.blue, .purple, .black], startPoint: .topLeading, endPoint: .bottomTrailing))
                if game.fileName.lowercased().hasSuffix(".pkg") {
                    MaxPS4PKGArtworkView(path: game.localPath, entryID: 0x1006)
                        .scaledToFill()
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .clipped()
                } else {
                    VStack(spacing: 12) {
                        Image(systemName: "gamecontroller.fill").font(.system(size: 48))
                        Text(game.name.uppercased())
                            .font(.title2.bold())
                            .lineLimit(2)
                    }
                    .foregroundStyle(.white)
                }
            }
            .frame(height: 180)
            .clipShape(RoundedRectangle(cornerRadius: 20))
            Text(game.fileName)
                .font(.footnote)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)
            Text(game.fileName.lowercased().hasSuffix(".pkg") ? "Format : PKG PS4" : "Format : ELF / SELF")
            if let attributes = try? FileManager.default.attributesOfItem(atPath: game.localPath),
               let value = attributes[.size] as? NSNumber {
                Text("Taille locale : " + ByteCountFormatter.string(fromByteCount: value.int64Value, countStyle: .file))
            }
            Text("Date d’import : " + game.importedAt.formatted(date: .abbreviated, time: .shortened))
            Text(detailsLaunchSummary ?? emulator.launchReadinessReport(for: game))
                .textSelection(.enabled)
                .foregroundStyle(.orange)
        }
    }

    private func gameDetailActions(_ game: MaxPS4Game) -> some View {
        VStack(alignment: .leading, spacing: 16) {
            Button("Lancer", systemImage: "play.fill") { emulator.launch(game) }
                .buttonStyle(.borderedProminent)
                .disabled(!MaxPS4LibraryPresentation.canRequestLaunch(fileName: game.fileName, backendReady: emulator.backendReady))
            if !MaxPS4LibraryPresentation.canRequestLaunch(fileName: game.fileName, backendReady: emulator.backendReady) {
                Text("Lancement indisponible : consultez le diagnostic ci-dessous.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Button("Essayer le moteur (diagnostic)") {
                emulator.tryGameEngine(game)
                detailsReport = emulator.status
            }
            .buttonStyle(.borderedProminent)
            Button(game.fileName.lowercased().hasSuffix(".pkg") ? "Analyser PKG/PFS" : "Analyser l’exécutable") {
                if game.fileName.lowercased().hasSuffix(".pkg") {
                    detailsReport = "Analyse du PKG en cours…"
                    Task { detailsReport = await emulator.analyzePKG(game) }
                } else {
                    emulator.inspect(game)
                    detailsReport = emulator.status
                }
            }
            .buttonStyle(.borderedProminent)
            if game.fileName.lowercased().hasSuffix(".elf") || game.fileName.lowercased() == "eboot.bin" {
                Button("Diagnostiquer le premier lancement PS4") {
                    emulator.diagnoseFirstPS4Launch(game)
                    detailsReport = emulator.status
                }
                .buttonStyle(.borderedProminent)
                Button("Inspecter les imports ELF64") {
                    emulator.inspectELFImports(game)
                    detailsReport = emulator.status
                }
                .buttonStyle(.bordered)
            }
            if game.fileName.lowercased().hasSuffix(".pkg") {
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
            if game.fileName.lowercased().hasSuffix(".pkg"),
               detailsReport?.contains("Diagnostic PKG/PFS : PFS non chiffré accessible") == true {
                NavigationLink("Extraire eboot.bin / fichier PFS accessible…") {
                    MaxPS4PFSExportView(source: URL(fileURLWithPath: game.localPath))
                }
            }
            if game.fileName.lowercased().hasSuffix(".pkg"),
               detailsReport?.contains("Extraction eboot.bin : impossible depuis ce PKG") == true {
                Label("eboot.bin inaccessible : PFS chiffré ou opaque. Aucun fichier exécutable ne peut être exporté depuis ce PKG.", systemImage: "lock.fill")
                    .font(.footnote)
                    .foregroundStyle(.orange)
            }
        }
    }

    private func gameDetailReportContent() -> some View {
        VStack(alignment: .leading, spacing: 16) {
            if let report = detailsReport {
                ShareLink(item: report) {
                    Label("Exporter le rapport texte", systemImage: "square.and.arrow.up")
                }
                Text(report)
                    .font(.subheadline)
                    .textSelection(.enabled)
                    .padding(16)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(MaxPS4Design.surface, in: RoundedRectangle(cornerRadius: 14))
            }
        }
    }

    private var home: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                heroPanel
                ViewThatFits(in: .horizontal) {
                    HStack(alignment: .top, spacing: 16) { backendCard; jitActivationCard }
                    VStack(spacing: 16) { backendCard; jitActivationCard }
                }
                libraryHeader
                if emulator.games.isEmpty { emptyLibrary }
                else {
                    ScrollView(.horizontal, showsIndicators: false) {
                        LazyHStack(alignment: .top, spacing: 16) {
                            ForEach(emulator.games.sorted { $0.importedAt > $1.importedAt }.prefix(6)) { game in
                                gameCard(game, fixedWidth: 180)
                            }
                        }.padding(.vertical, 4)
                    }
                }
                MaxPS4SectionHeading(title: "Votre espace de travail", subtitle: "Importez vos fichiers et consultez les outils disponibles.")
                secondaryAction(icon: "square.and.arrow.down", title: "Importer un fichier", subtitle: "PKG, ELF, SELF ou eboot.bin") { importingGame = true }
                secondaryAction(icon: "folder", title: "Ouvrir un dossier Orbis", subtitle: "Précontrôle et diagnostic du dossier existant") { showingGameDirectory = true }
                secondaryAction(icon: "gamecontroller", title: "Manettes", subtitle: "Consulter les manettes connectées et les commandes") { selectedTab = .controllers }
                statusCard
                legalNotice
            }
            .frame(maxWidth: 1100)
            .padding(24)
            .frame(maxWidth: .infinity)
        }
    }

    private var library: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                HStack(alignment: .center) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Vos jeux, à portée de main.").font(.title2.weight(.semibold))
                        Text("\(filteredGames.count) sur \(emulator.games.count) fichiers importés")
                            .font(.subheadline).foregroundStyle(.secondary)
                    }
                    Spacer(minLength: 12)
                    Picker("Présentation", selection: $libraryLayout) {
                        ForEach(MaxPS4LibraryLayout.allCases) { layout in
                            Label(layout.title, systemImage: layout.symbol).labelStyle(.iconOnly).tag(layout)
                        }
                    }
                    .pickerStyle(.segmented)
                    .frame(width: 112)
                    .accessibilityIdentifier("maxps4.library.layout")
                }
                ViewThatFits(in: .horizontal) {
                    HStack { librarySortMenu; Spacer(); libraryActions }
                    VStack(alignment: .leading, spacing: 12) { librarySortMenu; libraryActions }
                }
                if emulator.games.isEmpty { emptyLibrary }
                else if filteredGames.isEmpty {
                    ContentUnavailableView.search(text: gameSearch)
                        .accessibilityIdentifier("maxps4.library.noResults")
                } else if libraryLayout == .grid {
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 152, maximum: 240), spacing: 20)], alignment: .leading, spacing: 24) {
                        ForEach(filteredGames) { game in gameCard(game, fixedWidth: nil) }
                    }
                } else {
                    LazyVStack(spacing: 0) {
                        ForEach(filteredGames) { game in
                            gameRow(game)
                            if game.id != filteredGames.last?.id { Divider().padding(.leading, 84) }
                        }
                    }
                    .maxPS4Surface()
                }
                Label("Un fichier importé n’est pas nécessairement exécutable. Consultez sa fiche pour connaître son état.", systemImage: "info.circle")
                    .font(.footnote).foregroundStyle(.secondary)
                statusCard
            }
            .frame(maxWidth: 1280)
            .padding(24)
            .frame(maxWidth: .infinity)
        }
        .searchable(text: $gameSearch, prompt: "Rechercher un titre ou un fichier")
    }

    private var librarySortMenu: some View {
        Menu {
            Picker("Trier par", selection: $librarySort) {
                ForEach(MaxPS4LibrarySort.allCases) { sort in Text(sort.title).tag(sort) }
            }
        } label: {
            Label(librarySort.title, systemImage: "arrow.up.arrow.down")
        }
        .accessibilityIdentifier("maxps4.library.sort")
    }

    private var libraryActions: some View {
        HStack(spacing: 16) {
            Button("Dossier Orbis", systemImage: "folder") { showingGameDirectory = true }
                .accessibilityIdentifier("maxps4.library.orbisDirectory")
            Button("Doublons", systemImage: "square.on.square") { showingDuplicates = true }
                .accessibilityIdentifier("maxps4.library.duplicates")
        }
        .font(.subheadline)
    }

    private var settings: some View {
        Form {
            Section("Apparence") {
                Picker("Thème", selection: $appearance) {
                    ForEach(MaxPS4Appearance.allCases) { theme in Text(theme.title).tag(theme) }
                }
                .accessibilityIdentifier("maxps4.settings.appearance")
                Picker("Présentation de la bibliothèque", selection: $libraryLayout) {
                    ForEach(MaxPS4LibraryLayout.allCases) { layout in Text(layout.title).tag(layout) }
                }
                Picker("Tri de la bibliothèque", selection: $librarySort) {
                    ForEach(MaxPS4LibrarySort.allCases) { sort in Text(sort.title).tag(sort) }
                }
            }
            Section {
                LabeledContent("Moteur natif", value: emulator.backendReady ? "Connecté" : "Non connecté")
                Toggle("Afficher les FPS — indisponible", isOn: $showFPS).disabled(true)
                Toggle("Réseau invité — indisponible", isOn: $networkEnabled).disabled(true)
            } header: { Text("Émulation") } footer: {
                Text("L’affichage des FPS et le réseau invité ne sont pas encore reliés au moteur. Leurs anciennes préférences sont conservées.")
            }
            Section("Contrôles") {
                NavigationLink { MaxPS4ControllerView().navigationTitle("Manettes") } label: {
                    Label("Manettes et commandes", systemImage: "gamecontroller")
                }
            }
            Section("Moteur et diagnostics") {
                NavigationLink("Diagnostics avancés du JIT") {
                    ScrollView {
                        jitSettings.padding(24).frame(maxWidth: 900)
                    }
                    .frame(maxWidth: .infinity)
                    .background(MaxPS4Design.background)
                    .navigationTitle("JIT et outils natifs")
                }
                Button("Ouvrir le diagnostic FEXCore") { showingFEXDiagnostic = true }
                Button("Ouvrir le diagnostic Metal") { showingMetalDiagnostic = true }
                Button("Tester l’intégration") {
                    emulator.testBackend()
                    inspectionReport = emulator.status
                }
            }
            Section {
                Button("Gérer les doublons") { showingDuplicates = true }
                Button("Vider la bibliothèque…", role: .destructive) { confirmingLibraryRemoval = true }
                    .disabled(emulator.games.isEmpty)
                    .accessibilityIdentifier("maxps4.settings.clearLibrary")
            } header: { Text("Stockage local") } footer: {
                Text("MaxPS4 conserve une copie locale de vos imports. Les fichiers sources restent à leur emplacement d’origine.")
            }
            Section("À propos") {
                LabeledContent("Version", value: Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "Développement")
                Text("MaxPS4 n’inclut aucun jeu, firmware, clé ou contenu PlayStation.")
                    .font(.footnote).foregroundStyle(.secondary)
                ShareLink(item: emulator.nativeDiagnosticReport) { Label("Partager le rapport de diagnostic", systemImage: "square.and.arrow.up") }
            }
        }
        .formStyle(.grouped)
    }

    private var jitSettings: some View {
        settingsCard {
            settingInfoRow(
                icon: "cpu",
                title: "JIT ARM64",
                value: MaxPS4NativeLinkCheck.isARM64JITReady ? "Prêt" : "Inactif",
                color: MaxPS4NativeLinkCheck.isARM64JITReady ? .green : .orange
            )
            Divider()
            settingInfoRow(
                icon: "ant.fill",
                title: "Protocole StikDebug",
                value: MaxPS4NativeLinkCheck.isStikDebugProtocolPresent ? "Intégré" : "Absent",
                color: MaxPS4NativeLinkCheck.isStikDebugProtocolPresent ? .cyan : .orange
            )
            Divider()
            Button {
                jitStatusReport = MaxPS4NativeLinkCheck.jitStatusReport
            } label: {
                settingButton(icon: "checkmark.shield.fill", title: "Vérifier l’état du JIT")
            }
            Divider()
            Button {
                jitStatusReport = MaxPS4NativeLinkCheck.stikDebugSafePreflightReport
            } label: {
                settingButton(icon: "shield.lefthalf.filled", title: "Tester StikDebug sans risque")
            }
            Divider()
            Button {
                jitStatusReport = MaxPS4NativeLinkCheck.arm64PipelineSelfTestReport
            } label: {
                settingButton(icon: "cpu.fill", title: "Tester le pipeline ARM64")
            }
            Divider()
            Button {
                jitStatusReport = MaxPS4NativeLinkCheck.jitSafetyGateReport
            } label: {
                settingButton(icon: "lock.shield", title: "Contrôler la sécurité du JIT")
            }
            Divider()
            Button {
                jitStatusReport = MaxPS4NativeLinkCheck.arm64VerifierAdversarialReport
            } label: {
                settingButton(icon: "checkmark.shield", title: "Tester les limites du code ARM64")
            }
            Divider()
            Button {
                jitStatusReport = MaxPS4NativeLinkCheck.jitMemoryPreparationReport
            } label: {
                settingButton(icon: "memorychip", title: "Tester la mémoire du futur JIT")
            }
            Divider()
            Button {
                jitStatusReport = MaxPS4NativeLinkCheck.jitARM64StagingReport
            } label: {
                settingButton(icon: "memorychip.fill", title: "Préparer un bloc ARM64 en mémoire")
            }
            Divider()
            Button {
                jitStatusReport = MaxPS4NativeLinkCheck.jitBlockIntegrityReport
            } label: {
                settingButton(icon: "checkmark.seal", title: "Vérifier l’intégrité des blocs ARM64")
            }
            Divider()
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
                .foregroundStyle(.secondary)
            Divider()
            Button {
                jitStatusReport = MaxPS4NativeLinkCheck.jitDualMappingReport
            } label: {
                settingButton(icon: "square.on.square", title: "Tester la double mémoire JIT (RW)")
            }
            Divider()
            Button {
                jitStatusReport = MaxPS4NativeLinkCheck.aetherGuestBackendReport
            } label: {
                settingButton(icon: "cpu", title: "Tester le backend CPU natif")
            }
            Divider()
            Button {
                jitStatusReport = MaxPS4NativeLinkCheck.aetherJITABIReport
            } label: {
                settingButton(icon: "memorychip", title: "Vérifier l’interface JIT native")
            }
            Divider()
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
                Divider()
                Text(jitStatusReport)
                    .font(.footnote.monospaced())
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)
                    .padding(.vertical, 10)
            }
        }
    }

    private var jitActivationCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("Préparer une session", systemImage: "bolt.shield").font(.headline)
            Text("Vérifiez le JIT et le runtime avant un test sur l’appareil.")
                .font(.subheadline).foregroundStyle(.secondary)
            Button("Vérifier le JIT / FEXCore", systemImage: "arrow.up.right") { showingFEXDiagnostic = true }
                .buttonStyle(.bordered)
                .accessibilityIdentifier("maxps4.jitProminentDiagnostic")
            Text("L’activation du JIT nécessite un outil externe compatible.")
                .font(.caption).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(20).maxPS4Surface()
    }

    private var backendCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("État de l’émulateur", systemImage: "cpu").font(.headline)
            Label(emulator.backendReady ? "Moteur natif connecté" : "Moteur de jeu non connecté",
                  systemImage: emulator.backendReady ? "checkmark.circle.fill" : "circle.dotted")
                .font(.subheadline).foregroundStyle(emulator.backendReady ? Color.green : Color.orange)
            Text("La présence d’un fichier dans la bibliothèque ne garantit pas son lancement.")
                .font(.caption).foregroundStyle(.secondary)
            Button("Consulter les diagnostics") { selectedTab = .developer }
                .buttonStyle(.bordered)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(20).maxPS4Surface()
    }

    private func primaryAction(icon: String, title: String, subtitle: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            actionLabel(icon: icon, title: title, subtitle: subtitle)
        }
        .buttonStyle(.plain)
        .padding(18)
        .foregroundStyle(.white)
        .background(Color.accentColor, in: RoundedRectangle(cornerRadius: 18))
    }

    private func secondaryAction(icon: String, title: String, subtitle: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            actionLabel(icon: icon, title: title, subtitle: subtitle)
                .padding(18)
                .maxPS4Surface()
        }
        .buttonStyle(.plain)
        .foregroundStyle(.primary)
    }

    private func actionLabel(icon: String, title: String, subtitle: String) -> some View {
        HStack(spacing: 14) {
            Image(systemName: icon).font(.title3).frame(width: 28)
            VStack(alignment: .leading, spacing: 4) {
                Text(title).font(.headline)
                Text(subtitle).font(.subheadline).opacity(0.7)
            }
            Spacer(minLength: 8)
            Image(systemName: "chevron.right").font(.caption.weight(.semibold)).opacity(0.5)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .contentShape(Rectangle())
    }

    private var libraryHeader: some View {
        HStack {
            VStack(alignment: .leading, spacing: 4) {
                HStack(spacing: 8) {
                    Image(systemName: "gamecontroller.fill")
                        .foregroundStyle(Color.accentColor)
                    Text("Bibliothèque")
                        .font(.title2.bold())
                }

                Text(
                    emulator.games.isEmpty
                        ? "Aucun jeu importé"
                        : "\(emulator.games.count) élément\(emulator.games.count > 1 ? "s" : "")"
                )
                .font(.subheadline)
                .foregroundStyle(.secondary)
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
                    .foregroundStyle(Color.accentColor)
                }
            }
        }
        .padding(.top, 4)
    }

    private var emptyLibrary: some View {
        ContentUnavailableView {
            Label("Votre bibliothèque vous attend", systemImage: "gamecontroller")
        } description: {
            Text("Ajoutez un fichier PKG, ELF, SELF ou eboot.bin pour commencer à l’inspecter.")
        } actions: {
            Button("Importer un fichier", systemImage: "plus") { importingGame = true }
                .buttonStyle(.borderedProminent)
                .accessibilityIdentifier("maxps4.library.emptyImport")
        }
        .padding(.vertical, 32)
        .maxPS4Surface()
    }

    private func gameCard(_ game: MaxPS4Game, fixedWidth: CGFloat?) -> some View {
        let colors: [Color] = [.blue, .indigo, .purple, .cyan]
        let accent = colors[game.name.utf8.reduce(0) { ($0 + Int($1)) % colors.count }]
        let isPKG = game.fileName.lowercased().hasSuffix(".pkg")
        let displayedTitle = game.name

        return VStack(alignment: .leading, spacing: 10) {
            Button {
                selectedGameDetails = game
            } label: {
                ZStack(alignment: .bottomLeading) {
                    LinearGradient(
                        colors: [accent.opacity(0.95), .black],
                        startPoint: .topLeading,
                        endPoint: .bottomTrailing
                    )

                    if isPKG {
                        MaxPS4PKGArtworkView(path: game.localPath, entryID: 0x1200)
                            .scaledToFill()
                            .frame(maxWidth: .infinity, maxHeight: .infinity)
                            .clipped()
                    }

                    LinearGradient(colors: [.clear, .black.opacity(0.85)], startPoint: .top, endPoint: .bottom)

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
                    .foregroundStyle(.white)
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
            .accessibilityLabel("Afficher la fiche de " + game.name)

            HStack {
                Button {
                    selectedGameDetails = game
                } label: {
                    Label("Infos", systemImage: "info.circle")
                        .font(.caption.weight(.semibold))
                        .frame(minHeight: 44)
                }

                Spacer()

                gameActions(game)
            }
            .foregroundStyle(.primary)
        }
        .frame(width: fixedWidth)
    }


    private func gameRow(_ game: MaxPS4Game) -> some View {
        HStack(spacing: 12) {
            Button {
                selectedGameDetails = game
            } label: {
                HStack(spacing: 16) {
                    Image(systemName: "gamecontroller.fill")
                        .font(.title2).foregroundStyle(Color.accentColor)
                        .frame(width: 48, height: 56)
                        .background(Color.accentColor.opacity(0.08), in: RoundedRectangle(cornerRadius: 10))
                    VStack(alignment: .leading, spacing: 5) {
                        Text(game.name).font(.headline).foregroundStyle(.primary).lineLimit(2)
                        Text(game.fileName).font(.caption).foregroundStyle(.secondary).lineLimit(1)
                        Text(game.fileName.lowercased().hasSuffix(".pkg") ? "PKG · Analyse uniquement" : "Exécutable · Admission à vérifier")
                            .font(.caption2).foregroundStyle(.secondary)
                    }
                    Spacer(minLength: 0)
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            gameActions(game)
        }
        .padding(16)
        .accessibilityElement(children: .contain)
    }

    private func gameActions(_ game: MaxPS4Game) -> some View {
        Menu {
            Button("Lancer", systemImage: "play.fill") { emulator.launch(game) }
                .disabled(!MaxPS4LibraryPresentation.canRequestLaunch(fileName: game.fileName, backendReady: emulator.backendReady))
            Button("Renommer", systemImage: "pencil") {
                renamedGameTitle = game.name
                gameToRename = game
            }
            Button("Diagnostic de lancement", systemImage: "stethoscope") {
                inspectionReport = emulator.launchReadinessReport(for: game)
            }
            Button("Analyser le fichier", systemImage: "doc.text.magnifyingglass") {
                if game.fileName.lowercased().hasSuffix(".pkg") {
                    inspectionReport = "Analyse du PKG en cours…"
                    Task { inspectionReport = await emulator.analyzePKG(game) }
                } else {
                    emulator.inspect(game)
                    inspectionReport = emulator.status
                }
            }
            Divider()
            Button("Supprimer…", systemImage: "trash", role: .destructive) { gameToDelete = game }
        } label: {
            Label("Actions pour " + game.name, systemImage: "ellipsis.circle")
                .labelStyle(.iconOnly).font(.title3)
                .frame(minWidth: 44, minHeight: 44)
        }
        .accessibilityIdentifier("maxps4.game.actions." + game.id.uuidString)
    }

    private var statusCard: some View {
        HStack(spacing: 10) {
            Image(systemName: "info.circle.fill")
                .foregroundStyle(Color.accentColor)
            Text(emulator.status)
                .font(.footnote)
                .foregroundStyle(.secondary)
            Spacer()
        }
        .padding(14)
        .background(MaxPS4Design.surface, in: RoundedRectangle(cornerRadius: 16))
    }

    private var legalNotice: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: "info.circle")
                .foregroundStyle(.secondary)

            Text("Utilisez uniquement des fichiers que vous êtes autorisé à utiliser. Aucun jeu, firmware ou contenu PlayStation n’est inclus dans l’app.")
                .font(.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var heroPanel: some View {
        VStack(alignment: .leading, spacing: 16) {
            Label("VOTRE ESPACE MAXPS4", systemImage: "square.stack.3d.up")
                .font(.caption.weight(.semibold)).foregroundStyle(Color.accentColor)
            Text("Une place pour chaque jeu.")
                .font(.largeTitle.weight(.bold))
            Text("Organisez vos fichiers, préparez vos sessions et retrouvez tous vos outils.")
                .font(.body).foregroundStyle(.secondary)
            Button("Ouvrir la bibliothèque", systemImage: "arrow.right") { selectedTab = .games }
                .buttonStyle(.borderedProminent)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(28)
        .maxPS4Surface()
    }

    private var toolsPage: some View {
        ScrollView(showsIndicators: false) {
            VStack(alignment: .leading, spacing: 20) {
                MaxPS4SectionHeading(title: "Import et outils", subtitle: "Les actions disponibles sur cet appareil.")
                Text("CENTRE DE CONTRÔLE")
                    .font(.system(size: 12, weight: .bold, design: .monospaced))
                    .tracking(2).foregroundStyle(Color.accentColor)
                VStack(alignment: .leading, spacing: 9) {
                    Text("APPAREIL iOS")
                        .font(.caption.weight(.bold))
                        .foregroundStyle(Color.accentColor)
                    Text(emulator.deviceDiagnostic)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    Text("Moteur PS4 : " + (emulator.backendReady ? "connecté" : "non intégré"))
                        .font(.caption)
                        .foregroundStyle(emulator.backendReady ? Color.green : Color.orange)
                }
                .padding(18)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(MaxPS4Design.surface, in: RoundedRectangle(cornerRadius: 18))

                Text("JEUX ET STOCKAGE")
                    .font(.subheadline.weight(.semibold))
                    .tracking(1.1)
                    .foregroundStyle(Color.accentColor)
                    .padding(.top, 8)
                primaryAction(icon: "square.and.arrow.down.fill", title: "Importer un jeu", subtitle: "Sélectionner un fichier local") {
                    importingGame = true
                }
                secondaryAction(icon: "folder", title: "Dossier de jeu accessible", subtitle: "Précontrôle eboot.bin / PRX en lecture seule • rapport exportable") {
                    showingGameDirectory = true
                }
                secondaryAction(icon: "internaldrive", title: "Gestion du stockage", subtitle: "\(emulator.games.count) fichiers enregistrés localement") {
                    selectedTab = .games
                }
                Text("ÉTAT DU MOTEUR")
                    .font(.subheadline.weight(.semibold))
                    .tracking(1.1)
                    .foregroundStyle(Color.accentColor)
                    .padding(.top, 8)
                secondaryAction(icon: "triangle.fill", title: "Diagnostic GPU Metal", subtitle: "Triangle animé natif • aucune émulation GPU PS4") {
                    showingMetalDiagnostic = true
                }
                secondaryAction(icon: "cpu.fill", title: "Tester FEXCore", subtitle: "Contexte réel • x86-64 → 42 • backend affiché") {
                    showingFEXDiagnostic = true
                }
                secondaryAction(icon: "checkmark.shield", title: "Tester le pont C++ natif", subtitle: "Vérifier le composant natif sur iPhone • pas de jeu") {
                    emulator.testShadPS4NativeUtility()
                }
                secondaryAction(icon: "cpu", title: "Diagnostic du moteur", subtitle: "Tester la connexion native") {
                    emulator.testBackend()
                }
                secondaryAction(icon: "checkmark.circle", title: "Diagnostic complet (11 tests)", subtitle: "ELF, imports, mémoire, processus, services et bibliothèques") {
                    emulator.runBatchDiagnostics()
                }
                secondaryAction(icon: "doc.text", title: "État du système", subtitle: "Afficher les informations de diagnostic") {
                    emulator.testBackend()
                }
                Text("CHARGEUR ET EXÉCUTION")
                    .font(.subheadline.weight(.semibold))
                    .tracking(1.1)
                    .foregroundStyle(Color.accentColor)
                    .padding(.top, 8)
                secondaryAction(icon: "doc.zipper", title: "Tester le chargeur ELF64", subtitle: "Segments simulés • sans exécution PS4") {
                    emulator.testELFLoader()
                }
                secondaryAction(icon: "doc.badge.plus", title: "Créer ELF64 de démonstration", subtitle: "Générer un fichier test avec un import libkernel") {
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
                    .font(.subheadline.weight(.semibold))
                    .tracking(1.1)
                    .foregroundStyle(Color.accentColor)
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
                secondaryAction(icon: "memorychip", title: "Tester mémoire virtuelle PS4 (prototype)", subtitle: "Allocation, lecture, protection et libération simulées") {
                    emulator.testPS4VirtualMemoryService()
                }
                secondaryAction(icon: "square.3.layers.3d", title: "Tester mémoire et processus isolés", subtitle: "Deux processus, allocations indépendantes et libération") {
                    emulator.testProcessMemoryIsolation()
                }
                secondaryAction(icon: "books.vertical", title: "Tester bibliothèques système PS4", subtitle: "Résolution des symboles libkernel simulés") {
                    emulator.testPS4LibraryResolver()
                }
                secondaryAction(icon: "checkmark.shield", title: "Tester compatibilité PS4 (base)", subtitle: "Symboles système et refus des appels non implémentés") {
                    emulator.testPS4CompatibilityScaffold()
                }
                secondaryAction(icon: "cpu", title: "Tester les services système simulés", subtitle: "Appels virtuels, mémoire et refus des appels inconnus") {
                    emulator.testSimulatedKernelServices()
                }
                statusCard
            }
            .frame(maxWidth: 1000).padding(24).frame(maxWidth: .infinity)
        }
    }

    private var developerPage: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("Diagnostics du moteur")
                    .font(.title2.bold())
                    .foregroundStyle(Color.accentColor)
                Text("Tests sur l'iPhone • aucun jeu PS4 n'est exécuté par ces contrôles.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                        settingsCard {
                    settingInfoRow(icon: "cpu", title: "Pont natif ARM64",
                                   value: "ABI " + String(emulator.nativeABIVersion),
                                   color: emulator.nativeABIVersion == 1 ? .green : .orange)
                    Divider()
                    settingInfoRow(icon: "bolt.slash", title: "JIT FEXCore",
                                   value: "Non opérationnel", color: .orange)
                    Divider()
                    settingInfoRow(icon: "gamecontroller", title: "Exécution PS4",
                                   value: emulator.backendReady ? "Backend prêt" : "Indisponible",
                                   color: emulator.backendReady ? .green : .orange)
                }
                Button {
                    emulator.runNativeCPUCheck()
                } label: {
                    Label("Tester la connexion CPU native", systemImage: "cpu")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .tint(.accentColor)
                Button {
                    showingFEXDiagnostic = true
                } label: {
                    Label("Tester FEXCore", systemImage: "cpu.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .tint(.accentColor)
                .accessibilityIdentifier("maxps4.testFEXCore")
                Button {
                    showingMetalDiagnostic = true
                } label: {
                    Label("GPU Metal / FEX → Metal", systemImage: "cpu.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .tint(.accentColor)
                .accessibilityIdentifier("maxps4.openFEXMetal")
                Button {
                    emulator.status = MaxPS4OnDeviceJITProbe.stikAllocatorPreflightReport()
                } label: {
                    Label("Diagnostiquer StikDebug / mémoire JIT (sans BRK)", systemImage: "memorychip")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .tint(.accentColor)
                Text("Précontrôle uniquement : aucun JIT FEXCore ni jeu PS4 exécuté.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Text("Jeu à diagnostiquer")
                    .font(.headline)
                if emulator.games.isEmpty {
                    Text("Importe un jeu pour afficher son diagnostic de lancement.")
                        .foregroundStyle(.secondary)
                } else {
                    ForEach(emulator.games) { game in
                        Button {
                            emulator.status = emulator.launchReadinessReport(for: game)
                        } label: {
                            HStack {
                                Text(game.name).lineLimit(1)
                                Spacer()
                                Image(systemName: "doc.text.magnifyingglass")
                            }
                            .padding(12)
                        }
                        .buttonStyle(.bordered)
                        .tint(.accentColor)
                    }
                }
                Text("Journal du dernier test")
                    .font(.headline)
                Text(emulator.status)
                    .font(.system(.footnote, design: .monospaced))
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(14)
                    .background(MaxPS4Design.surface, in: RoundedRectangle(cornerRadius: 14))
                Button {
                    UIPasteboard.general.string = emulator.nativeDiagnosticReport
                } label: {
                    Label("Copier le rapport complet", systemImage: "doc.on.doc")
                }
                .buttonStyle(.bordered)
                .tint(.accentColor)
                Text("Les tests CPU sont limités. Un résultat positif ne signifie pas qu'un jeu PS4 peut démarrer.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .frame(maxWidth: 1000).padding(24).frame(maxWidth: .infinity)
        }
    }

    private var sidebar: some View {
        List(selection: $selectedTab) {
            Section("Bibliothèque") {
                sidebarLink(.home)
                sidebarLink(.games)
            }
            Section("Espace de travail") {
                sidebarLink(.tools)
                sidebarLink(.developer)
                sidebarLink(.controllers)
            }
            Section { sidebarLink(.settings) }
        }
        .listStyle(.sidebar)
        .navigationTitle("MaxPS4")
        .safeAreaInset(edge: .bottom) {
            HStack(spacing: 10) {
                Image(systemName: "cpu").foregroundStyle(emulator.backendReady ? Color.green : Color.orange)
                VStack(alignment: .leading, spacing: 3) {
                    Text(emulator.backendReady ? "Moteur connecté" : "Version expérimentale").font(.caption.weight(.semibold))
                    Text("\(emulator.games.count) fichiers locaux").font(.caption2).foregroundStyle(.secondary)
                }
                Spacer(minLength: 0)
            }
            .padding(20)
            .background(.bar)
        }
    }

    private func sidebarLink(_ tab: MaxPS4Tab) -> some View {
        NavigationLink(value: tab) {
            Label(tab.title, systemImage: tab.symbol)
        }
        .accessibilityIdentifier("maxps4.sidebar." + tab.rawValue)
    }

    private func settingsCard<Content: View>(@ViewBuilder content: () -> Content) -> some View {
        VStack(spacing: 0) { content() }
            .padding(.horizontal, 20)
            .maxPS4Surface()
    }

    private func settingLabel(icon: String, title: String) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .frame(width: 28)
                .foregroundStyle(Color.accentColor)
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
        color: Color = .primary
    ) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .frame(width: 28)
            Text(title)
            Spacer()
            Image(systemName: "chevron.right")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .foregroundStyle(color)
        .padding(.vertical, 16)
    }
}

private struct MaxPS4PKGArtworkView: View {
    let path: String
    let entryID: UInt32
    @State private var artwork: UIImage?

    var body: some View {
        Group {
            if let artwork {
                Image(uiImage: artwork).resizable()
            } else {
                Color.clear
            }
        }
        .task(id: path + ":" + String(entryID)) {
            guard let data = MaxPS4PKGInspector.artwork(url: URL(fileURLWithPath: path), entryID: entryID),
                  let image = UIImage(data: data) else {
                artwork = nil
                return
            }
            artwork = image
        }
    }
}
