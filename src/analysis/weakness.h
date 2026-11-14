#pragma once
#include "analysis/review.h"

#include <string>
#include <vector>

namespace analysis {
enum class ErrorKind { HungPiece, MissedMate, AllowedMate, MissedTactic, Other };
constexpr int ErrorKindCount = int(ErrorKind::Other) + 1;
constexpr int MoveBuckets = 5;

struct Mistake {
    int ply = 0, moveNumber = 0, phase = 0, piece = 0;
    ErrorKind kind = ErrorKind::Other;
    Grade grade = Grade::None;
    double winLoss = 0;
    std::string fen, prevUci, bestUci;
    bool puzzle = false;
};

struct GameMistakes {
    int moves[3] = {};
    std::vector<Mistake> mistakes;
};

struct WeaknessReport {
    int games = 0, errors = 0;
    int moves[3] = {}, phase[3] = {};
    int kind[ErrorKindCount] = {};
    int piece[7] = {};
    int bucket[MoveBuckets] = {};
};

int positionPhase(const chess::Position& p);
GameMistakes findMistakes(const std::string& startFen, const std::vector<chess::Move>& moves,
                          const std::vector<Eval>& evals, chess::Color me);
std::string mistakeKey(const Mistake& m);
const char* kindTheme(ErrorKind k);

std::string encodeMistakes(const GameMistakes& g);
GameMistakes decodeMistakes(const std::string& s);

void addToReport(WeaknessReport& r, const GameMistakes& g);
std::string weaknessPhrase(const WeaknessReport& r);
std::string weakestTheme(const WeaknessReport& r);
const char* errorKindName(ErrorKind k);
const char* phaseName(int phase);
const char* bucketName(int bucket);
}
