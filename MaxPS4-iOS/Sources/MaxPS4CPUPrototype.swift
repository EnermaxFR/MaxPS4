import Foundation

/// Tiny educational x86-64 interpreter. Runs only caller-provided byte arrays.
/// Not connected to imported games, native memory, or executable pages.
struct MaxPS4CPUPrototype {
    enum CPUError: Error {
        case unsupportedOpcode
        case truncatedInstruction
        case instructionLimit
    }

    private(set) var rax: UInt64 = 0
    private(set) var rip = 0

    mutating func run(_ program: [UInt8], limit: Int = 256) throws {
        var steps = 0
        while rip < program.count {
            guard steps < limit else { throw CPUError.instructionLimit }
            steps += 1
            let opcode = program[rip]
            if opcode == 0x90 { // NOP
                rip += 1
            } else if opcode == 0xC3 { // RET (terminates this standalone test)
                rip += 1
                return
            } else if rip + 2 <= program.count && opcode == 0x48 && program[rip + 1] == 0xB8 {
                // MOV RAX, imm64
                guard rip + 10 <= program.count else { throw CPUError.truncatedInstruction }
                var value: UInt64 = 0
                for index in 0..<8 {
                    value |= UInt64(program[rip + 2 + index]) << (index * 8)
                }
                rax = value
                rip += 10
            } else if rip + 3 <= program.count && opcode == 0x48 &&
                      program[rip + 1] == 0x83 && program[rip + 2] == 0xC0 {
                // ADD RAX, sign-extended imm8
                guard rip + 4 <= program.count else { throw CPUError.truncatedInstruction }
                let signed = Int64(Int8(bitPattern: program[rip + 3]))
                rax = rax &+ UInt64(bitPattern: signed)
                rip += 4
            } else {
                throw CPUError.unsupportedOpcode
            }
        }
    }

    static func selfTest() -> Bool {
        do {
            var cpu = Self()
            try cpu.run([0x48, 0xB8, 0x05, 0, 0, 0, 0, 0, 0, 0,
                         0x48, 0x83, 0xC0, 0x03, 0x90, 0xC3])
            guard cpu.rax == 8 && cpu.rip == 16 else { return false }
            var unsupported = Self()
            do {
                try unsupported.run([0x0F])
                return false
            } catch CPUError.unsupportedOpcode {}
            var truncated = Self()
            do {
                try truncated.run([0x48, 0xB8, 0x01])
                return false
            } catch CPUError.truncatedInstruction {}
            return true
        } catch { return false }
    }
}
