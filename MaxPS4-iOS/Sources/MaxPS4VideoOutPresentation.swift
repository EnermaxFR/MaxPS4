import Foundation

// Metal completion alone does not establish that a drawable was displayed.
// Both callbacks may arrive on different threads/in either order. Cancellation,
// errors and dropped drawables finish once without confirming a guest flip.
final class MaxPS4VideoOutPresentation {
    enum Outcome: Equatable {
        case presented, gpuFailed, notDisplayed, cancelled, timedOut
    }
    private let lock = NSLock()
    private var gpu: Bool?
    private var displayed: Bool?
    private var finish: ((Outcome) -> Void)?

    init(finish: @escaping (Bool) -> Void) { self.finish = { finish($0 == .presented) } }
    init(outcome: @escaping (Outcome) -> Void) { self.finish = outcome }
    func gpuFinished(_ succeeded: Bool) { update(gpu: succeeded, displayed: nil, cancellation: nil) }
    func drawablePresented(at time: Double) {
        update(gpu: nil, displayed: time.isFinite && time > 0, cancellation: nil)
    }
    func cancel() { update(gpu: nil, displayed: nil, cancellation: .cancelled) }
    func expire() { update(gpu: nil, displayed: nil, cancellation: .timedOut) }

    private func update(gpu: Bool?, displayed: Bool?, cancellation: Outcome?) {
        lock.lock()
        if self.gpu == nil { self.gpu = gpu }
        if self.displayed == nil { self.displayed = displayed }
        let result: Outcome?
        if let cancellation { result = cancellation }
        else if self.gpu == false { result = .gpuFailed }
        else if self.displayed == false { result = .notDisplayed }
        else if self.gpu == true && self.displayed == true { result = .presented }
        else { result = nil }
        let callback = result != nil ? finish : nil
        if callback != nil { finish = nil }
        lock.unlock()
        if let result { callback?(result) }
    }
}
