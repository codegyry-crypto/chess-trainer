#include "engine/bots.h"
#include "rating/elo.h"
#include "storage/profile.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <set>

static int failures = 0, checks = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        ++checks;                                                                  \
        if (!(cond)) {                                                             \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

static void testElo() {
    using namespace rating;
    CHECK(std::abs(expectedScore(1500, 1500) - 0.5) < 1e-9);
    CHECK(std::abs(expectedScore(1600, 1200) - 0.909) < 0.001);
    CHECK(kFactor(0) == 40 && kFactor(29) == 40 && kFactor(30) == 20);
    CHECK(ratingDelta(1200, 1200, 1.0, 10) == 20);
    CHECK(ratingDelta(1200, 1200, 0.0, 10) == -20);
    CHECK(ratingDelta(1200, 1200, 0.5, 50) == 0);
    CHECK(ratingDelta(1200, 1600, 1.0, 50) == 18);
    CHECK(ratingDelta(1600, 1200, 1.0, 50) == 2);

    CHECK(performanceRating({{1000, 1}, {1000, 1}, {1000, 1}, {1000, 0}, {1000, 0}}) == 1080);
    CHECK(performanceRating({{1000, 0.5}}) == 1000);
    CHECK(performanceRating({}) == StartRating);

    CHECK(levelFor(0).number == 1);
    CHECK(levelFor(599).number == 1);
    CHECK(levelFor(600).number == 2);
    CHECK(levelFor(1399).number == 5);
    CHECK(levelFor(1400).number == 6);
    CHECK(levelFor(1999).number == 9);
    CHECK(levelFor(2000).number == 10);
    CHECK(levelFor(3000).number == 10);
    CHECK(std::abs(levelProgress(1100) - 0.5f) < 1e-6);
}

static void testProfile() {
    namespace fs = std::filesystem;
    const fs::path file = fs::temp_directory_path() / "chess_trainer_rating_test.db";
    fs::remove(file);
    {
        storage::Profile p;
        std::string err;
        CHECK(p.open(file, &err));
        CHECK(p.rating() == rating::StartRating);
        CHECK(!p.calibrated());
        CHECK(p.calibrationLeft() == 5);

        auto game = [](double score, int opp, bool rated = true) {
            storage::GameRecord g;
            g.opponent = "Бот";
            g.botId = "bot";
            g.opponentRating = opp;
            g.color = "w";
            g.score = score;
            g.rated = rated;
            return g;
        };
        auto ch = p.recordGame(game(1, 1000, false));
        CHECK(!ch.rated && ch.delta == 0 && p.rating() == rating::StartRating);

        ch = p.recordGame(game(1, 1000));
        CHECK(ch.calibrating && ch.calibrationPlayed == 1 && p.rating() == 1400);
        p.recordGame(game(1, 1000));
        p.recordGame(game(1, 1000));
        p.recordGame(game(0, 1000));
        ch = p.recordGame(game(0, 1000));
        CHECK(ch.calibrationDone && !ch.calibrating);
        CHECK(p.rating() == 1080);
        CHECK(p.calibrated());

        ch = p.recordGame(game(1, 1080));
        CHECK(ch.delta == 20 && p.rating() == 1100);

        const auto st = p.stats();
        CHECK(st.games == 7 && st.wins == 5 && st.losses == 2 && st.draws == 0);
        CHECK(st.streak == 1);
        CHECK(st.bestStreak == 4);
        CHECK(p.ratingHistory().size() == 6);
        CHECK(p.recentGames(3).size() == 3);
        CHECK(p.recentGames(1)[0].ratingAfter == 1100);
    }
    {
        storage::Profile p;
        CHECK(p.open(file));
        CHECK(p.rating() == 1100 && p.ratedGames() == 6 && p.calibrated());
        CHECK(p.peak() == 1100);
    }
    std::error_code ec;
    fs::remove(file, ec);
}

static void testMatchmaking() {
    using rating::calibrationTarget;
    CHECK(calibrationTarget({}) == 1200);
    CHECK(calibrationTarget({{1200, 1}}) == 1600);
    CHECK(calibrationTarget({{1200, 0}}) == 800);
    CHECK(calibrationTarget({{1200, 1}, {1600, 0}}) == 1400);
    CHECK(calibrationTarget({{1200, 1}, {1600, 0}, {1400, 0.5}}) == 1400);
    CHECK(calibrationTarget({{1200, 0}, {700, 0}, {600, 0}, {500, 0}}) == 450);
    CHECK(calibrationTarget({{2600, 1}}) == 2600);

    std::mt19937 rng(1);
    const engine::BotProfile mid = engine::strengthBot(1350);
    CHECK(mid.rating == 1350 && mid.noise == 23 && mid.depth == 4 && !mid.fullSearch);
    CHECK(engine::strengthBot(1200).noise == engine::botById("aggressor")->noise);
    CHECK(engine::strengthBot(100).rating == 400 && engine::strengthBot(3000).rating == 2000);

    for (int target : {450, 1200, 1358, 1700}) {
        const engine::BotProfile b = engine::pickOpponent(target, true, true, rng);
        CHECK(b.rating == target / 10 * 10 && b.stockfishElo == 0);
    }
    const engine::BotProfile sf = engine::pickOpponent(2300, true, true, rng);
    CHECK(sf.id == "stockfish" && sf.rating == 2300 && sf.stockfishElo == 2300);
    CHECK(engine::pickOpponent(2300, true, false, rng).rating == 2000);

    std::set<std::string> seen;
    for (int i = 0; i < 50; ++i) {
        const engine::BotProfile b = engine::pickOpponent(1300, false, false, rng);
        CHECK(std::abs(b.rating - 1300) <= 100);
        seen.insert(b.name);
    }
    CHECK(seen.size() == 4);
    CHECK(std::abs(engine::pickOpponent(2400, false, true, rng).stockfishElo - 2400) <= 100);
}

static void testPlan() {
    using namespace storage;
    PlanInput in{"fork", "Вилка", "l1", "Урок 1", "queen", "Мат ферзём"};
    auto plan = buildPlan(in);
    CHECK(plan.size() == 5);
    CHECK(plan[0].kind == "puzzles" && plan[0].arg == "fork" && plan[0].target == 10);
    CHECK(plan.back().kind == "review" && plan.back().needsPrev);
    const int minutes = planMinutes(plan);
    CHECK(minutes >= 30 && minutes <= 60);
    CHECK(buildPlan({"fork", "Вилка", "", "", "", ""}).size() == 3);

    CHECK(!advancePlan(plan, "puzzles", "pin"));
    CHECK(!advancePlan(plan, "puzzles", ""));
    for (int i = 0; i < 10; ++i) CHECK(advancePlan(plan, "puzzles", "fork"));
    CHECK(plan[0].complete());
    CHECK(!advancePlan(plan, "puzzles", "fork"));
    CHECK(!advancePlan(plan, "lesson", "l2"));
    CHECK(advancePlan(plan, "lesson", "l1"));
    CHECK(!advancePlan(plan, "review", ""));
    CHECK(advancePlan(plan, "rated", ""));
    CHECK(advancePlan(plan, "review", ""));
    CHECK(!planComplete(plan));
    CHECK(advancePlan(plan, "endgame", "queen"));
    CHECK(planComplete(plan));

    const auto back = decodePlan(encodePlan(plan));
    CHECK(back.size() == plan.size());
    CHECK(back[0].title == plan[0].title && back[0].done == 10 && back[4].needsPrev && planComplete(back));

    namespace fs = std::filesystem;
    const fs::path file = fs::temp_directory_path() / "chess_trainer_plan_test.db";
    fs::remove(file);
    {
        Profile p;
        CHECK(p.open(file));
        CHECK(p.plan(100).empty());
        p.setPlan(100, plan);
        CHECK(p.plan(100).size() == plan.size());
        CHECK(p.plan(101).empty());
        CHECK(p.unlock("first_win", 100));
        CHECK(!p.unlock("first_win", 101));
        p.bump("puzzles_solved");
        p.bump("puzzles_solved");
        CHECK(p.counter("puzzles_solved") == 2);
        p.markActivity(100);
        p.markActivity(100);
        p.markActivity(101);
        CHECK(p.streak().current == 2 && p.streak().best == 2 && p.streak().lastDay == 101);
    }
    fs::remove(file);
}

static void testStreak() {
    using namespace storage;
    Streak s;
    CHECK(liveStreak(s, 10) == 0);
    s = advanceStreak(s, 10);
    CHECK(s.current == 1 && s.best == 1);
    s = advanceStreak(s, 10);
    CHECK(s.current == 1);
    s = advanceStreak(s, 11);
    s = advanceStreak(s, 12);
    CHECK(s.current == 3 && s.best == 3);
    CHECK(liveStreak(s, 12) == 3 && liveStreak(s, 13) == 3 && liveStreak(s, 14) == 0);
    s = advanceStreak(s, 15);
    CHECK(s.current == 1 && s.best == 3);

    AchievementStats a;
    CHECK(earnedAchievements(a).empty());
    a.wins = 1;
    a.puzzlesSolved = 100;
    a.bestStreak = 7;
    const auto got = earnedAchievements(a);
    auto has = [&](const char* id) { return std::find(got.begin(), got.end(), id) != got.end(); };
    CHECK(has("first_win") && has("puzzles10") && has("puzzles100") && !has("puzzles500"));
    CHECK(has("streak3") && has("streak7") && !has("streak30") && !has("calibrated") && !has("course"));

    std::vector<SrsItem> items = {{"a", "", "", "fork pin middlegame", "lichess"},
                                  {"b", "", "", "fork", "lichess"},
                                  {"c", "", "", "fork", "mine"}};
    const auto weak = weakThemes(items, {"fork", "pin"});
    CHECK(weak.size() == 2 && weak[0].first == "fork" && weak[0].second == 2 && weak[1].first == "pin");

    CHECK(fenPhase("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1") == 0);
    CHECK(fenPhase("r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 15") == 1);
    CHECK(fenPhase("8/5k2/8/3R4/8/2K5/8/6r1 w - - 0 40") == 2);
}

int main() {
    testPlan();
    testStreak();
    testElo();
    testMatchmaking();
    testProfile();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
