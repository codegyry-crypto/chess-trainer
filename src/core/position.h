#pragma once
#include "core/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace chess {
enum class GameStatus {
    Ongoing,
    Checkmate,
    Stalemate,
    FiftyMoveRule,
    ThreefoldRepetition,
    InsufficientMaterial
};

class Position {
public:
    static constexpr const char* StartFEN =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Position();

    bool setFEN(const std::string& fen, std::string* error = nullptr);
    std::string fen() const;

    Piece at(Square s) const { return board_[s]; }
    Color sideToMove() const { return side_; }
    int castlingRights() const { return castling_; }
    int halfmoveClock() const { return halfmove_; }
    int fullmoveNumber() const { return fullmove_; }
    Square kingSquare(Color c) const { return kingSq_[c]; }
    uint64_t hash() const { return hash_; }
    uint64_t computeHash() const;

    bool isAttacked(Square s, Color by) const { return attackers(s, by, nullptr); }
    bool attackers(Square s, Color by, std::vector<Square>* out) const;
    bool inCheck() const { return isAttacked(kingSq_[side_], ~side_); }

    std::vector<Move> legalMoves();
    Move findLegal(Square from, Square to, PieceType promo = NoPieceType);

    void makeMove(Move m);
    void unmakeMove();
    int plyCount() const { return int(history_.size()); }
    Move lastMove() const { return history_.empty() ? Move::none() : history_.back().move; }

    GameStatus status();
    bool repeats(int times) const;
    bool isThreefold() const { return repeats(3); }
    bool isRepetition() const { return repeats(2); }
    bool isInsufficientMaterial() const;

    void generateLegal(std::vector<Move>& out, bool capturesOnly = false);

private:
    struct State {
        Move move;
        Piece captured;
        uint8_t castling;
        Square ep;
        int halfmove;
        uint64_t hash;
    };

    void clear();
    void generatePseudo(std::vector<Move>& out) const;
    void addPawnMove(std::vector<Move>& out, Square from, Square to, uint8_t flags) const;
    void movePieceRaw(Square from, Square to);

    std::array<Piece, 64> board_{};
    Color side_ = White;
    uint8_t castling_ = 0;
    Square ep_ = NoSquare;
    int halfmove_ = 0;
    int fullmove_ = 1;
    std::array<Square, 2> kingSq_{NoSquare, NoSquare};
    uint64_t hash_ = 0;
    std::vector<State> history_;
};

}
