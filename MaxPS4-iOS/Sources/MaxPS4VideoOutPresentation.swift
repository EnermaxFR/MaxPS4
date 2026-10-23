import Foundation

// Metal completion alone does not establish that a drawable was displayed.
// Both callbacks may arrive on different threads/in either order. Cancellation,
// errors and dropped drawables finish once without confirming a guest flip.
final class MaxPS4VideoOutPresentation {
    private let lock = NSLock()
    private var gpu: Bool?
    private var displayed: Bool?
    private var finish: ((Bool) -> Void)?

    init(finish: @escaping (Bool) -> Void) { self.finish = finish }
    func gpuFinished(_ succeeded: Bool) { update(gpu: succeeded, displayed: nil, cancelled: false) }
    func drawablePresented(at time: Double) {
        update(gpu: nil, displayed: time.isFinite && time > 0, cancelled: false)
    }
    func cancel() { update(gpu: nil, displayed: nil, cancelled: true) }

    private func update(gpu: Bool?, displayed: Bool?, cancelled: Bool) {
        lock.lock()
        if self.gpu == nil { self.gpu = gpu }
        if self.displayed == nil { self.displayed = displayed }
        let failed = cancelled || self.gpu == false || self.displayed == false
        let succeeded = self.gpu == true && self.displayed == true
        let callback = failed || succeeded ? finish : nil
        if callback != nil { finish = nil }
        lock.unlock()
        callback?(succeeded && !failed)
    }
}
