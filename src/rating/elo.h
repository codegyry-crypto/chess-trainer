#pragma once
#include <string>
#include <vector>

namespace rating {
constexpr int StartRating = 800;
constexpr int CalibrationGames = 5;

double expectedScore(int player, int opponent);
int kFactor(int ratedGamesPlayed);
int ratingDelta(int player, int opponent, double score, int ratedGamesPlayed);

struct CalibrationGame {
    int opponent;
    double score;
};
int performanceRating(const std::vector<CalibrationGame>& games);

struct Level {
    int number;
    int from;
    int to;
    unsigned color;
};
const std::vector<Level>& levels();
const Level& levelFor(int elo);
float levelProgress(int elo);

}
