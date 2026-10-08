import SwiftUI

/// An original arcade-style maze mini-game, separate from PS4 emulation.
struct MaxPS4MazeGame: View {
    private struct Cell: Hashable {
        let row: Int
        let column: Int
    }

    private static let maze = [
        "#########",
        "#.......#",
        "#.##.##.#",
        "#...#...#",
        "###...###",
        "#...#...#",
        "#.##.##.#",
        "#.......#",
        "#########"
    ]
    private static let start = Cell(row: 1, column: 1)
    private static let ghostStart = Cell(row: 7, column: 7)

    @State private var player = Cell(row: 1, column: 1)
    @State private var ghost = Cell(row: 7, column: 7)
    @State private var pellets = Set<Cell>()
    @State private var score = 0
    @State private var moves = 0
    @State private var lost = false
    @State private var won = false
    @State private var powerTurns = 0
    @State private var bestScore = 0
    private static let powerCells: Set<Cell> = [Cell(row: 1, column: 7), Cell(row: 7, column: 1)]

    private var finished: Bool { lost || won }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text("MAX MAZE")
                    .font(.system(size: 16, weight: .black, design: .rounded))
                    .foregroundStyle(.cyan)
                Spacer()
                Text("Score : \(score) • Record : \(bestScore)")
                    .font(.subheadline.monospacedDigit().bold())
            }

            Text("Ramasse toutes les pastilles et évite le fantôme !")
                .font(.caption)
                .foregroundStyle(.white.opacity(0.8))

            VStack(spacing: 2) {
                ForEach(0..<Self.maze.count, id: \.self) { row in
                    HStack(spacing: 2) {
                        ForEach(0..<Self.maze[row].count, id: \.self) { col in
                            tile(Cell(row: row, column: col))
                        }
                    }
                }
            }
            .frame(maxWidth: .infinity)

            if powerTurns > 0 && !finished {
                Text("SUPER PASTILLE : fantôme ralenti (\(powerTurns) tours)")
                    .foregroundStyle(.cyan)
                    .font(.caption.bold())
            }

            if won {
                Text("Victoire ! Toutes les pastilles sont ramassées.")
                    .foregroundStyle(.green)
                    .font(.caption.bold())
            } else if lost {
                Text("Perdu ! Le fantôme t'a attrapé.")
                    .foregroundStyle(.orange)
                    .font(.caption.bold())
            } else {
                Text("Pastilles restantes : \(pellets.count)")
                    .foregroundStyle(.white.opacity(0.8))
                    .font(.caption)
            }

            VStack(spacing: 6) {
                directionButton("chevron.up", row: -1, col: 0)
                HStack(spacing: 22) {
                    directionButton("chevron.left", row: 0, col: -1)
                    directionButton("chevron.down", row: 1, col: 0)
                    directionButton("chevron.right", row: 0, col: 1)
                }
            }
            .frame(maxWidth: .infinity)

            Button("Recommencer") { reset() }
                .font(.subheadline.bold())
                .foregroundStyle(.cyan)

            Text("Mini-jeu iOS indépendant : il ne lance pas de jeu PS4.")
                .font(.caption2)
                .foregroundStyle(.white.opacity(0.6))
        }
        .padding(14)
        .background(.white.opacity(0.07), in: RoundedRectangle(cornerRadius: 15))
        .onAppear {
            if pellets.isEmpty && !finished { reset() }
        }
    }

    private func tile(_ cell: Cell) -> some View {
        ZStack {
            RoundedRectangle(cornerRadius: 4)
                .fill(isWall(cell) ? Color.blue.opacity(0.85) : Color.black.opacity(0.5))
            if player == cell {
                Circle().fill(.yellow).padding(4)
            } else if ghost == cell {
                Circle().fill(powerTurns > 0 ? .gray : .purple).padding(4)
                HStack(spacing: 3) {
                    Circle().fill(.white).frame(width: 4, height: 4)
                    Circle().fill(.white).frame(width: 4, height: 4)
                }
            } else if pellets.contains(cell) {
                Circle().fill(Self.powerCells.contains(cell) ? .cyan : .yellow.opacity(0.8))
                    .frame(width: Self.powerCells.contains(cell) ? 12 : 5, height: Self.powerCells.contains(cell) ? 12 : 5)
            }
        }
        .frame(width: 27, height: 27)
        .accessibilityLabel(player == cell ? "Joueur" : ghost == cell ? "Fantôme" : isWall(cell) ? "Mur" : pellets.contains(cell) ? "Pastille" : "Vide")
    }

    private func directionButton(_ icon: String, row: Int, col: Int) -> some View {
        Button { move(row: row, col: col) } label: {
            Image(systemName: icon)
                .font(.headline.bold())
                .frame(width: 54, height: 38)
                .background(.cyan.opacity(0.17), in: RoundedRectangle(cornerRadius: 10))
        }
        .disabled(finished)
        .accessibilityLabel("Déplacer " + (row == -1 ? "haut" : row == 1 ? "bas" : col == -1 ? "gauche" : "droite"))
    }

    private func isWall(_ cell: Cell) -> Bool {
        guard cell.row >= 0, cell.row < Self.maze.count,
              cell.column >= 0, cell.column < Self.maze[cell.row].count else { return true }
        let characters = Array(Self.maze[cell.row])
        return characters[cell.column] == "#"
    }

    private func reset() {
        player = Self.start
        ghost = Self.ghostStart
        pellets = Set(Self.maze.indices.flatMap { row in
            (0..<Self.maze[row].count).compactMap { col -> Cell? in
                let cell = Cell(row: row, column: col)
                return isWall(cell) || cell == Self.start || cell == Self.ghostStart ? nil : cell
            }
        })
        score = 0
        powerTurns = 0
        moves = 0
        lost = false
        won = false
    }

    private func move(row: Int, col: Int) {
        guard !finished else { return }
        let next = Cell(row: player.row + row, column: player.column + col)
        guard !isWall(next) else { return }
        player = next
        moves += 1
        if pellets.remove(next) != nil {
            if Self.powerCells.contains(next) {
                powerTurns = 5
                score += 50
            } else {
                score += 10
            }
            bestScore = max(bestScore, score)
        }
        if next == ghost { lost = true; return }
        if pellets.isEmpty { won = true; return }
        // Ghost advances every third move so the maze is actually playable.
        if powerTurns > 0 {
            powerTurns -= 1
        } else if moves % 3 == 0 {
            let candidates = [
                Cell(row: ghost.row - 1, column: ghost.column),
                Cell(row: ghost.row + 1, column: ghost.column),
                Cell(row: ghost.row, column: ghost.column - 1),
                Cell(row: ghost.row, column: ghost.column + 1)
            ].filter { !isWall($0) }
            if let nextGhost = candidates.min(by: {
                abs($0.row - player.row) + abs($0.column - player.column) <
                    abs($1.row - player.row) + abs($1.column - player.column)
            }) {
                ghost = nextGhost
            }
            if ghost == player { lost = true }
        }
    }
}
