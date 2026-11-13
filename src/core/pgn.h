#pragma once
#include "core/position.h"

#include <string>
#include <utility>
#include <vector>

namespace chess {
struct Game {
    std::vector<std::pair<std::string, std::string>> tags;
    std::string startFen;
    std::vector<Move> moves;
    std::string result = "*";

    std::string tag(const std::string& name) const;
    void setTag(const std::string& name, const std::string& value);
    Position startPosition() const;
    Position positionAt(size_t ply) const;
};

std::string toPGN(const Game& game);
std::vector<Game> parsePGN(const std::string& text, std::string* error = nullptr);

}
