#include "ui/board_view.h"

#include "ui/ui.h"

#include <algorithm>
#include <cmath>

namespace ui {
using namespace chess;

namespace {
constexpr PieceType PromoOrder[4] = {Queen, Knight, Rook, Bishop};

float easeOut(float t) { return 1 - (1 - t) * (1 - t) * (1 - t); }

}

Rectangle BoardView::squareRect(int sq) const {
    const int f = fileOf(sq), r = rankOf(sq);
    const int col = flipped_ ? 7 - f : f;
    const int row = flipped_ ? r : 7 - r;
    return {rect_.x + col * cell_, rect_.y + row * cell_, cell_, cell_};
}

Vector2 BoardView::squareCenter(int sq) const {
    const Rectangle r = squareRect(sq);
    return {r.x + cell_ / 2, r.y + cell_ / 2};
}

int BoardView::squareAt(Vector2 p) const {
    if (!CheckCollisionPointRec(p, rect_)) return -1;
    int col = std::clamp(int((p.x - rect_.x) / cell_), 0, 7);
    int row = std::clamp(int((p.y - rect_.y) / cell_), 0, 7);
    const int f = flipped_ ? 7 - col : col;
    const int r = flipped_ ? row : 7 - row;
    return makeSquare(f, r);
}

void BoardView::refreshLegal(Position& pos) {
    if (pos.hash() == legalHash_ && pos.plyCount() == legalPly_) return;
    legal_ = pos.legalMoves();
    legalHash_ = pos.hash();
    legalPly_ = pos.plyCount();
}

bool BoardView::isTarget(int from, int to) const {
    for (const Move& m : legal_)
        if (m.from == from && m.to == to) return true;
    return false;
}

Move BoardView::tryMove(int from, int to) {
    Move found = Move::none();
    int count = 0;
    for (const Move& m : legal_) {
        if (m.from == from && m.to == to) {
            found = m;
            ++count;
        }
    }
    if (count > 1) {
        promoOpen_ = true;
        promoFrom_ = from;
        promoTo_ = to;
        return Move::none();
    }
    return found;
}

void BoardView::animate(const Position& before, Move m) {
    anims_.clear();
    anims_.push_back({before.at(m.from), m.from, m.to});
    if (m.isCastle()) {
        const int base = m.from - fileOf(m.from);
        if (fileOf(m.to) == 6) anims_.push_back({before.at(base + 7), base + 7, base + 5});
        else anims_.push_back({before.at(base), base, base + 3});
    }
    animStart_ = GetTime();
}

void BoardView::animateBack(const Position& before, Move m) {
    anims_.clear();
    anims_.push_back({before.at(m.from), m.to, m.from});
    if (m.isCastle()) {
        const int base = m.from - fileOf(m.from);
        if (fileOf(m.to) == 6) anims_.push_back({before.at(base + 7), base + 5, base + 7});
        else anims_.push_back({before.at(base), base + 3, base});
    }
    animStart_ = GetTime();
}

void BoardView::reset() {
    selected_ = -1;
    dragging_ = dragMoved_ = false;
    promoOpen_ = false;
    anims_.clear();
    arrows_.clear();
    rightFrom_ = -1;
}

Move BoardView::update(Rectangle r, Position& pos, bool interactive, PieceSet& set, const BoardOptions& opt) {
    const Theme& t = theme();
    rect_ = {std::floor(r.x), std::floor(r.y), std::floor(r.width / 8) * 8, std::floor(r.width / 8) * 8};
    cell_ = rect_.width / 8;
    flipped_ = opt.flipped;
    refreshLegal(pos);

    const Vector2 mouse = ui::mouse();
    const int hoverSq = squareAt(mouse);
    const bool input = interactive && inputEnabled();
    const bool animating = opt.animate && !anims_.empty() && GetTime() - animStart_ < AnimTime;
    if (!animating) anims_.clear();
    if (!interactive) {
        selected_ = -1;
        dragging_ = false;
        promoOpen_ = false;
    }

    Move result = Move::none();
    auto ownPiece = [&](int sq) {
        return sq >= 0 && pos.at(sq) != NoPiece && colorOf(pos.at(sq)) == pos.sideToMove();
    };

    if (input && promoOpen_) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const Rectangle edge = squareRect(promoTo_);
            bool chosen = false;
            for (int i = 0; i < 4; ++i) {
                const float y = (rankOf(promoTo_) == 7) != flipped_ ? edge.y + i * cell_ : edge.y - i * cell_;
                const Rectangle cr{edge.x, y, cell_, cell_};
                if (CheckCollisionPointRec(mouse, cr)) {
                    result = pos.findLegal(promoFrom_, promoTo_, PromoOrder[i]);
                    chosen = true;
                }
            }
            promoOpen_ = false;
            selected_ = -1;
            if (chosen && !result.isNone() && opt.animate) animate(pos, result);
        }
        if (IsKeyPressed(KEY_ESCAPE)) promoOpen_ = false;
    } else if (input) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hoverSq >= 0) {
            arrows_.clear();
            if (selected_ >= 0 && selected_ != hoverSq && isTarget(selected_, hoverSq)) {
                const Move m = tryMove(selected_, hoverSq);
                if (!m.isNone()) {
                    if (opt.animate) animate(pos, m);
                    result = m;
                    selected_ = -1;
                }
            } else if (ownPiece(hoverSq)) {
                wasSelectedOnPress_ = selected_ == hoverSq;
                selected_ = hoverSq;
                dragging_ = true;
                dragMoved_ = false;
                pressPos_ = mouse;
            } else {
                selected_ = -1;
            }
        } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            selected_ = -1;
        }

        if (dragging_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const float dx = mouse.x - pressPos_.x, dy = mouse.y - pressPos_.y;
            if (dx * dx + dy * dy > 16) dragMoved_ = true;
        }
        if (dragging_ && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            dragging_ = false;
            if (hoverSq == selected_) {
                if (!dragMoved_ && wasSelectedOnPress_) selected_ = -1;
            } else if (hoverSq >= 0 && isTarget(selected_, hoverSq)) {
                const Move m = tryMove(selected_, hoverSq);
                if (!m.isNone()) {
                    result = m;
                    selected_ = -1;
                }
            } else {
                selected_ = -1;
            }
            dragMoved_ = false;
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            rightFrom_ = hoverSq;
            selected_ = -1;
            dragging_ = false;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) && rightFrom_ >= 0 && hoverSq >= 0) {
            ::Color c{21, 120, 27, 200};
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) c = {200, 40, 40, 200};
            else if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) c = {0, 70, 200, 200};
            else if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) c = {225, 160, 0, 200};
            auto same = std::find_if(arrows_.begin(), arrows_.end(), [&](const Arrow& a) {
                return a.from == rightFrom_ && a.to == hoverSq;
            });
            if (same != arrows_.end()) arrows_.erase(same);
            else arrows_.push_back({rightFrom_, hoverSq, c});
            rightFrom_ = -1;
        }
        if (IsKeyPressed(KEY_ESCAPE)) selected_ = -1;
    }
    if (!result.isNone()) {
        selected_ = -1;
        dragging_ = false;
    }

    for (int sq = 0; sq < 64; ++sq) {
        const bool light = (fileOf(sq) + rankOf(sq)) % 2 == 1;
        DrawRectangleRec(squareRect(sq), light ? t.light : t.dark);
    }
    const Move last = pos.lastMove();
    if (opt.lastMove && !last.isNone()) {
        const ::Color hl = opt.lastMoveTint.a ? opt.lastMoveTint : t.lastMove;
        DrawRectangleRec(squareRect(last.from), hl);
        DrawRectangleRec(squareRect(last.to), hl);
    }
    if (selected_ >= 0) DrawRectangleRec(squareRect(selected_), t.selected);

    if (pos.inCheck()) {
        const Vector2 c = squareCenter(pos.kingSquare(pos.sideToMove()));
        DrawCircleGradient((int)c.x, (int)c.y, cell_ * 0.62f, ::Color{255, 30, 30, 235}, ::Color{255, 30, 30, 0});
    }

    if (opt.coords) {
        const float fs = std::max(11.0f, cell_ * 0.2f);
        for (int i = 0; i < 8; ++i) {
            const int fileSq = makeSquare(flipped_ ? 7 - i : i, flipped_ ? 7 : 0);
            const Rectangle fr = squareRect(fileSq);
            const bool fl = (fileOf(fileSq) + rankOf(fileSq)) % 2 == 1;
            const std::string fl_s(1, char('a' + fileOf(fileSq)));
            const Vector2 fm = measure(fl_s, fs, true);
            text(fl_s, {fr.x + cell_ - fm.x - cell_ * 0.06f, fr.y + cell_ - fm.y - cell_ * 0.02f}, fs,
                 fl ? t.dark : t.light, true);

            const int rankSq = makeSquare(flipped_ ? 7 : 0, flipped_ ? i : 7 - i);
            const Rectangle rr = squareRect(rankSq);
            const bool rl = (fileOf(rankSq) + rankOf(rankSq)) % 2 == 1;
            text(std::string(1, char('1' + rankOf(rankSq))), {rr.x + cell_ * 0.05f, rr.y + cell_ * 0.02f}, fs,
                 rl ? t.dark : t.light, true);
        }
    }

    const bool showDrag = dragging_ && dragMoved_ && selected_ >= 0;
    for (int sq = 0; sq < 64; ++sq) {
        if (pos.at(sq) == NoPiece) continue;
        if (showDrag && sq == selected_) continue;
        bool animTarget = false;
        if (animating)
            for (const Anim& a : anims_) animTarget |= a.to == sq;
        if (animTarget) continue;
        set.draw(pos.at(sq), squareRect(sq));
    }

    if (opt.legalMoves && selected_ >= 0 && interactive) {
        const ::Color dot{20, 85, 30, 110};
        for (const Move& m : legal_) {
            if (m.from != selected_) continue;
            const Vector2 c = squareCenter(m.to);
            if (pos.at(m.to) != NoPiece) DrawRing(c, cell_ * 0.40f, cell_ * 0.49f, 0, 360, 48, dot);
            else DrawCircleV(c, cell_ * 0.15f, dot);
        }
        if (showDrag && hoverSq >= 0 && isTarget(selected_, hoverSq))
            DrawRectangleRec(squareRect(hoverSq), ::Color{255, 255, 255, 55});
    }

    if (animating) {
        const float k = easeOut(float((GetTime() - animStart_) / AnimTime));
        for (const Anim& a : anims_) {
            const Rectangle f = squareRect(a.from), to = squareRect(a.to);
            const Piece shown = pos.at(a.to) != NoPiece ? pos.at(a.to) : a.piece;
            set.draw(shown, {f.x + (to.x - f.x) * k, f.y + (to.y - f.y) * k, cell_, cell_});
        }
    }

    if (!animating)
        for (const Arrow& a : hints_) drawArrow(a);
    for (const Arrow& a : arrows_) drawArrow(a);
    if (rightFrom_ >= 0 && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && hoverSq >= 0)
        drawArrow({rightFrom_, hoverSq, ::Color{21, 120, 27, 140}});

    if (showDrag) {
        const float s = cell_ * 1.08f;
        set.draw(pos.at(selected_), {mouse.x - s / 2, mouse.y - s / 2, s, s});
    }

    if (promoOpen_) {
        DrawRectangleRec(rect_, ::Color{0, 0, 0, 120});
        const Rectangle edge = squareRect(promoTo_);
        const Piece pawnColor = pos.at(promoFrom_);
        for (int i = 0; i < 4; ++i) {
            const float y = (rankOf(promoTo_) == 7) != flipped_ ? edge.y + i * cell_ : edge.y - i * cell_;
            const Rectangle cr{edge.x, y, cell_, cell_};
            const bool hot = CheckCollisionPointRec(mouse, cr);
            DrawCircleV({cr.x + cell_ / 2, cr.y + cell_ / 2}, cell_ * 0.48f, hot ? t.accent : fade(t.panel2, 0.95f));
            const float pad = hot ? 0 : cell_ * 0.08f;
            set.draw(makePiece(colorOf(pawnColor), PromoOrder[i]),
                     {cr.x + pad, cr.y + pad, cell_ - 2 * pad, cell_ - 2 * pad});
            if (hot) setHandCursor();
        }
    }

    if (input && !promoOpen_ && hoverSq >= 0 && (ownPiece(hoverSq) || (selected_ >= 0 && isTarget(selected_, hoverSq))))
        setHandCursor();
    return result;
}

void BoardView::drawArrow(const Arrow& a) const {
    const Vector2 c0 = squareCenter(a.from), c1 = squareCenter(a.to);
    if (a.from == a.to) {
        DrawRing(c0, cell_ * 0.42f, cell_ * 0.49f, 0, 360, 48, a.color);
        return;
    }
    float dx = c1.x - c0.x, dy = c1.y - c0.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    dx /= len, dy /= len;
    const float headLen = std::min(cell_ * 0.42f, len * 0.36f);
    const float headW = std::min(cell_ * 0.27f, len * 0.24f), thick = cell_ * 0.15f;
    const Vector2 start{c0.x, c0.y};
    const Vector2 tip{c1.x - dx * cell_ * 0.12f, c1.y - dy * cell_ * 0.12f};
    const Vector2 base{tip.x - dx * headLen, tip.y - dy * headLen};
    DrawLineEx(start, base, thick, a.color);
    const Vector2 l{base.x - dy * headW, base.y + dx * headW};
    const Vector2 rr{base.x + dy * headW, base.y - dx * headW};
    DrawTriangle(tip, l, rr, a.color);
    DrawTriangle(tip, rr, l, a.color);
}

}
