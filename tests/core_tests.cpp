#include "core/notation.h"
#include "core/pgn.h"
#include "core/position.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

using namespace chess;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++checks;                                                                \
        if (!(cond)) {                                                           \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                                   \
    do {                                                                                 \
        ++checks;                                                                        \
        const auto va = (a);                                                             \
        const auto vb = (b);                                                             \
        if (!(va == vb)) {                                                               \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #a " == " #b     \
                      << "\n   got: " << va << "\n  want: " << vb << "\n";               \
            ++failures;                                                                  \
        }                                                                                \
    } while (0)

static uint64_t perft(Position& pos, int depth) {
    const auto moves = pos.legalMoves();
    if (depth == 1) return moves.size();
    uint64_t total = 0;
    for (const Move& m : moves) {
        pos.makeMove(m);
        total += perft(pos, depth - 1);
        pos.unmakeMove();
    }
    return total;
}

static void hashWalk(Position& pos, int depth) {
    if (depth == 0) return;
    for (const Move& m : pos.legalMoves()) {
        const std::string before = pos.fen();
        const uint64_t h = pos.hash();
        pos.makeMove(m);
        if (pos.hash() != pos.computeHash()) {
            std::cout << "FAIL hash after " << toUCI(m) << " from " << before << "\n";
            ++failures;
            pos.unmakeMove();
            return;
        }
        hashWalk(pos, depth - 1);
        pos.unmakeMove();
        if (pos.fen() != before || pos.hash() != h) {
            std::cout << "FAIL unmake " << toUCI(m) << " from " << before << "\n";
            ++failures;
            return;
        }
    }
}

static Position fromFEN(const std::string& fen) {
    Position p;
    std::string err;
    if (!p.setFEN(fen, &err)) {
        std::cout << "FAIL bad test FEN: " << fen << " (" << err << ")\n";
        ++failures;
    }
    return p;
}

static void play(Position& pos, std::initializer_list<const char*> sans) {
    for (const char* s : sans) {
        const Move m = parseSAN(pos, s);
        if (m.isNone()) {
            std::cout << "FAIL cannot play " << s << " in " << pos.fen() << "\n";
            ++failures;
            return;
        }
        pos.makeMove(m);
    }
}

static void testPerft(bool deep) {
    struct Case {
        const char* name;
        const char* fen;
        std::vector<uint64_t> counts;
    };
    const Case cases[] = {
        {"start", Position::StartFEN, {20, 400, 8902, 197281, 4865609}},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         {48, 2039, 97862, 4085603}},
        {"pos3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", {14, 191, 2812, 43238, 674624}},
        {"pos4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
         {6, 264, 9467, 422333}},
        {"pos5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", {44, 1486, 62379, 2103487}},
        {"pos6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
         {46, 2079, 89890, 3894594}},
        {"960-0", "bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w HFhf - 0 1", {20, 400, 9006, 201143, 4975808}},
        {"960-959", "rkrnnqbb/pppppppp/8/8/8/8/PPPPPPPP/RKRNNQBB w CAca - 0 1", {20, 400, 9006, 201143, 4973573}},
        {"960-a", "bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - 2 9",
         {21, 528, 12189, 326672, 8146062}},
        {"960-b", "2nnrbkr/p1qppppp/8/1ppb4/6PP/3PP3/PPP2P2/BQNNRBKR w HEhe - 1 9",
         {21, 807, 18002, 667366, 16253601}},
        {"960-c", "b1q1rrkb/pppppppp/3nn3/8/P7/1PPP4/4PPPP/BQNNRKRB w GE - 1 9",
         {20, 479, 10471, 273318, 6417013}},
        {"960-d", "rk5r/pp4pp/8/8/8/8/PP4PP/RK5R w HAha - 0 1", {17, 280, 5356, 99262, 2041642}},
        {"960-e", "1r3rkr/8/8/8/8/8/8/1R3RKR w HFhf - 0 1", {29, 710, 20572, 567062, 16944465}},
        {"960-f", "r1k4r/8/8/8/2q5/8/8/R1K4R w HAha - 0 1", {4, 200, 3577, 153761, 2902496}},
    };
    for (const Case& c : cases) {
        Position pos = fromFEN(c.fen);
        const size_t maxDepth = deep ? c.counts.size() : std::min<size_t>(c.counts.size(), 3);
        for (size_t d = 1; d <= maxDepth; ++d) {
            const auto t0 = std::chrono::steady_clock::now();
            const uint64_t got = perft(pos, int(d));
            const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            ++checks;
            if (got != c.counts[d - 1]) {
                std::cout << "FAIL perft " << c.name << " depth " << d << ": got " << got << ", want "
                          << c.counts[d - 1] << "\n";
                ++failures;
            } else if (sec > 0.2) {
                std::printf("  perft %-8s d%zu = %llu  (%.2f s, %.1f M/s)\n", c.name, d,
                            (unsigned long long)got, sec, got / sec / 1e6);
            }
        }
        CHECK_EQ(pos.fen(), fromFEN(c.fen).fen());
        hashWalk(pos, 3);
    }
}

static void testFEN() {
    for (const char* fen : {Position::StartFEN,
                            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                            "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                            "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3"}) {
        CHECK_EQ(fromFEN(fen).fen(), std::string(fen));
    }
    Position p;
    std::string err;
    CHECK(!p.setFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1", &err));
    CHECK(!p.setFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQ1BNR w kq - 0 1", &err));
    CHECK(!p.setFEN("4k2R/8/8/8/8/8/8/4K3 w - - 0 1", &err));
    CHECK_EQ(p.fen(), std::string(Position::StartFEN));
}

static void testRules() {
    Position p;
    play(p, {"e4"});
    CHECK_EQ(p.fen(), std::string("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"));

    p = Position();
    play(p, {"e4", "d5", "e5", "f5"});
    CHECK(p.fen().find(" f6 ") != std::string::npos);
    const Move ep = parseSAN(p, "exf6");
    CHECK(!ep.isNone() && ep.isEnPassant());
    p.makeMove(ep);
    CHECK(p.at(parseSquare("f5")) == NoPiece);
    CHECK(p.at(parseSquare("f6")) == WPawn);
    p.unmakeMove();
    CHECK(p.at(parseSquare("f5")) == BPawn);

    p = Position();
    play(p, {"f3", "e5", "g4"});
    CHECK_EQ(toSAN(p, parseSAN(p, "Qh4")), std::string("Qh4#"));
    play(p, {"Qh4"});
    CHECK(p.status() == GameStatus::Checkmate);

    p = fromFEN("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    CHECK(p.status() == GameStatus::Stalemate);

    CHECK(fromFEN("8/8/4k3/8/8/3NK3/8/8 w - - 0 1").isInsufficientMaterial());
    CHECK(fromFEN("8/8/4k3/8/8/3BK3/8/8 w - - 0 1").isInsufficientMaterial());
    CHECK(fromFEN("8/8/2b1k3/8/8/3BK3/8/8 w - - 0 1").isInsufficientMaterial());
    CHECK(!fromFEN("8/8/3bk3/8/8/3BK3/8/8 w - - 0 1").isInsufficientMaterial());
    CHECK(!fromFEN("8/8/4k3/8/8/2NNK3/8/8 w - - 0 1").isInsufficientMaterial());
    CHECK(!fromFEN("8/8/4k3/8/8/4K3/4P3/8 w - - 0 1").isInsufficientMaterial());

    p = Position();
    play(p, {"Nf3", "Nf6", "Ng1", "Ng8", "Nf3", "Nf6", "Ng1"});
    CHECK(p.status() == GameStatus::Ongoing);
    play(p, {"Ng8"});
    CHECK(p.status() == GameStatus::ThreefoldRepetition);

    p = fromFEN("8/8/4k3/8/8/4K3/8/R7 w - - 99 80");
    CHECK(p.status() == GameStatus::Ongoing);
    play(p, {"Ra2"});
    CHECK(p.status() == GameStatus::FiftyMoveRule);

    p = fromFEN("r3k2r/8/8/8/8/8/5r2/R3K2R w KQkq - 0 1");
    CHECK(parseSAN(p, "O-O").isNone());
    CHECK(!parseSAN(p, "O-O-O").isNone());
    p = fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    play(p, {"Rb1"});
    CHECK_EQ(p.castlingRights(), int(WhiteOO | BlackOO | BlackOOO));
    play(p, {"Rxh1+"});
    CHECK_EQ(p.castlingRights(), int(BlackOOO));
}

static void testSAN() {
    Position p = fromFEN("R7/8/8/8/8/7k/8/R3K3 w - - 0 1");
    CHECK_EQ(toSAN(p, p.findLegal(parseSquare("a1"), parseSquare("a4"))), std::string("R1a4"));
    p = fromFEN("4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1");
    CHECK_EQ(toSAN(p, p.findLegal(parseSquare("b1"), parseSquare("d2"))), std::string("Nbd2"));
    p = fromFEN("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1");
    CHECK_EQ(toSAN(p, p.findLegal(parseSquare("b7"), parseSquare("b8"), Queen)), std::string("b8=Q+"));
    CHECK(!parseSAN(p, "b8Q").isNone());
    CHECK(!parseSAN(p, "b8=N").isNone());
    p = fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    CHECK_EQ(toSAN(p, parseSAN(p, "0-0-0")), std::string("O-O-O"));

    p = Position();
    CHECK_EQ(toSAN(p, parseSAN(p, "Nf3"), RussianLetters), std::string("Кf3"));
    CHECK(parseSAN(p, "Кf3") == parseSAN(p, "Nf3"));

    CHECK_EQ(toUCI(parseUCI(p, "e2e4")), std::string("e2e4"));
    CHECK(parseUCI(p, "e2e5").isNone());
    CHECK(parseSAN(p, "Ke2").isNone());
}

static void testPGN() {
    Game g;
    g.setTag("White", "Ты");
    g.setTag("Black", "Бот \"Тактик\"");
    Position p;
    for (const char* s : {"f3", "e5", "g4", "Qh4"}) {
        const Move m = parseSAN(p, s);
        g.moves.push_back(m);
        p.makeMove(m);
    }
    g.result = "0-1";
    const std::string text = toPGN(g);
    CHECK(text.find("1. f3 e5 2. g4 Qh4# 0-1") != std::string::npos);

    std::string err;
    auto games = parsePGN(text + "\n" + text, &err);
    CHECK_EQ(err, std::string());
    CHECK_EQ(games.size(), size_t(2));
    if (!games.empty()) {
        CHECK_EQ(games[0].moves.size(), size_t(4));
        CHECK_EQ(games[0].result, std::string("0-1"));
        CHECK_EQ(games[0].tag("Black"), std::string("Бот \"Тактик\""));
    }

    games = parsePGN("1.e4 {лучший ход} e5 $1 (1... c5 2. Nf3 {сицилианская}) 2. Nf3 Nc6!? 3.Bb5 a6 *", &err);
    CHECK_EQ(err, std::string());
    CHECK_EQ(games.size(), size_t(1));
    if (!games.empty()) {
        CHECK_EQ(games[0].moves.size(), size_t(6));
        CHECK_EQ(games[0].positionAt(6).fen(),
                 std::string("r1bqkbnr/1ppp1ppp/p1n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 4"));
    }

    Game g2;
    g2.startFen = "4k3/8/8/8/8/8/8/R3K3 b - - 0 1";
    Position p2 = g2.startPosition();
    g2.moves.push_back(parseSAN(p2, "Kd7"));
    const std::string t2 = toPGN(g2);
    CHECK(t2.find("1... Kd7 *") != std::string::npos);
    games = parsePGN(t2, &err);
    CHECK(games.size() == 1 && games[0].moves.size() == 1);

    games = parsePGN("1. e4 e5 2. Ke3 *", &err);
    CHECK(err.find("Ke3") != std::string::npos);
}

static void testChess960() {
    CHECK_EQ(Position::chess960FEN(518), std::string(Position::StartFEN));
    CHECK_EQ(Position::chess960FEN(0), std::string("bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w KQkq - 0 1"));
    CHECK_EQ(Position::chess960FEN(959), std::string("rkrnnqbb/pppppppp/8/8/8/8/PPPPPPPP/RKRNNQBB w KQkq - 0 1"));
    for (int n = 0; n < 960; ++n) {
        Position p = fromFEN(Position::chess960FEN(n));
        CHECK_EQ(p.castlingRights(), 15);
        CHECK_EQ(p.chess960(), !(p.at(4) == WKing && p.at(0) == WRook && p.at(7) == WRook));
    }
    Position p = fromFEN(Position::chess960FEN(0));
    CHECK_EQ(p.fen(), std::string("bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w HFhf - 0 1"));
    p = fromFEN("rk5r/8/8/8/8/8/8/R4RKR w HFha - 0 1");
    const Move ooo = parseSAN(p, "O-O-O");
    CHECK(ooo.isCastle());
    CHECK_EQ(toUCI(ooo), std::string("g1f1"));
    CHECK(parseSAN(p, "O-O").isNone());
    p.makeMove(ooo);
    CHECK_EQ(p.fen(), std::string("rk5r/8/8/8/8/8/8/R1KR3R b ha - 1 1"));
    const Move boo = parseUCI(p, "b8a8");
    CHECK(boo.isCastle());
    p.makeMove(boo);
    CHECK_EQ(p.fen(), std::string("2kr3r/8/8/8/8/8/8/R1KR3R w - - 2 2"));
    p.unmakeMove();
    p.unmakeMove();
    CHECK_EQ(p.fen(), std::string("rk5r/8/8/8/8/8/8/R4RKR w HFha - 0 1"));
    CHECK_EQ(p.hash(), p.computeHash());
}

int main(int argc, char** argv) {
    const bool deep = argc > 1 && std::strcmp(argv[1], "--deep") == 0;
    const auto t0 = std::chrono::steady_clock::now();
    testFEN();
    testRules();
    testSAN();
    testPGN();
    testChess960();
    testPerft(deep);
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%d checks, %d failures, %.2f s\n", checks, failures, sec);
    return failures ? 1 : 0;
}
