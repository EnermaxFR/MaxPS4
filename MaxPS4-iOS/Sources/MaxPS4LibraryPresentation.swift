import Foundation

// Presentation preferences only. These never change emulation or JIT settings.
enum MaxPS4LibraryLayout: String, CaseIterable, Identifiable, Hashable {
    case grid, list
    var id: String { rawValue }
    var title: String { self == .grid ? "Grille" : "Liste" }
    var symbol: String { self == .grid ? "square.grid.2x2" : "list.bullet" }
}

enum MaxPS4LibrarySort: String, CaseIterable, Identifiable, Hashable {
    case name, newest, oldest
    var id: String { rawValue }
    var title: String {
        switch self {
        case .name: return "Nom"
        case .newest: return "Ajouts récents"
        case .oldest: return "Ajouts anciens"
        }
    }
}

enum MaxPS4Appearance: String, CaseIterable, Identifiable, Hashable {
    case system, light, dark
    var id: String { rawValue }
    var title: String {
        switch self {
        case .system: return "Automatique"
        case .light: return "Clair"
        case .dark: return "Sombre"
        }
    }
}

enum MaxPS4LibraryPresentation {
    static func canRequestLaunch(fileName: String, backendReady: Bool) -> Bool {
        backendReady && !fileName.lowercased().hasSuffix(".pkg")
    }

    static func results<Item>(
        _ items: [Item], query: String, sort: MaxPS4LibrarySort,
        title: KeyPath<Item, String>, fileName: KeyPath<Item, String>, importedAt: KeyPath<Item, Date>
    ) -> [Item] {
        let words = query.split(whereSeparator: { $0.isWhitespace }).map(String.init)
        return items.enumerated().filter { entry in
            let searchable = entry.element[keyPath: title] + " " + entry.element[keyPath: fileName]
            return words.allSatisfy {
                searchable.range(of: $0, options: [.caseInsensitive, .diacriticInsensitive]) != nil
            }
        }.sorted { lhs, rhs in
            let left = lhs.element, right = rhs.element
            let leftDate = left[keyPath: importedAt], rightDate = right[keyPath: importedAt]
            if sort != .name && leftDate != rightDate {
                return sort == .newest ? leftDate > rightDate : leftDate < rightDate
            }
            let comparison = left[keyPath: title].localizedStandardCompare(right[keyPath: title])
            if comparison != .orderedSame { return comparison == .orderedAscending }
            let files = left[keyPath: fileName].localizedStandardCompare(right[keyPath: fileName])
            if files != .orderedSame { return files == .orderedAscending }
            return lhs.offset < rhs.offset
        }.map(\.element)
    }
}
