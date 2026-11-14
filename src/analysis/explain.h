#pragma once
#include "core/position.h"

#include <functional>
#include <string>
#include <vector>

namespace analysis {
using MoveNamer = std::function<std::string(chess::Position&, chess::Move)>;

std::string whyBad(const chess::Position& before, chess::Move played, const std::vector<std::string>& reply,
                   const MoveNamer& name);
std::string whyGood(const chess::Position& before, const std::vector<std::string>& line, const MoveNamer& name);

}
