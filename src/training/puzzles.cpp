#include "training/puzzles.h"

#include "core/notation.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace training {

using namespace chess;

namespace {
std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}
}

Puzzle parsePuzzle(const std::string& id, const std::string& fen, const std::string& moves, int rating,
                   const std::string& themes) {
    return {id, fen, split(moves), rating, split(themes)};
}

std::string joinMoves(const std::vector<std::string>& moves) {
    std::string s;
    for (const std::string& m : moves) s += (s.empty() ? "" : " ") + m;
    return s;
}

std::vector<Puzzle> loadPuzzles(const std::filesystem::path& csv) {
    std::vector<Puzzle> out;
    std::ifstream in(csv);
    std::string line;
    while (std::getline(in, line)) {
        std::string f[5];
        std::istringstream ls(line);
        for (std::string& x : f) std::getline(ls, x, ',');
        if (f[1].empty()) continue;
        out.push_back(parsePuzzle(f[0], f[1], f[2], std::atoi(f[3].c_str()), f[4]));
    }
    return out;
}

std::string themeName(const std::string& id) {
    static const std::map<std::string, std::string> names = {
        {"fork", "Вилка"},
        {"pin", "Связка"},
        {"skewer", "Сквозной удар"},
        {"discoveredAttack", "Вскрытое нападение"},
        {"doubleCheck", "Двойной шах"},
        {"hangingPiece", "Незащищённая фигура"},
        {"mateIn1", "Мат в 1 ход"},
        {"mateIn2", "Мат в 2 хода"},
        {"mateIn3", "Мат в 3 хода"},
        {"mate", "Мат"},
        {"backRankMate", "Мат на краю доски"},
        {"smotheredMate", "Спёртый мат"},
        {"sacrifice", "Жертва"},
        {"deflection", "Отвлечение"},
        {"attraction", "Завлечение"},
        {"capturingDefender", "Уничтожение защитника"},
        {"trappedPiece", "Ловля фигуры"},
        {"promotion", "Превращение"},
        {"advancedPawn", "Проходная пешка"},
        {"intermezzo", "Промежуточный ход"},
        {"quietMove", "Тихий ход"},
        {"defensiveMove", "Защита"},
        {"zugzwang", "Цугцванг"},
        {"clearance", "Освобождение линии"},
        {"interference", "Перекрытие"},
        {"xRayAttack", "Рентген"},
        {"kingsideAttack", "Атака на короля"},
        {"exposedKing", "Открытый король"},
        {"endgame", "Эндшпиль"},
        {"middlegame", "Миттельшпиль"},
        {"opening", "Дебют"},
        {"mine", "Моя ошибка"},
    };
    const auto it = names.find(id);
    return it == names.end() ? "" : it->second;
}

const std::vector<std::string>& pickerThemes() {
    static const std::vector<std::string> t = {"fork",         "pin",       "skewer",     "discoveredAttack",
                                               "hangingPiece", "mateIn1",   "mateIn2",    "backRankMate",
                                               "sacrifice",    "deflection", "trappedPiece", "endgame"};
    return t;
}

bool PuzzleRun::start(const Puzzle& p, bool setup) {
    puzzle_ = p;
    step_ = 0;
    if ((setup && p.moves.size() < 2) || !pos_.setFEN(p.fen)) return false;
    player_ = setup ? ~pos_.sideToMove() : pos_.sideToMove();
    return true;
}

Move PuzzleRun::setupMove() { return parseUCI(pos_, puzzle_.moves[0]); }

void PuzzleRun::applySetup() {
    if (step_ != 0) return;
    pos_.makeMove(setupMove());
    step_ = 1;
}

Move PuzzleRun::expected() {
    return step_ < puzzle_.moves.size() ? parseUCI(pos_, puzzle_.moves[step_]) : Move::none();
}

PuzzleRun::Result PuzzleRun::play(Move m) {
    const Move want = expected();
    bool ok = m == want;
    if (!ok && step_ + 1 == puzzle_.moves.size()) {
        Position p = pos_;
        p.makeMove(m);
        ok = p.inCheck() && p.legalMoves().empty();
    }
    if (!ok) return Result::Wrong;
    pos_.makeMove(m);
    ++step_;
    return step_ >= puzzle_.moves.size() ? Result::Solved : Result::Continue;
}

void PuzzleRun::applyReply() {
    const Move m = expected();
    if (m.isNone()) return;
    pos_.makeMove(m);
    ++step_;
}

}
