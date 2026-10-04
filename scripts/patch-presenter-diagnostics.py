#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: patch-presenter-diagnostics.py <AetherPS4 engine root>")

root = Path(sys.argv[1])
p = root / "src/video_core/renderer_vulkan/vk_presenter.cpp"
s = p.read_text()

# Diagnostic-only patch. Do not alter Vulkan synchronization yet: first identify the
# exact call after FRAME_SLOT_ACQUIRE that stops making progress on the iOS device.
def inject_once(anchor: str, replacement: str, marker: str):
    global s
    if marker in s:
        return
    if anchor not in s:
        raise SystemExit(f"diagnostic anchor changed: {marker}")
    s = s.replace(anchor, replacement, 1)

inject_once(
    "    if (!swapchain.AcquireNextImage()) {\n",
    """    if (trace) {
        LOG_INFO(Render_Vulkan, "MAXPS4_FRAME_DIAG id={} stage=acquire_begin", trace_id);
    }
    if (!swapchain.AcquireNextImage()) {
""",
    "stage=acquire_begin",
)

inject_once(
    "    const auto reset_result = instance.GetDevice().resetFences(frame->present_done);\n",
    """    if (trace) {
        LOG_INFO(Render_Vulkan, "MAXPS4_FRAME_DIAG id={} stage=reset_fence_begin", trace_id);
    }
    const auto reset_result = instance.GetDevice().resetFences(frame->present_done);
    if (trace) {
        LOG_INFO(Render_Vulkan, "MAXPS4_FRAME_DIAG id={} stage=reset_fence_end result={}", trace_id,
                 vk::to_string(reset_result));
    }
""",
    "stage=reset_fence_begin",
)

inject_once(
    "    ImGuiID dockId = ImGui::Core::NewFrame(is_reusing_frame);\n",
    """    if (trace) {
        LOG_INFO(Render_Vulkan, "MAXPS4_FRAME_DIAG id={} stage=imgui_new_frame_begin", trace_id);
    }
    ImGuiID dockId = ImGui::Core::NewFrame(is_reusing_frame);
    if (trace) {
        LOG_INFO(Render_Vulkan, "MAXPS4_FRAME_DIAG id={} stage=imgui_new_frame_end", trace_id);
    }
""",
    "stage=imgui_new_frame_begin",
)

inject_once(
    "    scheduler.Flush(info);\n    if (trace) {\n        LOG_INFO(Render_Vulkan, \"BACHATA_PRESENT_TRACE id={} stage=flush_done\", trace_id);\n",
    """    if (trace) {
        LOG_INFO(Render_Vulkan, "MAXPS4_FRAME_DIAG id={} stage=flush_begin", trace_id);
    }
    scheduler.Flush(info);
    if (trace) {
        LOG_INFO(Render_Vulkan, "MAXPS4_FRAME_DIAG id={} stage=flush_end", trace_id);
        LOG_INFO(Render_Vulkan, "BACHATA_PRESENT_TRACE id={} stage=flush_done", trace_id);
""",
    "stage=flush_begin",
)

inject_once(
    "        const bool presented = swapchain.Present();\n",
    """        if (trace) {
            LOG_INFO(Render_Vulkan, "MAXPS4_FRAME_DIAG id={} stage=present_begin", trace_id);
        }
        const bool presented = swapchain.Present();
        if (trace) {
            LOG_INFO(Render_Vulkan, "MAXPS4_FRAME_DIAG id={} stage=present_end ok={}", trace_id,
                     presented);
        }
""",
    "stage=present_begin",
)

p.write_text(s)

# Make the on-device loading overlay advance on the new exact diagnostic stages, so a
# screenshot is sufficient to tell us the last completed stage without exporting logs.
swift = root / "AetherPS4-iOS/Sources/LoadingOverlayWindow.swift"
t = swift.read_text()
anchor = '("FRAME_SLOT_ACQUIRE presentId=0 ", 0.75),\n'
if "MAXPS4_FRAME_DIAG id=0 stage=reset_fence_end" not in t:
    if anchor not in t:
        raise SystemExit("LoadingOverlay milestone anchor changed")
    t = t.replace(anchor, anchor +
        '        ("MAXPS4_FRAME_DIAG id=0 stage=reset_fence_end", 0.78),\n'
        '        ("MAXPS4_FRAME_DIAG id=0 stage=imgui_new_frame_end", 0.81),\n'
        '        ("MAXPS4_FRAME_DIAG id=0 stage=flush_begin", 0.84),\n'
        '        ("MAXPS4_FRAME_DIAG id=0 stage=flush_end", 0.87),\n'
        '        ("MAXPS4_FRAME_DIAG id=0 stage=present_begin", 0.89),\n'
        '        ("MAXPS4_FRAME_DIAG id=0 stage=present_end", 0.90),\n', 1)
    swift.write_text(t)

print(f"patched {p}")
print(f"patched {swift}")
