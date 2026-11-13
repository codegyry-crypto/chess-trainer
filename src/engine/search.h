#pragma once
#include "core/position.h"
#include "engine/eval.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace engine {
constexpr int Inf = 32000;
constexpr int Mate = 30000;
constexpr int MateBound = Mate - 500;
constexpr int MaxPly = 128;

struct Limits {
    int maxDepth = 64;
    int timeMs = 1000;
    uint64_t maxNodes = 0;
    bool quiescence = true;
};

struct RootMove {
    chess::Move move;
    int score;
};

struct Result {
    chess::Move best = chess::Move::none();
    int score = 0;
    int depth = 0;
    uint64_t nodes = 0;
    double seconds = 0;
    std::vector<chess::Move> pv;
};

class Searcher {
public:
    explicit Searcher(int ttSizeMb = 16);

    Result search(chess::Position pos, const Limits& limits, const EvalParams& params = {},
                  std::atomic<bool>* stop = nullptr);

    std::vector<RootMove> scoreRootMoves(chess::Position pos, int depth, const Limits& limits,
                                         const EvalParams& params = {}, std::atomic<bool>* stop = nullptr);

    void clear();

private:
    struct TTEntry {
        uint64_t key = 0;
        chess::Move move;
        int16_t score = 0;
        int8_t depth = -1;
        uint8_t flag = 0;
    };

    int negamax(chess::Position& pos, int depth, int alpha, int beta, int ply, bool pvNode);
    int quiesce(chess::Position& pos, int alpha, int beta, int ply, int qdepth);
    void orderMoves(chess::Position& pos, std::vector<chess::Move>& moves, chess::Move ttMove, int ply);
    void checkLimits();
    void begin(const Limits& limits, const EvalParams& params, std::atomic<bool>* stop);
    std::vector<chess::Move> extractPv(chess::Position pos, int maxLen);

    TTEntry* probe(uint64_t key) { return &tt_[key & ttMask_]; }

    std::vector<TTEntry> tt_;
    uint64_t ttMask_ = 0;
    chess::Move killers_[MaxPly][2]{};
    int history_[64][64]{};
    std::vector<chess::Move> moveBuf_[MaxPly];
    std::vector<int> scoreBuf_[MaxPly];

    Limits limits_;
    EvalParams params_;
    std::atomic<bool>* externalStop_ = nullptr;
    bool stopped_ = false;
    chess::Move rootBest_;
    uint64_t nodes_ = 0;
    int64_t startUs_ = 0;
};

}
