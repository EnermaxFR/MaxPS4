import Darwin
#if MAXPS4_V4_FRAMEWORK_SMOKE
import MaxPS4FEXRuntime
#endif

@main
struct V4PRXLinkSmoke {
    static func main() {
        precondition(MemoryLayout<maxps4_v4_prx_request>.size == 40)
        precondition(MemoryLayout<maxps4_v4_prx_request>.offset(of: \.entry) == 24)
        precondition(MemoryLayout<maxps4_v4_prx_request>.offset(of: \.abi) == 32)
        var request = maxps4_v4_prx_request(file: nil, length: 0, load_bias: 0, entry: 0,
            abi: UInt32(MAXPS4_V4_ABI_SYNTHETIC_HLT), instruction_budget: 3)
        var result: UInt64 = 123
        var backend: UInt32 = 123
        let status = maxps4_fex_engine_run_v4_prx(nil, &request, &result, &backend)
        precondition(status == -EINVAL && result == 0 && backend == 0)
        precondition(MAXPS4_V4_ABI_STARTUP_RETURN == 3)
        precondition(MAXPS4_V4_ABI_IMPORT_THREAD_SELF == 4)
        precondition(MAXPS4_V4_ABI_IMPORT_CLOCK_GETRES == 5)
        var diagnostic = [CChar](repeating: 0, count: 128)
        let admission = diagnostic.withUnsafeMutableBufferPointer { buffer in
            maxps4_fex_v4_preflight(nil, buffer.baseAddress, buffer.count)
        }
        precondition(admission == -EINVAL && diagnostic[0] != 0)
        let eboot = diagnostic.withUnsafeMutableBufferPointer { buffer in
            maxps4_fex_eboot_preflight(nil, 0, buffer.baseAddress, buffer.count)
        }
        precondition(eboot == -EINVAL && diagnostic[0] != 0)
        result = 123; backend = 123
        precondition(maxps4_fex_engine_run_eboot(nil, nil, 0, &result, &backend) == -EINVAL)
        precondition(result == 0 && backend == 0)
        let experimental = diagnostic.withUnsafeMutableBufferPointer { buffer in
            maxps4_fex_eboot_experimental_preflight(nil, 0, buffer.baseAddress, buffer.count)
        }
        precondition(experimental == -ENOTSUP)
        result = 123; backend = 123
        precondition(maxps4_fex_engine_run_eboot_experimental(nil, nil, 0, &result, &backend) == -EINVAL)
        precondition(result == 0 && backend == 0)
        print("PASS linked Swift V4 ABI: imported request layout, exact native symbol and zero failure outputs; no JIT execution")
    }
}
