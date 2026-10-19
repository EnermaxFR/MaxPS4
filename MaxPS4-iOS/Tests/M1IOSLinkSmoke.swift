import Darwin

@main
struct M1IOSLinkSmoke {
    static func main() {
        precondition(MemoryLayout<maxps4_m1_collision_report>.size == 40)
        var reservation = maxps4_m1_collision_report()
        precondition(maxps4_fex_m1_collision_diagnostic(&reservation) == 0)
        precondition(reservation.controlled_direct_status == -EADDRINUSE)
        precondition(reservation.translated_status == 0 && reservation.marker_preserved == 1 && reservation.recreated == 1)
        var reports = [maxps4_m1_thread_report](repeating: .init(), count: 2)
        let status = reports.withUnsafeMutableBufferPointer { output in
            maxps4_fex_engine_run_m1_threads(nil, nil, 0, 1, 0, nil, 0, 0, output.baseAddress, 2)
        }
        precondition(status == -EINVAL && reports.allSatisfy { $0.joined == 0 && $0.cpu.backend == 0 })
        print("PASS linked Swift M1 collision ABI and refusal outputs; reservation probe only, no CPU execution")
    }
}
