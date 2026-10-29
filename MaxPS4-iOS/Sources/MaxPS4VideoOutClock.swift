import Foundation

// One guest vblank per 60 Hz interval, independent of rendering/copy latency.
// Manual redraws do not generate extra events; a stall does not replay a burst.
struct MaxPS4VideoOutClock {
    private var next: TimeInterval?
    mutating func due(at now: TimeInterval) -> Bool {
        guard now.isFinite else { return false }
        let interval = 1.0 / 60.0
        // GCD timer delivery can move around its deadline. Allow ordinary jitter
        // without accidentally turning alternating early/late callbacks into 30 Hz.
        let tolerance = interval / 4
        guard let deadline = next else { next = now + interval; return true }
        guard now + tolerance >= deadline else { return false }
        let missed = max(0, floor((now + tolerance - deadline) / interval))
        next = deadline + (missed + 1) * interval
        return true
    }
}
