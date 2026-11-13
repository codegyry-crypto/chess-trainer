#pragma once
#include "core/position.h"

namespace engine {
struct EvalParams {
    int mobility = 100;
    int kingAttack = 100;
    int kingSafety = 100;
    int pawnStructure = 100;
    int center = 100;
    int tradeWhenAhead = 0;
};

int evaluate(const chess::Position& pos, const EvalParams& p = {});

int pieceValue(chess::PieceType t);

}
