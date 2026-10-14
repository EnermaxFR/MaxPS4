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
        print("PASS V2 real Swift/C snapshot validation, legal fixture SHA256, W+X refusal, redacted export; no execution in this test")
    }
}
