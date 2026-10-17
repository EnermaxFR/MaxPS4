/* Original GPL-2.0 repository probe, no proprietary SDK.
 * V4 function-entry ABI; no claim of a PS4 process CRT. */
unsigned maxps4_initialized = 0x1234;
unsigned maxps4_zero_initialized;

unsigned _start(const unsigned long long *startup) {
    return (unsigned)startup[0] + maxps4_initialized + maxps4_zero_initialized;
}
