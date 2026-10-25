import Foundation

@main struct VideoOutPresentationTest {
    static func main() {
        for reversed in [false, true] {
            var results: [Bool] = []
            let gate = MaxPS4VideoOutPresentation { results.append($0) }
            if reversed { gate.drawablePresented(at: 1) } else { gate.gpuFinished(true) }
            precondition(results.isEmpty, "One callback must not confirm a flip")
            if reversed { gate.gpuFinished(true) } else { gate.drawablePresented(at: 1) }
            precondition(results == [true])
            gate.cancel(); gate.gpuFinished(false); gate.drawablePresented(at: 0)
            precondition(results == [true], "A finished ticket cannot be completed twice")
        }
        for time in [0.0, -1, Double.nan, Double.infinity] {
            var results: [Bool] = []
            let gate = MaxPS4VideoOutPresentation { results.append($0) }
            gate.drawablePresented(at: time); gate.gpuFinished(true)
            precondition(results == [false], "Dropped/unpresented drawable must cancel")
        }
        for presentedFirst in [false, true] {
            var results: [Bool] = []
            let gate = MaxPS4VideoOutPresentation { results.append($0) }
            if presentedFirst { gate.drawablePresented(at: 2) }
            gate.gpuFinished(false); gate.drawablePresented(at: 2)
            precondition(results == [false], "GPU failure must not confirm a flip")
        }
        var cancelled: [Bool] = []
        let gate = MaxPS4VideoOutPresentation { cancelled.append($0) }
        gate.cancel(); gate.drawablePresented(at: 1); gate.gpuFinished(true)
        precondition(cancelled == [false])
        // Callbacks and stop can race. Whichever wins, completion is exactly once.
        for _ in 0..<100 {
            let lock = NSLock()
            var count = 0
            let concurrent = MaxPS4VideoOutPresentation { (_: Bool) in lock.lock(); count += 1; lock.unlock() }
            let group = DispatchGroup()
            for action in [0, 1, 2] {
                group.enter()
                DispatchQueue.global().async {
                    if action == 0 { concurrent.gpuFinished(true) }
                    else if action == 1 { concurrent.drawablePresented(at: 3) }
                    else { concurrent.cancel() }
                    group.leave()
                }
            }
            group.wait(); precondition(count == 1)
        }
        // A dropped attempt must not retire the guest flip; late callbacks from
        // that attempt cannot confirm it during a second drawable's retry.
        var attempts: [MaxPS4VideoOutPresentation.Outcome] = []
        let first = MaxPS4VideoOutPresentation(outcome: { attempts.append($0) })
        first.gpuFinished(true); first.drawablePresented(at: 0)
        precondition(attempts == [.notDisplayed])
        let retry = MaxPS4VideoOutPresentation(outcome: { attempts.append($0) })
        first.drawablePresented(at: 5); first.expire()
        retry.drawablePresented(at: 6)
        precondition(attempts == [.notDisplayed], "Retry still requires its own GPU completion")
        retry.gpuFinished(true); retry.expire()
        precondition(attempts == [.notDisplayed, .presented])
        for gpuCompleted in [false, true] {
            var outcomes: [MaxPS4VideoOutPresentation.Outcome] = []
            let timeout = MaxPS4VideoOutPresentation(outcome: { outcomes.append($0) })
            if gpuCompleted { timeout.gpuFinished(true) }
            timeout.expire(); timeout.gpuFinished(true); timeout.drawablePresented(at: 7)
            precondition(outcomes == [.timedOut], "A timed out attempt cannot later confirm a flip")
        }
        print("PASS presentation gate: callback ordering, dropped retry, timeout, stale callbacks, GPU failure and cancellation")
    }
}
