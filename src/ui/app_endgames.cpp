#include "ui/app.h"

#include "core/notation.h"
#include "ui/sound.h"
#include "ui/ui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>

namespace ui {

using chess::Move;
using training::Goal;
using training::Verdict;

namespace {
constexpr float M = 24;
constexpr float PanelL = 430;
constexpr int ThinkMs = 400;

void egSound(const GameSession& s, Move m) {
    if (s.reason() == EndReason::Checkmate) sound::play(sound::Id::Checkmate);
    else if (s.over()) sound::play(sound::Id::Draw);
    else if (s.position().inCheck()) sound::play(sound::Id::Check);
    else sound::play(m.isCapture() ? sound::Id::Capture : sound::Id::Move);
}
}

const training::Endgame& App::endgame() const { return training::endgames()[egIdx_]; }

int App::egScore(size_t i) const { return egEvals_[i].cp * (egColor_ == chess::White ? 1 : -1); }

void App::openEndgame(int idx) {
    egIdx_ = idx;
    screen_ = Screen::Endgames;
    restartEndgame(0);
}

void App::restartEndgame(int keepMoves) {
    if (egFuture_.valid()) egFuture_.wait();
    egFuture_ = {};
    if (keepMoves == 0) {
        eg_.start(timeControls()[0], "", "", endgame().fen);
        egColor_ = eg_.position().sideToMove();
        egEvals_.clear();
    } else {
        while (int(eg_.moves().size()) > 2 * keepMoves) eg_.undo();
        egEvals_.resize(keepMoves + 1);
    }
    egVerdict_ = Verdict::Continue;
    egMsg_.clear();
    egMistake_ = 0;
    egShowMistake_ = false;
    board_.reset();
    if (egEvals_.empty()) askEndgameEngine();
}

void App::askEndgameEngine() {
    std::vector<std::string> uci;
    for (const Move& m : eg_.moves()) uci.push_back(chess::toUCI(m));
    const std::string fen = endgame().fen;
    egFuture_ = std::async(std::launch::async, [this, fen, uci]() { return quickEval(fen, uci, ThinkMs); });
}

void App::finishEndgame(Verdict v, const std::string& msg) {
    egVerdict_ = v;
    egMsg_ = msg;
    if (v == Verdict::Won) {
        sound::play(sound::Id::GameStart);
        if (egDone_.insert(endgame().id).second && shotFile_.empty()) profile_.setValue("endgame:" + endgame().id, "1");
        progress("endgame", endgame().id);
        return;
    }
    int worst = -1000000;
    for (size_t k = 1; k < egEvals_.size(); ++k)
        if (egScore(k - 1) - egScore(k) > worst) worst = egScore(k - 1) - egScore(k), egMistake_ = int(k);
}

void App::endgameMove(Move m) {
    if (!eg_.play(m)) return;
    egSound(eg_, m);
    egPromoted_ = m.isPromotion();
    if (eg_.over()) {
        egEvals_.push_back(analysis::terminalEval(eg_.position()));
        if (eg_.reason() == EndReason::Checkmate) finishEndgame(Verdict::Won, "Мат! Задание выполнено");
        else if (endgame().goal == Goal::Draw) finishEndgame(Verdict::Won, "Ничья! Задание выполнено");
        else finishEndgame(Verdict::Lost, eg_.reason() == EndReason::Stalemate ? "Пат — ничья" : "Ничья — выигрыш упущен");
        return;
    }
    askEndgameEngine();
}

void App::pollEndgame() {
    if (!egFuture_.valid() || egFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    analysis::Eval e = egFuture_.get();
    if (!e.known) {
        egVerdict_ = Verdict::Lost;
        egMsg_ = "Stockfish не запустился";
        return;
    }
    egEvals_.push_back(e);
    const int played = int(egEvals_.size()) - 1;
    if (played > 0) {
        const training::Endgame& g = endgame();
        const Verdict v = training::judge(g.goal, g.moves, played, egScore(played), egPromoted_);
        if (v == Verdict::Won)
            return finishEndgame(v, g.goal == Goal::Draw ? "Ничья удержана! Задание выполнено" : "Пешка в ферзях! Задание выполнено");
        if (v == Verdict::Lost) {
            const bool slow = egScore(played) >= 300 && g.goal != Goal::Draw;
            return finishEndgame(v, slow ? "Не уложился в " + std::to_string(g.moves) + " ходов"
                                         : g.goal == Goal::Draw ? "Позиция стала проигранной" : "Выигрыш упущен");
        }
    }
    if (played == 0) return;
    const chess::Position before = eg_.position();
    const Move m = chess::parseUCI(eg_.position(), e.bestUci);
    if (m.isNone() || !eg_.play(m)) return;
    if (settings_.animate) board_.animate(before, m);
    egSound(eg_, m);
    if (!eg_.over()) return;
    if (eg_.reason() == EndReason::Checkmate) finishEndgame(Verdict::Lost, "Тебе поставили мат");
    else if (endgame().goal == Goal::Draw) finishEndgame(Verdict::Won, "Ничья! Задание выполнено");
    else finishEndgame(Verdict::Lost, "Ничья — выигрыш упущен");
}

void App::drawEndgameList() {
    const Theme& t = theme();
    const float W = screenW();
    const float left = 56, top = 40;
    const auto& list = training::endgames();

    text("Эндшпили", {left, top}, 34, t.text, true);
    text("Пройдено " + std::to_string(egDone_.size()) + " из " + std::to_string(list.size()) +
             " · соперник — Stockfish в полную силу",
         {left, top + 48}, 16, t.sub);
    if (button({W - left - 130, top + 6, 130, 46}, "‹ В меню", Style::Ghost) || (inputEnabled() && IsKeyPressed(KEY_ESCAPE)))
        screen_ = Screen::Menu;

    constexpr int Cols = 2;
    constexpr float Gap = 16, CardH = 96;
    const float colW = (W - 2 * left - Gap) / Cols;
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& e = list[i];
        const Rectangle r{left + (i % Cols) * (colW + Gap), top + 96 + (i / Cols) * (CardH + Gap), colW, CardH};
        const bool hot = hovered(r);
        roundRect(r, 12, hot ? mix(t.panel, t.text, 0.06f) : t.panel);
        if (hot) setHandCursor();
        const Vector2 c{r.x + 28, r.y + 34};
        if (egDone_.count(e.id)) {
            DrawCircleV(c, 11, t.good);
            DrawLineEx({c.x - 5.5f, c.y}, {c.x - 1.2f, c.y + 4.3f}, 2.6f, t.bg);
            DrawLineEx({c.x - 1.2f, c.y + 4.3f}, {c.x + 6, c.y - 4.3f}, 2.6f, t.bg);
        } else {
            DrawCircleLinesV(c, 10, t.sub);
        }
        text(e.title, {r.x + 52, r.y + 20}, 21, t.text, true);
        text(training::goalText(e) + (e.goal == Goal::Draw ? " · играешь за слабейшую сторону" : ""), {r.x + 52, r.y + 56}, 16, t.sub);
        if (clicked(r)) openEndgame(int(i));
    }
}

void App::drawEndgames() {
    if (egIdx_ < 0) {
        drawEndgameList();
        return;
    }
    pollEndgame();
    const Theme& t = theme();
    const float W = screenW(), H = screenH();
    const training::Endgame& g = endgame();
    const int played = (int(eg_.moves().size()) + 1) / 2;
    const bool over = egVerdict_ != Verdict::Continue;
    const bool thinking = egFuture_.valid();

    const float availW = W - 3 * M - PanelL, availH = H - 2 * M - 56;
    const float boardSize = std::floor(std::min(availW, availH) / 8) * 8;
    const Rectangle boardRect{M + std::floor((availW - boardSize) / 2), M + 56, boardSize, boardSize};
    BoardOptions opt;
    opt.flipped = egColor_ == chess::Black;
    opt.coords = settings_.showCoords;
    opt.legalMoves = settings_.showLegalMoves;
    opt.animate = settings_.animate;

    std::vector<BoardView::Arrow> arrows;
    chess::Position shown = eg_.position();
    if (egShowMistake_ && egMistake_ > 0) {
        const int ply = 2 * (egMistake_ - 1);
        shown = eg_.positionAt(ply);
        const Move mv = eg_.moves()[ply];
        arrows.push_back({mv.from, mv.to, ::Color{230, 140, 40, 220}});
        const analysis::Eval& prev = egEvals_[egMistake_ - 1];
        const std::string best = egMistake_ == 1 ? prev.bestUci : prev.pv.size() > 1 ? prev.pv[1] : "";
        if (best.size() >= 4 && best != chess::toUCI(mv))
            arrows.push_back({chess::parseSquare(best.substr(0, 2)), chess::parseSquare(best.substr(2, 2)), ::Color{40, 120, 230, 220}});
    }
    board_.setHintArrows(std::move(arrows));
    const bool active = !over && !thinking && !egEvals_.empty() && eg_.position().sideToMove() == egColor_;
    const Move m = board_.update(boardRect, shown, active, pieces_.get(settings_.pieceSet), opt);
    if (!m.isNone()) endgameMove(m);
    text(egShowMistake_ ? "Позиция перед ошибкой" : g.title, {boardRect.x, M + 14}, 22, t.text, true);

    const float px = W - M - PanelL;
    float py = M;
    text("Эндшпили", {px, py}, 15, t.sub);
    text(g.title, {px, py + 22}, 26, t.text, true);
    py += 72;
    const std::string cnt = "Ход " + std::to_string(std::min(played, g.moves)) + " из " + std::to_string(g.moves);
    text(training::goalText(g), {px, py}, 18, t.accent, true);
    text(cnt, {px + PanelL - measure(cnt, 15).x, py + 3}, 15, t.sub);
    py += 34;

    const float buttonsH = 116;
    const Rectangle ic{px, py, PanelL, std::max(200.0f, H - M - buttonsH - py)};
    card(ic);
    float y = ic.y + 18;
    y += textWrapped(g.text, {ic.x + 18, y, ic.width - 36, 0}, 17, t.text) + 14;
    std::string status = egColor_ == chess::White ? "Ты играешь белыми — твой ход" : "Ты играешь чёрными — твой ход";
    ::Color sc = t.sub;
    if (thinking) status = "Stockfish думает…";
    if (over) status = egMsg_, sc = egVerdict_ == Verdict::Won ? t.good : t.danger;
    y += textWrapped(status, {ic.x + 18, y, ic.width - 36, 0}, 19, sc, true) + 10;
    if (egVerdict_ == Verdict::Lost && egMistake_ > 0) {
        const int ply = 2 * (egMistake_ - 1);
        const std::string san = std::to_string(ply / 2 + 1) + (egColor_ == chess::White ? ". " : "… ") + sanList(eg_.moves(), eg_.sanEn(), eg_.sanRu())[ply];
        y += textWrapped("Главная ошибка — " + san + ". Оценка Stockfish до хода " + analysis::formatEval(egEvals_[egMistake_ - 1]) +
                             ", после " + analysis::formatEval(egEvals_[egMistake_]) + ".",
                         {ic.x + 18, y, ic.width - 36, 0}, 16, t.text) + 6;
        if (egShowMistake_)
            textWrapped("Оранжевая стрелка — твой ход, синяя — лучший ход Stockfish.", {ic.x + 18, y, ic.width - 36, 0}, 15, t.sub);
    }

    py = ic.y + ic.height + 12;
    const float bw = (PanelL - 8) / 2;
    const bool mistake = egVerdict_ == Verdict::Lost && egMistake_ > 0;
    if (button({px, py, bw, 46}, "Сначала", Style::Secondary, !thinking, 15)) restartEndgame(0);
    if (mistake && button({px + bw + 8, py, bw, 46}, "С ошибки", Style::Primary, true, 15)) restartEndgame(egMistake_ - 1);
    if (egVerdict_ == Verdict::Won && egIdx_ + 1 < int(training::endgames().size()) &&
        button({px + bw + 8, py, bw, 46}, "Следующая ›", Style::Primary, true, 15)) {
        openEndgame(egIdx_ + 1);
        return;
    }
    py += 58;
    if (mistake && button({px, py, bw, 42}, egShowMistake_ ? "Скрыть ошибку" : "Показать ошибку", Style::Secondary, true, 15))
        egShowMistake_ = !egShowMistake_;
    if (button({mistake ? px + bw + 8 : px, py, mistake ? bw : PanelL, 42}, "‹ К списку", Style::Ghost) ||
        (inputEnabled() && IsKeyPressed(KEY_ESCAPE))) {
        if (egFuture_.valid()) egFuture_.wait();
        egFuture_ = {};
        egIdx_ = -1;
    }
}

}
