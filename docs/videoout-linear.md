# Linear VideoOut handoff

The runtime implements a bounded, CPU-visible VideoOut surface for synthetic
guest programs. It does not connect a game renderer or present a drawable on
an iPhone. Interface layouts, NIDs and library-specific errors follow the
[shadPS4 VideoOut interface](https://github.com/shadps4-emu/shadPS4/blob/91e6afa69b0a64b0afaa0281b80f8fbb0cd88136/src/core/libraries/videoout/video_out.h),
[buffer attributes](https://github.com/shadps4-emu/shadPS4/blob/91e6afa69b0a64b0afaa0281b80f8fbb0cd88136/src/core/libraries/videoout/buffer.h)
and its VideoOut implementation.

## Guest interfaces

The libSceVideoOut registry provides Open/Close, SetBufferAttribute,
RegisterBuffers/UnregisterBuffers, SubmitFlip/IsFlipPending, GetFlipStatus,
GetVblankStatus, Add/DeleteFlipEvent, Add/DeleteVblankEvent and the event ID,
data and count getters. The general FEX path reads SetBufferAttribute's
seventh SysV argument (pitch) from the guest stack. SubmitFlip's fourth
argument (flip argument) follows the normal RCX/R10 trampoline convention.

There is one main virtual port (bus 0, index 0, null parameters). User IDs
are not authenticated or associated with a UserService session. Opening an
already open port returns RESOURCE_BUSY. Port handles increase monotonically
and are not reused after close or restaging. Buffer registration is atomic:
an invalid address or occupied slot does not partially register the batch.
Four attribute groups share sixteen buffer slots; registration returns the
group index. A pending group's buffers cannot be unregistered.

Supported attributes:

- Linear tiling mode 1, aspect-ratio value 0, zero options/reserved fields.
- A8B8G8R8 SRGB (0x80002200, RGBA byte order) and A8R8G8B8 SRGB
  (0x80000000, BGRA byte order); bytes are preserved without conversion.
- Width 1–1920, height 1–1080, pitch from width to 2048 pixels.
- Readable, non-executable guest buffers aligned to four bytes. The checked
  buffer span is pitch × height × 4, including row padding.

Tiled buffers, HDR/compressed formats, stereo, nonzero options, other ports
and flip modes other than 1 are refused. There is one pending flip at a
time; another submission returns FLIP_QUEUE_FULL. Resolution configuration,
flip-rate scheduling, buffer labels, WaitVblank and GNM command submission
remain unsupported. This surface does not resolve all of Sonic Mania's
imports and does not implement its shaders or tiled render targets.

## Host handoff and notifications

SubmitFlip takes an owned snapshot of the checked guest buffer. The host can
query/copy it through `maxps4_fex_engine_videoout_snapshot`, receiving a
56-byte `maxps4_videoout_frame` and raw pixels. A metadata-only query uses a
null pixel pointer and capacity zero. A successful copy returns metadata
from the same snapshot under the same mutex; callers must use that returned
metadata even if an earlier query described another frame. No pending frame
returns -EAGAIN; insufficient capacity/invalid arguments return -EINVAL.
Failure clears metadata and leaves the pixel destination untouched.

The host reports presentation using
`maxps4_fex_engine_videoout_complete(engine, ticket, presented)`. A successful
confirmation advances the flip count/current buffer/flip argument and emits
a flip event. `presented=0` cancels the submission without advancing status or
emitting an event. Closing/restaging invalidates pending tickets. Tickets are
monotonic and a duplicate or stale completion is refused. Neither submitting
nor copying pixels claims they were displayed. No Metal consumer calls this
API yet; tests provide the confirmation explicitly.

Vblank comes separately from
`maxps4_fex_engine_videoout_vblank(engine, port)`. A real display timing source
must drive that API when a display is connected. It is never synthesized by
SubmitFlip or flip completion. Old-port notifications are rejected after
close/reopen. These three host APIs avoid the global FEX runtime lock, so
the host can complete a presentation while the guest waits on an event. The
caller must retain the engine until all callbacks finish and serialize close
with them. Complete/vblank return signed VideoOut errors (0x8029...), not
POSIX errno.

Flip/vblank events use the VideoOut filter (-13) and internal IDs 6/7.
The event-data word carries the low 48 bits of the flip argument, a pending
notification count saturated at 15, and twelve low timestamp bits. Event
getters expose public IDs 0/1 and sign-extend flip arguments from 48 bits.
Delivery clears readiness. Registrations share the existing queue budget;
closing a port removes its VideoOut subscriptions without removing user or
GNM events. Deleting a queue naturally removes its subscriptions.

GetFlipStatus serializes 64 bytes; GetVblankStatus serializes 40. Counts
advance only on explicit host notifications. Process-time fields contain
host steady-clock microseconds; TSC/reserved fields stay zero. No PS4 GPU
clock or hardware display timing is claimed.

## Validation

`PS4VideoOutTest` covers layouts, formats, stride, atomic registrations,
group/queue limits, immutable submitted pixels, polling without invented
completion, cancellation, cross-thread wakeup, signed arguments, coalesced
vblank counts, status, stale handles and isolation from user/GNM events.
It runs in the existing Linux/macOS sanitizer suite.

The linked FEX test executes a synthetic x86 guest that opens a port,
constructs attributes using the seventh stack argument, writes two RGBA
pixels, registers/submits them, waits for an explicit host-thread
confirmation, reads flip argument -123 through the real VideoOut getter and
closes the port. The host verifies the eight copied bytes and the guest exit
status 123. This checks pixel handoff and notification plumbing, not Metal
presentation, a game frame or an iPhone device run.
