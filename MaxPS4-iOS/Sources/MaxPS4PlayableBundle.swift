import Foundation

struct MaxPS4PlayableBundle: Decodable {
    struct Module: Decodable { let path: String; let loadBias: UInt64; let dependencies: [String] }
    struct Frame: Decodable { let pixels: UInt64; let input: UInt64; let width: UInt32; let height: UInt32 }
    let root: String
    let modules: [Module]
    let frame: Frame
    static func basename(_ name: String) -> Bool {
        !name.isEmpty && name.utf8.count <= 255 && name != "." && name != ".." &&
            !name.contains("/") && !name.contains("\\") && !name.contains("\0")
    }
    static func read(_ directory: URL, _ name: String, limit: Int) throws -> Data {
        guard basename(name) else { throw CocoaError(.fileReadInvalidFileName) }
        let parent = directory.resolvingSymlinksInPath().standardizedFileURL
        let file = parent.appendingPathComponent(name).resolvingSymlinksInPath()
        guard file.deletingLastPathComponent() == parent else { throw CocoaError(.fileReadNoPermission) }
        let handle = try FileHandle(forReadingFrom: file)
        defer { try? handle.close() }
        let size = try handle.seekToEnd()
        guard size > 0 && size <= UInt64(limit) else { throw CocoaError(.fileReadTooLarge) }
        try handle.seek(toOffset: 0)
        let bytes = try handle.read(upToCount: Int(size)) ?? Data()
        guard bytes.count == Int(size) else { throw CocoaError(.fileReadCorruptFile) }
        return bytes
    }
    static func load(_ url: URL) throws -> (Self, [Data]) {
        let manifest = try JSONDecoder().decode(Self.self, from: read(url, "maxps4.json", limit: 65536))
        guard basename(manifest.root), !manifest.modules.isEmpty, manifest.modules.count <= 128,
              Set(manifest.modules.map(\.path)).count == manifest.modules.count,
              manifest.modules.contains(where: { $0.path == manifest.root }),
              manifest.modules.allSatisfy({ basename($0.path) && $0.dependencies.count <= 128 && $0.dependencies.allSatisfy(basename) }),
              manifest.frame.width > 0, manifest.frame.width <= 320,
              manifest.frame.height > 0, manifest.frame.height <= 240 else { throw CocoaError(.fileReadCorruptFile) }
        var total = 0
        let data = try manifest.modules.map { module -> Data in
            let bytes = try read(url, module.path, limit: 32 * 1024 * 1024)
            total += bytes.count
            guard total <= 64 * 1024 * 1024 else { throw CocoaError(.fileReadTooLarge) }
            return bytes
        }
        return (manifest, data)
    }
}

