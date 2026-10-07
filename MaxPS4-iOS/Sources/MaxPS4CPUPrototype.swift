import Foundation

/// Educational, bounded x86-64 instruction interpreter.
/// Operates exclusively on supplied test bytes; does not launch PS4 software.
struct MaxPS4CPUPrototype {
    enum CPUError: Error {
        case unsupportedOpcode
        case truncatedInstruction
        case instructionLimit
    }

    // x86-64 register order: RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI.
    private(set) var registers = [UInt64](repeating: 0, count: 8)
    private(set) var rip = 0
    private(set) var zeroFlag = false
    var rax: UInt64 { registers[0] }
    private(set) var guestMemory = MaxPS4GuestMemory()

    mutating func prepareGuestMemory(address: UInt64, size: Int) throws {
        try guestMemory.mapZeroFilled(at: address, size: size)
    }

    mutating func storeRAX(address: UInt64) throws {
        let bytes = Data((0..<8).map { UInt8(truncatingIfNeeded: rax >> ($0 * 8)) })
        try guestMemory.write(bytes, at: address)
    }

    mutating func loadRAX(address: UInt64) throws {
        let bytes = try guestMemory.read(at: address, count: 8)
        registers[0] = (0..<8).reduce(UInt64(0)) { result, index in
            result | (UInt64(bytes[index]) << (index * 8))
        }
    }


    mutating func run(_ program: [UInt8], limit: Int = 256) throws {
        var steps = 0
        while rip < program.count {
            guard steps < limit else { throw CPUError.instructionLimit }
            steps += 1
            let opcode = program[rip]
            if opcode == 0x90 { // NOP
                rip += 1
            } else if opcode == 0xC3 { // RET ends the isolated test
                rip += 1
                return
            } else if opcode == 0x48 {
                guard rip + 2 <= program.count else { throw CPUError.truncatedInstruction }
                let next = program[rip + 1]
                if next >= 0xB8 && next <= 0xBF { // MOV r64, imm64
                    guard rip + 10 <= program.count else { throw CPUError.truncatedInstruction }
                    var value: UInt64 = 0
                    for index in 0..<8 {
                        value |= UInt64(program[rip + 2 + index]) << (index * 8)
                    }
                    registers[Int(next - 0xB8)] = value
                    rip += 10
                } else if next == 0xA1 || next == 0xA3 { // MOV RAX, moffs64 / MOV moffs64, RAX
                    guard rip + 10 <= program.count else { throw CPUError.truncatedInstruction }
                    var address: UInt64 = 0
                    for index in 0..<8 {
                        address |= UInt64(program[rip + 2 + index]) << (index * 8)
                    }
                    if next == 0xA1 {
                        try loadRAX(address: address)
                    } else {
                        try storeRAX(address: address)
                    }
                    rip += 10
                } else if next == 0x83 { // ADD/SUB/CMP r64, sign-extended imm8
                    guard rip + 4 <= program.count else { throw CPUError.truncatedInstruction }
                    let modrm = program[rip + 2]
                    guard modrm & 0xC0 == 0xC0 else { throw CPUError.unsupportedOpcode }
                    let operation = (modrm >> 3) & 7
                    guard operation == 0 || operation == 5 || operation == 7 else {
                        throw CPUError.unsupportedOpcode
                    }
                    let index = Int(modrm & 7)
                    let immediate = UInt64(bitPattern: Int64(Int8(bitPattern: program[rip + 3])))
                    let result = registers[index] &- (operation == 0 ? (~immediate &+ 1) : immediate)
                    if operation != 7 { registers[index] = result }
                    zeroFlag = result == 0
                    rip += 4
                } else {
                    throw CPUError.unsupportedOpcode
                }
            } else {
                throw CPUError.unsupportedOpcode
            }
        }
    }

    static func selfTest() -> Bool {
        do {
            var cpu = Self()
            try cpu.run([0x48, 0xB8, 0x05, 0, 0, 0, 0, 0, 0, 0,
                         0x48, 0x83, 0xC0, 0x03,
                         0x48, 0xBB, 0x08, 0, 0, 0, 0, 0, 0, 0,
                         0x48, 0x83, 0xFB, 0x08,
                         0x48, 0x83, 0xEB, 0x02, 0xC3])
            guard cpu.rax == 8, cpu.registers[3] == 6, !cpu.zeroFlag else { return false }
            var unsupported = Self()
            do { try unsupported.run([0x0F]); return false }
            catch CPUError.unsupportedOpcode {}
            var truncated = Self()
            do { try truncated.run([0x48, 0xB8, 0x01]); return false }
            catch CPUError.truncatedInstruction {}
            var limit = Self()
            do { try limit.run([0x90, 0x90], limit: 1); return false }
            catch CPUError.instructionLimit {}
            var memoryCPU = Self()
            try memoryCPU.prepareGuestMemory(address: 0x1000, size: 4096)
            try memoryCPU.run([0x48, 0xB8, 0x2A, 0, 0, 0, 0, 0, 0, 0, 0xC3])
            try memoryCPU.storeRAX(address: 0x1010)
            var receiver = Self()
            receiver.guestMemory = memoryCPU.guestMemory
            try receiver.loadRAX(address: 0x1010)
            guard receiver.rax == 42 else { return false }
            var instructionCPU = Self()
            try instructionCPU.prepareGuestMemory(address: 0x1000, size: 4096)
            try instructionCPU.run([
                0x48, 0xB8, 0x2A, 0, 0, 0, 0, 0, 0, 0,
                0x48, 0xA3, 0x10, 0x10, 0, 0, 0, 0, 0, 0,
                0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,
                0x48, 0xA1, 0x10, 0x10, 0, 0, 0, 0, 0, 0,
                0xC3
            ])
            guard instructionCPU.rax == 42 else { return false }
            var invalidAccess = Self()
            do {
                try invalidAccess.run([0x48, 0xA1, 0, 0, 0, 0, 0, 0, 0, 0])
                return false
            } catch MaxPS4GuestMemory.MemoryError.outOfBounds {}

            return true
        } catch { return false }
    }
}
