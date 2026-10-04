#!/usr/bin/env bash
set -euo pipefail
ROOT="${1:-.}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Apply the exact Vulkan presenter instrumentation before the engine is compiled.
python3 "$SCRIPT_DIR/patch-presenter-diagnostics.py" "$ROOT"

warn(){ printf '::warning::%s\n' "$*"; }
scan(){
  local label="$1" regex="$2" out count=0
  out=$(grep -RInE --exclude-dir=.git --exclude-dir=build --exclude-dir=docs --exclude-dir=tests --exclude='*.md' --exclude='*.min.js' "$regex" "$ROOT" 2>/dev/null || true)
  if [[ -n "$out" ]]; then
    warn "$label"
    while IFS= read -r line; do printf '%s\n' "$line"; (( ++count >= 200 )) && break; done <<< "$out"
  fi
}

OVERLAY="$ROOT/AetherPS4-iOS/Sources/LoadingOverlayWindow.swift"
if [[ -f "$OVERLAY" ]]; then
python3 - "$OVERLAY" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1]); s = p.read_text()
needle = '    @Published var syntheticProgress: Double = 0\n'
replacement = needle + '    @Published var diagnosticStage: String = "Starting emulator…"\n'
if 'diagnosticStage:' not in s:
    if needle not in s: raise SystemExit('LoadingOverlay syntheticProgress layout changed')
    s = s.replace(needle, replacement, 1)
old = '''            if self.consoleLoggingEnabled {
                self.refreshLog()
            } else {
                self.syntheticTick()
            }
'''
new = '''            if self.consoleLoggingEnabled {
                self.refreshLog()
            } else {
                self.syntheticTick()
                self.refreshDiagnosticStage()
            }
'''
if 'self.refreshDiagnosticStage()' not in s:
    if old not in s: raise SystemExit('LoadingOverlay timer layout changed')
    s = s.replace(old, new, 1)
anchor = '''    private func refreshLog() {
'''
method = '''    private func refreshDiagnosticStage() {
        guard let documentsURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first else { return }
        let logURL = documentsURL.appendingPathComponent("aether_crash.log")
        guard let handle = try? FileHandle(forReadingFrom: logURL) else {
            diagnosticStage = "Waiting for emulator log…"
            return
        }
        defer { try? handle.close() }
        let fileSize = (try? handle.seekToEnd()) ?? 0
        let maxBytes: UInt64 = 8 * 1024
        let start = fileSize > maxBytes ? fileSize - maxBytes : 0
        try? handle.seek(toOffset: start)
        guard let data = try? handle.readToEnd() else { return }
        let text = String(decoding: data, as: UTF8.self)
        let stages: [(String, String)] = [
            ("MAXPS4_FRAME_DIAG id=0 stage=present_end", "Vulkan present completed"),
            ("MAXPS4_FRAME_DIAG id=0 stage=present_begin", "Vulkan present started — waiting for display"),
            ("MAXPS4_FRAME_DIAG id=0 stage=flush_end", "GPU submission completed — starting present"),
            ("MAXPS4_FRAME_DIAG id=0 stage=flush_begin", "Submitting frame to GPU"),
            ("MAXPS4_FRAME_DIAG id=0 stage=imgui_new_frame_end", "Frame UI prepared — recording GPU commands"),
            ("MAXPS4_FRAME_DIAG id=0 stage=reset_fence_end", "Frame fence reset completed"),
            ("At the Title Screen", "Title screen reached — waiting for first rendered frame"),
            ("FRAME_SLOT_ACQUIRE", "Renderer active — frame slot acquired"),
            ("GET_RENDER_FRAME_WAIT", "Renderer active — waiting for game frame"),
            ("InitHLELibs: Initializing HLE libraries", "Initializing PS4 HLE libraries"),
            ("TryOpenSDLControllers", "Initializing controllers"),
            ("SDL Vulkan window creation returned", "Vulkan window created — initializing renderer"),
            ("Creating SDL Vulkan window", "Creating Vulkan rendering window"),
            ("SDL video subsystem initialized", "SDL video initialized")
        ]
        for (marker, label) in stages where text.contains(marker) {
            diagnosticStage = label
            return
        }
        if !text.isEmpty { diagnosticStage = "Emulator running — waiting for a known boot milestone" }
    }

'''
if 'private func refreshDiagnosticStage()' not in s:
    if anchor not in s: raise SystemExit('LoadingOverlay refreshLog layout changed')
    s = s.replace(anchor, method + anchor, 1)
percent = '''                    Text("\\(Int(progress * 100))%")
                        .font(.system(size: 12, weight: .medium))
                        .foregroundColor(.white.opacity(0.6))
'''
percent_new = percent + '''                    Text(state.consoleLoggingEnabled ? "Boot milestone estimate" : state.diagnosticStage)
                        .font(.system(size: 10, weight: .regular))
                        .foregroundColor(.white.opacity(0.55))
                        .lineLimit(2)
'''
if 'Boot milestone estimate' not in s:
    if percent not in s: raise SystemExit('LoadingOverlay percentage UI layout changed')
    s = s.replace(percent, percent_new, 1)
p.write_text(s)
print('Patched LoadingOverlayWindow.swift with exact Vulkan boot diagnostics')
PY
fi

scan 'Shell/process execution found; verify every call is excluded/replaced on iOS' 'std::system\(|(^|[^A-Za-z_])system\(|popen\(|fork\(|exec(v|ve|vp|vpe|l|le|lp|lpe)?\('
scan 'Linux-only runtime API found; verify iOS guards' 'epoll_(create|create1|ctl|wait|pwait)|eventfd\(|timerfd_|signalfd\(|inotify_'
scan 'Dynamic loading found; verify iOS/static-link strategy' 'dlopen\(|dlsym\(|dlclose\('
scan 'Executable/JIT memory found; verify iOS JIT implementation/entitlements' 'MAP_JIT|PROT_EXEC|pthread_jit_write_protect_np|mprotect\('
scan 'Desktop-only frameworks/UI references found; verify they are not linked into iOS target' 'Qt[0-9]?::|X11|wayland|AppKit|NSApplication'
scan 'CMake runtime probes found; cross compilation must not try to execute target binaries' 'try_run\(|check_cxx_source_runs\(|check_c_source_runs\('
scan 'Host/Linux shared-library path found; verify it is unreachable for the iOS target' '(/usr/local/lib/.*\.so|/usr/lib/.*\.so|libuuid\.so)'
printf 'iOS compatibility audit completed (warnings are non-blocking).\n'
exit 0
