# PS4 input to linear Metal display

The engine now resolves `libScePad` Init, Open, Close, GetHandle, Read,
ReadState and GetControllerInformation. One virtual standard port (type 0,
index 0) belongs to local user 1. This removes the Init/Open/ReadState/
GetControllerInformation import group identified in Sonic Mania. It does
not establish that the game boots or renders.

The ABI and NIDs follow the pinned shadPS4
[Pad interface](https://github.com/shadps4-emu/shadPS4/blob/91e6afa69b0a64b0afaa0281b80f8fbb0cd88136/src/core/libraries/pad/pad.h),
[registry](https://github.com/shadps4-emu/shadPS4/blob/91e6afa69b0a64b0afaa0281b80f8fbb0cd88136/src/core/libraries/pad/pad.cpp)
and [errors](https://github.com/shadps4-emu/shadPS4/blob/91e6afa69b0a64b0afaa0281b80f8fbb0cd88136/src/core/libraries/pad/pad_errors.h).
Guest copies serialize little endian fields explicitly: PadData is 120 bytes,
buttons at 0, sticks at 4/6, triggers at 8/9, quaternion W at 24, connected
at 76, steady-clock microseconds at 80 and connection count at 104.
ControllerInformation is 28 bytes. Unsupported touch/sensor/extension data
remains zero. The quaternion is identity; no tracking is claimed.

`maxps4_fex_engine_pad_update` supplies PS4 button bits, four unsigned stick
axes (center 128), two triggers and connection state. Updates use the Pad
mutex without the global FEX lock or guest-memory access, so input can arrive
while the guest executes. The caller retains the engine and serializes close.
Invalid button bits/connection flags are rejected. Disconnection clears
buttons/triggers and centers sticks in guest output. Connection count saturates.
Reset clears input, initialization and the open port; handles are never reused
within an engine. Read returns one current snapshot, even when the capacity
is larger; there is no synthetic sample history. Device outputs, motion,
remote/special ports and additional users remain unsupported.

Diagnostic → VideoOut → Metal now defaults to an interactive synthetic
120-frame program. It opens the Pad port, reads `scePadReadState` each frame,
uses the button word as RGB for the left pixel, submits a linear VideoOut
buffer, and waits for its presentation before reusing pixels. The right pixel
stays fixed. Touch directions/Cross and iOS controller directions/A produce
PS4 button bits. The iOS left stick is mapped to digital directions here;
the native host API also supports full analog samples. Leaving/backgrounding
stops the source, preventing later input or confirmed flips.

The bundled `VideoOutDemo/pad-videoout.elf` is fabricated x86 code, contains
no game data, and is compared byte-for-byte with its fixture exporter in CI.
The original single-frame diagnostic remains selectable.

Validation includes guest-memory errors, layouts, lifecycle, disconnect,
concurrent updates/reads and stale handles. The linked FEX test changes host
input halfway through 120 frames and verifies both resulting pixel values,
explicit flip completion, guest exit 123 and reset on same-engine restaging.
The Metal checks separately verify actual offscreen pixel readback,
presentation callback ordering, shader compilation and iPhone frontend types.
These checks do not prove physical iPhone display or controller behavior.

This is CPU-generated linear imagery. Sonic's GPU command streams, shader
translation and tiled render targets still need implementation; this fixture
does not substitute for a PS4 GPU renderer. No IPA build is required.
