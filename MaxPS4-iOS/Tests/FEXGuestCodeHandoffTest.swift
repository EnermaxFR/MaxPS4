import Foundation

@main
struct FEXGuestCodeHandoffTest {
    static func main() throws {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString + ".elf")
        defer { try? FileManager.default.removeItem(at: url) }
        try MaxPS4SyntheticELFFEXBridge.syntheticELF().write(to: url)
        let snapshot = try MaxPS4FEXGuestCodeHandoff.snapshot(url: url)
        precondition(snapshot.guestEntry == 0x1000)
        precondition(snapshot.bytes == MaxPS4SyntheticELFFEXBridge.program)

        var calls = 0
        let result = try MaxPS4FEXGuestCodeHandoff.submit(snapshot) { bytes in
            calls += 1
            precondition(bytes == snapshot.bytes)
            return (0, 42, 2)
        }
        precondition(calls == 1 && result.rax == 42 && result.backend == 2)

        do {
            _ = try MaxPS4FEXGuestCodeHandoff.submit(snapshot) { _ in (0, 42, 1) }
            fatalError("Interpreter backend must be rejected")
        } catch MaxPS4SyntheticELFFEXBridge.BridgeError.unexpectedBackend {}

        var memory = MaxPS4GuestMemory()
        try memory.mapZeroFilled(at: 0x1000, size: snapshot.bytes.count)
        try memory.write(snapshot.bytes, at: 0x1000)
        do {
            _ = try MaxPS4FEXGuestCodeHandoff.snapshot(memory: memory, entry: 0x1000)
            fatalError("Non-executable memory must be rejected")
        } catch MaxPS4GuestMemory.MemoryError.accessDenied {}

        try memory.protect(at: 0x1000, size: snapshot.bytes.count, permissions: [.read, .execute])
        let checked = try MaxPS4FEXGuestCodeHandoff.snapshot(memory: memory, entry: 0x1000)
        precondition(checked.bytes == snapshot.bytes)
        print("PASS guest RX snapshot and strict backend transport boundary; native FEX mapping NOT tested")
    }
}
