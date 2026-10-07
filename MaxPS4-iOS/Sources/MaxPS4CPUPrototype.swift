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
            var processB = MaxPS4VirtualProcess(pid: 102)
            try processA.cpu.prepareGuestMemory(address: 0x5000, size: 4096)
            try processA.cpu.run([0x48, 0xB8, 0x2A, 0, 0, 0, 0, 0, 0, 0, 0xC3])
            try processA.cpu.storeRAX(address: 0x5000)
            guard processA.pid == 101, processB.pid == 102,
                  try processA.cpu.guestMemory.read(at: 0x5000, count: 1) == Data([42]),
                  processB.cpu.guestMemory.allocatedBytes == 0 else { return false }
            do {
                try processB.cpu.guestMemory.read(at: 0x5000, count: 1)
                return false
            } catch MaxPS4GuestMemory.MemoryError.outOfBounds {}
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

    init(pid: UInt32) {
        self.pid = pid
    }
}

 
/// Bounded registry for toy guest processes; never starts host processes.
struct MaxPS4VirtualProcessManager {
    enum ProcessError: Error {
        case capacityReached
    }

    private var processes: [UInt32: MaxPS4VirtualProcess] = [:]
    private var nextPID: UInt32 = 100
    private let maximumProcesses = 16

    var count: Int { processes.count }

    mutating func create() throws -> UInt32 {
        guard processes.count < maximumProcesses else { throw ProcessError.capacityReached }
        let pid = nextPID
        nextPID += 1
        processes[pid] = MaxPS4VirtualProcess(pid: pid)
        return pid
    }

    func process(pid: UInt32) -> MaxPS4VirtualProcess? {
        processes[pid]
    }

    @discardableResult
    mutating func terminate(pid: UInt32) -> Bool {
        processes.removeValue(forKey: pid) != nil
    }
}
