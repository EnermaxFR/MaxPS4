import Foundation

@main
struct LibraryPresentationTest {
    struct Item: Equatable {
        let name: String
        let file: String
        let date: Date
    }
    static func main() {
        let old = Date(timeIntervalSince1970: 100)
        let recent = Date(timeIntervalSince1970: 200)
        let items = [Item(name: "Étoile", file: "CUSA00002.PKG", date: old),
                     Item(name: "Alpha 10", file: "test.elf", date: recent),
                     Item(name: "Alpha 2", file: "homebrew.self", date: old)]
        func query(_ text: String = "", _ sort: MaxPS4LibrarySort = .name) -> [Item] {
            MaxPS4LibraryPresentation.results(items, query: text, sort: sort,
                                              title: \.name, fileName: \.file, importedAt: \.date)
        }
        precondition(query().map(\.name) == ["Alpha 2", "Alpha 10", "Étoile"])
        precondition(query("  \n ") == query())
        precondition(query("ETOILE") == [items[0]], "Accent/case insensitive title search")
        precondition(query("cusa00002.pkg") == [items[0]], "Search persisted filenames")
        precondition(query("  étoile   PKG ") == [items[0]], "All search tokens must match")
        precondition(query("Alpha missing").isEmpty)
        precondition(query("", .newest).map(\.name) == ["Alpha 10", "Alpha 2", "Étoile"])
        precondition(query("", .oldest).map(\.name) == ["Alpha 2", "Étoile", "Alpha 10"])
        precondition(query("Alpha", .newest).map(\.name) == ["Alpha 10", "Alpha 2"])
        let duplicates = [items[0], items[0]]
        precondition(MaxPS4LibraryPresentation.results(duplicates, query: "", sort: .name,
                      title: \.name, fileName: \.file, importedAt: \.date).count == 2)
        precondition(items[0].name == "Étoile", "Presentation must not change stored records")
        precondition(!MaxPS4LibraryPresentation.canRequestLaunch(fileName: "game.elf", backendReady: false))
        precondition(!MaxPS4LibraryPresentation.canRequestLaunch(fileName: "game.PKG", backendReady: true))
        precondition(MaxPS4LibraryPresentation.canRequestLaunch(fileName: "eboot.bin", backendReady: true))
        let suite = "MaxPS4LibraryPresentationTest." + UUID().uuidString
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        defaults.set(MaxPS4Appearance.dark.rawValue, forKey: "maxps4.appearance")
        defaults.set(MaxPS4LibraryLayout.list.rawValue, forKey: "maxps4.library.layout")
        defaults.set(MaxPS4LibrarySort.newest.rawValue, forKey: "maxps4.library.sort")
        precondition(MaxPS4Appearance(rawValue: defaults.string(forKey: "maxps4.appearance")!) == .dark)
        precondition(MaxPS4LibraryLayout(rawValue: defaults.string(forKey: "maxps4.library.layout")!) == .list)
        precondition(MaxPS4LibrarySort(rawValue: defaults.string(forKey: "maxps4.library.sort")!) == .newest)
        precondition(MaxPS4Appearance(rawValue: "invalid") == nil)
        print("PASS library: Unicode/file/token search, natural/date sorting, duplicates, launch availability and preference storage")
    }
}
