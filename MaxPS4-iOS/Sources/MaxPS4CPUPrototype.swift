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

    /// Private test ABI, unrelated to real PS4 kernel syscall numbers.
    private enum SimulatedServices {
        static func dispatch(number: UInt64, address: UInt64, size: UInt64, flags: UInt64, memory: inout MaxPS4GuestMemory) throws -> UInt64 {
            switch number {
            case 0: return 42 // deterministic health check
            case 1: return UInt64(memory.allocatedBytes) // current guest allocation
            case 2:
                guard size > 0 && size <= UInt64(MaxPS4GuestMemory.maximumBytes) else { throw MaxPS4GuestMemory.MemoryError.invalidRange }
                try memory.mapZeroFilled(at: address, size: Int(size))
                return 0
            case 3:
                guard size > 0 && size <= UInt64(MaxPS4GuestMemory.maximumBytes) else { throw MaxPS4GuestMemory.MemoryError.invalidRange }
                try memory.unmap(at: address, size: Int(size))
                return 0
            case 4:
                guard size > 0 && size <= UInt64(MaxPS4GuestMemory.maximumBytes),
                      flags <= 7 else { throw MaxPS4GuestMemory.MemoryError.invalidRange }
                var permissions: MaxPS4GuestMemory.Permissions = []
                if flags & 1 != 0 { permissions.insert(.read) }
                if flags & 2 != 0 { permissions.insert(.write) }
                if flags & 4 != 0 { permissions.insert(.execute) }
                try memory.protect(at: address, size: Int(size), permissions: permissions)
                return 0
            default: throw CPUError.unsupportedOpcode
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

    /// Load bounded synthetic instructions into this CPU's isolated guest memory.
    mutating func loadTestProgram(_ program: [UInt8], at address: UInt64) throws {
        guard !program.isEmpty && program.count <= 256 else {
            throw CPUError.instructionLimit
        }
        try guestMemory.mapZeroFilled(at: address, size: program.count)
        try guestMemory.write(Data(program), at: address)
        try guestMemory.protect(at: address, size: program.count,
                                permissions: [.read, .execute])
    }

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
        let bytes = try memory.fetchInstructionBytes(at: entry, count: length)
        guestMemory = memory
        rip = 0
        try run(Array(bytes), limit: 256)
    }

    /// Guest-only allocation using the prototype syscall dispatcher.
    /// Does not call the host OS or implement the PS4 kernel ABI.
    mutating func simulateMemoryAllocate(at address: UInt64, size: Int) throws {
        guard size > 0 else { throw MaxPS4GuestMemory.MemoryError.invalidRange }
        _ = try SimulatedServices.dispatch(number: 2, address: address,
                                           size: UInt64(size), flags: 0, memory: &guestMemory)
    }

    mutating func simulateMemoryFree(at address: UInt64, size: Int) throws {
        guard size > 0 else { throw MaxPS4GuestMemory.MemoryError.invalidRange }
        _ = try SimulatedServices.dispatch(number: 3, address: address,
                                           size: UInt64(size), flags: 0, memory: &guestMemory)
    }

    mutating func prepareTestStack(address: UInt64, size: Int) throws {
        try guestMemory.mapZeroFilled(at: address, size: size)
        registers[4] = address + UInt64(size)
    }

    /// Execute at most one instruction, preserving RIP and registers for resumption.
    mutating func step(_ program: [UInt8]) throws {
        try run(program, limit: 1, pauseAtLimit: true)
    }

    mutating func run(_ program: [UInt8], limit: Int = 256, pauseAtLimit: Bool = false) throws {
        var steps = 0
        executedInstructions = 0
        recentInstructionOffsets = []
        while rip < program.count {
            guard steps < limit else {
                if pauseAtLimit { return }
                throw CPUError.instructionLimit
            }
            steps += 1
            executedInstructions += 1
            recentInstructionOffsets.append(rip)
            if recentInstructionOffsets.count > 16 { recentInstructionOffsets.removeFirst() }
            let opcode = program[rip]
            if opcode == 0x90 { // NOP
                rip += 1
            } else if opcode == 0x0F { // Two-byte opcode: isolated synthetic SYSCALL
                guard rip + 2 <= program.count else { throw CPUError.truncatedInstruction }
                guard program[rip + 1] == 0x05 else { throw CPUError.unsupportedOpcode }
                // Isolated test services; never forwarded to the host OS.
                registers[0] = try SimulatedServices.dispatch(
                    number: registers[0], address: registers[1], size: registers[2], flags: registers[3], memory: &guestMemory
                )
                rip += 2
            } else if opcode == 0x31 { // XOR r/m32, r32 (register-only)
                guard rip + 2 <= program.count else { throw CPUError.truncatedInstruction }
                let modrm = program[rip + 1]
                guard modrm & 0xC0 == 0xC0 else { throw CPUError.unsupportedOpcode }
                let dest = Int(modrm & 7)
                let src = Int((modrm >> 3) & 7)
                let value = UInt32(truncatingIfNeeded: registers[dest]) ^
                    UInt32(truncatingIfNeeded: registers[src])
                registers[dest] = UInt64(value) // x86-64 clears upper 32 bits
                zeroFlag = value == 0
                rip += 2
            } else if opcode == 0x89 || opcode == 0x8B {
                // MOV r/m32,r32 and MOV r32,r/m32; register operands only.
                guard program.count - rip >= 2 else { throw CPUError.truncatedInstruction }
                let modrm = program[rip + 1]
                let mode = modrm >> 6
                let reg = Int((modrm >> 3) & 7)
                let rm = Int(modrm & 7)
                if mode == 0x03 {
                    let destination = opcode == 0x89 ? rm : reg
                    let source = opcode == 0x89 ? reg : rm
                    registers[destination] = UInt64(UInt32(truncatingIfNeeded: registers[source]))
                } else if mode == 0 && rm != 4 && rm != 5 {
                    // Base-register indirect [r64], no displacement or SIB.
                    // GuestMemory enforces bounds and page permissions.
                    let address = registers[rm]
                    if opcode == 0x89 {
                        let value = UInt32(truncatingIfNeeded: registers[reg])
                        let bytes = Data((0..<4).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) })
                        try guestMemory.write(bytes, at: address)
                    } else {
                        let bytes = try guestMemory.read(at: address, count: 4)
                        let value = (0..<4).reduce(UInt32(0)) {
                            $0 | (UInt32(bytes[$1]) << ($1 * 8))
                        }
                        registers[reg] = UInt64(value)
                    }
                } else {
                    throw CPUError.unsupportedOpcode
                }
                // MOV does not update flags.
                rip += 2
            } else if opcode == 0x85 { // TEST r/m32, r32 (register-only)
                guard rip + 2 <= program.count else { throw CPUError.truncatedInstruction }
                let modrm = program[rip + 1]
                guard modrm & 0xC0 == 0xC0 else { throw CPUError.unsupportedOpcode }
                let lhs = Int(modrm & 7)
                let rhs = Int((modrm >> 3) & 7)
                let value = UInt32(truncatingIfNeeded: registers[lhs]) &
                    UInt32(truncatingIfNeeded: registers[rhs])
                zeroFlag = value == 0 // TEST does not change either register
                rip += 2
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
            } else if opcode >= 0xB8 && opcode <= 0xBF {
                // MOV r32, imm32. In 64-bit mode, writing a 32-bit register
                // clears its upper 32 bits (unlike an 8/16-bit write).
                guard program.count - rip >= 5 else { throw CPUError.truncatedInstruction }
                var value: UInt32 = 0
                for index in 0..<4 {
                    value |= UInt32(program[rip + 1 + index]) << (index * 8)
                }
                registers[Int(opcode - 0xB8)] = UInt64(value)
                rip += 5
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
            // MOV EAX,imm32 must zero-extend into RAX; truncated immediates
            // must fail without reading past the end of guest test bytes.
            var mov32CPU = Self()
            try mov32CPU.run([
                0x48, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                0xB8, 0x78, 0x56, 0x34, 0x12, 0xC3
            ])
            guard mov32CPU.rax == 0x12345678 else { return false }
            var incompleteMov32 = Self()
            do {
                try incompleteMov32.run([0xBB, 0x01, 0x02])
                return false
            } catch CPUError.truncatedInstruction {}
            var registerMoveCPU = Self()
            try registerMoveCPU.run([
                0xB8, 0x2A, 0, 0, 0,           // MOV EAX,42
                0x89, 0xC3,                   // MOV EBX,EAX
                0x8B, 0xCB,                   // MOV ECX,EBX
                0xC3
            ])
            guard registerMoveCPU.registers[3] == 42,
                  registerMoveCPU.registers[1] == 42 else { return false }
            var unsupportedMemoryMove = Self()
            do {
                try unsupportedMemoryMove.run([0x8B, 0x00])
                return false
            } catch CPUError.unsupportedOpcode {}
            var memoryMoveCPU = Self()
            try memoryMoveCPU.prepareGuestMemory(address: 0x2000, size: 16)
            try memoryMoveCPU.run([
                0x48, 0xBB, 0x00, 0x20, 0, 0, 0, 0, 0, 0, // MOV RBX,0x2000
                0xB8, 0x78, 0x56, 0x34, 0x12,          // MOV EAX,0x12345678
                0x89, 0x03,                            // MOV [RBX],EAX
                0x8B, 0x0B,                            // MOV ECX,[RBX]
                0xC3
            ])
            guard memoryMoveCPU.registers[1] == 0x12345678,
                  memoryMoveCPU.guestMemory.read(at: 0x2000, count: 4) ==
                    Data([0x78, 0x56, 0x34, 0x12]) else { return false }
            var unmappedMemoryMove = Self()
            do {
                try unmappedMemoryMove.run([0x8B, 0x03])
                return false
            } catch MaxPS4GuestMemory.MemoryError.outOfBounds {}
            var truncatedRegisterMove = Self()
            do {
                try truncatedRegisterMove.run([0x89])
                return false
            } catch CPUError.truncatedInstruction {}
            var xorCPU = Self()
            try xorCPU.run([0x48, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                            0x31, 0xC0, 0x74, 0x02, 0x0F, 0x0F, 0xC3])
            guard xorCPU.rax == 0, xorCPU.zeroFlag,
                  xorCPU.executedInstructions == 4 else { return false }
            var testCPU = Self()
            try testCPU.run([0x48, 0xB8, 0x04, 0, 0, 0, 0, 0, 0, 0,
                             0x48, 0xB9, 0x02, 0, 0, 0, 0, 0, 0, 0,
                             0x85, 0xC8, 0x74, 0x02, 0x0F, 0x0F, 0xC3])
            guard testCPU.rax == 4, testCPU.registers[1] == 2,
                  testCPU.zeroFlag, testCPU.executedInstructions == 5 else { return false }
            var invalidTEST = Self()
            do { try invalidTEST.run([0x85, 0x00]); return false }
            catch CPUError.unsupportedOpcode {}
            var badXOR = Self()
            do { try badXOR.run([0x31, 0x00]); return false }
            catch CPUError.unsupportedOpcode {}
            var serviceCPU = Self()
            try serviceCPU.run([0x31, 0xC0, 0x0F, 0x05, 0xC3])
            guard serviceCPU.rax == 42, serviceCPU.executedInstructions == 3 else { return false }
            var memoryServiceCPU = Self()
            try memoryServiceCPU.prepareGuestMemory(address: 0x9000, size: 4096)
            try memoryServiceCPU.run([0x48, 0xB8, 0x01, 0, 0, 0, 0, 0, 0, 0,
                                      0x0F, 0x05, 0xC3])
            guard memoryServiceCPU.rax == 4096,
                  memoryServiceCPU.executedInstructions == 3 else { return false }
            var allocationCPU = Self()
            try allocationCPU.run([
                0x48, 0xB9, 0x00, 0xA0, 0, 0, 0, 0, 0, 0,
                0x48, 0xBA, 0x00, 0x10, 0, 0, 0, 0, 0, 0,
                0x48, 0xB8, 0x02, 0, 0, 0, 0, 0, 0, 0,
                0x0F, 0x05,
                0x48, 0xB8, 0x03, 0, 0, 0, 0, 0, 0, 0,
                0x0F, 0x05, 0xC3
            ])
            guard allocationCPU.rax == 0, allocationCPU.guestMemory.allocatedBytes == 0,
                  allocationCPU.executedInstructions == 7 else { return false }
            var protectCPU = Self()
            try protectCPU.prepareGuestMemory(address: 0xB000, size: 4096)
            try protectCPU.run([
                0x48, 0xB9, 0x00, 0xB0, 0, 0, 0, 0, 0, 0,
                0x48, 0xBA, 0x00, 0x10, 0, 0, 0, 0, 0, 0,
                0x48, 0xBB, 0x05, 0, 0, 0, 0, 0, 0, 0,
                0x48, 0xB8, 0x04, 0, 0, 0, 0, 0, 0, 0,
                0x0F, 0x05, 0xC3
            ])
            guard protectCPU.rax == 0 else { return false }
            do {
                try protectCPU.guestMemory.write(Data([0x90]), at: 0xB000)
                return false
            } catch MaxPS4GuestMemory.MemoryError.accessDenied {}
            guard try protectCPU.guestMemory.fetchInstructionBytes(at: 0xB000, count: 1) == Data([0]) else { return false }
            // Simulated services must reject invalid sizes, overlap and double-free.
            var invalidSizeCPU = Self()
            do {
                try invalidSizeCPU.run([
                    0x48, 0xB9, 0x00, 0xD0, 0, 0, 0, 0, 0, 0,
                    0x48, 0xBA, 0, 0, 0, 0, 0, 0, 0, 0,
                    0x48, 0xB8, 0x02, 0, 0, 0, 0, 0, 0, 0,
                    0x0F, 0x05
                ])
                return false
            } catch MaxPS4GuestMemory.MemoryError.invalidRange {}
            guard invalidSizeCPU.guestMemory.allocatedBytes == 0 else { return false }
            var doubleFreeCPU = Self()
            do {
                try doubleFreeCPU.run([
                    0x48, 0xB9, 0x00, 0xE0, 0, 0, 0, 0, 0, 0,
                    0x48, 0xBA, 0x00, 0x10, 0, 0, 0, 0, 0, 0,
                    0x48, 0xB8, 0x03, 0, 0, 0, 0, 0, 0, 0,
                    0x0F, 0x05
                ])
                return false
            } catch MaxPS4GuestMemory.MemoryError.outOfBounds {}
            var invalidService = Self()
            do { try invalidService.run([0x48, 0xB8, 0x63, 0, 0, 0, 0, 0, 0, 0, 0x0F, 0x05]); return false }
            catch CPUError.unsupportedOpcode {}
            var unsupported = Self()
            do { try unsupported.run([0x0F, 0x0B]); return false }
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
            // A loaded guest-memory program must preserve its memory writes.
            var loadedCPU = Self()
            var loadedMemory = MaxPS4GuestMemory()
            try loadedMemory.mapZeroFilled(at: 0x4000, size: 4096)
            try loadedMemory.mapZeroFilled(at: 0x6000, size: 4096)
            let program: [UInt8] = [
                0x48, 0xB8, 0x2A, 0, 0, 0, 0, 0, 0, 0,
                0x48, 0xA3, 0x20, 0x60, 0, 0, 0, 0, 0, 0,
                0xC3
            ]
            try loadedMemory.write(Data(program), at: 0x4000)
            try loadedMemory.protect(at: 0x4000, size: 4096, permissions: [.read, .execute])
            try loadedCPU.runLoadedTest(memory: loadedMemory, entry: 0x4000, length: program.count)
            guard loadedCPU.executedInstructions == 3,
                  try loadedCPU.guestMemory.read(at: 0x6020, count: 8) ==
                     Data([42, 0, 0, 0, 0, 0, 0, 0]) else { return false }
            // Never interpret a guest page without execute permission.
            var deniedCPU = Self()
            var deniedMemory = MaxPS4GuestMemory()
            try deniedMemory.mapZeroFilled(at: 0x8000, size: 4096)
            try deniedMemory.write(Data([0x90, 0xC3]), at: 0x8000)
            do {
                try deniedCPU.runLoadedTest(memory: deniedMemory, entry: 0x8000, length: 2)
                return false
            } catch MaxPS4GuestMemory.MemoryError.accessDenied {}
            try deniedMemory.protect(at: 0x8000, size: 4096, permissions: [.read, .execute])
            try deniedCPU.runLoadedTest(memory: deniedMemory, entry: 0x8000, length: 2)
            guard deniedCPU.executedInstructions == 2 else { return false }
            // Two virtual processes cannot access each other's guest memory.
            var processA = MaxPS4VirtualProcess(pid: 101)
            let processB = MaxPS4VirtualProcess(pid: 102)
            try processA.cpu.prepareGuestMemory(address: 0x5000, size: 4096)
            try processA.cpu.run([0x48, 0xB8, 0x2A, 0, 0, 0, 0, 0, 0, 0, 0xC3])
            try processA.cpu.storeRAX(address: 0x5000)
            guard processA.pid == 101, processB.pid == 102,
                  try processA.cpu.guestMemory.read(at: 0x5000, count: 1) == Data([42]),
                  processB.cpu.guestMemory.allocatedBytes == 0 else { return false }
            do {
                _ = try processB.cpu.guestMemory.read(at: 0x5000, count: 1)
                return false
            } catch MaxPS4GuestMemory.MemoryError.outOfBounds {}
            var stepped = Self()
            let steppedProgram: [UInt8] = [
                0x48, 0xB8, 0x05, 0, 0, 0, 0, 0, 0, 0,
                0x48, 0x83, 0xC0, 0x03, 0xC3
            ]
            try stepped.step(steppedProgram)
            guard stepped.rax == 5, stepped.rip == 10 else { return false }
            try stepped.step(steppedProgram)
            guard stepped.rax == 8, stepped.rip == 14 else { return false }
            try stepped.step(steppedProgram)
            guard stepped.rip == steppedProgram.count else { return false }
            var processManager = MaxPS4VirtualProcessManager()
            let firstPID = try processManager.create()
            let secondPID = try processManager.create()
            guard firstPID != secondPID, processManager.count == 2,
                  processManager.process(pid: firstPID) != nil else { return false }
            guard processManager.terminate(pid: firstPID),
                  processManager.process(pid: firstPID) == nil,
                  !processManager.terminate(pid: firstPID),
                  processManager.count == 1,
                  processManager.process(pid: secondPID) != nil else { return false }
            guard processManager.scheduleNext() == secondPID,
                  processManager.currentPID == secondPID,
                  processManager.process(pid: secondPID)?.state == .running else { return false }
            let thirdPID = try processManager.create()
            guard processManager.scheduleNext() == thirdPID,
                  processManager.process(pid: secondPID)?.state == .ready,
                  processManager.scheduleNext() == secondPID else { return false }
            guard processManager.terminate(pid: secondPID),
                  processManager.currentPID == nil,
                  processManager.scheduleNext() == thirdPID,
                  processManager.terminate(pid: thirdPID),
                  processManager.scheduleNext() == nil else { return false }
            var guests = MaxPS4VirtualProcessManager()
            let guestA = try guests.create()
            let guestB = try guests.create()
            guard guests.scheduleNext() == guestA else { return false }
            try guests.runCurrent([0x48, 0xB8, 0x2A, 0, 0, 0, 0, 0, 0, 0, 0xC3])
            guard guests.scheduleNext() == guestB else { return false }
            try guests.runCurrent([0x48, 0xB8, 0x07, 0, 0, 0, 0, 0, 0, 0, 0xC3])
            guard guests.process(pid: guestA)?.cpu.rax == 42,
                  guests.process(pid: guestB)?.cpu.rax == 7,
                  guests.process(pid: guestA)?.cpu.guestMemory.allocatedBytes == 0,
                  guests.process(pid: guestB)?.cpu.guestMemory.allocatedBytes == 0 else { return false }
            var stepGuests = MaxPS4VirtualProcessManager()
            let stepPID = try stepGuests.create()
            guard stepGuests.scheduleNext() == stepPID else { return false }
            try stepGuests.stepCurrent(steppedProgram)
            guard stepGuests.process(pid: stepPID)?.cpu.rax == 5,
                  stepGuests.process(pid: stepPID)?.cpu.rip == 10 else { return false }
            guard stepGuests.suspend(pid: stepPID), stepGuests.resume(pid: stepPID),
                  stepGuests.scheduleNext() == stepPID else { return false }
            try stepGuests.stepCurrent(steppedProgram)
            guard stepGuests.process(pid: stepPID)?.cpu.rax == 8,
                  stepGuests.process(pid: stepPID)?.cpu.rip == 14 else { return false }
            var alternating = MaxPS4VirtualProcessManager()
            let altA = try alternating.create()
            let altB = try alternating.create()
            let altPrograms: [UInt32: [UInt8]] = [
                altA: [0x48, 0xB8, 0x05, 0, 0, 0, 0, 0, 0, 0, 0xC3],
                altB: [0x48, 0xB8, 0x07, 0, 0, 0, 0, 0, 0, 0, 0xC3]
            ]
            guard try alternating.stepRoundRobin(altPrograms) == [altA, altB],
                  alternating.process(pid: altA)?.cpu.rax == 5,
                  alternating.process(pid: altB)?.cpu.rax == 7,
                  alternating.process(pid: altA)?.cpu.rip == 10,
                  alternating.process(pid: altB)?.cpu.rip == 10 else { return false }
            guard alternating.suspend(pid: altA),
                  try alternating.stepRoundRobin(altPrograms) == [altB],
                  alternating.process(pid: altA)?.cpu.rip == 10,
                  alternating.resume(pid: altA),
                  try alternating.stepRoundRobin(altPrograms) == [altA],
                  alternating.process(pid: altA)?.cpu.rip == 11,
                  alternating.process(pid: altB)?.cpu.rip == 11 else { return false }
            var memoryGuests = MaxPS4VirtualProcessManager()
            let memoryA = try memoryGuests.create()
            let memoryB = try memoryGuests.create()
            guard memoryGuests.scheduleNext() == memoryA else { return false }
            let codeA: [UInt8] = [0x48, 0xB8, 0x2A, 0, 0, 0, 0, 0, 0, 0, 0xC3]
            let codeB: [UInt8] = [0x48, 0xB8, 0x07, 0, 0, 0, 0, 0, 0, 0, 0xC3]
            try memoryGuests.loadCurrent(codeA, at: 0x4000)
            try memoryGuests.runLoadedCurrent(at: 0x4000, length: codeA.count)
            guard memoryGuests.scheduleNext() == memoryB else { return false }
            do {
                try memoryGuests.runLoadedCurrent(at: 0x4000, length: codeA.count)
                return false
            } catch MaxPS4GuestMemory.MemoryError.outOfBounds {}
            try memoryGuests.loadCurrent(codeB, at: 0x4000)
            try memoryGuests.runLoadedCurrent(at: 0x4000, length: codeB.count)
            guard memoryGuests.process(pid: memoryA)?.cpu.rax == 42,
                  memoryGuests.process(pid: memoryB)?.cpu.rax == 7 else { return false }
            var batch = MaxPS4VirtualProcessManager()
            let batchA = try batch.create()
            guard batch.scheduleNext() == batchA else { return false }
            try batch.loadCurrent(codeA, at: 0x4000)
            let batchB = try batch.create()
            guard batch.scheduleNext() == batchB else { return false }
            try batch.loadCurrent(codeB, at: 0x4000)
            let completed = try batch.runRoundRobinLoaded(
                at: 0x4000, lengths: [batchA: codeA.count, batchB: codeB.count]
            )
            guard completed == [batchA, batchB],
                  batch.process(pid: batchA)?.cpu.rax == 42,
                  batch.process(pid: batchB)?.cpu.rax == 7 else { return false }
            // Suspension preserves register values and removes the process from scheduling.
            guard batch.suspend(pid: batchA),
                  batch.process(pid: batchA)?.state == .suspended,
                  batch.scheduleNext() == batchB,
                  batch.process(pid: batchA)?.cpu.rax == 42,
                  !batch.suspend(pid: batchA) else { return false }
            guard batch.resume(pid: batchA),
                  batch.process(pid: batchA)?.state == .ready,
                  !batch.resume(pid: batchA),
                  batch.scheduleNext() == batchA,
                  batch.process(pid: batchA)?.cpu.rax == 42 else { return false }
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
            guard branchCPU.zeroFlag,
                  branchCPU.executedInstructions == 4,
                  branchCPU.recentInstructionOffsets == [0, 10, 14, 18] else {
                return false
            }
            var badBranch = Self()
            do { try badBranch.run([0xEB, 0x7F]); return false }
            catch CPUError.invalidBranch {}
            var unallocatedStack = Self()
            do {
                try unallocatedStack.run([0x50])
                return false
            } catch MaxPS4GuestMemory.MemoryError.outOfBounds {}
            return true
        } catch { return false }
    }
}

/// Isolated virtual test process. No host process or PS4 process is created.
struct MaxPS4VirtualProcess {
    let pid: UInt32
    var cpu = MaxPS4CPUPrototype()
    enum State { case ready, running, suspended, stopped }
    var state: State = .ready

    init(pid: UInt32) {
        self.pid = pid
    }
}

 
/// Bounded registry for toy guest processes; never starts host processes.
struct MaxPS4VirtualProcessManager {
    enum ProcessError: Error {
        case capacityReached
        case noRunningProcess
    }

    private var processes: [UInt32: MaxPS4VirtualProcess] = [:]
    private var nextPID: UInt32 = 100
    private let maximumProcesses = 16
    private var runningPID: UInt32?

    var count: Int { processes.count }
    var currentPID: UInt32? { runningPID }

    /// Deterministic round-robin state switch; executes no guest instructions.
    @discardableResult
    mutating func scheduleNext() -> UInt32? {
        let ready = processes.values.filter { $0.state == .ready || $0.state == .running }.map { $0.pid }.sorted()
        guard !ready.isEmpty else { runningPID = nil; return nil }
        let next = ready.first(where: { $0 > (runningPID ?? 0) }) ?? ready[0]
        if let old = runningPID, var previous = processes[old] {
            previous.state = .ready
            processes[old] = previous
        }
        var selected = processes[next]!
        selected.state = .running
        processes[next] = selected
        runningPID = next
        return next
    }

    /// Pause a virtual process without discarding registers or guest memory.
    @discardableResult
    mutating func suspend(pid: UInt32) -> Bool {
        guard var process = processes[pid], process.state != .suspended else { return false }
        process.state = .suspended
        processes[pid] = process
        if runningPID == pid { runningPID = nil }
        return true
    }

    @discardableResult
    mutating func resume(pid: UInt32) -> Bool {
        guard var process = processes[pid], process.state == .suspended else { return false }
        process.state = .ready
        processes[pid] = process
        return true
    }

    mutating func create() throws -> UInt32 {
        guard processes.count < maximumProcesses else { throw ProcessError.capacityReached }
        let pid = nextPID
        nextPID += 1
        processes[pid] = MaxPS4VirtualProcess(pid: pid)
        return pid
    }

    /// Runs a bounded toy bytecode program only for the scheduled guest.
    /// Each process retains its own CPU registers and simulated memory.
    mutating func runCurrent(_ program: [UInt8]) throws {
        guard let pid = runningPID, var process = processes[pid] else {
            throw ProcessError.noRunningProcess
        }
        try process.cpu.run(program, limit: 256)
        processes[pid] = process
    }

    /// Step through one test instruction in the currently scheduled process.
    mutating func stepCurrent(_ program: [UInt8]) throws {
        guard let pid = runningPID, var process = processes[pid] else {
            throw ProcessError.noRunningProcess
        }
        try process.cpu.step(program)
        processes[pid] = process
    }

    /// Map or release guest memory within the selected virtual process.
    mutating func allocateCurrentMemory(at address: UInt64, size: Int) throws {
        guard let pid = runningPID, var process = processes[pid] else {
            throw ProcessError.noRunningProcess
        }
        try process.cpu.simulateMemoryAllocate(at: address, size: size)
        processes[pid] = process
    }

    mutating func freeCurrentMemory(at address: UInt64, size: Int) throws {
        guard let pid = runningPID, var process = processes[pid] else {
            throw ProcessError.noRunningProcess
        }
        try process.cpu.simulateMemoryFree(at: address, size: size)
        processes[pid] = process
    }

    /// Maps a bounded test program into the scheduled process only.
    mutating func loadCurrent(_ program: [UInt8], at address: UInt64) throws {
        guard let pid = runningPID, var process = processes[pid] else {
            throw ProcessError.noRunningProcess
        }
        guard !program.isEmpty && program.count <= 256 else {
            throw MaxPS4CPUPrototype.CPUError.instructionLimit
        }
        try process.cpu.loadTestProgram(program, at: address)
        processes[pid] = process
    }

    /// Execute only bytes fetched from this process's executable guest region.
    mutating func runLoadedCurrent(at address: UInt64, length: Int) throws {
        guard let pid = runningPID, var process = processes[pid] else {
            throw ProcessError.noRunningProcess
        }
        try process.cpu.runLoadedTest(memory: process.cpu.guestMemory, entry: address, length: length)
        processes[pid] = process
    }

    /// Run one bounded, preloaded test program per process in PID order.
    /// This is cooperative batch scheduling, not preemptive CPU emulation.
    mutating func runRoundRobinLoaded(at address: UInt64, lengths: [UInt32: Int]) throws -> [UInt32] {
        let ids = processes.keys.sorted()
        guard !ids.isEmpty else { return [] }
        guard ids.allSatisfy({ lengths[$0] != nil }) else {
            throw ProcessError.noRunningProcess
        }
        var completed: [UInt32] = []
        for pid in ids {
            guard let current = scheduleNext(), current == pid,
                  let length = lengths[pid] else { throw ProcessError.noRunningProcess }
            try runLoadedCurrent(at: address, length: length)
            completed.append(pid)
        }
        return completed
    }

    /// One instruction per runnable guest, in deterministic PID order.
    /// No host threads or real PS4 scheduling.
    mutating func stepRoundRobin(_ programs: [UInt32: [UInt8]]) throws -> [UInt32] {
        let runnable = processes.values.filter { $0.state == .ready || $0.state == .running }
            .map { $0.pid }.sorted()
        guard runnable.allSatisfy({ programs[$0] != nil }) else {
            throw ProcessError.noRunningProcess
        }
        var stepped: [UInt32] = []
        for pid in runnable {
            guard let chosen = scheduleNext(), chosen == pid,
                  let program = programs[pid] else { throw ProcessError.noRunningProcess }
            if let process = processes[pid], process.cpu.rip < program.count {
                try stepCurrent(program)
                stepped.append(pid)
            }
        }
        return stepped
    }

    func process(pid: UInt32) -> MaxPS4VirtualProcess? {
        processes[pid]
    }

    @discardableResult
    mutating func terminate(pid: UInt32) -> Bool {
        if runningPID == pid { runningPID = nil }
        return processes.removeValue(forKey: pid) != nil
    }
}


/// A tiny, isolated guest execution environment for synthetic x86-64 tests.
/// This is NOT the PlayStation 4 OS, kernel, graphics or system-library runtime.
enum MaxPS4VirtualRuntime {
    /// Compatibility scaffold. Explicitly not a verified PS4 syscall ABI.
    /// No iOS host calls, kernel forwarding or arbitrary guest execution.
    /// Standalone compatibility shim for *synthetic* symbol calls only.
    /// This does not implement the real PS4 ABI or forward anything to iOS.
    private enum PS4CompatibilityShim {
        enum ShimError: Error { case unsupported }
        static func invoke(_ symbol: String, virtualTicks: UInt64) throws -> UInt64 {
            switch symbol {
            case "sceKernelGetProcessTime":
                return virtualTicks // deterministic virtual microseconds for tests
            default:
                throw ShimError.unsupported
            }
        }
    }

    /// Explicitly simulated library resolver; not Sony's dynamic linker.
    private struct SimulatedLibraryResolver {
        enum ResolutionError: Error { case libraryUnavailable, symbolUnavailable }
        private let exports: [String: Set<String>] = [
            "libkernel": ["sceKernelGetProcessTime"]
        ]
        func resolve(library: String, symbol: String) throws -> String {
            guard let functions = exports[library] else { throw ResolutionError.libraryUnavailable }
            guard functions.contains(symbol) else { throw ResolutionError.symbolUnavailable }
            return symbol
        }
    }

    /// A test import is resolved only after validating a synthetic ELF64 image.
    /// This does not parse ELF dynamic relocations or load PS4 shared libraries.
    static func runBatchDiagnostics() -> String {
        let checks: [(String, String, String)] = [
            ("ELF + CPU", testELFLibraryIntegration(), "ELF64 + bibliothèques OK"),
            ("Imports ELF64", MaxPS4ELFLoader.importDemoSelfTest() ? "Imports ELF64 OK" : "Imports ELF64 ÉCHEC", "Imports ELF64 OK"),
            ("ELF invalide", MaxPS4ELFLoader.malformedImportSelfTest() ? "ELF invalide refusé" : "ELF invalide accepté", "ELF invalide refusé"),
            ("Relocalisations", MaxPS4ELFLoader.relocationSelfTest() ? "Relocalisations OK" : "Relocalisations ÉCHEC", "Relocalisations OK"),
            ("Relocalisation mémoire", MaxPS4ELFLoader.relativeRelocationSelfTest() ? "Relocalisation mémoire OK" : "Relocalisation mémoire ÉCHEC", "Relocalisation mémoire OK"),
            ("Relocalisations groupées", MaxPS4ELFLoader.relocationBatchSelfTest() ? "Relocalisations groupées OK" : "Relocalisations groupées ÉCHEC", "Relocalisations groupées OK"),
            ("Bibliothèques", testPS4LibraryResolver(), "Bibliothèques système OK"),
            ("Mémoire", testPS4VirtualMemoryService(), "Mémoire virtuelle OK"),
            ("Processus", testProcessMemoryIsolation(), "Processus + mémoire OK"),
            ("Services", testSimulatedKernelServices(), "Services virtuels OK"),
            ("Compatibilité", testPS4CompatibilityScaffold(), "Compatibilité PS4 (base) OK")
        ]
        let passed = checks.filter { $0.1.contains($0.2) }.count
        let report = checks.map { ($0.1.contains($0.2) ? "✅ " : "❌ ") + $0.0 }
        return (["Diagnostic global : \(passed)/\(checks.count) tests validés"] + report +
                ["Exécution réelle de jeux PS4 : non prise en charge"]).joined(separator: "\n")
    }

    static func testELFLibraryIntegration() -> String {
        guard MaxPS4ELFLoader.integrationTest() else {
            return "ELF64 + bibliothèques : échec du chargement ELF de test"
        }
        let resolver = SimulatedLibraryResolver()
        do {
            let importName = try resolver.resolve(library: "libkernel", symbol: "sceKernelGetProcessTime")
            let result = try PS4CompatibilityShim.invoke(importName, virtualTicks: 3200)
            guard result == 3200 else { return "ELF64 + bibliothèques : résultat inattendu" }
            do {
                _ = try resolver.resolve(library: "libkernel", symbol: "sceKernelCreateEqueue")
                return "ELF64 + bibliothèques : symbole absent accepté"
            } catch SimulatedLibraryResolver.ResolutionError.symbolUnavailable {}
            return "ELF64 + bibliothèques OK ✅ • ELF64 de test chargé et exécuté • import libkernel simulé résolu (3200 µs) • symbole manquant refusé • pas de liaison dynamique PS4 réelle"
        } catch {
            return "ELF64 + bibliothèques : résolution impossible"
        }
    }

    static func testPS4LibraryResolver() -> String {
        let resolver = SimulatedLibraryResolver()
        do {
            let symbol = try resolver.resolve(library: "libkernel", symbol: "sceKernelGetProcessTime")
            let ticks = try PS4CompatibilityShim.invoke(symbol, virtualTicks: 2500)
            guard ticks == 2500 else { return "Bibliothèques PS4 : valeur d’horloge incorrecte" }
            do {
                _ = try resolver.resolve(library: "libkernel", symbol: "sceKernelAllocateDirectMemory")
                return "Bibliothèques PS4 : symbole non implémenté accepté"
            } catch SimulatedLibraryResolver.ResolutionError.symbolUnavailable {}
            do {
                _ = try resolver.resolve(library: "libSceGnmDriver", symbol: "sceGnmSubmitCommandBuffers")
                return "Bibliothèques PS4 : bibliothèque absente acceptée"
            } catch SimulatedLibraryResolver.ResolutionError.libraryUnavailable {}
            return "Bibliothèques système OK ✅ • libkernel simulée • sceKernelGetProcessTime résolu (2500 µs) • symbole et bibliothèque indisponibles refusés • pas de chargement de modules PS4"
        } catch {
            return "Bibliothèques PS4 : échec de résolution du symbole de test"
        }
    }

    static func testPS4CompatibilityScaffold() -> String {
        do {
            let first = try PS4CompatibilityShim.invoke("sceKernelGetProcessTime", virtualTicks: 1200)
            let second = try PS4CompatibilityShim.invoke("sceKernelGetProcessTime", virtualTicks: 1250)
            guard first == 1200, second == 1250, second - first == 50 else {
                return "Compatibilité PS4 : horloge virtuelle incohérente"
            }
            for symbol in ["sceKernelAllocateDirectMemory", "sceKernelCreateEqueue", "sceKernelUnknownSymbol"] {
                do {
                    _ = try PS4CompatibilityShim.invoke(symbol, virtualTicks: 1250)
                    return "Compatibilité PS4 : fonction non implémentée acceptée"
                } catch PS4CompatibilityShim.ShimError.unsupported {
                    continue
                }
            }
            return "Compatibilité PS4 (base) OK ✅ • sceKernelGetProcessTime simulé (1200 → 1250 µs) • 2 fonctions absentes et appel inconnu refusés • aucun noyau PS4 exécuté"
        } catch {
            return "Compatibilité PS4 : échec du service de temps virtuel"
        }
    }

    static func testProcessMemoryIsolation() -> String {
        do {
            var manager = MaxPS4VirtualProcessManager()
            let a = try manager.create()
            let b = try manager.create()
            guard manager.scheduleNext() == a else { return "Isolation : ordonnanceur A indisponible" }
            try manager.allocateCurrentMemory(at: 0x9000, size: 4096)
            guard manager.process(pid: a)?.cpu.guestMemory.allocatedBytes == 4096 else {
                return "Isolation : allocation A échouée"
            }
            guard manager.scheduleNext() == b else { return "Isolation : ordonnanceur B indisponible" }
            guard manager.process(pid: b)?.cpu.guestMemory.allocatedBytes == 0 else {
                return "Isolation : fuite mémoire entre processus"
            }
            try manager.allocateCurrentMemory(at: 0x9000, size: 2048)
            guard manager.process(pid: a)?.cpu.guestMemory.allocatedBytes == 4096,
                  manager.process(pid: b)?.cpu.guestMemory.allocatedBytes == 2048 else {
                return "Isolation : espaces mémoire non indépendants"
            }
            try manager.freeCurrentMemory(at: 0x9000, size: 2048)
            guard manager.scheduleNext() == a else { return "Isolation : retour au processus A impossible" }
            try manager.freeCurrentMemory(at: 0x9000, size: 4096)
            guard manager.process(pid: a)?.cpu.guestMemory.allocatedBytes == 0,
                  manager.process(pid: b)?.cpu.guestMemory.allocatedBytes == 0,
                  manager.terminate(pid: a), manager.terminate(pid: b),
                  manager.count == 0 else { return "Isolation : nettoyage incomplet" }
            return "Processus + mémoire OK ✅ • 2 processus isolés • allocations virtuelles 4096/2048 octets • adresses identiques sans partage • libération et fermeture validées • simulation, pas de noyau PS4"
        } catch {
            return "Processus + mémoire : échec • \(error.localizedDescription)"
        }
    }

    static func testPS4VirtualMemoryService() -> String {
        // A safe, guest-only proof of concept. NOT Sony direct-memory semantics.
        do {
            var memory = MaxPS4GuestMemory()
            let address: UInt64 = 0x8000
            let size = 4096
            try memory.mapZeroFilled(at: address, size: size)
            guard memory.allocatedBytes == size else {
                return "Mémoire PS4 expérimentale : allocation incohérente"
            }
            let testBytes = Data([0x4D, 0x41, 0x58, 0x34])
            try memory.write(testBytes, at: address)
            guard try memory.read(at: address, count: testBytes.count) == testBytes else {
                return "Mémoire PS4 expérimentale : erreur de lecture"
            }
            try memory.protect(at: address, size: size, permissions: [.read])
            guard try memory.read(at: address, count: testBytes.count) == testBytes else {
                return "Mémoire PS4 expérimentale : lecture protégée incorrecte"
            }
            try memory.unmap(at: address, size: size)
            guard memory.allocatedBytes == 0 else {
                return "Mémoire PS4 expérimentale : libération incomplète"
            }
            return "Mémoire virtuelle OK ✅ • 4096 octets alloués, lecture/écriture vérifiées, protection lecture seule, libération validée • aucune mémoire PS4 réelle"
        } catch {
            return "Mémoire virtuelle : échec • \(error.localizedDescription)"
        }
    }

    static func testSimulatedKernelServices() -> String {
        // Synthetic syscall numbers are private to this prototype, not PS4 ABI.
        do {
            var cpu = MaxPS4CPUPrototype()
            // number 0 = heartbeat, number 1 = allocated bytes.
            try cpu.prepareGuestMemory(address: 0x9000, size: 4096)
            try cpu.run([
                0x31, 0xC0, 0x0F, 0x05, // XOR EAX,EAX; SYSCALL => 42
                0x48, 0xB8, 0x01, 0, 0, 0, 0, 0, 0, 0,
                0x0F, 0x05, 0xC3 // allocated bytes => 4096
            ])
            guard cpu.rax == 4096, cpu.executedInstructions == 5,
                  cpu.guestMemory.allocatedBytes == 4096 else {
                return "Services virtuels : résultat inattendu"
            }
            var rejected = MaxPS4CPUPrototype()
            do {
                try rejected.run([0x48, 0xB8, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0x0F, 0x05])
                return "Services virtuels : appel inconnu accepté (échec)"
            } catch {
                return "Services virtuels OK ✅ • appel santé = 42, mémoire = 4096 octets, appel inconnu refusé • services simulés uniquement, pas de syscalls PS4"
            }
        } catch {
            return "Services virtuels : erreur \(error.localizedDescription)"
        }
    }

    static func bootELFIntegrationTest() -> String {
        // The integration test builds an independent synthetic ELF64 fixture,
        // maps its PT_LOAD segment and runs its entry point in guest memory.
        if MaxPS4ELFLoader.integrationTest() {
            return "ELF64 → mémoire virtuelle → CPU : OK ✅ • segment chargé, point d’entrée exécuté, écriture mémoire contrôlée • aucun code PS4 réel exécuté"
        }
        return "ELF64 → mémoire virtuelle → CPU : ÉCHEC • vérifier le chargeur et l’interpréteur"
    }

    static func bootSelfTest() -> String {
        do {
            var manager = MaxPS4VirtualProcessManager()
            let pid = try manager.create()
            guard manager.scheduleNext() == pid else { return "Environnement virtuel : ordonnanceur indisponible" }
            let code: [UInt8] = [
                0x48, 0xB8, 0x2A, 0, 0, 0, 0, 0, 0, 0,
                0xC3
            ]
            try manager.loadCurrent(code, at: 0x4000)
            try manager.runLoadedCurrent(at: 0x4000, length: code.count)
            guard let process = manager.process(pid: pid),
                  process.cpu.rax == 42,
                  process.cpu.executedInstructions == 2,
                  process.cpu.guestMemory.allocatedBytes == code.count else {
                return "Environnement virtuel : échec de l'exécution du programme de test"
            }
            guard manager.terminate(pid: pid), manager.count == 0 else {
                return "Environnement virtuel : échec de fermeture du processus"
            }
            return "Environnement virtuel OK : processus \(pid), mémoire isolée, CPU x86-64 de test, programme exécuté (RAX=42), processus arrêté • aucun jeu PS4 lancé"
        } catch {
            return "Environnement virtuel : \(error.localizedDescription)"
        }
    }
}
