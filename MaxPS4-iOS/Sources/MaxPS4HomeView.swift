import SwiftUI
import UniformTypeIdentifiers

private enum MaxPS4Section: Hashable {
    case home
    case library
    case settings
}

struct MaxPS4HomeView: View {
    @EnvironmentObject private var emulator: MaxPS4Emulator

    @State private var importingGame = false
    @State private var selectedSection: MaxPS4Section = .home

    var body: some View {
        ZStack {
            background

            Group {
                switch selectedSection {
                case .home:
                    homeView
                case .library:
                    libraryView
                case .settings:
                    settingsView
                }
            }
        }
        .preferredColorScheme(.dark)
        .safeAreaInset(edge: .bottom) {
            bottomBar
        }
        .fileImporter(
            isPresented: $importingGame,
            allowedContentTypes: [.data],
            allowsMultipleSelection: false
        ) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                emulator.importGame(from: url)
            case .failure(let error):
                emulator.status = "Import impossible: \(error.localizedDescription)"
            }
        }
    }

    private var homeView: some View {
        ScrollView(showsIndicators: false) {
            VStack(alignment: .leading, spacing: 20) {
                header

                backendStatusCard

                primaryAction(
                    icon: "square.and.arrow.down.fill",
                    title: "Importer un eboot.bin / SELF",
                    subtitle: "Sélectionne un exécutable PS4 autorisé"
                ) {
                    importingGame = true
                }

                secondaryAction(
                    icon: "cpu",
                    title: "Tester le backend shadPS4/FEX",
                    subtitle: "Vérifie le cœur intégré et son état"
                ) {
                    emulator.testBackend()
                }

                stopAction

                VStack(alignment: .leading, spacing: 10) {
                    HStack(spacing: 8) {
                        Image(systemName: "waveform.path.ecg")
                            .foregroundStyle(.cyan)
                        Text("État actuel")
                            .font(.title3.bold())
                    }

                    Text(emulator.status)
                        .font(.footnote.monospaced())
                        .foregroundStyle(.white.opacity(0.72))
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(16)
                        .background(.white.opacity(0.045), in: RoundedRectangle(cornerRadius: 20))
                        .overlay(
                            RoundedRectangle(cornerRadius: 20)
                                .stroke(.blue.opacity(0.20), lineWidth: 1)
                        )
                }

                legalNotice
                Spacer(minLength: 90)
            }
            .padding(.horizontal, 20)
            .padding(.top, 12)
        }
    }

    private var libraryView: some View {
        ScrollView(showsIndicators: false) {
            VStack(alignment: .leading, spacing: 20) {
                HStack {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Jeux")
                            .font(.system(size: 36, weight: .black, design: .rounded))
                        Text("Importe un exécutable pour le lancer")
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

                VStack(spacing: 14) {
                    Image(systemName: "gamecontroller")
                        .font(.system(size: 42))
                        .foregroundStyle(.white.opacity(0.55))

                    Text("Bibliothèque prête")
                        .font(.headline)

                    Text("La version intégrée actuelle lance directement le fichier sélectionné. Cette nouvelle interface conserve ce fonctionnement sans modifier le backend.")
                        .font(.subheadline)
                        .foregroundStyle(.white.opacity(0.58))
                        .multilineTextAlignment(.center)

                    Button {
                        importingGame = true
                    } label: {
                        Label("Importer un jeu", systemImage: "plus.circle.fill")
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(.blue)
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 34)
                .padding(.horizontal, 18)
                .background(.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 24))
                .overlay(
                    RoundedRectangle(cornerRadius: 24)
                        .stroke(
                            .blue.opacity(0.30),
                            style: StrokeStyle(lineWidth: 1, dash: [7, 6])
                        )
                )

                statusMiniCard
                Spacer(minLength: 100)
            }
            .padding(.horizontal, 20)
            .padding(.top, 12)
        }
    }

    private var settingsView: some View {
        ScrollView(showsIndicators: false) {
            VStack(alignment: .leading, spacing: 18) {
                Text("Paramètres")
                    .font(.system(size: 36, weight: .black, design: .rounded))
                    .padding(.top, 12)

                settingsCard {
                    settingsRow(
                        icon: "cpu",
                        title: "Backend",
                        subtitle: "shadPS4 / FEXCore intégré"
                    )

                    Divider().overlay(.white.opacity(0.08))

                    settingsRow(
                        icon: "bolt.fill",
                        title: "JIT",
                        subtitle: "État détaillé affiché sur l’accueil"
                    )

                    Divider().overlay(.white.opacity(0.08))

                    settingsRow(
                        icon: "iphone",
                        title: "Version",
                        subtitle: "MaxPS4 0.8 Integration"
                    )
                }

                settingsCard {
                    Button {
                        emulator.testBackend()
                    } label: {
                        settingButton(
                            icon: "checkmark.circle.fill",
                            title: "Tester le backend"
                        )
                    }

                    Divider().overlay(.white.opacity(0.08))

                    Button {
                        emulator.stop()
                    } label: {
                        settingButton(
                            icon: "stop.circle.fill",
                            title: "Arrêter l’exécution",
                            color: .orange
                        )
                    }
                }

                legalNotice
                statusMiniCard
                Spacer(minLength: 100)
            }
            .padding(.horizontal, 20)
        }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 6) {
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

            Text("iPhone • shadPS4 / FEXCore")
                .font(.subheadline.weight(.medium))
                .foregroundStyle(.white.opacity(0.60))
        }
        .padding(.top, 8)
    }

    private var backendStatusCard: some View {
        HStack(spacing: 14) {
            Circle()
                .fill(.green)
                .frame(width: 14, height: 14)
                .shadow(color: .green.opacity(0.85), radius: 9)

            VStack(alignment: .leading, spacing: 4) {
                Text("Version intégrée chargée")
                    .font(.headline)

                Text("MaxPS4 0.8 • backend natif conservé")
                    .font(.subheadline)
                    .foregroundStyle(.white.opacity(0.62))
            }

            Spacer()

            Image(systemName: "checkmark.circle.fill")
                .font(.title3)
                .foregroundStyle(.green)
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
                        Color(red: 0.00, green: 0.56, blue: 1.00)
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

    private var stopAction: some View {
        Button {
            emulator.stop()
        } label: {
            HStack(spacing: 10) {
                Image(systemName: "stop.circle.fill")
                Text("Arrêter")
                    .fontWeight(.semibold)
                Spacer()
            }
            .foregroundStyle(.orange)
            .padding(.horizontal, 16)
            .padding(.vertical, 13)
            .background(.orange.opacity(0.08), in: RoundedRectangle(cornerRadius: 16))
        }
        .buttonStyle(.plain)
    }

    private var statusMiniCard: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: "info.circle.fill")
                .foregroundStyle(.cyan)

            Text(emulator.status)
                .font(.footnote)
                .foregroundStyle(.white.opacity(0.70))
                .fixedSize(horizontal: false, vertical: true)

            Spacer()
        }
        .padding(14)
        .background(.white.opacity(0.04), in: RoundedRectangle(cornerRadius: 16))
    }

    private var legalNotice: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: "info.circle")
                .foregroundStyle(.white.opacity(0.55))

            Text("Utilisez uniquement des fichiers que vous êtes autorisé à utiliser. Aucun jeu, firmware, clé ou contenu PlayStation n’est inclus dans l’app.")
                .font(.caption)
                .foregroundStyle(.white.opacity(0.46))
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var bottomBar: some View {
        HStack {
            tabButton(.home, icon: "house.fill", title: "Accueil")
            Spacer()
            tabButton(.library, icon: "gamecontroller.fill", title: "Jeux")
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

    private func tabButton(
        _ section: MaxPS4Section,
        icon: String,
        title: String
    ) -> some View {
        Button {
            withAnimation(.easeInOut(duration: 0.18)) {
                selectedSection = section
            }
        } label: {
            VStack(spacing: 5) {
                Image(systemName: icon)
                    .font(.title3)
                Text(title)
                    .font(.caption2.weight(.semibold))
            }
            .foregroundStyle(
                selectedSection == section
                    ? Color.cyan
                    : Color.white.opacity(0.55)
            )
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

    private func settingsRow(
        icon: String,
        title: String,
        subtitle: String
    ) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .frame(width: 28)
                .foregroundStyle(.cyan)

            VStack(alignment: .leading, spacing: 3) {
                Text(title)
                Text(subtitle)
                    .font(.caption)
                    .foregroundStyle(.white.opacity(0.50))
            }

            Spacer()
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

    private var background: some View {
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
