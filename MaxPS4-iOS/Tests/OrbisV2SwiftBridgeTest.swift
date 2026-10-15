import Foundation
import CryptoKit
@main
struct OrbisV2SwiftBridgeTest {
    static func main() {
        let fixture = MaxPS4OrbisTest.fixture()
        let digest = SHA256.hash(data: fixture).map { String(format: "%02x", $0) }.joined()
        precondition(digest == "a80acf5c6b919cae109c01ec135516807bfc2c358c940f6f0d8c0abe3e5f57f0")
        let checked = MaxPS4OrbisTest.inspect(fixture)
        precondition(checked.ready && checked.export.contains("execution=not_attempted"))
        precondition(!checked.export.contains("test.elf") && !checked.export.contains("NID="))
        var corrupt=fixture; corrupt[68]=7
        precondition(!MaxPS4OrbisTest.inspect(corrupt).ready)
        precondition(!MaxPS4OrbisTest.inspect(Data()).ready)
        precondition(MaxPS4OrbisTest.run(MaxPS4OrbisTest.inspect(corrupt)) == "execution=refused_validation")
        let second = MaxPS4OrbisTest.inspect(MaxPS4OrbisTest.fixture(initial: 7))
        precondition(second.ready)
        let tests = MaxPS4OrbisTest.autotests()
        precondition(tests.contains("rax=42") && tests.contains("rax=9"))
        precondition(tests.components(separatedBy: "fixture_verified=true").count == 5)
        precondition(tests.components(separatedBy: "cpu_state_verified=true").count == 5)
        precondition(tests.contains("wx_refused=true") && tests.contains("truncated_refused=true"))
        precondition(CommandLine.arguments.count == 2)
        let m1 = MaxPS4OrbisTest.m1Autotests(bundleDirectory: URL(fileURLWithPath: CommandLine.arguments[1]))
        print(m1)
        precondition(m1.contains("m1_threads=PASS") && m1.contains("m1_multi_module_tls=PASS"))
        precondition(m1.contains("budget_refusal=PASS"))
        print(tests)
        print("PASS V3 real Swift/C snapshot validation, legal fixture SHA256, W+X refusal, redacted export; real host FEX 42/9 and CPU state verified; no iPhone execution")
    }
}
