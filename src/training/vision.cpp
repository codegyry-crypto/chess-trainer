#include "training/vision.h"

namespace training {

uint64_t visionReach(VisionMode mode, int square) {
    if (mode == VisionMode::Square) return 1ull << square;
    const int f = square & 7, r = square >> 3;
    uint64_t out = 0;
    if (mode == VisionMode::Knight) {
        const int d[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
        for (const auto& [df, dr] : d)
            if (f + df >= 0 && f + df < 8 && r + dr >= 0 && r + dr < 8) out |= 1ull << ((r + dr) * 8 + f + df);
        return out;
    }
    for (int df : {-1, 1})
        for (int dr : {-1, 1})
            for (int x = f + df, y = r + dr; x >= 0 && x < 8 && y >= 0 && y < 8; x += df, y += dr)
                out |= 1ull << (y * 8 + x);
    return out;
}

VisionTask visionTask(VisionMode mode, std::mt19937& rng, int avoid) {
    int sq;
    do sq = int(rng() % 64);
    while (sq == avoid);
    return {mode, sq, visionReach(mode, sq)};
}

}

namespace training {

void VisionRound::start(VisionMode mode, std::mt19937& rng) {
    *this = {};
    task = visionTask(mode, rng);
}

void VisionRound::next(std::mt19937& rng) {
    task = visionTask(task.mode, rng, task.square);
    marked = 0;
    finished = false;
}

VisionRound::Click VisionRound::click(int square) {
    const uint64_t bit = 1ull << square;
    if (finished || marked & bit) return Click::Ignored;
    finished = !(task.targets & bit);
    if (finished) {
        ++errors;
        return Click::Wrong;
    }
    marked |= bit;
    finished = marked == task.targets;
    score += finished;
    return finished ? Click::Done : Click::Hit;
}

}
