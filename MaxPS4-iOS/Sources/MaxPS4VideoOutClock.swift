import Foundation

// One guest vblank per 60 Hz interval, independent of rendering/copy latency.
// Manual redraws do not generate extra events; a stall does not replay a burst.
struct MaxPS4VideoOutClock {
    private var next: TimeInterval?
    mutating func due(at now: TimeInterval) -> Bool {
        guard now.isFinite else { return false }
        let interval = 1.0 / 60.0
        guard let deadline = next else { next = now + interval; return true }
        guard now + 0.0001 >= deadline else { return false }
        let missed = max(0, floor((now + 0.0001 - deadline) / interval))
        next = deadline + (missed + 1) * interval
        return true
    }
}
