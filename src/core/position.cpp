#include "core/position.h"

#include <algorithm>
#include <sstream>

namespace chess {
namespace {
struct Zobrist {
    uint64_t psq[16][64];
    uint64_t side;
    uint64_t castle[16];
    uint64_t ep[8];

    Zobrist() {
        uint64_t s = 0x9E3779B97F4A7C15ull;
        auto next = [&] {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            return s;
        };
        for (auto& p : psq)
            for (auto& v : p) v = next();
        side = next();
        for (auto& v : castle) v = next();
        for (auto& v : ep) v = next();
    }
};
const Zobrist& Z() {
    static const Zobrist z;
    return z;
}

constexpr int KnightD[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int KingD[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
constexpr int BishopD[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
constexpr int RookD[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

constexpr uint8_t castleMask(Square s) {
    switch (s) {
    case 0: return uint8_t(~WhiteOOO & 15);
    case 4: return uint8_t(~(WhiteOO | WhiteOOO) & 15);
    case 7: return uint8_t(~WhiteOO & 15);
    case 56: return uint8_t(~BlackOOO & 15);
    case 60: return uint8_t(~(BlackOO | BlackOOO) & 15);
    case 63: return uint8_t(~BlackOO & 15);
    default: return 15;
    }
}

constexpr const char* PieceChars = " PNBRQK  pnbrqk";

}

Position::Position() { setFEN(StartFEN); }

void Position::clear() {
    board_.fill(NoPiece);
    side_ = White;
    castling_ = 0;
    ep_ = NoSquare;
    halfmove_ = 0;
    fullmove_ = 1;
    kingSq_ = {NoSquare, NoSquare};
    hash_ = 0;
    history_.clear();
}

bool Position::setFEN(const std::string& fen, std::string* error) {
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };

    std::istringstream in(fen);
    std::string placement, side, castling = "-", ep = "-";
    int halfmove = 0, fullmove = 1;
    if (!(in >> placement >> side)) return fail("FEN: не хватает полей");
    in >> castling >> ep;
    if (!(in >> halfmove)) halfmove = 0;
    if (!(in >> fullmove)) fullmove = 1;

    Position p(*this);
    p.clear();

    int rank = 7, file = 0;
    for (char c : placement) {
        if (c == '/') {
            if (file != 8) return fail("FEN: в горизонтали не 8 клеток");
            --rank;
            file = 0;
            if (rank < 0) return fail("FEN: больше 8 горизонталей");
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return fail("FEN: в горизонтали больше 8 клеток");
        } else {
            const char* found = std::char_traits<char>::find(PieceChars, 16, c);
            if (!found || c == ' ') return fail("FEN: неизвестная фигура");
            if (file >= 8) return fail("FEN: в горизонтали больше 8 клеток");
            Piece pc = Piece(found - PieceChars);
            Square s = makeSquare(file, rank);
            p.board_[s] = pc;
            if (typeOf(pc) == King) {
                if (p.kingSq_[colorOf(pc)] != NoSquare) return fail("FEN: два короля одного цвета");
                p.kingSq_[colorOf(pc)] = s;
            }
            if (typeOf(pc) == Pawn && (rank == 0 || rank == 7))
                return fail("FEN: пешка на крайней горизонтали");
            ++file;
        }
    }
    if (rank != 0 || file != 8) return fail("FEN: неполная расстановка");
    if (p.kingSq_[White] == NoSquare || p.kingSq_[Black] == NoSquare)
        return fail("FEN: нет короля");

    if (side == "w") p.side_ = White;
    else if (side == "b") p.side_ = Black;
    else return fail("FEN: очередь хода должна быть w или b");

    if (castling != "-") {
        for (char c : castling) {
            switch (c) {
            case 'K': p.castling_ |= WhiteOO; break;
            case 'Q': p.castling_ |= WhiteOOO; break;
            case 'k': p.castling_ |= BlackOO; break;
            case 'q': p.castling_ |= BlackOOO; break;
            default: return fail("FEN: неверные права рокировки");
            }
        }
    }
    if (p.board_[4] != WKing) p.castling_ &= ~(WhiteOO | WhiteOOO);
    if (p.board_[7] != WRook) p.castling_ &= ~WhiteOO;
    if (p.board_[0] != WRook) p.castling_ &= ~WhiteOOO;
    if (p.board_[60] != BKing) p.castling_ &= ~(BlackOO | BlackOOO);
    if (p.board_[63] != BRook) p.castling_ &= ~BlackOO;
    if (p.board_[56] != BRook) p.castling_ &= ~BlackOOO;

    if (ep != "-") {
        Square s = parseSquare(ep);
        if (s == NoSquare || ep.size() != 2) return fail("FEN: неверная клетка взятия на проходе");
        int expectRank = p.side_ == White ? 5 : 2;
        if (rankOf(s) != expectRank) return fail("FEN: неверная клетка взятия на проходе");
        const int pawnRank = p.side_ == White ? 4 : 3;
        const Piece ourPawn = makePiece(p.side_, Pawn);
        const int f = fileOf(s);
        if ((f > 0 && p.board_[makeSquare(f - 1, pawnRank)] == ourPawn) ||
            (f < 7 && p.board_[makeSquare(f + 1, pawnRank)] == ourPawn))
            p.ep_ = s;
    }

    if (halfmove < 0 || fullmove < 1) return fail("FEN: неверные счётчики ходов");
    p.halfmove_ = halfmove;
    p.fullmove_ = fullmove;

    if (p.isAttacked(p.kingSq_[~p.side_], p.side_))
        return fail("FEN: король стороны, которая не ходит, под шахом");

    p.hash_ = p.computeHash();
    *this = std::move(p);
    return true;
}

std::string Position::fen() const {
    std::string s;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            Piece pc = board_[makeSquare(file, rank)];
            if (pc == NoPiece) {
                ++empty;
                continue;
            }
            if (empty) s += char('0' + empty), empty = 0;
            s += PieceChars[pc];
        }
        if (empty) s += char('0' + empty);
        if (rank) s += '/';
    }
    s += side_ == White ? " w " : " b ";
    if (!castling_) s += '-';
    if (castling_ & WhiteOO) s += 'K';
    if (castling_ & WhiteOOO) s += 'Q';
    if (castling_ & BlackOO) s += 'k';
    if (castling_ & BlackOOO) s += 'q';
    s += ' ';
    s += ep_ == NoSquare ? "-" : squareName(ep_);
    s += ' ' + std::to_string(halfmove_) + ' ' + std::to_string(fullmove_);
    return s;
}

uint64_t Position::computeHash() const {
    const Zobrist& z = Z();
    uint64_t h = 0;
    for (Square s = 0; s < 64; ++s)
        if (board_[s] != NoPiece) h ^= z.psq[board_[s]][s];
    if (side_ == Black) h ^= z.side;
    h ^= z.castle[castling_];
    if (ep_ != NoSquare) h ^= z.ep[fileOf(ep_)];
    return h;
}

bool Position::attackers(Square sq, Color by, std::vector<Square>* out) const {
    const int f = fileOf(sq), r = rankOf(sq);
    auto hit = [&](int x, int y) {
        if (!out) return true;
        out->push_back(makeSquare(x, y));
        return false;
    };
    auto is = [&](int x, int y, Piece p) { return onBoard(x, y) && board_[makeSquare(x, y)] == p; };

    const int pr = by == White ? r - 1 : r + 1;
    for (int df : {-1, 1})
        if (is(f + df, pr, makePiece(by, Pawn)) && hit(f + df, pr)) return true;
    for (auto& d : KnightD)
        if (is(f + d[0], r + d[1], makePiece(by, Knight)) && hit(f + d[0], r + d[1])) return true;
    for (auto& d : KingD)
        if (is(f + d[0], r + d[1], makePiece(by, King)) && hit(f + d[0], r + d[1])) return true;

    const Piece queen = makePiece(by, Queen);
    auto slide = [&](const int (*dirs)[2], Piece slider) {
        for (int i = 0; i < 4; ++i) {
            for (int x = f + dirs[i][0], y = r + dirs[i][1]; onBoard(x, y); x += dirs[i][0], y += dirs[i][1]) {
                const Piece p = board_[makeSquare(x, y)];
                if (p == NoPiece) continue;
                if ((p == slider || p == queen) && hit(x, y)) return true;
                break;
            }
        }
        return false;
    };
    if (slide(BishopD, makePiece(by, Bishop)) || slide(RookD, makePiece(by, Rook))) return true;
    return out && !out->empty();
}

void Position::addPawnMove(std::vector<Move>& out, Square from, Square to, uint8_t flags) const {
    if (rankOf(to) == 0 || rankOf(to) == 7) {
        for (PieceType t : {Queen, Rook, Bishop, Knight})
            out.push_back({uint8_t(from), uint8_t(to), uint8_t(t), flags});
    } else {
        out.push_back({uint8_t(from), uint8_t(to), NoPieceType, flags});
    }
}

void Position::generatePseudo(std::vector<Move>& out) const {
    const Color us = side_;
    auto add = [&](Square from, Square to, uint8_t flags) {
        out.push_back({uint8_t(from), uint8_t(to), NoPieceType, flags});
    };

    for (Square s = 0; s < 64; ++s) {
        const Piece pc = board_[s];
        if (pc == NoPiece || colorOf(pc) != us) continue;
        const int f = fileOf(s), r = rankOf(s);

        switch (typeOf(pc)) {
        case Pawn: {
            const int dir = us == White ? 1 : -1;
            const int startRank = us == White ? 1 : 6;
            const int r1 = r + dir;
            if (board_[makeSquare(f, r1)] == NoPiece) {
                addPawnMove(out, s, makeSquare(f, r1), Move::Quiet);
                if (r == startRank && board_[makeSquare(f, r1 + dir)] == NoPiece)
                    add(s, makeSquare(f, r1 + dir), Move::DoublePush);
            }
            for (int df : {-1, 1}) {
                if (!onBoard(f + df, r1)) continue;
                const Square t = makeSquare(f + df, r1);
                if (board_[t] != NoPiece && colorOf(board_[t]) != us)
                    addPawnMove(out, s, t, Move::Capture);
                else if (t == ep_)
                    add(s, t, Move::Capture | Move::EnPassant);
            }
            break;
        }
        case Knight:
        case King: {
            const auto& dirs = typeOf(pc) == Knight ? KnightD : KingD;
            for (auto& d : dirs) {
                if (!onBoard(f + d[0], r + d[1])) continue;
                const Square t = makeSquare(f + d[0], r + d[1]);
                if (board_[t] == NoPiece) add(s, t, Move::Quiet);
                else if (colorOf(board_[t]) != us) add(s, t, Move::Capture);
            }
            break;
        }
        case Bishop:
        case Rook:
        case Queen: {
            auto slide = [&](const int (*dirs)[2], int n) {
                for (int i = 0; i < n; ++i) {
                    for (int x = f + dirs[i][0], y = r + dirs[i][1]; onBoard(x, y); x += dirs[i][0], y += dirs[i][1]) {
                        const Square t = makeSquare(x, y);
                        if (board_[t] == NoPiece) {
                            add(s, t, Move::Quiet);
                            continue;
                        }
                        if (colorOf(board_[t]) != us) add(s, t, Move::Capture);
                        break;
                    }
                }
            };
            if (typeOf(pc) != Rook) slide(BishopD, 4);
            if (typeOf(pc) != Bishop) slide(RookD, 4);
            break;
        }
        default: break;
        }
    }

    const Color them = ~us;
    const int base = us == White ? 0 : 56;
    const uint8_t oo = us == White ? WhiteOO : BlackOO;
    const uint8_t ooo = us == White ? WhiteOOO : BlackOOO;
    if ((castling_ & (oo | ooo)) && !isAttacked(base + 4, them)) {
        if ((castling_ & oo) && board_[base + 5] == NoPiece && board_[base + 6] == NoPiece &&
            !isAttacked(base + 5, them) && !isAttacked(base + 6, them))
            add(base + 4, base + 6, Move::Castle);
        if ((castling_ & ooo) && board_[base + 3] == NoPiece && board_[base + 2] == NoPiece &&
            board_[base + 1] == NoPiece && !isAttacked(base + 3, them) && !isAttacked(base + 2, them))
            add(base + 4, base + 2, Move::Castle);
    }
}

std::vector<Move> Position::legalMoves() {
    std::vector<Move> legal;
    generateLegal(legal);
    return legal;
}

void Position::generateLegal(std::vector<Move>& out, bool capturesOnly) {
    out.clear();
    generatePseudo(out);
    const Color us = side_;
    size_t n = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        const Move m = out[i];
        if (capturesOnly && !m.isCapture() && !m.isPromotion()) continue;
        makeMove(m);
        const bool ok = !isAttacked(kingSq_[us], ~us);
        unmakeMove();
        if (ok) out[n++] = m;
    }
    out.resize(n);
}

Move Position::findLegal(Square from, Square to, PieceType promo) {
    for (const Move& m : legalMoves())
        if (m.from == from && m.to == to && m.promo == promo) return m;
    return Move::none();
}

void Position::movePieceRaw(Square from, Square to) {
    const Zobrist& z = Z();
    const Piece pc = board_[from];
    hash_ ^= z.psq[pc][from] ^ z.psq[pc][to];
    board_[to] = pc;
    board_[from] = NoPiece;
}

void Position::makeMove(Move m) {
    const Zobrist& z = Z();
    const Color us = side_, them = ~us;
    const Piece pc = board_[m.from];

    history_.push_back({m, NoPiece, castling_, ep_, halfmove_, hash_});
    State& st = history_.back();

    if (ep_ != NoSquare) hash_ ^= z.ep[fileOf(ep_)];
    hash_ ^= z.castle[castling_];

    const Square capSq = m.isEnPassant() ? (us == White ? m.to - 8 : m.to + 8) : m.to;
    const Piece cap = board_[capSq];
    if (cap != NoPiece) {
        hash_ ^= z.psq[cap][capSq];
        board_[capSq] = NoPiece;
    }
    st.captured = cap;

    const Piece placed = m.isPromotion() ? makePiece(us, PieceType(m.promo)) : pc;
    hash_ ^= z.psq[pc][m.from] ^ z.psq[placed][m.to];
    board_[m.from] = NoPiece;
    board_[m.to] = placed;
    if (typeOf(pc) == King) kingSq_[us] = m.to;

    if (m.isCastle()) {
        const int base = us == White ? 0 : 56;
        if (m.to == base + 6) movePieceRaw(base + 7, base + 5);
        else movePieceRaw(base + 0, base + 3);
    }

    castling_ &= castleMask(m.from) & castleMask(m.to);
    hash_ ^= z.castle[castling_];

    ep_ = NoSquare;
    if (m.flags & Move::DoublePush) {
        const Piece enemyPawn = makePiece(them, Pawn);
        const int f = fileOf(m.to), r = rankOf(m.to);
        if ((f > 0 && board_[makeSquare(f - 1, r)] == enemyPawn) ||
            (f < 7 && board_[makeSquare(f + 1, r)] == enemyPawn)) {
            ep_ = (m.from + m.to) / 2;
            hash_ ^= z.ep[fileOf(ep_)];
        }
    }

    halfmove_ = (typeOf(pc) == Pawn || cap != NoPiece) ? 0 : halfmove_ + 1;
    if (us == Black) ++fullmove_;
    side_ = them;
    hash_ ^= z.side;
}

void Position::unmakeMove() {
    const State st = history_.back();
    history_.pop_back();
    const Move m = st.move;
    side_ = ~side_;
    const Color us = side_;

    const Piece placed = board_[m.to];
    const Piece pc = m.isPromotion() ? makePiece(us, Pawn) : placed;
    board_[m.to] = NoPiece;
    board_[m.from] = pc;
    if (typeOf(pc) == King) kingSq_[us] = m.from;

    if (m.isCastle()) {
        const int base = us == White ? 0 : 56;
        if (m.to == base + 6) {
            board_[base + 7] = board_[base + 5];
            board_[base + 5] = NoPiece;
        } else {
            board_[base + 0] = board_[base + 3];
            board_[base + 3] = NoPiece;
        }
    }

    if (st.captured != NoPiece) {
        const Square capSq = m.isEnPassant() ? (us == White ? m.to - 8 : m.to + 8) : m.to;
        board_[capSq] = st.captured;
    }

    castling_ = st.castling;
    ep_ = st.ep;
    halfmove_ = st.halfmove;
    hash_ = st.hash;
    if (us == Black) --fullmove_;
}

bool Position::repeats(int times) const {
    int count = 1;
    const int n = int(history_.size());
    const int limit = std::min(halfmove_, n);
    for (int back = 2; back <= limit; back += 2) {
        if (history_[n - back].hash == hash_ && ++count >= times) return true;
    }
    return false;
}

bool Position::isInsufficientMaterial() const {
    int minors = 0, knights = 0;
    int bishopColors = 0;
    for (Square s = 0; s < 64; ++s) {
        switch (typeOf(board_[s])) {
        case Pawn:
        case Rook:
        case Queen: return false;
        case Knight: ++minors, ++knights; break;
        case Bishop:
            ++minors;
            bishopColors |= ((fileOf(s) + rankOf(s)) % 2) ? 1 : 2;
            break;
        default: break;
        }
    }
    if (minors <= 1) return true;
    return knights == 0 && bishopColors != 3;
}

GameStatus Position::status() {
    if (legalMoves().empty()) return inCheck() ? GameStatus::Checkmate : GameStatus::Stalemate;
    if (isInsufficientMaterial()) return GameStatus::InsufficientMaterial;
    if (halfmove_ >= 100) return GameStatus::FiftyMoveRule;
    if (isThreefold()) return GameStatus::ThreefoldRepetition;
    return GameStatus::Ongoing;
}

}
