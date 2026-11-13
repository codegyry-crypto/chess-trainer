#include "rating/elo.h"
#include "storage/profile.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>

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

int main() {
    testElo();
    testProfile();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
