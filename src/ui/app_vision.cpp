#include "ui/app.h"

#include "ui/fx.h"
#include "ui/sound.h"
#include "ui/ui.h"

#include <raylib.h>

#include <algorithm>
#include <bit>

namespace ui {

using training::VisionMode;
using training::VisionRound;

namespace {
constexpr float M = 24;
constexpr float PanelP = 430;
constexpr double RoundSeconds = 30;

std::string bestKey(VisionMode m) { return "vision_best_" + std::to_string(int(m)); }
}

void App::openVision(VisionMode mode) {
    vis_.start(mode, rng_);
    visEnd_ = 0;
    visWrongSq_ = -1;
    screen_ = Screen::Vision;
}

void App::drawVision() {
    const Theme& t = theme();
    const float W = screenW(), H = screenH();
    const double now = GetTime();
    const VisionMode mode = vis_.task.mode;

    const float availW = W - 3 * M - PanelP, availH = H - 2 * M - 56;
    const float boardSize = std::floor(std::min(availW, availH) / 8) * 8, cell = boardSize / 8;
    const Rectangle br{M + std::floor((availW - boardSize) / 2), M + 56, boardSize, boardSize};
    text("Видение доски", {br.x, M + 14}, 22, t.text, true);

    if (visEnd_ > 0 && now >= visEnd_) {
        const int best = profile_.counter(bestKey(mode));
        profile_.setBest(bestKey(mode), vis_.score);
        visEnd_ = -1;
        if (vis_.score > best)
            fx::celebrate(fx::Kind::Achievement, {br.x + boardSize / 2, br.y + boardSize / 2}, "Новый рекорд!");
        else
            sound::play(sound::Id::Checkmate);
    }
    if (visEnd_ > 0 && vis_.finished && now >= visPause_) {
        vis_.next(rng_);
        visWrongSq_ = -1;
    }

    auto rectOf = [&](int sq) {
        const int f = sq & 7, r = sq >> 3;
        return Rectangle{br.x + (visBlack_ ? 7 - f : f) * cell, br.y + (visBlack_ ? r : 7 - r) * cell, cell, cell};
    };
    const bool showAnswer = vis_.finished && visEnd_ > 0;
    for (int sq = 0; sq < 64; ++sq) {
        const Rectangle r = rectOf(sq);
        DrawRectangleRec(r, ((sq & 7) + (sq >> 3)) % 2 ? t.light : t.dark);
        if (vis_.marked >> sq & 1) DrawRectangleRec(r, fade(t.good, 0.6f));
        else if (showAnswer && vis_.task.targets >> sq & 1) DrawRectangleRec(r, fade(t.good, 0.3f));
        if (sq == visWrongSq_) DrawRectangleRec(r, fade(t.danger, 0.65f));
    }
    if (mode != VisionMode::Square && visEnd_ != 0)
        pieces_.get(settings_.pieceSet).draw(chess::makePiece(visBlack_ ? chess::Black : chess::White,
                                                              mode == VisionMode::Knight ? chess::Knight : chess::Bishop),
                                             rectOf(vis_.task.square));
    if (visEnd_ > 0 && !vis_.finished && hovered(br)) {
        setHandCursor();
        const Vector2 m = mouse();
        int f = int((m.x - br.x) / cell), r = 7 - int((m.y - br.y) / cell);
        if (visBlack_) f = 7 - f, r = 7 - r;
        const int sq = std::clamp(r, 0, 7) * 8 + std::clamp(f, 0, 7);
        DrawRectangleRec(rectOf(sq), Color{255, 255, 255, 40});
        if (clicked(br)) {
            const VisionRound::Click c = vis_.click(sq);
            if (c == VisionRound::Click::Hit) sound::play(sound::Id::Move);
            if (c == VisionRound::Click::Done) {
                sound::play(mode == VisionMode::Square ? sound::Id::Move : sound::Id::Solved);
                visPause_ = now + (mode == VisionMode::Square ? 0.15 : 0.45);
            }
            if (c == VisionRound::Click::Wrong) {
                sound::play(sound::Id::Illegal);
                visWrongSq_ = sq;
                visPause_ = now + 0.9;
            }
        }
    }

    const float px = W - M - PanelP;
    float py = M;
    const std::pair<VisionMode, const char*> modes[] = {
        {VisionMode::Square, "Клетки"}, {VisionMode::Knight, "Конь"}, {VisionMode::Bishop, "Слон"}};
    const float cw = (PanelP - 16) / 3;
    for (int i = 0; i < 3; ++i)
        if (chip({px + i * (cw + 8), py, cw, 44}, modes[i].second, mode == modes[i].first, true, 15))
            openVision(modes[i].first);
    py += 56;
    bool black = visBlack_;
    if (toggle({px, py, PanelP, 40}, "Смотреть со стороны чёрных", black) && visEnd_ <= 0) visBlack_ = black;
    py += 52;

    const Rectangle ic{px, py, PanelP, 330};
    card(ic);
    const float x = ic.x + 18, iw = ic.width - 36;
    float y = ic.y + 16;
    const std::string sqName = chess::squareName(vis_.task.square);
    const char* piece = mode == VisionMode::Knight ? "конь" : "слон";
    if (visEnd_ == 0) {
        text("Раунд — 30 секунд", {x, y}, 22, t.text, true);
        textWrapped(mode == VisionMode::Square
                        ? "Появится название клетки — кликни по ней. Координат на доске нет, ориентируйся по памяти."
                        : std::string("На доске стоит ") + piece +
                              ". Отметь все клетки, куда он может пойти. Ошибся — задание сгорает.",
                    {x, y + 40, iw, 120}, 17, t.sub);
    } else if (visEnd_ > 0) {
        const double left = std::max(0.0, visEnd_ - now);
        text("0:" + std::string(left < 9.5 ? "0" : "") + std::to_string(int(std::ceil(left))), {x, y}, 34,
             left < 6 ? t.danger : t.text, true);
        roundRect({x + 110, y + 18, iw - 110, 8}, 4, t.panel2);
        roundRect({x + 110, y + 18, std::max(8.0f, (iw - 110) * float(left / RoundSeconds)), 8}, 4,
                  left < 6 ? t.danger : t.accent);
        y += 58;
        if (mode == VisionMode::Square) {
            text("Найди клетку", {x, y}, 18, t.sub);
            text(sqName, {x, y + 26}, 64, visWrongSq_ >= 0 ? t.danger : t.text, true);
        } else {
            textWrapped(std::string("Куда может пойти ") + piece + " с " + sqName + "?", {x, y, iw, 60}, 22, t.text, true);
            const int rest = std::popcount(vis_.task.targets & ~vis_.marked);
            text(visWrongSq_ >= 0 ? "Мимо — вот все клетки" : "Отметь все клетки · осталось " + std::to_string(rest),
                 {x, y + 40}, 17, visWrongSq_ >= 0 ? t.danger : t.sub);
        }
    } else {
        text("Время вышло!", {x, y}, 26, t.text, true);
    }
    const Rectangle sr{x, ic.y + ic.height - 130, iw, 54};
    roundRect(sr, 10, t.panel2);
    text("Счёт", {sr.x + 14, sr.y + 6}, 13, t.sub);
    text(std::to_string(vis_.score), {sr.x + 14, sr.y + 22}, 24, t.good, true);
    text("Ошибок", {sr.x + iw / 3 + 14, sr.y + 6}, 13, t.sub);
    text(std::to_string(vis_.errors), {sr.x + iw / 3 + 14, sr.y + 22}, 24, vis_.errors ? t.danger : t.text, true);
    text("Рекорд", {sr.x + 2 * iw / 3 + 14, sr.y + 6}, 13, t.sub);
    text(std::to_string(profile_.counter(bestKey(mode))), {sr.x + 2 * iw / 3 + 14, sr.y + 22}, 24, t.text, true);
    if (visEnd_ <= 0 && (button({x, ic.y + ic.height - 62, iw, 46}, visEnd_ == 0 ? "Старт" : "Ещё раз", Style::Primary) ||
                         (inputEnabled() && IsKeyPressed(KEY_ENTER)))) {
        vis_.start(mode, rng_);
        visWrongSq_ = -1;
        visEnd_ = now + RoundSeconds;
        sound::play(sound::Id::GameStart);
    }
    if (visEnd_ > 0 && button({x, ic.y + ic.height - 62, iw, 46}, "Закончить", Style::Ghost)) visEnd_ = now;
    if (button({px, ic.y + ic.height + 12, PanelP, 42}, "‹ В меню", Style::Ghost) ||
        (inputEnabled() && IsKeyPressed(KEY_ESCAPE))) {
        visEnd_ = 0;
        screen_ = Screen::Menu;
    }
}

}
