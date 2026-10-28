import SwiftUI
import UniformTypeIdentifiers
import UIKit

private enum MaxPS4Tab: Hashable { case home, games, settings }
private struct MaxPS4PlayingFolder: Identifiable { let id = UUID(); let url: URL }

struct MaxPS4HomeView: View {
    @EnvironmentObject private var emulator: MaxPS4Emulator
    @Environment(\.scenePhase) private var scenePhase
    @ObservedObject private var folders = MaxPS4GameFolders.shared
    @State private var playingFolder: MaxPS4PlayingFolder?
    @State private var selectedTab: MaxPS4Tab = .home
    @State private var importingGame = false
    @State private var showingGameDirectory = false
    @State private var showingJIT = false
    @State private var gameSearch = ""
    @State private var gameFilter = 0
    @State private var selectedGameDetails: MaxPS4Game?
    @State private var selectedTitleInfo: MaxPS4PKGInspector.TitleInfo?
    @State private var gameToRename: MaxPS4Game?
    @State private var renamedGameTitle = ""
    @State private var gameToDelete: MaxPS4Game?
    @State private var confirmingClear = false
    @State private var pendingDirectory = false
    @State private var message: String?

    private var filteredGames: [MaxPS4Game] {
        let query = gameSearch.trimmingCharacters(in: .whitespacesAndNewlines)
        let matching = emulator.games.filter { query.isEmpty || $0.name.localizedCaseInsensitiveContains(query) }
        return gameFilter == 1 ? matching.sorted { $0.importedAt > $1.importedAt } : matching
    }
    private var filteredFolders: [MaxPS4GameFolders.Folder] {
        let query = gameSearch.trimmingCharacters(in: .whitespacesAndNewlines)
        return folders.folders.filter { query.isEmpty || $0.name.localizedCaseInsensitiveContains(query) }
    }
    private var filteredLocal: [MaxPS4GameFolders.LocalGame] {
        let query = gameSearch.trimmingCharacters(in: .whitespacesAndNewlines)
        return folders.local.filter { query.isEmpty || $0.name.localizedCaseInsensitiveContains(query) }
    }
    private var libraryEmpty: Bool { emulator.games.isEmpty && folders.folders.isEmpty && folders.local.isEmpty }
    var body: some View {
        ZStack {
            MaxPS4Background()
            Group {
                switch selectedTab {
                case .home: home
                case .games: library
                case .settings: settings
                }
            }
        }
        .preferredColorScheme(.dark)
        .safeAreaInset(edge: .bottom) { tabBar }
        .sheet(isPresented: $showingGameDirectory) { MaxPS4GameDirectoryView() }
        .fullScreenCover(item: $playingFolder) { MaxPS4GameView(directory: $0.url) }
        // Back from the Files app: pick up games copied into MaxPS4's folder.
        .onChange(of: scenePhase) { _, phase in if phase == .active { folders.scanDocuments() } }
        .sheet(isPresented: $showingJIT) { MaxPS4JITSetupView() }
        .sheet(item: $selectedGameDetails, onDismiss: {
            selectedTitleInfo = nil
            if pendingDirectory { pendingDirectory = false; showingGameDirectory = true }
        }) { game in
            gameDetails(game)
        }
        .alert("Renommer le jeu", isPresented: Binding(
            get: { gameToRename != nil }, set: { if !$0 { gameToRename = nil } }
        )) {
            TextField("Nom", text: $renamedGameTitle)
            Button("Annuler", role: .cancel) { gameToRename = nil }
            Button("Enregistrer") {
                if let game = gameToRename { emulator.rename(game, to: renamedGameTitle) }
                gameToRename = nil
            }
        }
        .alert("Supprimer ce fichier ?", isPresented: Binding(
            get: { gameToDelete != nil }, set: { if !$0 { gameToDelete = nil } }
        )) {
            Button("Annuler", role: .cancel) { gameToDelete = nil }
            Button("Supprimer", role: .destructive) {
                if let game = gameToDelete { emulator.remove(game) }
                gameToDelete = nil
            }
        } message: { Text("Seule la copie importée dans MaxPS4 sera supprimée.") }
        .confirmationDialog("Vider la bibliothèque ?", isPresented: $confirmingClear, titleVisibility: .visible) {
            Button("Supprimer les fichiers importés", role: .destructive) { emulator.removeAll() }
        } message: { Text("Les copies importées dans MaxPS4 seront supprimées.") }
        .alert("MaxPS4", isPresented: Binding(
            get: { message != nil }, set: { if !$0 { message = nil } }
        )) { Button("Fermer", role: .cancel) { message = nil } } message: { Text(message ?? "") }
        .fileImporter(isPresented: $importingGame, allowedContentTypes: [.data, .item], allowsMultipleSelection: false) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                let count = emulator.games.count
                emulator.importGame(from: url)
                if emulator.games.count == count { message = emulator.status }
                else { selectedTab = .games }
            case .failure(let error): message = "Import impossible : \(error.localizedDescription)"
            }
        }
    }

    private var home: some View {
        ScrollView(showsIndicators: false) {
            VStack(alignment: .leading, spacing: 20) {
                header
                heroPanel
                primaryAction(icon: "play.fill", title: "Jouer", subtitle: "Choisir le dossier complet de ton jeu") {
                    showingGameDirectory = true
                }.accessibilityIdentifier("maxps4.openGameDirectory")
                secondaryAction(icon: "bolt.fill", title: "Activer le JIT", subtitle: "Préparer MaxPS4 avec StikDebug") {
                    showingJIT = true
                }
                libraryHeader
                if libraryEmpty { emptyLibrary }
                else {
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 14) {
                            ForEach(folders.local.prefix(6)) { game in localCard(game, fixedWidth: 158) }
                            ForEach(folders.folders.prefix(6)) { folder in folderCard(folder, fixedWidth: 158) }
                            ForEach(emulator.games.prefix(6)) { game in gameCard(game, fixedWidth: 158) }
                        }.padding(.vertical, 4)
                    }
                }
                Spacer(minLength: 30)
            }.padding(.horizontal, 20).padding(.top, 12)
        }
    }

    private var settings: some View {
        ScrollView(showsIndicators: false) {
            VStack(alignment: .leading, spacing: 20) {
                Text("RÉGLAGES").font(.system(size: 36, weight: .black, design: .rounded))
                secondaryAction(icon: "bolt.fill", title: "JIT et StikDebug", subtitle: "Activation et état de préparation") {
                    showingJIT = true
                }
                VStack(alignment: .leading, spacing: 14) {
                    Label("Stockage", systemImage: "internaldrive").font(.headline)
                    Text("\(emulator.games.count) fichier(s) importé(s)").foregroundStyle(.white.opacity(0.65))
                    Button("Ouvrir la bibliothèque") { selectedTab = .games }.tint(.cyan)
                    Button("Vider la bibliothèque…", role: .destructive) { confirmingClear = true }
                        .disabled(emulator.games.isEmpty)
                }
                .padding(18).frame(maxWidth: .infinity, alignment: .leading)
                .background(.white.opacity(0.055), in: RoundedRectangle(cornerRadius: 22))
                Text("MaxPS4 · \(Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "") · build \(Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "")")
                    .font(.footnote).foregroundStyle(.white.opacity(0.55))
                Text("Aucun jeu ni firmware n’est inclus. Utilise uniquement tes fichiers autorisés.")
                    .font(.caption).foregroundStyle(.white.opacity(0.45))
            }.padding(20)
        }
    }

    private var emptyLibrary: some View {
        VStack(spacing: 14) {
            Image(systemName: "gamecontroller").font(.system(size: 36)).foregroundStyle(.cyan)
            Text("Ta bibliothèque commence ici").font(.headline)
            Text("Copie le dossier d’un jeu dans « Sur mon iPhone › MaxPS4 › Jeux » avec l’app Fichiers, lance-le depuis son dossier ou importe un fichier.")
                .font(.subheadline).foregroundStyle(.white.opacity(0.58)).multilineTextAlignment(.center)
            Button("Importer un fichier") { importingGame = true }.buttonStyle(.bordered).tint(.cyan)
        }
        .frame(maxWidth: .infinity).padding(.vertical, 30).padding(.horizontal, 16)
        .background(.white.opacity(0.025), in: RoundedRectangle(cornerRadius: 24))
        .overlay(RoundedRectangle(cornerRadius: 24).stroke(.blue.opacity(0.32), style: StrokeStyle(lineWidth: 1, dash: [7,6])))
    }

    private func gameCard(_ game: MaxPS4Game, fixedWidth: CGFloat?) -> some View {
        let colors: [Color] = [.blue, .indigo, .purple, .cyan]
        let accent = colors[game.name.utf8.reduce(0) { ($0 + Int($1)) % colors.count }]
        let isPKG = game.fileName.lowercased().hasSuffix(".pkg")
        return VStack(alignment: .leading, spacing: 10) {
            Button { selectedGameDetails = game } label: {
                ZStack(alignment: .bottomLeading) {
                    LinearGradient(colors: [accent.opacity(0.95), .black], startPoint: .topLeading, endPoint: .bottomTrailing)
                    if isPKG {
                        MaxPS4PKGArtworkView(path: game.localPath, entryID: 0x1200)
                            .scaledToFill().frame(maxWidth: .infinity, maxHeight: .infinity).clipped()
                    }
                    Circle().fill(.white.opacity(0.10)).frame(width: 120, height: 120).offset(x: 58, y: -78)
                    VStack(alignment: .leading, spacing: 6) {
                        Image(systemName: "gamecontroller.fill").font(.title2).foregroundStyle(.white.opacity(0.78))
                        Spacer()
                        Text(game.name).font(.headline).lineLimit(2).multilineTextAlignment(.leading)
                        Group {
                            if isPKG { MaxPS4PKGSubtitleView(path: game.localPath) }
                            else { Text(game.fileName) }
                        }.font(.caption2).lineLimit(1).foregroundStyle(.white.opacity(0.56))
                    }.padding(14)
                }
                .frame(maxWidth: fixedWidth == nil ? .infinity : nil)
                .frame(width: fixedWidth, height: 220).clipShape(RoundedRectangle(cornerRadius: 20))
                .overlay(RoundedRectangle(cornerRadius: 20).stroke(.white.opacity(0.12), lineWidth: 1))
            }.buttonStyle(.plain)
            HStack {
                Button { showingGameDirectory = true } label: {
                    Label("Jouer", systemImage: "play.fill").font(.caption.weight(.semibold))
                }
                Spacer()
                Menu {
                    Button { renamedGameTitle = game.name; gameToRename = game } label: { Label("Renommer", systemImage: "pencil") }
                    Button(role: .destructive) { gameToDelete = game } label: { Label("Supprimer…", systemImage: "trash") }
                } label: { Image(systemName: "ellipsis.circle").font(.title3) }
            }.foregroundStyle(.white.opacity(0.82))
        }.frame(width: fixedWidth)
    }

    private func folderCard(_ folder: MaxPS4GameFolders.Folder, fixedWidth: CGFloat?) -> some View {
        folderCard(name: folder.name, icon: nil, fixedWidth: fixedWidth, play: { play(folder) },
                   remove: { folders.remove(folder) })
    }

    private func localCard(_ game: MaxPS4GameFolders.LocalGame, fixedWidth: CGFloat?) -> some View {
        folderCard(name: game.name, icon: game.icon, fixedWidth: fixedWidth,
                   play: { playingFolder = MaxPS4PlayingFolder(url: game.url) }, remove: nil)
    }

    // `remove` is nil for a game found in MaxPS4's folder: delete it with Files.
    private func folderCard(name: String, icon: URL?, fixedWidth: CGFloat?, play: @escaping () -> Void,
                            remove: (() -> Void)?) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Button(action: play) {
                ZStack(alignment: .bottomLeading) {
                    LinearGradient(colors: [Color.cyan.opacity(0.85), .black], startPoint: .topLeading, endPoint: .bottomTrailing)
                    if let icon, let image = UIImage(contentsOfFile: icon.path) {
                        Image(uiImage: image).resizable().scaledToFill()
                            .frame(maxWidth: .infinity, maxHeight: .infinity).clipped()
                        LinearGradient(colors: [.clear, .black.opacity(0.8)], startPoint: .center, endPoint: .bottom)
                    } else {
                        Circle().fill(.white.opacity(0.10)).frame(width: 120, height: 120).offset(x: 58, y: -78)
                    }
                    VStack(alignment: .leading, spacing: 6) {
                        Image(systemName: "folder.fill").font(.title2).foregroundStyle(.white.opacity(0.78))
                        Spacer()
                        Text(name).font(.headline).lineLimit(2).multilineTextAlignment(.leading)
                        Text("Dossier de jeu").font(.caption2).lineLimit(1).foregroundStyle(.white.opacity(0.56))
                    }.padding(14)
                }
                .frame(maxWidth: fixedWidth == nil ? .infinity : nil)
                .frame(width: fixedWidth, height: 220).clipShape(RoundedRectangle(cornerRadius: 20))
                .overlay(RoundedRectangle(cornerRadius: 20).stroke(.white.opacity(0.12), lineWidth: 1))
            }.buttonStyle(.plain)
            HStack {
                Button(action: play) {
                    Label("Jouer", systemImage: "play.fill").font(.caption.weight(.semibold))
                }
                Spacer()
                if let remove {
                    Menu {
                        Button(role: .destructive, action: remove) {
                            Label("Retirer de la bibliothèque", systemImage: "minus.circle")
                        }
                    } label: { Image(systemName: "ellipsis.circle").font(.title3) }
                }
            }.foregroundStyle(.white.opacity(0.82))
        }.frame(width: fixedWidth)
    }

    private func play(_ folder: MaxPS4GameFolders.Folder) {
        if let url = folders.resolve(folder) { playingFolder = MaxPS4PlayingFolder(url: url) }
        else { message = "Dossier introuvable : il a été déplacé ou supprimé. Choisis-le à nouveau avec Jouer." }
    }

    private func gameDetails(_ game: MaxPS4Game) -> some View {
        NavigationStack {
            ZStack {
                MaxPS4Background()
                ScrollView {
                    VStack(alignment: .leading, spacing: 18) {
                        Text(game.name).font(.title2.bold())
                        if game.fileName.lowercased().hasSuffix(".pkg") {
                            MaxPS4PKGArtworkView(path: game.localPath, entryID: 0x1006)
                                .scaledToFit().frame(maxHeight: 200).clipShape(RoundedRectangle(cornerRadius: 20))
                        }
                        primaryAction(icon: "folder.fill", title: "Choisir le dossier du jeu", subtitle: "Le dossier complet est nécessaire au lancement") {
                            pendingDirectory = true; selectedGameDetails = nil
                        }
                        Text(game.fileName).font(.footnote).foregroundStyle(.secondary).textSelection(.enabled)
                        if let info = selectedTitleInfo { MaxPS4TitleInfoView(info: info) }
                        if let attributes = try? FileManager.default.attributesOfItem(atPath: game.localPath),
                           let size = attributes[.size] as? NSNumber {
                            Text("Taille : " + ByteCountFormatter.string(fromByteCount: size.int64Value, countStyle: .file))
                                .font(.footnote).foregroundStyle(.secondary)
                        }
                        if game.fileName.lowercased().hasSuffix(".pkg") {
                            Text("Un fichier PKG seul ne suffit pas au lancement. Sélectionne le dossier du jeu avec eboot.bin et ses données.")
                                .font(.footnote).foregroundStyle(.secondary)
                            NavigationLink("Extraire un fichier du PKG") { MaxPS4PFSExportView(source: URL(fileURLWithPath: game.localPath)) }
                                .tint(.cyan)
                        }
                    }.padding(20).frame(maxWidth: .infinity, alignment: .leading)
                }
            }
            .navigationTitle("Fiche du jeu").navigationBarTitleDisplayMode(.inline)
            .toolbar { Button("Fermer") { selectedGameDetails = nil } }
            .task(id: game.id) {
                selectedTitleInfo = nil
                guard game.fileName.lowercased().hasSuffix(".pkg") else { return }
                let url = URL(fileURLWithPath: game.localPath)
                let info = await Task.detached(priority: .utility) { MaxPS4PKGInspector.titleInfo(url: url) }.value
                guard !Task.isCancelled, selectedGameDetails?.id == game.id else { return }
                selectedTitleInfo = info
            }
        }.preferredColorScheme(.dark)
    }

    private var tabBar: some View {
        HStack(spacing: 0) {
            tabButton(.home, icon: "house.fill", title: "Accueil")
            tabButton(.games, icon: "square.grid.2x2.fill", title: "Bibliothèque")
            tabButton(.settings, icon: "gearshape.fill", title: "Réglages")
        }
        .padding(.horizontal, 12).padding(.vertical, 11)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 24))
        .overlay(RoundedRectangle(cornerRadius: 24).stroke(.white.opacity(0.10)))
        .padding(.horizontal, 20).padding(.bottom, 8)
    }
    private func tabButton(_ tab: MaxPS4Tab, icon: String, title: String) -> some View {
        Button {
            withAnimation(.easeInOut(duration: 0.22)) { selectedTab = tab }
        } label: {
            VStack(spacing: 5) { Image(systemName: icon).font(.system(size: 18, weight: .semibold)); Text(title).font(.caption2.weight(.semibold)) }
                .frame(maxWidth: .infinity).padding(.vertical, 8)
                .foregroundStyle(selectedTab == tab ? Color.cyan : Color.white.opacity(0.52))
                .background(selectedTab == tab ? Color.cyan.opacity(0.12) : Color.clear, in: RoundedRectangle(cornerRadius: 15))
        }.buttonStyle(.plain).accessibilityAddTraits(selectedTab == tab ? .isSelected : [])
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

            Text("AU-DESSUS DES NUAGES • MAXPS4")
                .font(.subheadline.weight(.medium))
                .foregroundStyle(.white.opacity(0.60))
        }
        .padding(.top, 8)
    }

    private var heroPanel: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Label("MAXPS4  /  ÉDITION NOCTURNE", systemImage: "moon.stars.fill")
                    .font(.system(size: 11, weight: .bold, design: .monospaced))
                    .tracking(2)
                    .foregroundStyle(.cyan)
                Spacer()
                Text("MaxPS4")
                    .font(.system(size: 10, weight: .bold, design: .monospaced))
                    .foregroundStyle(.white.opacity(0.55))
            }
            Text("Dans la légende")
                .font(.system(size: 34, weight: .black, design: .rounded))
                .foregroundStyle(.white)
            Text("Retrouve tes jeux et lance une session.")
                .font(.subheadline)
                .foregroundStyle(.white.opacity(0.66))
            HStack(spacing: 10) {
                Label("\(emulator.games.count) FICHIERS", systemImage: "gamecontroller.fill")
                Spacer()
                Label("iOS", systemImage: "iphone.gen3")
            }
            .font(.system(size: 11, weight: .bold, design: .monospaced))
            .foregroundStyle(.cyan)
            Label("Active le JIT avant de lancer une session", systemImage: "bolt.fill")
                .font(.caption.weight(.medium))
                .foregroundStyle(.white.opacity(0.70))
        }
        .padding(22)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            LinearGradient(colors: [
                Color(red: 0.025, green: 0.045, blue: 0.10),
                Color(red: 0.11, green: 0.075, blue: 0.20),
                Color(red: 0.015, green: 0.02, blue: 0.06)
            ], startPoint: .topLeading, endPoint: .bottomTrailing),
            in: RoundedRectangle(cornerRadius: 28)
        )
        .overlay(RoundedRectangle(cornerRadius: 28)
            .stroke(LinearGradient(colors: [.cyan.opacity(0.8), .purple.opacity(0.65)], startPoint: .topLeading, endPoint: .bottomTrailing), lineWidth: 1.3))
        .shadow(color: .purple.opacity(0.22), radius: 20, y: 8)
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
                Image(systemName: icon)
                    .font(.title2)
                    .frame(width: 52, height: 52)
                    .background(.white.opacity(0.08), in: RoundedRectangle(cornerRadius: 15))

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

                let count = emulator.games.count + folders.folders.count + folders.local.count
                Text(count == 0 ? "Aucun jeu" : "\(count) élément\(count > 1 ? "s" : "")")
                .font(.subheadline)
                .foregroundStyle(.white.opacity(0.58))
            }

            Spacer()

            if !libraryEmpty {
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

    private var library: some View {
        ScrollView(showsIndicators: false) {
            VStack(alignment: .leading, spacing: 18) {
                HStack {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("BIBLIOTHÈQUE")
                            .font(.system(size: 36, weight: .black, design: .rounded))

                        Text("Tes dossiers de jeu et fichiers importés")
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
                    }.accessibilityLabel("Importer un fichier")
                }

                secondaryAction(icon: "folder.fill", title: "Jouer depuis un dossier",
                    subtitle: "Ouvrir un jeu avec ses données") { showingGameDirectory = true }

                HStack(alignment: .firstTextBaseline) {
                    Text("Jeux trouvés dans « Sur mon iPhone › MaxPS4 » : \(folders.local.count) (\(folders.searched) dossiers parcourus). Un jeu est un dossier qui contient eboot.bin.")
                        .font(.caption).foregroundStyle(.white.opacity(0.55))
                    Spacer()
                    Button("Actualiser") { folders.scanDocuments() }.font(.caption.weight(.semibold)).tint(.cyan)
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

                if !filteredFolders.isEmpty || !filteredLocal.isEmpty {
                    LazyVGrid(columns: [GridItem(.flexible(), spacing: 14), GridItem(.flexible(), spacing: 14)], spacing: 18) {
                        ForEach(filteredLocal) { game in localCard(game, fixedWidth: nil) }
                        ForEach(filteredFolders) { folder in folderCard(folder, fixedWidth: nil) }
                    }
                }
                if libraryEmpty {
                    emptyLibrary.padding(.top, 24)
                } else if emulator.games.isEmpty {
                    EmptyView()
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

                Spacer(minLength: 90)
            }
            .padding(.horizontal, 20)
            .padding(.top, 12)
        }
    }

}

struct MaxPS4Background: View {
    var body: some View {
        ZStack {
            LinearGradient(
                colors: [
                    Color(red: 0.012, green: 0.025, blue: 0.07),
                    Color(red: 0.055, green: 0.04, blue: 0.12),
                    .black
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .ignoresSafeArea()

            Circle()
                .fill(.purple.opacity(0.18))
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

/// Card subtitle such as "CUSA12345 • v01.23 • Jeu", read once per file and cached
/// for the session so scrolling the library never rereads the PKG.
private struct MaxPS4PKGSubtitleView: View {
    let path: String
    private static let cache = NSCache<NSString, NSString>()
    @State private var subtitle: String?

    var body: some View {
        Text(subtitle ?? "PKG PS4")
            .task(id: path) {
                let url = URL(fileURLWithPath: path)
                let text = await Task.detached(priority: .utility) { () -> String in
                    // A file replaced under the same name gets a new modification date.
                    let modified = (try? url.resourceValues(forKeys: [.contentModificationDateKey]))?
                        .contentModificationDate?.timeIntervalSince1970 ?? 0
                    let key = "\(url.path)|\(modified)" as NSString
                    if let cached = Self.cache.object(forKey: key) { return cached as String }
                    let info = MaxPS4PKGInspector.titleInfo(url: url)
                    let parts = [info?.titleID, info?.appVersion.map { "v" + $0 }, info?.categoryName].compactMap { $0 }
                    let text = parts.isEmpty ? "PKG PS4" : parts.joined(separator: " • ")
                    Self.cache.setObject(text as NSString, forKey: key)
                    return text
                }.value
                subtitle = text
            }
    }
}

private struct MaxPS4TitleInfoView: View {
    let info: MaxPS4PKGInspector.TitleInfo

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Informations du jeu (param.sfo)")
                .font(.headline)
            row("Titre", info.title)
            row("Identifiant", info.titleID)
            row("Catégorie", info.categoryName)
            row("Version de l’application", info.appVersion)
            row("Version du contenu", info.version)
            row("Content ID", info.contentID)
            Text("Lu depuis le PKG ; authenticité non vérifiée.")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .padding(14)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(.white.opacity(0.08), in: RoundedRectangle(cornerRadius: 14))
    }

    @ViewBuilder
    private func row(_ label: String, _ value: String?) -> some View {
        if let value {
            HStack(alignment: .firstTextBaseline) {
                Text(label).foregroundStyle(.secondary)
                Spacer()
                Text(value).multilineTextAlignment(.trailing).textSelection(.enabled)
            }
            .font(.subheadline)
        }
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
