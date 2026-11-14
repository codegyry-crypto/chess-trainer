#pragma once
#include <cstdint>
#include <random>

namespace training {
enum class VisionMode { Square, Knight, Bishop };

struct VisionTask {
    VisionMode mode = VisionMode::Square;
    int square = 0;
    uint64_t targets = 0;
};

uint64_t visionReach(VisionMode mode, int square);
VisionTask visionTask(VisionMode mode, std::mt19937& rng, int avoid = -1);

struct VisionRound {
    enum class Click { Ignored, Hit, Done, Wrong };
    VisionTask task;
    uint64_t marked = 0;
    int score = 0, errors = 0;
    bool finished = false;

    void start(VisionMode mode, std::mt19937& rng);
    void next(std::mt19937& rng);
    Click click(int square);
};

}
