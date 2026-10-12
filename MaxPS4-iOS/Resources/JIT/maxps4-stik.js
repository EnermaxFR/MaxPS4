// MaxPS4 fixed-region adapter for StikDebug's documented universal JIT service.
// Prefix supplied by MaxPS4: mailbox, nonce and sizes; never user-provided code.
// Source contract: StikDebug/StikJIT INTEGRATION.md and Resources/universal.js.
// No app breakpoint, register manipulation, CS flag changes or entitlement hacks.
function le64(value) {
    let result = "";
    for (let i = 0; i < 8; ++i) {
        result += Number(value & 255n).toString(16).padStart(2, "0");
        value >>= 8n;
    }
    return result;
}
function checked(command) {
    const response = send_command(command);
    if (response !== "OK") throw new Error(command.split(":")[0] + ": " + response);
}
const regions = [];
let attached = false;
try {
    const reply = send_command("vAttach;" + get_pid().toString(16));
    if (!reply || !/^T[0-9a-f]{2}/i.test(reply)) throw new Error("Attach: " + reply);
    attached = true;
    for (const size of MAXPS4_SIZES) {
        const allocated = send_command("_M" + size.toString(16) + ",rx");
        if (!allocated || !/^[0-9a-f]+$/i.test(allocated)) throw new Error("RX allocation: " + allocated);
        const address = BigInt("0x" + allocated);
        if (address === 0n || address > 0x1fffffffffffffn || address % BigInt(MAXPS4_SIZES[2]) !== 0n) throw new Error("Unaligned RX address");
        regions.push(address);
        const prepared = prepare_memory_region(Number(address), size);
        if (prepared !== "OK") throw new Error("RX preparation: " + prepared);
        log("MaxPS4 prepared RX region " + address.toString(16) + " size=" + size);
    }
    // Publish metadata first and the per-request nonce last, while all threads stop.
    checked("M" + (MAXPS4_MAILBOX + 8n).toString(16) + ",20:" +
            le64(1n) + regions.map(le64).join(""));
    checked("M" + MAXPS4_MAILBOX.toString(16) + ",8:" + le64(MAXPS4_NONCE));
    log("MaxPS4 RX preparation complete; detaching before host execution");
} catch (error) {
    log("MaxPS4 JIT unavailable: " + error.message);
    // Debugger-owned allocations have not been handed to MaxPS4 on failure.
    for (const address of regions) {
        log("RX cleanup: " + send_command("_m" + address.toString(16)));
    }
    if (attached) {
        try {
            checked("M" + (MAXPS4_MAILBOX + 8n).toString(16) + ",20:" + le64(2n) + "00".repeat(24));
            checked("M" + MAXPS4_MAILBOX.toString(16) + ",8:" + le64(MAXPS4_NONCE));
        } catch (mailboxError) { log("Mailbox: " + mailboxError.message); }
    }
} finally {
    if (attached) {
        const detached = send_command("D");
        log("MaxPS4 detach: " + detached);
        // A failed detach must not resume execution: the debugger owns recovery.
        if (detached !== "OK") throw new Error("Detach failed: " + detached);
    }
}
