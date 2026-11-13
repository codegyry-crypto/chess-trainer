#include "core/notation.h"
#include "core/position.h"
#include "coach/coach.h"
#include "engine/bots.h"
#include "engine/search.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <string>

using namespace chess;
using namespace engine;

static int failures = 0, checks = 0;

static Position fen(const char* f) {
    Position p;
    if (!p.setFEN(f)) {
        std::cout << "FAIL bad FEN " << f << "\n";
        ++failures;
    }
    return p;
}

static void expectBest(const char* name, const char* f, const char* uci, int timeMs = 1000) {
    ++checks;
    Searcher s;
    Position p = fen(f);
    Limits lim;
    lim.timeMs = timeMs;
    const Result r = s.search(p, lim);
    const std::string got = toUCI(r.best);
    if (got != uci) {
        std::cout << "FAIL " << name << ": got " << got << " (score " << r.score << ", depth " << r.depth
                  << "), want " << uci << "\n";
        ++failures;
    } else {
        std::printf("  ok %-22s %s  score %6d  depth %2d  %.2fs\n", name, uci, r.score, r.depth, r.seconds);
    }
}

static void testTactics() {
    expectBest("мат в 1 (ладья)", "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", "a1a8");
    expectBest("детский мат", "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", "h5f7");
    expectBest("забрать ферзя", "4k3/8/8/3q4/8/8/8/3RK3 w - - 0 1", "d1d5");
    expectBest("вилка конём", "r3k3/8/8/1N6/8/8/4P3/4K3 w - - 0 1", "b5c7");
    {
        ++checks;
        Searcher s;
        Position p = fen("4k3/8/8/8/1p6/2Q5/8/4K3 w - - 0 1");
        Limits lim;
        lim.timeMs = 500;
        const Result r = s.search(p, lim);
        if (r.best.from != parseSquare("c3")) {
            std::cout << "FAIL ферзь остался под боем: " << toUCI(r.best) << "\n";
            ++failures;
        } else std::printf("  ok %-22s %s\n", "ферзь не зевается", toUCI(r.best).c_str());
    }
    {
        ++checks;
        Searcher s;
        Position p = fen("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
        Limits lim;
        lim.timeMs = 500;
        const Result r = s.search(p, lim);
        if (r.score < MateBound) {
            std::cout << "FAIL мат не распознан, score " << r.score << "\n";
            ++failures;
        }
    }
}

static void testWac() {
    struct Pos {
        const char* fen;
        const char* best;
    };
    const Pos wac[] = {
        {"2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1", "g3g6"},
        {"8/7p/5k2/5p2/p1p2P2/Pr1pPK2/1P1R3P/8 b - - 0 1", "b3b2"},
        {"5rk1/1ppb3p/p1pb4/6q1/3P1p1r/2P1R2P/PP1BQ1P1/5RKN w - - 0 1", "e3g3"},
        {"r1bq2rk/pp3pbp/2p1p1pQ/7P/3P4/2PB1N2/PP3PPR/2KR4 w - - 0 1", "h6h7"},
        {"5k2/6pp/p1qN4/1p1p4/3P4/2PKP2Q/PP3r2/3R4 b - - 0 1", "c6c4"},
    };
    int solved = 0;
    for (const Pos& w : wac) {
        Searcher s;
        Position p = fen(w.fen);
        Limits lim;
        lim.timeMs = 2000;
        const Result r = s.search(p, lim);
        const bool ok = toUCI(r.best) == w.best;
        solved += ok;
        std::printf("  WAC %s: %s (нужно %s), глубина %d, %llu позиций\n", ok ? "да " : "НЕТ",
                    toUCI(r.best).c_str(), w.best, r.depth, (unsigned long long)r.nodes);
    }
    std::printf("  решено %d из 5\n", solved);
    ++checks;
    if (solved < 3) {
        std::cout << "FAIL решено меньше 3 задач WAC\n";
        ++failures;
    }
}

static void testSpeed() {
    Searcher s;
    Position p;
    Limits lim;
    lim.timeMs = 3000;
    lim.maxDepth = 7;
    const Result r = s.search(p, lim);
    std::printf("  скорость: глубина %d за %.2fs, %llu позиций, %.0f тыс/с, ход %s\n", r.depth, r.seconds,
                (unsigned long long)r.nodes, r.nodes / std::max(r.seconds, 0.001) / 1000, toUCI(r.best).c_str());
    ++checks;
    if (r.depth < 5) {
        std::cout << "FAIL слишком медленно: глубина " << r.depth << "\n";
        ++failures;
    }
}

static void testBook() {
    ++checks;
    std::string err;
    if (!bookIsValid(&err)) {
        std::cout << "FAIL книга: " << err << "\n";
        ++failures;
    }
    ++checks;
    if (bookMoves({}).size() < 3) {
        std::cout << "FAIL в книге мало первых ходов\n";
        ++failures;
    }
    std::printf("  книга: %d линий, первых ходов %zu\n", bookLineCount(), bookMoves({}).size());
}

static void expectText(const char* name, const std::string& got, const std::string& part) {
    ++checks;
    if (got.find(part) == std::string::npos) {
        std::cout << "FAIL " << name << ": \"" << got << "\" не содержит \"" << part << "\"\n";
        ++failures;
    }
}

static void testCoach() {
    Searcher s;
    Position p = fen("4k3/8/2n5/8/8/8/8/3QK3 w - - 0 1");
    const auto bad = coach::checkMove(p, p.findLegal(parseSquare("d1"), parseSquare("d4")), s);
    ++checks;
    if (!bad.blunder) std::cout << "FAIL тренер не заметил зевок ферзя\n", ++failures;
    expectText("зевок", bad.reason, "ферзя на d4 без защиты");
    ++checks;
    if (coach::checkMove(p, p.findLegal(parseSquare("d1"), parseSquare("d2")), s).blunder)
        std::cout << "FAIL тренер ругает нормальный ход\n", ++failures;

    Position t = fen("4k3/8/2n5/8/3Q4/8/8/4K3 w - - 0 1");
    const auto th = coach::threats(t, White);
    ++checks;
    if (th.size() != 1 || th[0].from != parseSquare("c6") || th[0].to != parseSquare("d4"))
        std::cout << "FAIL угрозы: ждали Кc6 → d4\n", ++failures;

    Position c = fen("r3k2r/8/8/8/8/8/5r2/R3K2R w KQkq - 0 1");
    auto cp = coach::castlingProblems(c);
    ++checks;
    if (cp.size() != 1) std::cout << "FAIL рокировка: ждали 1 причину, получили " << cp.size() << "\n", ++failures;
    else expectText("рокировка под боем", cp[0], "поле f1 под боем");
    c = fen("r3k2r/8/8/8/8/8/8/R3K1R1 w Qkq - 0 1");
    cp = coach::castlingProblems(c);
    expectText("ладья ходила", cp.empty() ? "" : cp[0], "ладья h1 уже ходила");
    c = Position();
    cp = coach::castlingProblems(c);
    expectText("фигуры мешают", cp.empty() ? "" : cp[0], "стоят фигуры");

    Position h = fen("r3k3/8/8/1N6/8/8/4P3/4K3 w - - 0 1");
    const Move best = h.findLegal(parseSquare("b5"), parseSquare("c7"));
    expectText("подсказка 2", coach::hint(h, best, false, 2, true), "коня на b5");
    expectText("подсказка 3", coach::hint(h, best, false, 3, true), "Кc7+");
    std::printf("  тренер: зевки, угрозы, рокировка, подсказки — ок\n");
}

static void testBotsMove() {
    std::mt19937 rng(1);
    for (const BotProfile& b : bots()) {
        Searcher s;
        for (const char* f : {Position::StartFEN, "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
                              "8/5k2/8/8/3K4/8/4P3/8 w - - 0 1"}) {
            Position p = fen(f);
            const BotMove m = chooseMove(p, {}, b, s, rng, 300, nullptr);
            ++checks;
            if (p.findLegal(m.move.from, m.move.to, PieceType(m.move.promo)).isNone()) {
                std::cout << "FAIL бот " << b.name << " сделал нелегальный ход\n";
                ++failures;
            }
        }
    }
    std::printf("  все %zu ботов ходят легально\n", bots().size());
}

static double playGame(const BotProfile& w, const BotProfile& b, std::mt19937& rng, double timeScale) {
    Position pos;
    std::vector<Move> moves;
    Searcher sw, sb;
    for (int ply = 0; ply < 300; ++ply) {
        const GameStatus st = pos.status();
        if (st == GameStatus::Checkmate) return pos.sideToMove() == White ? 0.0 : 1.0;
        if (st != GameStatus::Ongoing) return 0.5;
        const bool white = pos.sideToMove() == White;
        BotProfile prof = white ? w : b;
        prof.timeMs = std::max(30, int(prof.timeMs * timeScale));
        const BotMove m = chooseMove(pos, moves, prof, white ? sw : sb, rng, 0, nullptr);
        pos.makeMove(m.move);
        moves.push_back(m.move);
    }
    return 0.5;
}

static void runMatches(int gamesPerPair) {
    std::mt19937 rng(12345);
    const auto& all = bots();
    std::printf("\nМатчи соседних ботов (%d партий на пару, цвета чередуются):\n", gamesPerPair);
    for (size_t i = 0; i + 1 < all.size(); ++i) {
        const BotProfile& weak = all[i];
        const BotProfile& strong = all[i + 1];
        double strongScore = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int g = 0; g < gamesPerPair; ++g) {
            const bool strongWhite = g % 2 == 0;
            const double r = strongWhite ? playGame(strong, weak, rng, 0.25) : playGame(weak, strong, rng, 0.25);
            strongScore += strongWhite ? r : 1 - r;
        }
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  %-12s vs %-12s  %4.1f : %4.1f   (%.0f с)\n", strong.name.c_str(), weak.name.c_str(), strongScore,
                    gamesPerPair - strongScore, sec);
        std::fflush(stdout);
    }
}

int main(int argc, char** argv) {
    if (argc > 4 && !std::strcmp(argv[1], "--pair")) {
        const BotProfile* a = botById(argv[2]);
        const BotProfile* b = botById(argv[3]);
        if (!a || !b) return 2;
        std::mt19937 rng(777);
        const int n = std::atoi(argv[4]);
        double score = 0;
        for (int g = 0; g < n; ++g) {
            const double r = g % 2 == 0 ? playGame(*a, *b, rng, 0.25) : playGame(*b, *a, rng, 0.25);
            score += g % 2 == 0 ? r : 1 - r;
        }
        std::printf("%s vs %s: %.1f : %.1f\n", a->name.c_str(), b->name.c_str(), score, n - score);
        return 0;
    }
    if (argc > 2 && !std::strcmp(argv[1], "--match")) {
        runMatches(std::atoi(argv[2]));
        return 0;
    }
    testBook();
    testTactics();
    testWac();
    testSpeed();
    testCoach();
    testBotsMove();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
