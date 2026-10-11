import Foundation

/// Stage two: a bounded, immutable handoff of bytes fetched from guest RX memory.
/// This does NOT map guest virtual memory into FEXCore or authorize execution.
enum MaxPS4FEXGuestCodeHandoff {
    enum HandoffError: Error {
        case unsupportedEntry
        case unsupportedCode
    }

    struct Snapshot {
        let guestEntry: UInt64
        let bytes: Data
    }

    /// Require the already-admitted synthetic ELF and fetch instructions through
    /// guest execute permissions, not from the original ELF file or a hardcoded probe.
    static func snapshot(memory: MaxPS4GuestMemory, entry: UInt64) throws -> Snapshot {
        guard entry == MaxPS4SyntheticELFFEXBridge.entry else {
            throw HandoffError.unsupportedEntry
        }
        try MaxPS4SyntheticELFFEXBridge.validate(memory: memory, entry: entry)
        let bytes = try memory.fetchInstructionBytes(
            at: entry, count: MaxPS4SyntheticELFFEXBridge.program.count)
        guard bytes == MaxPS4SyntheticELFFEXBridge.program else {
            throw HandoffError.unsupportedCode
        }
        return Snapshot(guestEntry: entry, bytes: bytes)
    }

    static func snapshot(url: URL) throws -> Snapshot {
        let image = try MaxPS4ELFLoader.load(url: url)
        return try snapshot(memory: image.memory, entry: image.entry)
    }

    /// Injectable transport boundary. The receiver must independently validate
    /// the exact bytes and report native FEX JIT execution; no interpreter fallback.
    static func submit(
        _ snapshot: Snapshot,
        to receiver: (Data) throws -> (status: Int32, rax: UInt64, backend: UInt32)
    ) throws -> MaxPS4SyntheticELFFEXBridge.Result {
        guard snapshot.guestEntry == MaxPS4SyntheticELFFEXBridge.entry,
              snapshot.bytes == MaxPS4SyntheticELFFEXBridge.program else {
            throw HandoffError.unsupportedCode
        }
        let response = try receiver(snapshot.bytes)
        guard response.status == 0 else {
            throw MaxPS4SyntheticELFFEXBridge.BridgeError.nativeFailure(response.status)
        }
        guard response.backend == 2, response.rax == 42 else {
            throw MaxPS4SyntheticELFFEXBridge.BridgeError.unexpectedBackend
        }
        return .init(rax: response.rax, backend: response.backend)
    }
}
