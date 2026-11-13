#include "ui/app.h"

#include "coach/coach.h"
#include "ui/sound.h"
#include "ui/ui.h"

#include <raylib.h>

namespace ui {

using chess::Move;
using chess::Position;

chess::Color App::coachSide() const { return bot_ ? humanColor_ : session_.position().sideToMove(); }

void App::coachUpdate() {
    const int ply = int(session_.moves().size());
    if (ply != coachPly_) {
        coachPly_ = ply;
        hintLevel_ = 0;
        hintReady_ = false;
        showThreats_ = false;
    }
    if (hintFuture_.valid() && hintFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const engine::Result r = hintFuture_.get();
        hintReady_ = hintForPly_ == coachPly_;
        hintMove_ = r.best;
        hintMate_ = r.score > engine::MateBound;
    }
}

std::vector<BoardView::Arrow> App::coachArrows() {
    std::vector<BoardView::Arrow> out;
    if (!coachOn_ || !session_.viewingLatest()) return out;
    if (showThreats_)
        for (const coach::Threat& t : coach::threats(session_.position(), coachSide()))
            out.push_back({t.from, t.to, ::Color{220, 60, 60, 200}});
    if (hintReady_ && hintLevel_ >= 2) {
        const ::Color green{40, 170, 90, 210};
        out.push_back({hintMove_.from, hintLevel_ >= 3 ? hintMove_.to : hintMove_.from, green});
    }
    return out;
}

bool App::coachIntercept(Move m) {
    if (!coachOn_) return false;
    const coach::MoveCheck c = coach::checkMove(session_.position(), m, checkSearcher_);
    if (!c.blunder) return false;
    pendingMove_ = m;
    warning_ = c.reason;
    board_.reset();
    sound::play(sound::Id::Illegal);
    return true;
}

void App::nextHint() {
    if (hintLevel_ == 0) {
        if (hintFuture_.valid()) hintFuture_.wait();
        hintFuture_ = {};
        hintForPly_ = coachPly_;
        engine::Limits lim;
        lim.timeMs = 600;
        const Position p = session_.position();
        hintFuture_ = std::async(std::launch::async, [this, p, lim]() { return hintSearcher_.search(p, lim); });
    }
    ++hintLevel_;
}

void App::drawCoachCard(Rectangle r) {
    const Theme& t = theme();
    card(r);
    roundRectLines(r, 12, 1, fade(t.accent, 0.6f));
    text("Тренер", {r.x + 18, r.y + 12}, 18, t.accent, true);

    Position& pos = session_.position();
    const bool myTurn = humanToMove() && !session_.over() && session_.viewingLatest();
    std::string msg = myTurn ? "Твой ход. Застрял — жми «Подсказка»." : "Соперник думает…";
    ::Color mc = t.sub;
    if (showThreats_) {
        const size_t n = coach::threats(pos, coachSide()).size();
        msg = n ? "Красные стрелки — что у тебя под боем (" + std::to_string(n) + ")"
                : "Сейчас тебе ничего не угрожает";
    }
    if (hintLevel_ > 0) {
        msg = hintReady_ ? coach::hint(pos, hintMove_, hintMate_, hintLevel_, settings_.russianNotation)
                         : "Тренер смотрит позицию…";
        mc = t.text;
    }
    const int sel = board_.selectedSquare();
    if (sel >= 0 && sel == pos.kingSquare(pos.sideToMove())) {
        const auto problems = coach::castlingProblems(pos);
        if (!problems.empty()) {
            msg.clear();
            for (const std::string& p : problems) msg += p + "\n";
            mc = t.text;
        }
    }
    textWrapped(msg, {r.x + 18, r.y + 42, r.width - 36, 70}, 15, mc);

    const float bw = (r.width - 36 - 8) / 2, by = r.y + r.height - 52;
    const std::string hintLabel = hintLevel_ == 0 ? "Подсказка" : "Подсказка " + std::to_string(hintLevel_) + "/3";
    if (button({r.x + 18, by, bw, 40}, hintLabel, Style::Secondary, myTurn && hintLevel_ < 3, 15)) {
        nextHint();
    }
    if (chip({r.x + 18 + bw + 8, by, bw, 40}, "Угрозы", showThreats_, myTurn, 15)) showThreats_ = !showThreats_;
}

void App::drawBlunderWarning(Rectangle b) {
    const Theme& t = theme();
    DrawRectangleRec(b, ::Color{0, 0, 0, 120});
    const Rectangle r{b.x + (b.width - 440) / 2, b.y + (b.height - 220) / 2, 440, 220};
    card(r, 16);
    roundRectLines(r, 16, 2, t.danger);
    text("Подумай ещё!", {r.x + 24, r.y + 20}, 26, t.danger, true);
    textWrapped(warning_, {r.x + 24, r.y + 64, r.width - 48, 70}, 17, t.text);
    if (button({r.x + 24, r.y + 150, 190, 48}, "Переходить", Style::Primary)) pendingMove_ = Move::none();
    if (button({r.x + 226, r.y + 150, 190, 48}, "Всё равно сходить")) {
        const Move m = pendingMove_;
        pendingMove_ = Move::none();
        if (session_.play(m)) moveSound(m);
    }
}

}
