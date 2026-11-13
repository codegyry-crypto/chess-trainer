#include "core/notation.h"
#include "storage/profile.h"
#include "engine/bots.h"
#include "training/endgames.h"
#include "training/lessons.h"
#include "training/puzzles.h"
#include "training/update.h"

#include <zstd.h>

#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iostream>

using namespace chess;
using namespace training;

static int failures = 0, checks = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        ++checks;                                                                  \
        if (!(cond)) {                                                             \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

static Move uci(Position& p, const char* s) { return parseUCI(p, s); }

static void testRun() {
    PuzzleRun r;
    CHECK(r.start(parsePuzzle("a", "5rk1/pQ4bp/6p1/1P4B1/3p2P1/8/P5PP/2R2q1K w - - 5 29", "c1f1 f8f1", 400, "mateIn1")));
    CHECK(r.playerColor() == Black);
    r.applySetup();
    CHECK(r.play(uci(r.position(), "g8h8")) == PuzzleRun::Result::Wrong);
    CHECK(r.play(uci(r.position(), "f8f1")) == PuzzleRun::Result::Solved);

    CHECK(r.start(parsePuzzle("b", "8/p5kp/r5p1/2r5/8/4P3/5PPP/2R3K1 w - - 0 31", "c1c5 a6a1 c5c1 a1c1", 400, "")));
    r.applySetup();
    CHECK(r.play(uci(r.position(), "a6a1")) == PuzzleRun::Result::Continue);
    r.applyReply();
    CHECK(r.play(uci(r.position(), "a1c1")) == PuzzleRun::Result::Solved);
}

static void testDatabase() {
    namespace fs = std::filesystem;
    const auto all = loadPuzzles(fs::path(CHESS_SOURCE_DIR) / "assets" / "puzzles" / "puzzles.csv");
    CHECK(all.size() > 30000);
    int bad = 0;
    for (const Puzzle& p : all) {
        Position pos;
        if (!pos.setFEN(p.fen)) {
            ++bad;
            continue;
        }
        for (const std::string& m : p.moves) {
            const Move mv = parseUCI(pos, m);
            if (mv.isNone()) {
                ++bad;
                break;
            }
            pos.makeMove(mv);
        }
    }
    CHECK(bad == 0);
    std::printf("  задач в базе: %zu, с ошибками: %d\n", all.size(), bad);
}

static void testSrs() {
    namespace fs = std::filesystem;
    const fs::path file = fs::temp_directory_path() / "chess_trainer_srs_test.db";
    fs::remove(file);
    storage::Profile p;
    CHECK(p.open(file));
    CHECK(p.puzzleRating() == 1000);
    CHECK(p.recordPuzzle(1000, true) == 20 && p.puzzleRating() == 1020);

    const int day = 20000;
    p.srsAdd({"k", "fen", "a b", "fork", "lichess", 1200}, day);
    CHECK(p.srsItems(day, true, "").empty());
    CHECK(p.srsItems(day + 1, true, "").size() == 1);
    p.srsResult("k", true, day + 1);
    CHECK(p.srsItems(day + 3, true, "").empty() && p.srsItems(day + 4, true, "").size() == 1);
    p.srsResult("k", false, day + 4);
    CHECK(p.srsItems(day + 5, true, "").size() == 1);
    for (int i = 0; i < 4; ++i) p.srsResult("k", true, day + 100);
    CHECK(p.srsItems(day + 999, false, "").empty());
    std::error_code ec;
    fs::remove(file, ec);
}

static void testImport() {
    namespace fs = std::filesystem;
    const std::string csv =
        "PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags\n"
        "a,5rk1/pQ4bp/6p1/1P4B1/3p2P1/8/P5PP/2R2q1K w - - 5 29,c1f1 f8f1,900,75,95,1000,mateIn1 short,u,\n"
        "b,8/p5kp/r5p1/2r5/8/4P3/5PPP/2R3K1 w - - 0 31,c1c5 a6a1 c5c1 a1c1,1500,75,95,1000,endgame,u,\n"
        "c,8/p5kp/r5p1/2r5/8/4P3/5PPP/2R3K1 w - - 0 31,c1c5 a6a1,1500,200,95,1000,endgame,u,\n"
        "d,8/p5kp/r5p1/2r5/8/4P3/5PPP/2R3K1 w - - 0 31,e2e4 a6a1,1500,75,95,1000,endgame,u,\n"
        "e,8/p5kp/r5p1/2r5/8/4P3/5PPP/2R3K1 w - - 0 31,c1c5 a6a1,1600,75,95,1000,endgame,u,Opening\n";
    std::string z(ZSTD_compressBound(csv.size()), '\0');
    z.resize(ZSTD_compress(z.data(), z.size(), csv.data(), csv.size(), 3));
    const fs::path zst = fs::temp_directory_path() / "chess_trainer_import.zst";
    const fs::path out = fs::temp_directory_path() / "chess_trainer_import.csv";
    std::ofstream(zst, std::ios::binary).write(z.data(), z.size());
    fs::remove(out);
    ImportProgress prog;
    std::vector<Puzzle> got;
    CHECK(importPuzzles(zst, out, {"b"}, 10, got, prog));
    CHECK(got.size() == 2 && got[0].id == "a" && got[1].id == "e" && got[0].themes.size() == 2);
    CHECK(prog.done == prog.total);
    got.clear();
    CHECK(importPuzzles(zst, out, {}, 1, got, prog) && got.size() == 1);
    const auto saved = loadPuzzles(out);
    CHECK(saved.size() == 3 && saved[1].moves.size() == 2 && saved[1].rating == 1600);
    fs::remove(zst);
    fs::remove(out);
}

static bool legalFen(Position& p, const std::string& fen) {
    return p.setFEN(fen) && !p.isAttacked(p.kingSquare(~p.sideToMove()), p.sideToMove());
}

static void testLessons() {
    const auto sections = loadLessons(std::filesystem::path(CHESS_SOURCE_DIR) / "assets" / "lessons" / "lessons.txt");
    CHECK(sections.size() == 12);
    std::unordered_set<std::string> ids;
    for (const auto& sec : sections) {
        CHECK(!sec.lessons.empty());
        for (const auto& l : sec.lessons) {
            CHECK(ids.insert(l.id).second);
            CHECK(!l.steps.empty());
            for (const auto& st : l.steps) {
                Position p;
                const bool ok = legalFen(p, st.fen);
                if (!ok) std::cout << "bad fen in " << l.id << ": " << st.fen << '\n';
                CHECK(ok);
                CHECK(!st.text.empty());
                for (const auto& a : st.arrows) CHECK(a.size() == 4 && parseSquare(a.substr(0, 2)) != NoSquare && parseSquare(a.substr(2)) != NoSquare);
                PuzzleRun run;
                if (st.solution.empty()) continue;
                CHECK(run.start(parsePuzzle(l.id, st.fen, joinMoves(st.solution), 0, ""), false));
                PuzzleRun::Result r = PuzzleRun::Result::Continue;
                for (size_t i = 0; i < st.solution.size(); i += 2) {
                    const Move m = run.expected();
                    if (m.isNone()) std::cout << "bad move in " << l.id << ": " << st.solution[i] << '\n';
                    CHECK(!m.isNone());
                    if (m.isNone()) break;
                    r = run.play(m);
                    if (r == PuzzleRun::Result::Continue) {
                        CHECK(!run.expected().isNone());
                        run.applyReply();
                    }
                }
                CHECK(r == PuzzleRun::Result::Solved);
                CHECK(!st.after.empty());
            }
            if (l.practiceFen.empty()) continue;
            Position p;
            CHECK(legalFen(p, l.practiceFen));
            CHECK(engine::botById(l.practiceBot) != nullptr);
        }
    }
    CHECK(ids.size() >= 20);
}

static void testEndgames() {
    std::unordered_set<std::string> ids;
    CHECK(endgames().size() >= 7);
    for (const auto& e : endgames()) {
        Position p;
        CHECK(ids.insert(e.id).second);
        CHECK(legalFen(p, e.fen));
        CHECK(e.moves > 0 && e.moves < 50 && !e.text.empty());
    }
    CHECK(judge(Goal::Mate, 10, 3, 9950, false) == Verdict::Continue);
    CHECK(judge(Goal::Mate, 10, 10, 9950, false) == Verdict::Lost);
    CHECK(judge(Goal::Mate, 10, 3, 0, false) == Verdict::Lost);
    CHECK(judge(Goal::Promote, 10, 3, 9000, true) == Verdict::Won);
    CHECK(judge(Goal::Promote, 10, 3, 100, true) == Verdict::Lost);
    CHECK(judge(Goal::Draw, 15, 5, -50, false) == Verdict::Continue);
    CHECK(judge(Goal::Draw, 15, 15, -50, false) == Verdict::Won);
    CHECK(judge(Goal::Draw, 15, 5, -9990, false) == Verdict::Lost);
}

static void testUpdate() {
    CHECK(update::isNewer("v1.0.1", "1.0.0"));
    CHECK(update::isNewer("v1.10.0", "1.9.9"));
    CHECK(update::isNewer("v2.0.0", "1.99.99"));
    CHECK(!update::isNewer("v1.0.0", "1.0.0"));
    CHECK(!update::isNewer("v0.9.9", "1.0.0"));
    CHECK(!update::isNewer("", "1.0.0"));
    CHECK(!update::isNewer("v1.0", "1.0.0"));
    CHECK(update::isNewer("v1.1", "1.0.0"));
}

int main() {
    testUpdate();
    testEndgames();
    testLessons();
    testRun();
    testDatabase();
    testSrs();
    testImport();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
