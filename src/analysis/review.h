#pragma once
#include "core/position.h"
#include "uci/uci_engine.h"

#include <string>
#include <vector>

namespace analysis {
struct Eval {
    bool known = false;
    int cp = 0;
    bool mate = false;
    int mateIn = 0;
    std::string bestUci;
    std::vector<std::string> pv;
    bool hasSecond = false;
    int secondCp = 0;
};

enum class Grade { None, Brilliant, Great, Best, Excellent, Good, Book, Inaccuracy, Mistake, Miss, Blunder, Forced };
constexpr int GradeCount = int(Grade::Forced) + 1;

struct MoveReview {
    Grade grade = Grade::None;
    double winLoss = 0;
    double accuracy = 100;
};

struct Summary {
    double accuracy[2] = {0, 0};
    int count[2][GradeCount] = {};
    int reviewed = 0;
};

double winPercent(int cpWhite);
int cpFromScore(const uci::Score& s);
Eval evalFromAnalysis(const uci::Analysis& a, chess::Color sideToMove);
Eval terminalEval(chess::Position& pos);

std::vector<MoveReview> reviewMoves(const std::vector<Eval>& evals, const std::vector<chess::Move>& moves,
                                    const std::string& startFen = {});
bool isSacrifice(const chess::Position& before, chess::Position& after, chess::Move m, chess::Piece captured);
Summary summarize(const std::vector<MoveReview>& reviews, chess::Color firstMover);

const char* gradeName(Grade g);
const char* gradeSymbol(Grade g);
std::string formatEval(const Eval& e);

}
