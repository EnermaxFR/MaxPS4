import Foundation

/// Value snapshots only: no guest pointers or mutable native resources reach Metal.
struct MaxPS4FEXMetalState {
    enum PositionError: Error { case unexpectedBackend, invalidValue, invalidCoordinate }
    private(set) var cpuOffset = SIMD2<Float>(0, 0)
    private(set) var rax: UInt64?

    static func bounded(_ value: SIMD2<Float>) throws -> SIMD2<Float> {
        guard value.x.isFinite, value.y.isFinite else { throw PositionError.invalidCoordinate }
        return SIMD2(max(-0.55, min(0.55, value.x)), max(-0.65, min(0.65, value.y)))
    }

    mutating func accept(_ result: MaxPS4SyntheticELFFEXBridge.Result) throws {
        guard result.backend == 2 else { throw PositionError.unexpectedBackend }
        // MOV/ADD EAX produces an unsigned 32-bit result; reject malformed ABI output.
        guard result.rax <= UInt64(UInt32.max) else { throw PositionError.invalidValue }
        let position = try Self.bounded(SIMD2((Float(result.rax) - 1000) / 1000, 0))
        cpuOffset = position
        rax = result.rax
    }

    mutating func reset() { cpuOffset = .zero; rax = nil }

    static func objectOffset(mode: Int, touch: SIMD2<Float>, cpu: SIMD2<Float>) -> SIMD2<Float> {
        let offset = mode == 3 ? cpu : (mode == 2 ? touch : .zero)
        return SIMD2(-0.43 + offset.x, offset.y)
    }

    /// Guest, rather than Swift, performs the final addition before returning RAX.
    static func program(initial: UInt32) -> Data {
        var file = MaxPS4SyntheticELFFEXBridge.syntheticELF()
        for i in 0..<4 {
            file[121 + i] = UInt8(truncatingIfNeeded: initial >> (8 * i))
            file[126 + i] = UInt8(truncatingIfNeeded: UInt32(250) >> (8 * i))
        }
        return file
    }
}

/// FEX config and its existing runtime diagnostics use process-global state.
/// Serialize this work; each invocation still owns and closes a fresh engine.
enum MaxPS4FEXMetalExecution {
    static let queue = DispatchQueue(label: "com.enermaxfr.maxps4.fex-metal")
    static func run(initial: UInt32) throws -> MaxPS4SyntheticELFFEXBridge.Result {
#if MAXPS4_FEX_APP
        return try MaxPS4SyntheticELFFEXBridge.run(file: MaxPS4FEXMetalState.program(initial: initial))
#else
        throw MaxPS4SyntheticELFFEXBridge.BridgeError.nativeFailure(-95)
#endif
    }
}
