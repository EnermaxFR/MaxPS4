import Foundation
import CoreGraphics

@main enum PlayableSwiftBridgeTest {
    static func check(_ condition: @autoclosure () -> Bool, _ label: String) {
        guard condition() else { fatalError(label) }
    }
    static func main() throws {
        let directory = URL(fileURLWithPath: CommandLine.arguments[1])
        let (manifest, bytes) = try MaxPS4PlayableBundle.load(directory)
        check(manifest.modules.count == 2, "SCE and PRX import")
        check(!MaxPS4PlayableBundle.basename("../app.elf"), "Traversal")
        check(!MaxPS4PlayableBundle.basename("a\0b"), "NUL path")
        check(MaxPS4GuestFrameImage.make(bytes: Data(),width: 0,height: 24) == nil, "Zero dimensions")
        check(MaxPS4GuestFrameImage.make(bytes: Data(count: 3072),width: UInt32.max,height: 24) == nil, "Overflow dimensions")
        check(MaxPS4GuestFrameImage.make(bytes: Data(count: 3071),width: 32,height: 24) == nil, "Short snapshot")
        for _ in 0..<2 {
            var engine: OpaquePointer?
            check(maxps4_fex_engine_open(&engine) == 0, "Open")
            guard let engine else { fatalError("No engine") }
            defer { maxps4_fex_engine_close(engine) }
            let app = bytes[0] as NSData, provider = bytes[1] as NSData
            var reason = [CChar](repeating: 0,count: 1024)
            let prepared = "app.elf".withCString { root in
                "provider.prx".withCString { prx in
                    var dependency: UnsafePointer<CChar>? = prx
                    return withUnsafePointer(to: &dependency) { deps in
                        let modules = [
                            maxps4_orbis_module(path: root,bytes: app.bytes.assumingMemoryBound(to: UInt8.self),size: app.length,load_bias: 1048576,dependencies: deps,dependency_count: 1),
                            maxps4_orbis_module(path: prx,bytes: provider.bytes.assumingMemoryBound(to: UInt8.self),size: provider.length,load_bias: 2097152,dependencies: nil,dependency_count: 0)]
                        return modules.withUnsafeBufferPointer { modules in
                            maxps4_fex_engine_prepare_orbis(engine,modules.baseAddress,modules.count,root,65536,&reason,reason.count)
                        }
                    }
                }
            }
            check(prepared == 0, "Prepare: \(String(cString: reason))")
            let f = manifest.frame
            check(maxps4_fex_engine_configure_frame(engine,f.pixels,f.input,f.width,f.height) == 0,"Configure")
            check(maxps4_fex_engine_initialize(engine) == 0,"Real FEX init")
            var previous = Data()
            var neutralSprite = Data()
            for (index, input) in [UInt32(0),16,0,2,16,8].enumerated() {
                var pixels = Data(count: Int(f.width*f.height*4))
                var sequence: UInt64 = 0; var backend: UInt32 = 0
                let status = pixels.withUnsafeMutableBytes {
                    maxps4_fex_engine_step_frame(engine,input,$0.bindMemory(to: UInt8.self).baseAddress,$0.count,&sequence,&backend)
                }
                check(status == 0 && backend == 2 && sequence == UInt64(index+1), "Native ARM64 frame")
                if index == 0 { neutralSprite = pixels.subdata(in: 824..<828) }
                if index == 1 {
                    let actionSprite = pixels.subdata(in: 824..<828)
                    check(actionSprite != neutralSprite, "Action must visibly change guest sprite RGBA")
                    check(actionSprite[2] != neutralSprite[2], "Action must change blue")
                }
                if index == 2 {
                    check(pixels.subdata(in: 824..<828) == neutralSprite, "Action release restores original guest color")
                }
                check(pixels != previous,"Guest input changes image")
                previous = pixels
                let image = MaxPS4GuestFrameImage.make(bytes: pixels,width: f.width,height: f.height)!
                let retained = image.dataProvider!.data! as Data
                check(retained == pixels,"CGImage contains exact guest snapshot")
                var readback = Data(count: pixels.count)
                let drawn = readback.withUnsafeMutableBytes { buffer -> Bool in
                    guard let context = CGContext(data: buffer.baseAddress,width: Int(f.width),height: Int(f.height),
                        bitsPerComponent: 8,bytesPerRow: Int(f.width)*4,space: CGColorSpaceCreateDeviceRGB(),
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return false }
                    context.draw(image,in: CGRect(x: 0,y: 0,width: Int(f.width),height: Int(f.height)))
                    return true
                }
                check(drawn && readback == pixels,"Native CoreGraphics render readback")
                print("PASS Swift folder -> SCE/PRX -> FEX backend=\(backend) -> guest frame \(sequence) -> CGImage -> identical native render")
            }
        }
        print("PASS Swift import, native display bytes, inputs, multiple frames and engine restart")
    }
}
