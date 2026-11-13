#pragma once
#include "core/pgn.h"
#include "core/position.h"

#include <string>
#include <vector>

namespace ui {
struct TimeControl {
    int baseSec = 0;
    int incSec = 0;
    std::string label;
};
const std::vector<TimeControl>& timeControls();

enum class EndReason { None, Checkmate, Stalemate, Fifty, Threefold, Insufficient, Resign, Agreement, Timeout };

class GameSession {
public:
    void start(const TimeControl& tc, const std::string& whiteName, const std::string& blackName,
               const std::string& fen = {});
    const std::string& startFen() const { return startFen_; }

    chess::Position& position() { return pos_; }
    const chess::Position& position() const { return pos_; }
    const std::vector<chess::Move>& moves() const { return moves_; }
    const std::vector<std::string>& sanEn() const { return sanEn_; }
    const std::vector<std::string>& sanRu() const { return sanRu_; }
    const TimeControl& timeControl() const { return tc_; }
    const std::string& playerName(chess::Color c) const { return names_[c]; }

    bool play(chess::Move m);
    bool canUndo() const { return !moves_.empty(); }
    void undo();
    void resign(chess::Color who);
    void agreeDraw();
    void tick(double dt);

    bool clocksRunning() const;
    double clock(chess::Color c) const { return clock_[c]; }

    bool over() const { return reason_ != EndReason::None; }
    EndReason reason() const { return reason_; }
    std::string result() const { return result_; }
    std::string resultTitle() const;
    std::string resultDetail() const;

    int viewPly() const { return viewPly_; }
    void setViewPly(int ply);
    bool viewingLatest() const { return viewPly_ == int(moves_.size()); }
    chess::Position viewPosition() const;
    chess::Position positionAt(int ply) const;

    chess::Game toGame() const;

private:
    void finish(EndReason r, const std::string& result);
    void checkEnd();

    TimeControl tc_;
    std::string names_[2];
    std::string startFen_;
    chess::Position pos_;
    std::vector<chess::Move> moves_;
    std::vector<std::string> sanEn_, sanRu_;
    std::vector<double> clockBefore_[2];
    double clock_[2] = {0, 0};
    EndReason reason_ = EndReason::None;
    std::string result_ = "*";
    int viewPly_ = 0;
};

}
