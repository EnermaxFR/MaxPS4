# Local UserService

The runtime now owns a UserService session per engine. It provides one
**virtual local user**, ID 1, named `MaxPS4`. This is not a Sony/PSN account,
a persisted console profile or the host OS user's identity. No network or
account authentication occurs.

Local analysis of the Sonic Mania executable identified five missing
UserService imports: Initialize, GetInitialUser, GetLoginUserIdList, GetEvent
and Terminate. All five are now in the HLE registry; GetUserName is also
provided for homebrew/local-profile queries. This does not resolve the game's
remaining GPU, input, audio, save-data or online-service requirements and does
not establish a game launch.

The ABI/NIDs and error namespace follow
[shadPS4's UserService interface](https://github.com/shadps4-emu/shadPS4/blob/91e6afa69b0a64b0afaa0281b80f8fbb0cd88136/src/core/libraries/system/userservice.h),
[registry](https://github.com/shadps4-emu/shadPS4/blob/91e6afa69b0a64b0afaa0281b80f8fbb0cd88136/src/core/libraries/system/userservice.cpp)
and [error definitions](https://github.com/shadps4-emu/shadPS4/blob/91e6afa69b0a64b0afaa0281b80f8fbb0cd88136/src/core/libraries/system/userservice_error.h).

| Function | NID | Implemented behavior |
| --- | --- | --- |
| Initialize | j3YMu1MVNNo | Creates the session and one pending LOGIN event |
| Terminate | bwFjS+bX9mA | Clears session/event state |
| GetInitialUser | CdWp0oHWGr0 | Writes the local user ID as 4 little-endian bytes |
| GetLoginUserIdList | fPhymKNvK-A | Writes four IDs: 1, -1, -1, -1 (16 bytes) |
| GetEvent | yH17Q6NWtVg | Writes LOGIN=0 followed by user ID 1 (8 bytes), once |
| GetUserName | 1xxcMiGu2fo | Writes `MaxPS4` and its NUL terminator (7 bytes) |

Initialize accepts null parameters (default priority) or a checked 4-byte
priority in 0x100–0x2ff. The priority is validated but no service thread or
scheduling policy is created. Repeated initialization returns
ALREADY_INITIALIZED (0x80960003). Queries and termination before initialization
return NOT_INITIALIZED (0x80960002).

Invalid pointers, read-only/executable outputs, malformed parameters and the
invalid user ID -1 return INVALID_ARGUMENT (0x80960005). Unknown users return
NOT_LOGGED_IN (0x80960009). Short name buffers return BUFFER_TOO_SHORT
(0x8096000a). Checked writes leave outputs unchanged on failure; a rejected
event destination never consumes the event. After its first successful read,
GetEvent returns NO_EVENT (0x80960007) without changing the output. These are
signed UserService errors, not Orbis kernel errno or POSIX -1/errno.

Termination/reinitialization produces a fresh login event. Restaging, including
failed preparation, resets the session just like other engine services.
An independent mutex protects lifecycle/event operations, so concurrent
initializers cannot both succeed and concurrent consumers cannot both receive
the same event. No logout queue, multiple profiles, PSN IDs, accounts, parental
controls, host identity lookup or persistent profile data is implemented.

`PS4UserServiceTest` checks lifecycle, priority bounds, exact output layouts,
buffer boundaries, invalid memory, short names, event retention, reinitialization,
reset, object isolation and concurrent callers. It runs with ASan/UBSan in the
Linux/macOS service suite.

The linked FEX suite executes a synthetic x86 guest importing the actual
UserService NIDs. It checks errors before initialization/after termination,
duplicate initialization, consumed events, user/list/name queries and a new
login after reinitialization. It exits with user × 100 + list[0] × 10 +
event.user = 111. The host verifies the copied name/list/event and then
restages the same engine to confirm that its service is uninitialized again.
This tests HLE execution, not a PSN login or an iPhone device session.
