import Darwin
import Foundation

/// Central JIT memory manager for the shadPS4 iOS port.
///
/// iOS requires executable JIT pages to be created with MAP_JIT and the
/// dynamic-codesigning entitlement. Writes are bracketed with
/// pthread_jit_write_protect_np so the same pages are never writable and
/// executable at the same time.
final class JITRuntime {
    static let shared = JITRuntime()

    private(set) var isAvailable = false
    private(set) var lastError: String?

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

        pthread_jit_write_protect_np(0)
        memset(memory, 0, pageSize)
        pthread_jit_write_protect_np(1)
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
        pthread_jit_write_protect_np(0)
    }

    @inline(__always)
    func endWrite() {
        pthread_jit_write_protect_np(1)
    }

    func release(_ pointer: UnsafeMutableRawPointer, size: Int) {
        let page = Int(getpagesize())
        let aligned = (size + page - 1) & ~(page - 1)
        munmap(pointer, aligned)
    }
}
