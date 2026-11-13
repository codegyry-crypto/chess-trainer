#pragma once
#include <string>
#include <vector>

namespace training {

enum class Goal { Mate, Promote, Draw };
enum class Verdict { Continue, Won, Lost };

struct Endgame {
    std::string id, title, text, fen;
    Goal goal;
    int moves;
};

const std::vector<Endgame>& endgames();
std::string goalText(const Endgame& e);
Verdict judge(Goal goal, int limit, int played, int score, bool promoted);

}
