#include "analysis/analyzer.h"
#include "analysis/review.h"
#include "analysis/weakness.h"
#include "core/notation.h"
#include "uci/uci_engine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <thread>

using namespace chess;
using namespace analysis;

static int failures = 0, checks = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        ++checks;                                                                  \
        if (!(cond)) {                                                             \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

static Eval ev(int cp, const char* best = "") {
    Eval e;
    e.known = true;
    e.cp = cp;
    e.bestUci = best;
    return e;
}

static void testFormulas() {
    CHECK(std::abs(winPercent(0) - 50) < 1e-9);
    CHECK(winPercent(300) > 74 && winPercent(300) < 76);
    CHECK(winPercent(-300) < 26 && winPercent(-300) > 24);
    CHECK(winPercent(5000) == winPercent(1000));

    uci::Score mate3{true, 3}, mated2{true, -2}, cp{false, 45};
    CHECK(cpFromScore(mate3) == 9970);
    CHECK(cpFromScore(mated2) == -9980);
    CHECK(cpFromScore(cp) == 45);

    uci::Line l;
    CHECK(uci::parseInfo("info depth 18 seldepth 25 multipv 1 score cp -37 nodes 100 pv e7e5 g1f3 b8c6", l));
    CHECK(l.depth == 18 && l.score.value == -37 && !l.score.mate && l.pv.size() == 3 && l.pv[0] == "e7e5");
    CHECK(uci::parseInfo("info depth 9 multipv 2 score mate -4 pv a2a3", l) && l.multipv == 2 && l.score.mate);
    CHECK(!uci::parseInfo("info depth 9 score cp 20 lowerbound pv a2a3", l));
    CHECK(!uci::parseInfo("info string NNUE evaluation using nn.nnue", l));

    uci::Analysis a;
    a.bestmove = "e7e5";
    a.lines.push_back({1, 20, {false, 30}, {"e7e5"}});
    const Eval e = evalFromAnalysis(a, Black);
    CHECK(e.known && e.cp == -30 && e.bestUci == "e7e5");

    CHECK(formatEval(ev(140)) == "+1.4");
    CHECK(formatEval(ev(-30)) == "−0.3");
}

static const char* StartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static std::vector<Move> line(const char* fen, std::initializer_list<const char*> sans) {
    Position p;
    if (*fen) p.setFEN(fen);
    std::vector<Move> moves;
    for (const char* s : sans) {
        moves.push_back(parseSAN(p, s));
        p.makeMove(moves.back());
    }
    return moves;
}

static Eval ev2(int cp, const char* best, int second) {
    Eval e = ev(cp, best);
    e.hasSecond = true;
    e.secondCp = second;
    return e;
}

static void testReview() {
    std::vector<Move> moves = line("", {"e4", "Qh4"});
    std::vector<Eval> evals = {ev(30, "e2e4"), ev(30, "e7e5"), ev(900, "")};
    auto r = reviewMoves(evals, moves, StartFen);
    CHECK(r[0].grade == Grade::Best);
    CHECK(r[1].grade == Grade::Blunder);
    CHECK(r[1].winLoss > 30);

    evals = {ev(0, "x"), ev(-100, "x"), ev(-100, "x")};
    r = reviewMoves(evals, moves, StartFen);
    CHECK(r[0].grade == Grade::Inaccuracy || r[0].grade == Grade::Mistake);
    evals = {ev(0, "x"), ev(-40, "x"), ev(-40, "x")};
    CHECK(reviewMoves(evals, moves, StartFen)[0].grade == Grade::Good);
    evals = {ev(0, "x"), ev(-20, "x"), ev(-20, "x")};
    CHECK(reviewMoves(evals, moves, StartFen)[0].grade == Grade::Excellent);
    evals = {ev(0), Eval{}, Eval{}};
    CHECK(reviewMoves(evals, moves, StartFen)[0].grade == Grade::None);
    evals = {ev(9990, "x"), ev(1000, "x"), ev(1000, "x")};
    CHECK(reviewMoves(evals, moves, StartFen)[0].grade == Grade::Excellent);

    r = reviewMoves({ev(30, "e2e4"), ev(30, "e7e5"), ev(900, "")}, moves);
    CHECK(r[0].grade == Grade::Book);
    CHECK(r[1].grade == Grade::Blunder);
    r = reviewMoves({}, line("", {"e4", "e5", "Nf3"}));
    CHECK(r[0].grade == Grade::Book && r[1].grade == Grade::Book && r[2].grade == Grade::Book);
    CHECK(reviewMoves({}, line("", {"a4", "e5"}))[1].grade != Grade::Book);

    const char* forcedFen = "7k/8/8/8/8/8/1q6/K7 w - - 0 1";
    r = reviewMoves({ev(-900, "a1b2"), ev(-9990, "x")}, line(forcedFen, {"Kxb2"}), forcedFen);
    CHECK(r[0].grade == Grade::Forced);
    CHECK(reviewMoves({}, line(forcedFen, {"Kxb2"}), forcedFen)[0].grade == Grade::Forced);

    const char* sacFen = "4k3/8/8/2p5/8/8/8/3QK3 w - - 0 1";
    r = reviewMoves({ev(800, "d1d4"), ev(800, "x")}, line(sacFen, {"Qd4"}), sacFen);
    CHECK(r[0].grade == Grade::Brilliant);
    r = reviewMoves({ev(800, "d1d3"), ev(-200, "x")}, line(sacFen, {"Qd4"}), sacFen);
    CHECK(r[0].grade == Grade::Blunder);
    r = reviewMoves({ev(800, "d1d3"), ev(800, "x")}, line(sacFen, {"Qd3"}), sacFen);
    CHECK(r[0].grade == Grade::Best);

    {
        Position p;
        p.setFEN("4k3/6p1/5n2/8/8/8/1B6/4K3 w - - 0 1");
        const Move m = parseSAN(p, "Bxf6");
        const Piece cap = p.at(m.to);
        Position before = p;
        p.makeMove(m);
        CHECK(!isSacrifice(before, p, m, cap));
        p.setFEN("4k3/8/8/8/7n/8/6P1/4K1N1 w - - 0 1");
        const Move n = parseSAN(p, "Nf3");
        before = p;
        p.makeMove(n);
        CHECK(!isSacrifice(before, p, n, NoPiece));
        p.setFEN("4k3/8/2p5/8/8/8/8/3RK3 w - - 0 1");
        const Move q = parseSAN(p, "Rd5");
        before = p;
        p.makeMove(q);
        CHECK(isSacrifice(before, p, q, NoPiece));
        p.setFEN("4k3/7r/8/8/8/7N/8/4K3 w - - 0 1");
        const Move k = parseSAN(p, "Kd2");
        before = p;
        p.makeMove(k);
        CHECK(!isSacrifice(before, p, k, NoPiece));
        CHECK(toSquares(parseSAN(p, "Rxh3")) == "h7xh3");
    }

    r = reviewMoves({ev2(300, "d1d3", -200), ev(300, "x")}, line(sacFen, {"Qd3"}), sacFen);
    CHECK(r[0].grade == Grade::Great);
    r = reviewMoves({ev2(300, "d1d3", 250), ev(300, "x")}, line(sacFen, {"Qd3"}), sacFen);
    CHECK(r[0].grade == Grade::Best);

    r = reviewMoves({ev(500, "x"), ev(150, "x")}, line(sacFen, {"Qd3"}), sacFen);
    CHECK(r[0].grade == Grade::Miss);
    r = reviewMoves({ev(500, "x"), ev(-300, "x")}, line(sacFen, {"Qd3"}), sacFen);
    CHECK(r[0].grade == Grade::Blunder);

    const Summary s = summarize(reviewMoves({ev(30, "e2e4"), ev(30, "e7e5"), ev(900, "")}, moves, StartFen), White);
    CHECK(s.count[1][int(Grade::Blunder)] == 1 && s.count[0][int(Grade::Blunder)] == 0 &&
          s.count[0][int(Grade::Best)] == 1 && s.accuracy[0] > 99 && s.accuracy[1] < 40);
    CHECK(std::string(gradeName(Grade::Brilliant)) == "Бриллиантовый ход" && std::string(gradeSymbol(Grade::Great)) == "!");
}


static std::vector<Move> line(Position p, const char* uci) {
    std::vector<Move> out;
    std::string s(uci), u;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i < s.size() && s[i] != ' ') { u += s[i]; continue; }
        if (u.empty()) continue;
        out.push_back(parseUCI(p, u));
        p.makeMove(out.back());
        u.clear();
    }
    return out;
}

static void testWeakness() {
    const auto scholar = line(Position{}, "e2e4 e7e5 f1c4 b8c6 d1h5 g8f6 h5f7");
    std::vector<Eval> ev8 = {ev(20), ev(30), ev(25), ev(40), ev(30), ev(50, "g7g6"), ev(9990, "h5f7"), ev(10000)};
    ev8[6].mate = true, ev8[6].mateIn = 1;
    ev8[7].mate = true;
    GameMistakes g = findMistakes("", scholar, ev8, Black);
    CHECK(g.mistakes.size() == 1);
    const Mistake& m = g.mistakes[0];
    CHECK(m.ply == 5 && m.moveNumber == 3 && m.phase == 0 && m.piece == Knight);
    CHECK(m.kind == ErrorKind::AllowedMate && m.grade == Grade::Blunder);
    CHECK(m.puzzle && m.prevUci == "d1h5" && m.bestUci == "g7g6");
    CHECK(m.fen == "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/8/PPPP1PPP/RNBQK1NR w KQkq - 2 3");
    CHECK(mistakeKey(m).rfind("mine-", 0) == 0);
    CHECK(findMistakes("", scholar, ev8, White).mistakes.empty());

    ev8[5].hasSecond = true, ev8[5].secondCp = 60;
    CHECK(findMistakes("", scholar, ev8, Black).mistakes.size() == 1 && !findMistakes("", scholar, ev8, Black).mistakes[0].puzzle);

    const std::string fen = "4k3/8/3p4/8/8/5N2/8/4K3 w - - 0 25";
    Position fp;
    fp.setFEN(fen);
    const GameMistakes hung = findMistakes(fen, line(fp, "f3e5"), {ev(300, "f3d4"), ev(-50, "d6e5")}, White);
    CHECK(hung.moves[2] == 1 && hung.mistakes.size() == 1);
    CHECK(hung.mistakes[0].kind == ErrorKind::HungPiece && hung.mistakes[0].phase == 2 && hung.mistakes[0].piece == Knight);
    CHECK(!hung.mistakes[0].puzzle);
    const GameMistakes ok = findMistakes(fen, line(fp, "f3g5"), {ev(300, "f3d4"), ev(270, "e8e7")}, White);
    CHECK(ok.mistakes.empty() && ok.moves[2] == 1);

    const GameMistakes back = decodeMistakes(encodeMistakes(hung));
    CHECK(back.moves[2] == 1 && back.mistakes.size() == 1 && back.mistakes[0].kind == ErrorKind::HungPiece &&
          back.mistakes[0].moveNumber == 25 && back.mistakes[0].winLoss == int(hung.mistakes[0].winLoss));

    WeaknessReport r;
    CHECK(weaknessPhrase(r).empty() && weakestTheme(r).empty());
    addToReport(r, g);
    addToReport(r, hung);
    addToReport(r, hung);
    CHECK(r.games == 3 && r.errors == 3 && r.phase[2] == 2 && r.kind[int(ErrorKind::HungPiece)] == 2 && r.piece[Knight] == 3);
    CHECK(r.bucket[0] == 1 && r.bucket[2] == 2);
    const std::string phrase = weaknessPhrase(r);
    CHECK(phrase.find("теряешь фигуры на 21–30 ходу") != std::string::npos && phrase.find("коня") != std::string::npos);
    CHECK(phrase.find("в эндшпиле — 2 из 3") != std::string::npos);
    CHECK(weakestTheme(r) == "hangingPiece");
}

static void testStockfish() {
    namespace fs = std::filesystem;
    fs::path dir = fs::current_path() / "stockfish";
    if (!fs::exists(dir)) dir = fs::path(CHESS_SOURCE_DIR) / "third_party" / "stockfish";
    const fs::path exe = uci::findStockfish(dir);
    if (exe.empty()) {
        std::printf("  Stockfish не найден — живые тесты пропущены\n");
        return;
    }
    uci::Engine e;
    std::string err;
    CHECK(e.start(exe, &err));
    std::printf("  движок: %s\n", e.name().c_str());
    CHECK(e.isReady());
    const uci::Analysis a = e.analyse("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", {}, 10, 0);
    CHECK(a.bestmove == "a1a8");
    CHECK(!a.lines.empty() && a.lines[0].score.mate && a.lines[0].score.value == 1);
    e.stop();

    Position p;
    std::vector<Move> moves;
    for (const char* s : {"e4", "e5", "Bc4", "Nc6", "Qh5", "Nf6", "Qxf7"}) {
        moves.push_back(parseSAN(p, s));
        p.makeMove(moves.back());
    }
    Analyzer an;
    const auto t0 = std::chrono::steady_clock::now();
    an.start(exe, "", moves, 100);
    while (an.running()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(an.error().empty());
    const auto evals = an.snapshot();
    CHECK(evals.size() == 8 && an.done() == 8);
    CHECK(evals.back().mate && evals.back().cp > 0);
    CHECK(evals[1].hasSecond);
    const auto r = reviewMoves(evals, moves);
    const auto gm = findMistakes("", moves, evals, Black);
    CHECK(gm.mistakes.size() == 1 && gm.mistakes[0].kind == ErrorKind::AllowedMate && !gm.mistakes[0].puzzle);
    CHECK(r[0].grade == Grade::Book);
    CHECK(r[5].grade == Grade::Blunder);
    CHECK(r[6].grade == Grade::Best || r[6].grade == Grade::Great);

    an.start(exe, "", std::vector<Move>(moves.begin(), moves.begin() + 5), 100);
    CHECK(an.done() == 6);
    while (an.running()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(an.snapshot()[5].bestUci == evals[5].bestUci);
    std::printf("  разбор 8 позиций за %.1f с; 3...Кf6 — %s, оценка до: %s\n", sec, gradeName(r[5].grade),
                formatEval(evals[5]).c_str());
}

int main() {
    testFormulas();
    testReview();
    testWeakness();
    testStockfish();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
