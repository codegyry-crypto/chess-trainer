#include "ui/app.h"

#include "ui/ui.h"

#include <raylib.h>

namespace ui {

void App::drawChess960Picker(float x, float y) {
    const Theme& t = theme();
    const bool random = chess960Pos_ < 0;
    if (chip({x, y, 118, 44}, "Случайная", random, true, 15)) chess960Pos_ = -1;
    const int cur = random ? 518 : chess960Pos_;
    const Rectangle num{x + 160, y, 70, 44};
    int step = 0;
    if (chip({x + 126, y, 30, 44}, "‹", false, true, 18)) step = -1;
    if (chip({x + 234, y, 30, 44}, "›", false, true, 18)) step = 1;
    if (hovered({x + 126, y, 138, 44})) step += int(GetMouseWheelMove());
    if (step) chess960Pos_ = (cur + step + 960) % 960;
    textCentered(random ? "№ ?" : "№ " + std::to_string(chess960Pos_), num, 16, random ? t.sub : t.text, true);

    const Rectangle strip{x + 274, y, 224, 44};
    roundRect(strip, 8, t.panel2);
    if (random) {
        textCentered("расстановка выпадет на старте", strip, 13, t.sub);
        return;
    }
    chess::Position p;
    p.setFEN(chess::Position::chess960FEN(chess960Pos_));
    for (int f = 0; f < 8; ++f)
        pieces_.get(settings_.pieceSet).draw(p.at(f), {strip.x + f * 28, strip.y + 8, 28, 28});
}

std::string App::chess960Fen() {
    if (!chess960_) return {};
    const int n = chess960Pos_ < 0 ? int(rng_() % 960) : chess960Pos_;
    toast("Шахматы-960 · позиция № " + std::to_string(n));
    return n == 518 ? std::string() : chess::Position::chess960FEN(n);
}

}
