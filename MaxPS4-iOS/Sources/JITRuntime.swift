import Darwin
import Foundation

/// Central JIT memory manager for the MaxPS4 iOS frontend.
///
/// MAP_JIT is available to entitled sideloaded builds, but
/// pthread_jit_write_protect_np is marked unavailable by the public iOS SDK.
/// Resolve it dynamically when present so the app can still compile with the
/// official iPhoneOS SDK.
final class JITRuntime {
    static let shared = JITRuntime()

    private typealias JITWriteProtectFunction = @convention(c) (Int32) -> Void

    private(set) var isAvailable = false
    private(set) var lastError: String?

    private lazy var jitWriteProtect: JITWriteProtectFunction? = {
        guard let symbol = dlsym(UnsafeMutableRawPointer(bitPattern: -2), "pthread_jit_write_protect_np") else {
            return nil
        }
        return unsafeBitCast(symbol, to: JITWriteProtectFunction.self)
    }()

    private init() {
        probe()
    }

    func probe() {
        let pageSize = Int(getpagesize())
        let flags = MAP_PRIVATE | MAP_ANON | MAP_JIT
        guard let memory = mmap(nil, pageSize, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0),
              memory != MAP_FAILED else {
            isAvailable = false
            lastError = "MAP_JIT unavailable (errno \(errno))"
            return
        }

        jitWriteProtect?(0)
        memset(memory, 0, pageSize)
        jitWriteProtect?(1)
        munmap(memory, pageSize)

        isAvailable = true
        lastError = nil
    }

    func allocate(size: Int) -> UnsafeMutableRawPointer? {
        guard size > 0 else { return nil }
        let page = Int(getpagesize())
        let aligned = (size + page - 1) & ~(page - 1)
        let memory = mmap(nil, aligned, PROT_READ | PROT_WRITE | PROT_EXEC,
                          MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0)
        guard memory != MAP_FAILED else {
            lastError = "JIT mmap failed (errno \(errno))"
            return nil
        }
        return memory
    }

    @inline(__always)
    func beginWrite() {
        jitWriteProtect?(0)
    }

    @inline(__always)
    func endWrite() {
        jitWriteProtect?(1)
    }

    func release(_ pointer: UnsafeMutableRawPointer, size: Int) {
        let page = Int(getpagesize())
        let aligned = (size + page - 1) & ~(page - 1)
        munmap(pointer, aligned)
    }
}
