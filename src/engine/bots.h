#pragma once
#include "core/position.h"
#include "engine/eval.h"
#include "engine/search.h"

#include <atomic>
#include <random>
#include <string>
#include <vector>

namespace engine {
struct BotProfile {
    std::string id;
    std::string name;
    int rating;
    std::string style;
    std::string desc;
    unsigned color;

    bool fullSearch;
    int depth;
    int timeMs;
    bool quiescence;
    int noise;
    double blunderChance;
    bool useBook;
    EvalParams params;
    int stockfishElo = 0;
};

const std::vector<BotProfile>& bots();
const BotProfile* botById(const std::string& id);

struct BotMove {
    chess::Move move = chess::Move::none();
    int score = 0;
    int depth = 0;
    bool fromBook = false;
};

BotMove chooseMove(const chess::Position& pos, const std::vector<chess::Move>& gameMoves, const BotProfile& bot,
                   Searcher& searcher, std::mt19937& rng, int timeBudgetMs, std::atomic<bool>* stop);

std::vector<chess::Move> bookMoves(const std::vector<chess::Move>& gameMoves);
int bookLineCount();
bool bookIsValid(std::string* error);

}
