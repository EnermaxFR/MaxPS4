import SwiftUI
import UniformTypeIdentifiers

struct MaxPS4HomeView: View {
    @EnvironmentObject private var emulator: MaxPS4Emulator
    @State private var importingGame = false

    private let blue = Color(red: 0.0, green: 0.48, blue: 1.0)

    var body: some View {
        ZStack {
            LinearGradient(
                colors: [Color.black, Color(red: 0.01, green: 0.04, blue: 0.10), Color.black],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .ignoresSafeArea()

            ScrollView {
                VStack(alignment: .leading, spacing: 24) {
                    header
                    hero
                    sectionTitle("Mes jeux")
                    emptyLibrary
                    quickActions
                    statusCard
                    legalAndContributions
                }
                .padding(.horizontal, 20)
                .padding(.vertical, 14)
            }
        }
        .preferredColorScheme(.dark)
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

    private var header: some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text("MaxPS4")
                    .font(.system(size: 34, weight: .black, design: .rounded))
                    .foregroundStyle(
                        LinearGradient(colors: [.white, blue], startPoint: .leading, endPoint: .trailing)
                    )
                Text("PLAY BEYOND")
                    .font(.caption2.weight(.bold))
                    .tracking(3)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            Button(action: {}) {
                Image(systemName: "gearshape.fill")
                    .font(.title3)
                    .frame(width: 44, height: 44)
                    .background(.ultraThinMaterial, in: Circle())
            }
            .tint(.white)
        }
    }

    private var hero: some View {
        ZStack(alignment: .bottomLeading) {
            RoundedRectangle(cornerRadius: 28, style: .continuous)
                .fill(
                    RadialGradient(colors: [blue.opacity(0.55), Color(red: 0.01, green: 0.03, blue: 0.08)], center: .topTrailing, startRadius: 10, endRadius: 380)
                )
                .frame(height: 220)
                .overlay(alignment: .topTrailing) {
                    Image(systemName: "gamecontroller.fill")
                        .font(.system(size: 112, weight: .bold))
                        .foregroundStyle(.white.opacity(0.12))
                        .padding(22)
                }
                .overlay {
                    RoundedRectangle(cornerRadius: 28, style: .continuous)
                        .stroke(blue.opacity(0.45), lineWidth: 1)
                }

            VStack(alignment: .leading, spacing: 10) {
                Text("TON UNIVERS PS4")
                    .font(.caption.weight(.bold))
                    .foregroundStyle(.cyan)
                Text("Tes jeux.\nTon iPhone.")
                    .font(.system(size: 30, weight: .bold, design: .rounded))
                Button {
                    importingGame = true
                } label: {
                    Label("Importer un jeu", systemImage: "plus")
                        .font(.headline)
                        .padding(.horizontal, 18)
                        .padding(.vertical, 11)
                }
                .buttonStyle(.borderedProminent)
                .tint(blue)
            }
            .padding(22)
        }
    }

    private func sectionTitle(_ title: String) -> some View {
        HStack {
            Text(title).font(.title2.bold())
            Spacer()
            Text("Bibliothèque")
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(blue)
        }
    }

    private var emptyLibrary: some View {
        HStack(spacing: 16) {
            ZStack {
                RoundedRectangle(cornerRadius: 18)
                    .fill(blue.opacity(0.14))
                    .frame(width: 82, height: 108)
                Image(systemName: "plus.square.dashed")
                    .font(.system(size: 32))
                    .foregroundStyle(blue)
            }
            VStack(alignment: .leading, spacing: 7) {
                Text("Bibliothèque prête")
                    .font(.headline)
                Text("Importe ton premier jeu pour commencer ta collection MaxPS4.")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(16)
        .background(.white.opacity(0.045), in: RoundedRectangle(cornerRadius: 22))
    }

    private var quickActions: some View {
        HStack(spacing: 12) {
            actionCard("Importer", icon: "square.and.arrow.down.fill") { importingGame = true }
            actionCard("Manettes", icon: "gamecontroller.fill") {}
            actionCard("Réglages", icon: "slider.horizontal.3") {}
        }
    }

    private func actionCard(_ title: String, icon: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(spacing: 10) {
                Image(systemName: icon)
                    .font(.title2)
                    .foregroundStyle(blue)
                Text(title)
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.white)
            }
            .frame(maxWidth: .infinity)
            .frame(height: 84)
            .background(.white.opacity(0.055), in: RoundedRectangle(cornerRadius: 18))
        }
    }

    private var statusCard: some View {
        HStack(spacing: 10) {
            Circle()
                .fill(emulator.isRunning ? Color.green : blue)
                .frame(width: 8, height: 8)
            Text(emulator.status)
                .font(.footnote)
                .foregroundStyle(.secondary)
            Spacer()
            Text("shadPS4 core")
                .font(.caption2.monospaced())
                .foregroundStyle(.secondary)
        }
        .padding(14)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 16))
    }

    private var legalAndContributions: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("À propos")
                .font(.title2.bold())

            NavigationLink {
                LicensesView()
            } label: {
                infoRow("Licences", icon: "doc.text.fill")
            }
            .buttonStyle(.plain)

            NavigationLink {
                ContributionsView()
            } label: {
                infoRow("Contributions", icon: "person.3.fill")
            }
            .buttonStyle(.plain)
        }
    }

    private func infoRow(_ title: String, icon: String) -> some View {
        HStack(spacing: 14) {
            Image(systemName: icon)
                .foregroundStyle(blue)
                .frame(width: 28)
            Text(title)
                .font(.headline)
                .foregroundStyle(.white)
            Spacer()
            Image(systemName: "chevron.right")
                .font(.caption.bold())
                .foregroundStyle(.secondary)
        }
        .padding(16)
        .background(.white.opacity(0.055), in: RoundedRectangle(cornerRadius: 18))
    }
}

struct LicensesView: View {
    var body: some View {
        List {
            Section("MaxPS4") {
                Text("MaxPS4 est distribué selon les conditions de sa licence et conserve les mentions de licence applicables à son code et à ses composants.")
            }
            Section("shadPS4") {
                Text("MaxPS4 utilise et adapte des composants du projet open source shadPS4. Les droits d’auteur et conditions de licence de shadPS4 et de ses dépendances restent applicables.")
            }
            Section("Composants open source") {
                Text("Les bibliothèques tierces intégrées conservent leurs licences et avis de droits d’auteur respectifs.")
            }
        }
        .navigationTitle("Licences")
        .navigationBarTitleDisplayMode(.inline)
    }
}

struct ContributionsView: View {
    var body: some View {
        List {
            Section("MaxPS4") {
                Label("Développement et portage iOS", systemImage: "iphone")
                Label("Interface et intégration", systemImage: "hammer.fill")
            }
            Section("Projets open source") {
                Label("shadPS4 — cœur d’émulation", systemImage: "cpu")
                Label("FFmpeg — multimédia", systemImage: "film")
                Label("SDL — plateforme et contrôleurs", systemImage: "gamecontroller")
                Label("Mesa / Vulkan — rendu graphique", systemImage: "sparkles")
            }
            Section {
                Text("Merci aux développeurs et contributeurs des projets open source qui rendent MaxPS4 possible.")
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle("Contributions")
        .navigationBarTitleDisplayMode(.inline)
    }
}
