#include "rating/elo.h"

#include <algorithm>
#include <cmath>

namespace rating {
double expectedScore(int player, int opponent) {
    return 1.0 / (1.0 + std::pow(10.0, (opponent - player) / 400.0));
}

int kFactor(int ratedGamesPlayed) { return ratedGamesPlayed < 30 ? 40 : 20; }

int ratingDelta(int player, int opponent, double score, int ratedGamesPlayed) {
    return int(std::lround(kFactor(ratedGamesPlayed) * (score - expectedScore(player, opponent))));
}

int performanceRating(const std::vector<CalibrationGame>& games) {
    if (games.empty()) return StartRating;
    double sumOpp = 0, wins = 0, losses = 0;
    for (const CalibrationGame& g : games) {
        sumOpp += g.opponent;
        if (g.score > 0.75) wins += 1;
        else if (g.score < 0.25) losses += 1;
    }
    const double n = double(games.size());
    const int perf = int(std::lround(sumOpp / n + 400.0 * (wins - losses) / n));
    return std::clamp(perf, 100, 2800);
}

const std::vector<Level>& levels() {
    static const std::vector<Level> l = {
        {1, 0, 600, 0xc8c8c8},     {2, 600, 800, 0x1ce400},   {3, 800, 1000, 0x1ce400},
        {4, 1000, 1200, 0xffc800}, {5, 1200, 1400, 0xffc800}, {6, 1400, 1550, 0xffc800},
        {7, 1550, 1700, 0xffc800}, {8, 1700, 1850, 0xff6309}, {9, 1850, 2000, 0xff6309},
        {10, 2000, 2400, 0xfe1f00},
    };
    return l;
}

const Level& levelFor(int elo) {
    for (const Level& l : levels())
        if (elo < l.to) return l;
    return levels().back();
}

float levelProgress(int elo) {
    const Level& l = levelFor(elo);
    if (l.number == 10) return 1.0f;
    return std::clamp(float(elo - l.from) / float(l.to - l.from), 0.0f, 1.0f);
}

}
