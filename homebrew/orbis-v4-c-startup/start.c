/* Original GPL-2.0 repository test, no proprietary SDK or library.
 * Experimental V4 function-entry ABI: RDI points to the argc/argv block.
 * This is not a PS4 process CRT or a console-validated application. */
unsigned maxps4_homebrew_marker = 0xc0de;
unsigned char maxps4_homebrew_scratch[64];

unsigned _start(const unsigned long long *startup) {
    return (unsigned)startup[0] + 256U;
}
