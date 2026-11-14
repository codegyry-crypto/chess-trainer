#include "core/position.h"

#include <algorithm>
#include <cctype>
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

constexpr const char* PieceChars = " PNBRQK  pnbrqk";

}

Position::Position() { setFEN(StartFEN); }

std::string Position::chess960FEN(int n) {
    static constexpr int Knights[10][2] = {{0, 1}, {0, 2}, {0, 3}, {0, 4}, {1, 2}, {1, 3}, {1, 4}, {2, 3}, {2, 4}, {3, 4}};
    std::string rank(8, ' ');
    auto putEmpty = [&](int index, char c) {
        for (char& x : rank)
            if (x == ' ' && index-- == 0) return void(x = c);
    };
    n = ((n % 960) + 960) % 960;
    rank[2 * (n % 4) + 1] = 'b';
    n /= 4;
    rank[2 * (n % 4)] = 'b';
    n /= 4;
    putEmpty(n % 6, 'q');
    n /= 6;
    putEmpty(Knights[n][1], 'n');
    putEmpty(Knights[n][0], 'n');
    putEmpty(0, 'r');
    putEmpty(0, 'k');
    putEmpty(0, 'r');
    std::string upper = rank;
    for (char& c : upper) c = char(c - 32);
    return rank + "/pppppppp/8/8/8/8/PPPPPPPP/" + upper + " w KQkq - 0 1";
}

void Position::clear() {
    board_.fill(NoPiece);
    side_ = White;
    castling_ = 0;
    chess960_ = false;
    castleMask_.fill(15);
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
            const Color col = std::islower((unsigned char)c) ? Black : White;
            const char u = char(std::toupper((unsigned char)c));
            const int base = col == White ? 0 : 56;
            const Square k = p.kingSq_[col];
            const Piece rook = makePiece(col, Rook);
            if (u != 'K' && u != 'Q' && (u < 'A' || u > 'H')) return fail("FEN: неверные права рокировки");
            if (k - base < 0 || k - base > 7) continue;
            Square r = NoSquare;
            if (u == 'K') {
                for (int f = 7; f > fileOf(k) && r == NoSquare; --f)
                    if (p.board_[base + f] == rook) r = base + f;
            } else if (u == 'Q') {
                for (int f = 0; f < fileOf(k) && r == NoSquare; ++f)
                    if (p.board_[base + f] == rook) r = base + f;
            } else if (p.board_[base + u - 'A'] == rook && u - 'A' != fileOf(k)) {
                r = base + u - 'A';
            }
            if (r == NoSquare) continue;
            const bool isShort = r > k;
            const int idx = col * 2 + (isShort ? 0 : 1);
            p.castling_ |= 1 << idx;
            p.castleRook_[idx] = r;
            p.castleMask_[r] &= uint8_t(~(1 << idx));
            p.castleMask_[k] &= uint8_t(~(1 << idx));
            if (fileOf(k) != 4 || fileOf(r) != (isShort ? 7 : 0)) p.chess960_ = true;
        }
    }

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
    for (int i = 0; i < 4; ++i) {
        if (!(castling_ & (1 << i))) continue;
        const char c = chess960_ ? char('A' + fileOf(castleRook_[i])) : "KQ"[i & 1];
        s += i < 2 ? c : char(std::tolower(c));
    }
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
    const Square k = kingSq_[us];
    for (int idx = us * 2; idx < us * 2 + 2; ++idx) {
        if (!(castling_ & (1 << idx))) continue;
        const Square r = castleRook_[idx];
        const Move m{uint8_t(k), uint8_t(chess960_ ? r : (r > k ? k + 2 : k - 2)), NoPieceType, Move::Castle};
        const Square kt = castleKingTo(m), rt = castleRookTo(m);
        bool ok = true;
        for (Square s = std::min({k, r, kt, rt}); ok && s <= std::max({k, r, kt, rt}); ++s)
            if (s != k && s != r && board_[s] != NoPiece) ok = false;
        for (Square s = std::min(k, kt); ok && s <= std::max(k, kt); ++s)
            if (isAttacked(s, them)) ok = false;
        if (ok) out.push_back(m);
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

void Position::makeMove(Move m) {
    const Zobrist& z = Z();
    const Color us = side_, them = ~us;
    const Piece pc = board_[m.from];

    history_.push_back({m, NoPiece, castling_, ep_, halfmove_, hash_});
    State& st = history_.back();

    if (ep_ != NoSquare) hash_ ^= z.ep[fileOf(ep_)];
    hash_ ^= z.castle[castling_];

    Piece cap = NoPiece;
    if (m.isCastle()) {
        const Square r = castleRook(m), kt = castleKingTo(m), rt = castleRookTo(m);
        const Piece rook = makePiece(us, Rook);
        hash_ ^= z.psq[pc][m.from] ^ z.psq[rook][r] ^ z.psq[pc][kt] ^ z.psq[rook][rt];
        board_[m.from] = board_[r] = NoPiece;
        board_[kt] = pc;
        board_[rt] = rook;
        kingSq_[us] = kt;
    } else {
        const Square capSq = m.isEnPassant() ? (us == White ? m.to - 8 : m.to + 8) : m.to;
        cap = board_[capSq];
        if (cap != NoPiece) {
            hash_ ^= z.psq[cap][capSq];
            board_[capSq] = NoPiece;
        }
        const Piece placed = m.isPromotion() ? makePiece(us, PieceType(m.promo)) : pc;
        hash_ ^= z.psq[pc][m.from] ^ z.psq[placed][m.to];
        board_[m.from] = NoPiece;
        board_[m.to] = placed;
        if (typeOf(pc) == King) kingSq_[us] = m.to;
    }
    st.captured = cap;

    castling_ &= castleMask_[m.from] & castleMask_[m.to];
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

    if (m.isCastle()) {
        board_[castleKingTo(m)] = board_[castleRookTo(m)] = NoPiece;
        board_[m.from] = makePiece(us, King);
        board_[castleRook(m)] = makePiece(us, Rook);
        kingSq_[us] = m.from;
    } else {
        const Piece placed = board_[m.to];
        const Piece pc = m.isPromotion() ? makePiece(us, Pawn) : placed;
        board_[m.to] = NoPiece;
        board_[m.from] = pc;
        if (typeOf(pc) == King) kingSq_[us] = m.from;
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
