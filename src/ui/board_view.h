#pragma once
#include "core/position.h"
#include "ui/pieces.h"

#include <raylib.h>

#include <vector>

namespace ui {
struct BoardOptions {
    bool flipped = false;
    bool coords = true;
    bool legalMoves = true;
    bool lastMove = true;
    bool animate = true;
    Color lastMoveTint{};
};

class BoardView {
public:
    chess::Move update(Rectangle r, chess::Position& pos, bool interactive, PieceSet& set,
                       const BoardOptions& opt);

    void animate(const chess::Position& before, chess::Move m);
    void animateBack(const chess::Position& before, chess::Move m);
    void reset();
    int selectedSquare() const { return selected_; }
    Rectangle squareRect(int sq) const;

    struct Arrow {
        int from, to;
        Color color;
    };
    void setHintArrows(std::vector<Arrow> arrows) { hints_ = std::move(arrows); }

private:
    struct Anim {
        chess::Piece piece;
        int from, to;
    };

    int squareAt(Vector2 p) const;
    Vector2 squareCenter(int sq) const;
    void refreshLegal(chess::Position& pos);
    bool isTarget(int from, int to) const;
    chess::Move tryMove(int from, int to);
    void drawArrow(const Arrow& a) const;

    Rectangle rect_{};
    float cell_ = 0;
    bool flipped_ = false;

    uint64_t legalHash_ = 0;
    int legalPly_ = -1;
    std::vector<chess::Move> legal_;

    int selected_ = -1;
    bool wasSelectedOnPress_ = false;
    bool dragging_ = false;
    bool dragMoved_ = false;
    Vector2 pressPos_{};

    bool promoOpen_ = false;
    int promoFrom_ = -1, promoTo_ = -1;

    std::vector<Anim> anims_;
    double animStart_ = 0;
    static constexpr double AnimTime = 0.16;

    std::vector<Arrow> arrows_;
    std::vector<Arrow> hints_;
    int rightFrom_ = -1;
};

}
