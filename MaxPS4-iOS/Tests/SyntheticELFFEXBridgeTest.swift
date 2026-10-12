import Foundation

@main
struct SyntheticELFFEXBridgeTest {
    static func main() throws {
        let bridge = MaxPS4SyntheticELFFEXBridge.self
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: url) }
        let original = bridge.syntheticELF()
        try original.write(to: url, options: .withoutOverwriting)
        var calls = 0
        let result = try bridge.run(url: url) { calls += 1; return (0, 42, 2) }
        precondition(result.rax == 42 && result.backend == 2 && calls == 1)
        do {
            _ = try bridge.run(url: url) { (-13, 42, 2) }
            preconditionFailure("JIT failure accepted")
        } catch MaxPS4SyntheticELFFEXBridge.BridgeError.nativeFailure(-13) {}
        do {
            _ = try bridge.run(url: url) { (0, 42, 1) }
            preconditionFailure("Interpreter advertised as JIT")
        } catch MaxPS4SyntheticELFFEXBridge.BridgeError.unexpectedBackend {}
        for offset in [68, 120, 130, 24] {
            var corrupt = original
            corrupt[offset] ^= 2
            try corrupt.write(to: url)
            do {
                _ = try bridge.run(url: url) { calls += 1; return (0, 42, 2) }
                preconditionFailure("Malformed or unapproved image accepted")
            } catch {}
            precondition(calls == 1)
        }
        var writable = MaxPS4GuestMemory()
        try writable.mapZeroFilled(at: bridge.entry, size: bridge.program.count,
                                   permissions: [.read, .write, .execute])
        try writable.write(bridge.program, at: bridge.entry)
        do {
            try bridge.validate(memory: writable, entry: bridge.entry)
            preconditionFailure("RWX image accepted")
        } catch MaxPS4SyntheticELFFEXBridge.BridgeError.unsupportedImage {}
        print("PASS synthetic ELF admission and explicit backend checks; real FEX JIT NOT tested")
    }
}
