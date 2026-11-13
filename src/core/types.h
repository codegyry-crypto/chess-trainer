#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace chess {
enum Color : uint8_t { White = 0, Black = 1 };
constexpr Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : uint8_t { NoPieceType = 0, Pawn, Knight, Bishop, Rook, Queen, King };

enum Piece : uint8_t {
    NoPiece = 0,
    WPawn = 1, WKnight, WBishop, WRook, WQueen, WKing,
    BPawn = 9, BKnight, BBishop, BRook, BQueen, BKing,
};
constexpr Piece makePiece(Color c, PieceType t) { return Piece(t | (c << 3)); }
constexpr PieceType typeOf(Piece p) { return PieceType(p & 7); }
constexpr Color colorOf(Piece p) { return Color(p >> 3); }

using Square = int;
constexpr Square NoSquare = -1;
constexpr int fileOf(Square s) { return s & 7; }
constexpr int rankOf(Square s) { return s >> 3; }
constexpr Square makeSquare(int file, int rank) { return rank * 8 + file; }
constexpr bool onBoard(int file, int rank) { return file >= 0 && file < 8 && rank >= 0 && rank < 8; }

inline std::string squareName(Square s) {
    return {char('a' + fileOf(s)), char('1' + rankOf(s))};
}
inline Square parseSquare(std::string_view s) {
    if (s.size() < 2 || s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return NoSquare;
    return makeSquare(s[0] - 'a', s[1] - '1');
}

enum Castling : uint8_t { WhiteOO = 1, WhiteOOO = 2, BlackOO = 4, BlackOOO = 8 };

struct Move {
    enum Flags : uint8_t { Quiet = 0, Capture = 1, DoublePush = 2, EnPassant = 4, Castle = 8 };

    uint8_t from = 0;
    uint8_t to = 0;
    uint8_t promo = NoPieceType;
    uint8_t flags = Quiet;

    static constexpr Move none() { return {}; }
    constexpr bool isNone() const { return from == to; }
    constexpr bool isCapture() const { return flags & Capture; }
    constexpr bool isPromotion() const { return promo != NoPieceType; }
    constexpr bool isCastle() const { return flags & Castle; }
    constexpr bool isEnPassant() const { return flags & EnPassant; }

    constexpr bool operator==(const Move& o) const {
        return from == o.from && to == o.to && promo == o.promo;
    }
};

}
