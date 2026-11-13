#pragma once
#include "core/position.h"

#include <array>
#include <string>
#include <string_view>

namespace chess {
using PieceLetters = std::array<std::string, 7>;
extern const PieceLetters EnglishLetters;
extern const PieceLetters RussianLetters;

std::string toUCI(Move m);
Move parseUCI(Position& pos, std::string_view text);

std::string toSAN(Position& pos, Move m, const PieceLetters& letters = EnglishLetters);
Move parseSAN(Position& pos, std::string_view text);

}
