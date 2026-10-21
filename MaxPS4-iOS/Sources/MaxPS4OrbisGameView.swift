import SwiftUI
import UniformTypeIdentifiers
import GameController

// One Orbis V4 title run by maxps4_game_run (Native/maxps4_game_session.h):
// the user's own extracted title directory (eboot.bin, sce_module/libc.prx,
// sce_module/libSceFios2.prx), read only. Frames are shown as they are
// presented; a Bluetooth controller or the on-screen pad drives the game.
final class MaxPS4OrbisGameSession: ObservableObject {
    @Published var frame: CGImage?
    @Published var lines: [String] = []
    @Published var running = false
    @Published var result: String?
    @Published var presented = 0

    // SCE_PAD_BUTTON_* bits, written on the main thread, read by the game thread.
    private let touchButtons = UnsafeMutablePointer<UInt32>.allocate(capacity: 1)
    private let cancel = UnsafeMutablePointer<Int32>.allocate(capacity: 1)
    private var displaying = false

    init() { touchButtons.pointee = 0; cancel.pointee = 0 }
    deinit { touchButtons.deallocate(); cancel.deallocate() }

    func press(_ bit: UInt32, _ down: Bool) {
        if down { touchButtons.pointee |= bit } else { touchButtons.pointee &= ~bit }
    }

    func stop() { cancel.pointee = 1 }

    func start(directory: URL) {
        guard !running, let names = Bundle.main.path(forResource: "orbis-import-names", ofType: "tsv") else {
            result = "Table des noms d’import absente du bundle."
            return
        }
        running = true; result = nil; lines = []; presented = 0; cancel.pointee = 0
        let thread = Thread { [self] in
            let scoped = directory.startAccessingSecurityScopedResource()
            defer { if scoped { directory.stopAccessingSecurityScopedResource() } }
            var callbacks = maxps4_game_callbacks()
            callbacks.context = Unmanaged.passUnretained(self).toOpaque()
            callbacks.present = { context, width, height, rgba, buttons in
                let session = Unmanaged<MaxPS4OrbisGameSession>.fromOpaque(context!).takeUnretainedValue()
                buttons!.pointee = session.touchButtons.pointee | MaxPS4OrbisGameSession.controllerButtons()
                session.show(width: Int(width), height: Int(height), rgba: rgba!)
                return 0
            }
            callbacks.log = { context, line in
                let session = Unmanaged<MaxPS4OrbisGameSession>.fromOpaque(context!).takeUnretainedValue()
                let text = String(cString: line!)
                DispatchQueue.main.async {
                    session.lines.append(text)
                    if session.lines.count > 300 { session.lines.removeFirst(session.lines.count - 300) }
                }
            }
            let status = directory.path.withCString { dir in
                names.withCString { tsv in maxps4_game_run(dir, tsv, &callbacks, self.cancel) }
            }
            DispatchQueue.main.async {
                self.running = false
                self.result = status == 0 ? "Le jeu s’est terminé normalement." : "Session arrêtée (statut \(status)). Voir le journal."
            }
        }
        thread.stackSize = 64 << 20
        thread.qualityOfService = .userInteractive
        thread.start()
    }

    // Copies the frame; drops it while the previous one is still being shown.
    private func show(width: Int, height: Int, rgba: UnsafePointer<UInt8>) {
        let count = width * height * 4
        let shown: Bool = DispatchQueue.main.sync { displaying }
        if shown { return }
        let data = Data(bytes: rgba, count: count) as CFData
        guard let provider = CGDataProvider(data: data),
              let image = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                                  provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else { return }
        DispatchQueue.main.async {
            self.displaying = true
            self.frame = image
            self.presented += 1
            DispatchQueue.main.async { self.displaying = false }
        }
    }

    // First connected extended gamepad, as SCE_PAD_BUTTON_* bits.
    static func controllerButtons() -> UInt32 {
        guard let pad = GCController.controllers().first?.extendedGamepad else { return 0 }
        var bits: UInt32 = 0
        func set(_ on: Bool, _ bit: UInt32) { if on { bits |= bit } }
        set(pad.buttonA.isPressed, 0x4000); set(pad.buttonB.isPressed, 0x2000)
        set(pad.buttonX.isPressed, 0x8000); set(pad.buttonY.isPressed, 0x1000)
        set(pad.buttonMenu.isPressed, 0x8)
        set(pad.dpad.up.isPressed || pad.leftThumbstick.up.isPressed, 0x10)
        set(pad.dpad.right.isPressed || pad.leftThumbstick.right.isPressed, 0x20)
        set(pad.dpad.down.isPressed || pad.leftThumbstick.down.isPressed, 0x40)
        set(pad.dpad.left.isPressed || pad.leftThumbstick.left.isPressed, 0x80)
        set(pad.leftShoulder.isPressed, 0x400); set(pad.rightShoulder.isPressed, 0x800)
        set(pad.leftTrigger.isPressed, 0x100); set(pad.rightTrigger.isPressed, 0x200)
        return bits
    }
}

struct MaxPS4OrbisGameView: View {
    @Environment(\.dismiss) private var dismiss
    @StateObject private var session = MaxPS4OrbisGameSession()
    @State private var importing = false
    @State private var directory: URL?
    @State private var jitMessage = "Activer d’abord le JIT avec StikDebug, puis revenir ici."

    var body: some View {
        NavigationStack {
            VStack(spacing: 12) {
                if let frame = session.frame {
                    Image(decorative: frame, scale: 1).resizable().interpolation(.none).aspectRatio(contentMode: .fit)
                        .background(Color.black)
                } else {
                    Rectangle().fill(Color.black).aspectRatio(16.0 / 9.0, contentMode: .fit)
                        .overlay(Text(session.running ? "Démarrage…" : "Aucune image").foregroundStyle(.secondary))
                }
                if session.running { pad } else { setup }
                Text(session.result ?? (session.running ? "Images présentées : \(session.presented)" : ""))
                    .font(.footnote).foregroundStyle(.secondary)
                ScrollView {
                    Text(session.lines.suffix(60).joined(separator: "\n"))
                        .font(.system(size: 9, design: .monospaced)).frame(maxWidth: .infinity, alignment: .leading)
                        .textSelection(.enabled)
                }
            }
            .padding()
            .navigationTitle("Jeu Orbis")
            .toolbar {
                if session.running { Button("Arrêter") { session.stop() } }
                else { Button("Fermer") { dismiss() } }
            }
            .fileImporter(isPresented: $importing, allowedContentTypes: [.folder]) { result in
                if case .success(let url) = result { directory = url }
            }
        }
    }

    private var setup: some View {
        VStack(spacing: 8) {
            HStack {
                Button("Activer le JIT") { jitMessage = MaxPS4StikActivation.begin { jitMessage = $0 } }
                    .buttonStyle(.bordered)
                Button("Vérifier") { jitMessage = MaxPS4StikActivation.confirmation() }
                    .buttonStyle(.bordered)
            }
            Text(jitMessage).font(.caption).foregroundStyle(.secondary)
            Button(directory.map { "Dossier : \($0.lastPathComponent)" } ?? "Choisir le dossier du jeu…") { importing = true }
                .buttonStyle(.bordered)
            Text("Dossier extrait de votre propre jeu : eboot.bin et sce_module/ (libc.prx, libSceFios2.prx). Lecture seule.")
                .font(.caption).foregroundStyle(.secondary)
            Button("Lancer") { if let directory { session.start(directory: directory) } }
                .buttonStyle(.borderedProminent).disabled(directory == nil)
        }
    }

    private var pad: some View {
        HStack(spacing: 24) {
            VStack(spacing: 4) {
                padButton("▲", 0x10)
                HStack(spacing: 4) { padButton("◀", 0x80); padButton("▶", 0x20) }
                padButton("▼", 0x40)
            }
            padButton("OPTIONS", 0x8)
            VStack(spacing: 4) {
                padButton("△", 0x1000)
                HStack(spacing: 4) { padButton("□", 0x8000); padButton("○", 0x2000) }
                padButton("✕", 0x4000)
            }
        }
    }

    private func padButton(_ label: String, _ bit: UInt32) -> some View {
        Text(label).font(.headline).frame(minWidth: 44, minHeight: 44)
            .background(Color.secondary.opacity(0.25)).clipShape(RoundedRectangle(cornerRadius: 8))
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { _ in session.press(bit, true) }
                .onEnded { _ in session.press(bit, false) })
    }
}
