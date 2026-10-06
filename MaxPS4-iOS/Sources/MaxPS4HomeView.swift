import SwiftUI
import UniformTypeIdentifiers

private enum MaxPS4Tab: Hashable {
    case home
    case games
    case settings
}

struct MaxPS4HomeView: View {
    @EnvironmentObject private var emulator: MaxPS4Emulator

    @State private var selectedTab: MaxPS4Tab = .home
    @State private var importingGame = false

    @AppStorage("showFPS") private var showFPS = false
    @AppStorage("networkEnabled") private var networkEnabled = false

    var body: some View {
        ZStack {
            MaxPS4Background()

            Group {
                switch selectedTab {
                case .home:
                    home
                case .games:
                    library
                case .settings:
                    settings
                }
            }
        }
        .preferredColorScheme(.dark)
        .safeAreaInset(edge: .bottom) {
            tabBar
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

                backendCard

                primaryAction(
                    icon: "square.and.arrow.down.fill",
                    title: "Importer un eboot.bin / SELF",
                    subtitle: "Ajoute un fichier à votre bibliothèque"
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
                        Text("Jeux")
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

                if emulator.games.isEmpty {
                    emptyLibrary
                        .padding(.top, 24)
                } else {
                    LazyVGrid(
                        columns: [
                            GridItem(.flexible(), spacing: 14),
                            GridItem(.flexible(), spacing: 14)
                        ],
                        spacing: 18
                    ) {
                        ForEach(emulator.games) { game in
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

    private var settings: some View {
        ScrollView(showsIndicators: false) {
            VStack(alignment: .leading, spacing: 18) {
                Text("Paramètres")
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
                        settingLabel(icon: "speedometer", title: "Afficher les FPS")
                    }
                    .tint(.blue)
                    .padding(.vertical, 14)

                    Divider().overlay(.white.opacity(0.08))

                    Toggle(isOn: $networkEnabled) {
                        settingLabel(icon: "network", title: "Réseau")
                    }
                    .tint(.blue)
                    .padding(.vertical, 14)
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
                            colors: [.cyan, .blue],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        )
                    )
            }
            .font(.system(size: 42, weight: .black, design: .rounded))

            Text("iPhone • interface moderne")
                .font(.subheadline.weight(.medium))
                .foregroundStyle(.white.opacity(0.60))
        }
        .padding(.top, 8)
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
        let accent = colors[abs(game.name.hashValue) % colors.count]

        return VStack(alignment: .leading, spacing: 10) {
            Button {
                emulator.launch(game)
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

                        Text(game.name)
                            .font(.headline)
                            .lineLimit(2)
                            .multilineTextAlignment(.leading)

                        Text(game.fileName)
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
                    emulator.launch(game)
                } label: {
                    Label("Ouvrir", systemImage: "play.fill")
                        .font(.caption.weight(.semibold))
                }

                Spacer()

                Menu {
                    Button(role: .destructive) {
                        emulator.remove(game)
                    } label: {
                        Label("Supprimer", systemImage: "trash")
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

    private var tabBar: some View {
        HStack {
            tabButton(.home, icon: "house.fill", title: "Accueil")
            Spacer()
            tabButton(.games, icon: "gamecontroller.fill", title: "Jeux")
            Spacer()
            tabButton(.settings, icon: "gearshape.fill", title: "Paramètres")
        }
        .padding(.horizontal, 32)
        .padding(.vertical, 13)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 28))
        .overlay(
            RoundedRectangle(cornerRadius: 28)
                .stroke(.white.opacity(0.08), lineWidth: 1)
        )
        .padding(.horizontal, 14)
        .padding(.bottom, 4)
    }

    private func tabButton(_ tab: MaxPS4Tab, icon: String, title: String) -> some View {
        Button {
            withAnimation(.easeInOut(duration: 0.18)) {
                selectedTab = tab
            }
        } label: {
            VStack(spacing: 5) {
                Image(systemName: icon)
                    .font(.title3)
                Text(title)
                    .font(.caption2.weight(.semibold))
            }
            .foregroundStyle(selectedTab == tab ? Color.cyan : Color.white.opacity(0.55))
            .frame(minWidth: 64)
        }
        .buttonStyle(.plain)
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
                    Color(red: 0.015, green: 0.035, blue: 0.085),
                    Color(red: 0.005, green: 0.075, blue: 0.18),
                    .black
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .ignoresSafeArea()

            Circle()
                .fill(.blue.opacity(0.18))
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
