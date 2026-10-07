import Foundation

/// Educational, bounded x86-64 instruction interpreter.
/// Operates exclusively on supplied test bytes; does not launch PS4 software.
struct MaxPS4CPUPrototype {
    enum CPUError: LocalizedError {
        case unsupportedOpcode
        case truncatedInstruction
        case instructionLimit
        case invalidBranch

        var errorDescription: String? {
            switch self {
            case .unsupportedOpcode: return "Instruction x86-64 non prise en charge"
            case .truncatedInstruction: return "Instruction CPU incomplète"
            case .instructionLimit: return "Limite de 256 instructions ou programme de test dépassée"
            case .invalidBranch: return "Branchement hors du programme de test"
            }
        }
    }

    // x86-64 register order: RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI.
    private(set) var registers = [UInt64](repeating: 0, count: 8)
    private(set) var rip = 0
    private(set) var zeroFlag = false
    private(set) var executedInstructions = 0
    private(set) var recentInstructionOffsets: [Int] = []
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


    /// Execute only a bounded test program copied from simulated guest memory.
    /// This is not an ELF/PS4 execution environment.
    mutating func runLoadedTest(memory: MaxPS4GuestMemory, entry: UInt64, length: Int) throws {
        guard length > 0, length <= 256 else { throw CPUError.instructionLimit }
        let bytes = try memory.read(at: entry, count: length)
        guestMemory = memory
        rip = 0
        try run(Array(bytes), limit: 256)
    }

    mutating func prepareTestStack(address: UInt64, size: Int) throws {
        try guestMemory.mapZeroFilled(at: address, size: size)
        registers[4] = address + UInt64(size)
    }

    mutating func run(_ program: [UInt8], limit: Int = 256) throws {
        var steps = 0
        executedInstructions = 0
        recentInstructionOffsets = []
        while rip < program.count {
            guard steps < limit else { throw CPUError.instructionLimit }
            steps += 1
            executedInstructions += 1
            recentInstructionOffsets.append(rip)
            if recentInstructionOffsets.count > 16 { recentInstructionOffsets.removeFirst() }
            let opcode = program[rip]
            if opcode == 0x90 { // NOP
                rip += 1
            } else if opcode == 0xC3 { // RET ends the isolated test
                rip += 1
                return
            } else if opcode == 0xEB || opcode == 0x74 || opcode == 0x75 {
                // JMP rel8, JZ rel8, JNZ rel8. Jumps are confined to the test program.
                guard rip + 2 <= program.count else { throw CPUError.truncatedInstruction }
                let taken = opcode == 0xEB || (opcode == 0x74 ? zeroFlag : !zeroFlag)
                if taken {
                    let target = rip + 2 + Int(Int8(bitPattern: program[rip + 1]))
                    guard target >= 0 && target < program.count else { throw CPUError.invalidBranch }
                    rip = target
                } else {
                    rip += 2
                }
            } else if opcode >= 0x50 && opcode <= 0x57 {
                // PUSH r64 into bounded guest memory.
                let index = Int(opcode - 0x50)
                let stack = registers[4]
                guard stack >= 8 else { throw MaxPS4GuestMemory.MemoryError.outOfBounds }
                let address = stack - 8
                let data = Data((0..<8).map { UInt8(truncatingIfNeeded: registers[index] >> ($0 * 8)) })
                try guestMemory.write(data, at: address)
                registers[4] = address
                rip += 1
            } else if opcode >= 0x58 && opcode <= 0x5F {
                // POP r64; update RSP only after a successful memory read.
                let index = Int(opcode - 0x58)
                let stack = registers[4]
                guard stack <= UInt64.max - 8 else { throw MaxPS4GuestMemory.MemoryError.outOfBounds }
                let bytes = try guestMemory.read(at: stack, count: 8)
                let value = (0..<8).reduce(UInt64(0)) { $0 | (UInt64(bytes[$1]) << ($1 * 8)) }
                registers[4] = stack + 8
                registers[index] = value
                rip += 1
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

            var stackCPU = Self()
            try stackCPU.prepareTestStack(address: 0x2000, size: 4096)
            try stackCPU.run([
                0x48, 0xB8, 0x2A, 0, 0, 0, 0, 0, 0, 0,
                0x50, 0x59, 0xC3
            ])
            guard stackCPU.registers[1] == 42, stackCPU.registers[4] == 0x3000 else { return false }
            var branchCPU = Self()
            try branchCPU.run([
                0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,
                0x48, 0x83, 0xF8, 0,
                0x74, 0x02, 0x0F, 0x0F, 0xC3
            ])
            guard branchCPU.zeroFlag else { return false }
            var badBranch = Self()
            do { try badBranch.run([0xEB, 0x7F]); return false }
            catch CPUError.invalidBranch {}
            return true
        } catch { return false }
    }
}
