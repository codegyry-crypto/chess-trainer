#include "ui/game_session.h"

#include "core/notation.h"

#include <algorithm>
#include <ctime>

namespace ui {
using namespace chess;

const std::vector<TimeControl>& timeControls() {
    static const std::vector<TimeControl> tcs = {
        {0, 0, "Без часов"}, {180, 2, "3+2"},  {300, 0, "5+0"},   {300, 3, "5+3"},
        {600, 0, "10+0"},    {900, 10, "15+10"}, {1800, 0, "30+0"},
    };
    return tcs;
}

namespace {
bool canMate(const Position& p, Color c) {
    int minors = 0;
    for (int s = 0; s < 64; ++s) {
        const Piece pc = p.at(s);
        if (pc == NoPiece || colorOf(pc) != c) continue;
        switch (typeOf(pc)) {
        case Pawn:
        case Rook:
        case Queen: return true;
        case Knight:
        case Bishop: ++minors; break;
        default: break;
        }
    }
    return minors >= 2;
}

}

void GameSession::start(const TimeControl& tc, const std::string& whiteName, const std::string& blackName,
                        const std::string& fen) {
    *this = GameSession();
    if (!fen.empty() && pos_.setFEN(fen)) startFen_ = fen;
    tc_ = tc;
    names_[White] = whiteName;
    names_[Black] = blackName;
    clock_[White] = clock_[Black] = tc.baseSec;
}

bool GameSession::clocksRunning() const {
    return tc_.baseSec > 0 && !over() && moves_.size() >= 2;
}

bool GameSession::play(Move m) {
    if (over()) return false;
    const Color us = pos_.sideToMove();
    clockBefore_[White].push_back(clock_[White]);
    clockBefore_[Black].push_back(clock_[Black]);
    sanEn_.push_back(toSAN(pos_, m, EnglishLetters));
    sanRu_.push_back(toSAN(pos_, m, RussianLetters));
    const bool wasRunning = clocksRunning();
    pos_.makeMove(m);
    moves_.push_back(m);
    if (tc_.baseSec > 0 && wasRunning) clock_[us] += tc_.incSec;
    viewPly_ = int(moves_.size());
    checkEnd();
    return true;
}

void GameSession::undo() {
    if (moves_.empty()) return;
    pos_.unmakeMove();
    moves_.pop_back();
    sanEn_.pop_back();
    sanRu_.pop_back();
    clock_[White] = clockBefore_[White].back();
    clock_[Black] = clockBefore_[Black].back();
    clockBefore_[White].pop_back();
    clockBefore_[Black].pop_back();
    reason_ = EndReason::None;
    result_ = "*";
    viewPly_ = int(moves_.size());
}

void GameSession::resign(Color who) {
    if (!over()) finish(EndReason::Resign, who == White ? "0-1" : "1-0");
}

void GameSession::agreeDraw() {
    if (!over()) finish(EndReason::Agreement, "1/2-1/2");
}

void GameSession::tick(double dt) {
    if (!clocksRunning()) return;
    const Color us = pos_.sideToMove();
    clock_[us] = std::max(0.0, clock_[us] - dt);
    if (clock_[us] <= 0) {
        finish(EndReason::Timeout, canMate(pos_, ~us) ? (us == White ? "0-1" : "1-0") : "1/2-1/2");
    }
}

void GameSession::finish(EndReason r, const std::string& result) {
    reason_ = r;
    result_ = result;
}

void GameSession::checkEnd() {
    switch (pos_.status()) {
    case GameStatus::Ongoing: break;
    case GameStatus::Checkmate: finish(EndReason::Checkmate, pos_.sideToMove() == White ? "0-1" : "1-0"); break;
    case GameStatus::Stalemate: finish(EndReason::Stalemate, "1/2-1/2"); break;
    case GameStatus::FiftyMoveRule: finish(EndReason::Fifty, "1/2-1/2"); break;
    case GameStatus::ThreefoldRepetition: finish(EndReason::Threefold, "1/2-1/2"); break;
    case GameStatus::InsufficientMaterial: finish(EndReason::Insufficient, "1/2-1/2"); break;
    }
}

std::string GameSession::resultTitle() const {
    if (result_ == "1-0") return "Победа белых";
    if (result_ == "0-1") return "Победа чёрных";
    if (result_ == "1/2-1/2") return "Ничья";
    return "";
}

std::string GameSession::resultDetail() const {
    switch (reason_) {
    case EndReason::None: return "";
    case EndReason::Checkmate: return "Мат";
    case EndReason::Stalemate: return "Пат";
    case EndReason::Fifty: return "Правило 50 ходов";
    case EndReason::Threefold: return "Троекратное повторение";
    case EndReason::Insufficient: return "Недостаточно материала";
    case EndReason::Resign: return result_ == "1-0" ? "Чёрные сдались" : "Белые сдались";
    case EndReason::Agreement: return "По соглашению";
    case EndReason::Timeout:
        return result_ == "1/2-1/2" ? "Время вышло, но матовать нечем" : "Время вышло";
    }
    return "";
}

void GameSession::setViewPly(int ply) { viewPly_ = std::clamp(ply, 0, int(moves_.size())); }

Position GameSession::viewPosition() const { return positionAt(viewPly_); }

Position GameSession::positionAt(int ply) const {
    Position p;
    if (!startFen_.empty()) p.setFEN(startFen_);
    for (int i = 0; i < ply && i < int(moves_.size()); ++i) p.makeMove(moves_[i]);
    return p;
}

Game GameSession::toGame() const {
    Game g;
    char date[16];
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    std::strftime(date, sizeof date, "%Y.%m.%d", &tm);
    g.setTag("Event", "Шахматный тренер");
    g.setTag("Site", "ChessTrainer");
    g.setTag("Date", date);
    g.setTag("White", names_[White]);
    g.setTag("Black", names_[Black]);
    if (tc_.baseSec > 0) g.setTag("TimeControl", std::to_string(tc_.baseSec) + "+" + std::to_string(tc_.incSec));
    if (reason_ == EndReason::Timeout) g.setTag("Termination", "time forfeit");
    g.startFen = startFen_;
    g.moves = moves_;
    g.result = result_;
    return g;
}

}
