import Foundation

@main
struct OrbisLinkerBridgeTest {
    static func main() throws {
        precondition(CommandLine.arguments.count == 2)
        let directory = URL(fileURLWithPath: CommandLine.arguments[1])
        let provider = [UInt8](try Data(contentsOf: directory.appendingPathComponent("provider.prx")))
        let consumer = [UInt8](try Data(contentsOf: directory.appendingPathComponent("app.elf")))
        provider.withUnsafeBufferPointer { p in
            consumer.withUnsafeBufferPointer { c in
                "provider.prx".withCString { providerPath in
                    "app.elf".withCString { consumerPath in
                        var dependency: UnsafePointer<CChar>? = providerPath
                        withUnsafePointer(to: &dependency) { dependencies in
                            let modules = [
                                maxps4_orbis_module(path: consumerPath, bytes: c.baseAddress, size: c.count,
                                    load_bias: 0x100000, dependencies: dependencies, dependency_count: 1),
                                maxps4_orbis_module(path: providerPath, bytes: p.baseAddress, size: p.count,
                                    load_bias: 0x200000, dependencies: nil, dependency_count: 0)
                            ]
                            modules.withUnsafeBufferPointer { m in
                                var image: OpaquePointer?
                                var diagnostic = [CChar](repeating: 0, count: 256)
                                precondition(maxps4_orbis_link(m.baseAddress, m.count, 1024, &image, &diagnostic, 256) == 0)
                                defer { maxps4_orbis_close(image) }
                                precondition(maxps4_orbis_mapping_count(image) == 4)
                                var mapping = maxps4_orbis_mapping()
                                precondition(maxps4_orbis_get_mapping(image, 0, &mapping) == 0)
                                precondition(mapping.address == 0x201000 && mapping.permissions == 5)
                                var entry: UInt64 = 0
                                precondition(maxps4_orbis_entry(image, consumerPath, &entry) == 0 && entry == 0x101000)
                                var bytes = [UInt8](repeating: 0, count: 8)
                                precondition(maxps4_orbis_read(image, 0x102000, &bytes, 8) == 0)
                                precondition(bytes == [0, 0x10, 0x20, 0, 0, 0, 0, 0])
                                bytes = [UInt8](repeating: 0xa5, count: 8)
                                precondition(maxps4_orbis_read(image, 0x1020ff, &bytes, 8) < 0)
                                precondition(bytes.allSatisfy { $0 == 0xa5 })
                                var failed: OpaquePointer?
                                precondition(maxps4_orbis_link(m.baseAddress, m.count, 1, &failed, &diagnostic, 256) < 0)
                                precondition(failed == nil && diagnostic[0] != 0)
                            }
                        }
                    }
                }
            }
        }
        print("PASS Swift Orbis bridge: linked NID, mapping, entry, exact read and transactional failure")
    }
}
