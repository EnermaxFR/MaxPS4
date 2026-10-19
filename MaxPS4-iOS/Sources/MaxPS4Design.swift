import SwiftUI

extension MaxPS4Appearance {
    var colorScheme: ColorScheme? {
        switch self {
        case .system: return nil
        case .light: return .light
        case .dark: return .dark
        }
    }
}

// System colors retain contrast in both appearances and Increased Contrast.
enum MaxPS4Design {
    static let background = Color(uiColor: .systemGroupedBackground)
    static let surface = Color(uiColor: .secondarySystemGroupedBackground)
    static let border = Color(uiColor: .separator).opacity(0.25)
    static let accent = Color.accentColor
    static let spacing: CGFloat = 20
    static let radius: CGFloat = 20
}

private struct MaxPS4Surface: ViewModifier {
    func body(content: Content) -> some View {
        content
            .background(MaxPS4Design.surface, in: RoundedRectangle(cornerRadius: MaxPS4Design.radius))
            .overlay {
                RoundedRectangle(cornerRadius: MaxPS4Design.radius)
                    .strokeBorder(MaxPS4Design.border, lineWidth: 0.5)
            }
    }
}

extension View {
    func maxPS4Surface() -> some View { modifier(MaxPS4Surface()) }
}

struct MaxPS4SectionHeading: View {
    let title: String
    let subtitle: String
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title).font(.title2.weight(.semibold))
            Text(subtitle).font(.subheadline).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .accessibilityElement(children: .combine)
        .accessibilityAddTraits(.isHeader)
    }
}
