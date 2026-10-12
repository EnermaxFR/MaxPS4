import Foundation

@main
struct FEXMetalStateTest {
    static func main() throws {
        var state = MaxPS4FEXMetalState()
        try state.accept(.init(rax: 1250, backend: 2))
        precondition(state.cpuOffset == SIMD2<Float>(0.25, 0))
        var copy = state
        copy.reset()
        precondition(state.rax == 1250 && copy.rax == nil)
        try copy.accept(.init(rax: 0, backend: 2))
        precondition(copy.cpuOffset.x == -0.55)
        try copy.accept(.init(rax: UInt64(UInt32.max), backend: 2))
        precondition(copy.cpuOffset.x == 0.55)
        let before = copy.cpuOffset
        for bad in [MaxPS4SyntheticELFFEXBridge.Result(rax: 42, backend: 1), .init(rax: UInt64.max, backend: 2)] {
            do { try copy.accept(bad); preconditionFailure("invalid provenance/value accepted") } catch {}
            precondition(copy.cpuOffset == before)
        }
        for bad in [SIMD2<Float>(.nan, 0), SIMD2<Float>(0, .infinity)] {
            do { _ = try MaxPS4FEXMetalState.bounded(bad); preconditionFailure("nonfinite accepted") } catch {}
        }
        let clamped = try MaxPS4FEXMetalState.bounded(SIMD2(9, -9))
        precondition(clamped == SIMD2<Float>(0.55, -0.65))
        let touch = SIMD2<Float>(0.1, 0.2)
        precondition(MaxPS4FEXMetalState.objectOffset(mode: 2, touch: touch, cpu: state.cpuOffset) == SIMD2<Float>(-0.43 + touch.x, touch.y))
        precondition(MaxPS4FEXMetalState.objectOffset(mode: 1, touch: touch, cpu: state.cpuOffset) == SIMD2<Float>(-0.43, 0))
        precondition(MaxPS4FEXMetalState.objectOffset(mode: 3, touch: touch, cpu: state.cpuOffset) == SIMD2<Float>(-0.43 + 0.25, 0))
        let file = MaxPS4FEXMetalState.program(initial: 750)
        precondition(file[120] == 0xB8 && file[125] == 0x05 && file[130] == 0xF4)
        print("PASS position conversion/clamp, invalid values/backend, copy isolation, reset and independent touch modes; no JIT claimed")
    }
}
