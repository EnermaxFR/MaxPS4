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
        // Never report success for a native error, an incorrect result or
        // a snapshot whose entry/code differs from the admitted synthetic ELF.
        do {
            _ = try MaxPS4FEXGuestCodeHandoff.submit(snapshot) { _ in (-7, 42, 2) }
            fatalError("Native failure must be rejected")
        } catch MaxPS4SyntheticELFFEXBridge.BridgeError.nativeFailure(let status) {
            precondition(status == -7)
        }
        do {
            _ = try MaxPS4FEXGuestCodeHandoff.submit(snapshot) { _ in (0, 41, 2) }
            fatalError("Incorrect CPU result must be rejected")
        } catch MaxPS4SyntheticELFFEXBridge.BridgeError.unexpectedBackend {}

        let changedEntry = MaxPS4FEXGuestCodeHandoff.Snapshot(
            guestEntry: snapshot.guestEntry + 1, bytes: snapshot.bytes)
        do {
            _ = try MaxPS4FEXGuestCodeHandoff.submit(changedEntry) { _ in
                fatalError("Receiver must not be invoked for wrong entry")
            }
            fatalError("Wrong guest entry must be rejected")
        } catch MaxPS4FEXGuestCodeHandoff.HandoffError.unsupportedCode {}

        var changedBytes = snapshot.bytes
        changedBytes[changedBytes.startIndex] ^= 0x01
        let changedCode = MaxPS4FEXGuestCodeHandoff.Snapshot(
            guestEntry: snapshot.guestEntry, bytes: changedBytes)
        do {
            _ = try MaxPS4FEXGuestCodeHandoff.submit(changedCode) { _ in
                fatalError("Receiver must not be invoked for modified code")
            }
            fatalError("Modified guest code must be rejected")
        } catch MaxPS4FEXGuestCodeHandoff.HandoffError.unsupportedCode {}

        print("PASS guest RX snapshot and strict backend transport boundary; native FEX mapping NOT tested")
    }
}
