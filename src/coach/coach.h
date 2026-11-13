#pragma once
#include "core/position.h"
#include "engine/search.h"

#include <string>
#include <vector>

namespace coach {

std::string pieceName(chess::PieceType t);
std::string pieceAcc(chess::PieceType t);

struct Threat {
    chess::Square from, to;
};
std::vector<Threat> threats(const chess::Position& pos, chess::Color victim);

struct MoveCheck {
    bool blunder = false;
    std::string reason;
};
MoveCheck checkMove(const chess::Position& pos, chess::Move m, engine::Searcher& s);

std::vector<std::string> castlingProblems(chess::Position& pos);

std::string hint(chess::Position& pos, chess::Move best, bool mate, int level, bool russian);

}
