#include "core/notation.h"
#include "training/openings.h"
#include "uci/uci_engine.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>

using namespace chess;
using namespace training;

static int cp(const uci::Analysis& a) {
    if (a.lines.empty()) return 0;
    const uci::Score s = a.lines[0].score;
    return s.mate ? (s.value > 0 ? 10000 : -10000) : s.value;
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const fs::path root = CHESS_SOURCE_DIR;
    const int depth = argc > 1 ? std::atoi(argv[1]) : 18;
    const int limit = argc > 2 ? std::atoi(argv[2]) : 70;
    uci::Engine sf;
    std::string err;
    if (!sf.start(uci::findStockfish(root / "third_party" / "stockfish"), &err)) {
        std::cout << "Stockfish не запустился: " << err << "\n";
        return 2;
    }
    sf.setOption("Threads", "4");
    sf.setOption("Hash", "256");

    int bad = 0, checked = 0;
    for (const Repertoire& rep : loadRepertoires(root / "assets" / "openings" / "openings.txt", {})) {
        for (const std::string& e : checkRepertoire(rep)) std::cout << "ФОРМАТ " << e << "\n", ++bad;
        const Color me = rep.black ? Black : White;
        std::set<std::string> seen;
        for (const OpeningLine& l : rep.lines) {
            Position p;
            std::vector<std::string> played;
            for (const Move m : lineMoves(l)) {
                const std::string u = toUCI(m);
                if (p.sideToMove() == me && seen.insert(positionKey(p) + u).second) {
                    ++checked;
                    const uci::Analysis best = sf.analyse("startpos", played, depth, 0);
                    int loss = 0;
                    if (best.bestmove != u) {
                        auto after = played;
                        after.push_back(u);
                        loss = cp(best) + cp(sf.analyse("startpos", after, depth, 0));
                    }
                    if (loss > limit) {
                        ++bad;
                        Position q = p;
                        const Move b = parseUCI(q, best.bestmove);
                        std::cout << rep.id << " «" << l.title << "»: " << p.fullmoveNumber() << ". " << toSAN(p, m)
                                  << " теряет " << loss << " (лучше " << toSAN(q, b) << ", оценка " << cp(best) << ")\n";
                    }
                }
                played.push_back(u);
                p.makeMove(m);
            }
        }
        std::cout << rep.id << ": веток " << rep.lines.size() << ", проверено ходов " << checked << "\n" << std::flush;
    }
    std::cout << (bad ? "ЕСТЬ ЗАМЕЧАНИЯ: " : "Всё чисто, замечаний: ") << bad << "\n";
    return bad ? 1 : 0;
}
