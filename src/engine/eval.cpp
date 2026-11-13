#include "engine/eval.h"

#include <algorithm>
#include <cstdlib>

namespace engine {
using namespace chess;

namespace {
constexpr int PawnMg[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0};
constexpr int PawnEg[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    90, 90, 90, 90, 90, 90, 90, 90,
    55, 55, 55, 55, 55, 55, 55, 55,
    30, 30, 30, 30, 30, 30, 30, 30,
    15, 15, 15, 15, 15, 15, 15, 15,
     5,  5,  5,  5,  5,  5,  5,  5,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0};
constexpr int KnightPst[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50};
constexpr int BishopPst[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -20,-10,-10,-10,-10,-10,-10,-20};
constexpr int RookPst[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10, 10, 10, 10, 10,  5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     0,  0,  0,  5,  5,  0,  0,  0};
constexpr int QueenPst[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5,  5,  5,  5,  0,-10,
    -5,  0,  5,  5,  5,  5,  0, -5,
     0,  0,  5,  5,  5,  5,  0, -5,
   -10,  5,  5,  5,  5,  5,  0,-10,
   -10,  0,  5,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20};
constexpr int KingMg[64] = {
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -10,-20,-20,-20,-20,-20,-20,-10,
    20, 20,  0,  0,  0,  0, 20, 20,
    20, 30, 10,  0,  0, 10, 30, 20};
constexpr int KingEg[64] = {
   -50,-40,-30,-20,-20,-30,-40,-50,
   -30,-20,-10,  0,  0,-10,-20,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-30,  0,  0,  0,  0,-30,-30,
   -50,-30,-30,-30,-30,-30,-30,-50};

constexpr int ValueMg[7] = {0, 100, 320, 330, 500, 950, 0};
constexpr int ValueEg[7] = {0, 120, 300, 320, 530, 950, 0};
constexpr int PhaseWeight[7] = {0, 0, 1, 1, 2, 4, 0};
constexpr int PassedMg[8] = {0, 5, 10, 15, 25, 40, 60, 0};
constexpr int PassedEg[8] = {0, 10, 20, 35, 60, 95, 140, 0};
constexpr int MobilityWeight[7] = {0, 0, 4, 4, 2, 1, 0};
constexpr int MobilityBase[7] = {0, 0, 4, 6, 6, 12, 0};
constexpr int AttackWeight[7] = {0, 0, 14, 10, 12, 22, 0};

constexpr int Dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
constexpr int KnightD[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};

int tableIndex(Color c, Square s) {
    return c == White ? (7 - rankOf(s)) * 8 + fileOf(s) : rankOf(s) * 8 + fileOf(s);
}

int pstMg(PieceType t, int idx) {
    switch (t) {
    case Pawn: return PawnMg[idx];
    case Knight: return KnightPst[idx];
    case Bishop: return BishopPst[idx];
    case Rook: return RookPst[idx];
    case Queen: return QueenPst[idx];
    case King: return KingMg[idx];
    default: return 0;
    }
}

int pstEg(PieceType t, int idx) {
    switch (t) {
    case Pawn: return PawnEg[idx];
    case King: return KingEg[idx];
    default: return pstMg(t, idx);
    }
}

int mobility(const Position& pos, Square s, PieceType t, Color us) {
    const int f = fileOf(s), r = rankOf(s);
    int n = 0;
    if (t == Knight) {
        for (auto& d : KnightD) {
            if (!onBoard(f + d[0], r + d[1])) continue;
            const Piece p = pos.at(makeSquare(f + d[0], r + d[1]));
            if (p == NoPiece || colorOf(p) != us) ++n;
        }
        return n;
    }
    const int from = t == Bishop ? 4 : 0, to = t == Rook ? 4 : 8;
    for (int i = from; i < to; ++i) {
        for (int x = f + Dirs[i][0], y = r + Dirs[i][1]; onBoard(x, y); x += Dirs[i][0], y += Dirs[i][1]) {
            const Piece p = pos.at(makeSquare(x, y));
            if (p == NoPiece) {
                ++n;
                continue;
            }
            if (colorOf(p) != us) ++n;
            break;
        }
    }
    return n;
}

}

int pieceValue(PieceType t) { return ValueMg[t]; }

int evaluate(const Position& pos, const EvalParams& p) {
    int mg[2] = {0, 0}, eg[2] = {0, 0};
    int phase = 0;
    int material[2] = {0, 0};
    int bishops[2] = {0, 0};
    int pawnFiles[2][8] = {};
    int pawnMaxRank[2][8], pawnMinRank[2][8];
    for (int c = 0; c < 2; ++c)
        for (int f = 0; f < 8; ++f) pawnMaxRank[c][f] = -1, pawnMinRank[c][f] = 8;

    for (Square s = 0; s < 64; ++s) {
        const Piece pc = pos.at(s);
        if (pc == NoPiece) continue;
        const Color c = colorOf(pc);
        const PieceType t = typeOf(pc);
        const int idx = tableIndex(c, s);
        mg[c] += ValueMg[t] + pstMg(t, idx);
        eg[c] += ValueEg[t] + pstEg(t, idx);
        phase += PhaseWeight[t];
        material[c] += ValueMg[t];
        if (t == Bishop) ++bishops[c];
        if (t == Pawn) {
            const int f = fileOf(s), r = rankOf(s);
            ++pawnFiles[c][f];
            pawnMaxRank[c][f] = std::max(pawnMaxRank[c][f], r);
            pawnMinRank[c][f] = std::min(pawnMinRank[c][f], r);
        }
    }
    phase = std::min(phase, 24);

    for (int c = 0; c < 2; ++c) {
        if (bishops[c] >= 2) mg[c] += 30, eg[c] += 50;
    }

    for (Square s = 0; s < 64; ++s) {
        const Piece pc = pos.at(s);
        if (pc == NoPiece || typeOf(pc) != Pawn) continue;
        const Color c = colorOf(pc), o = ~c;
        const int f = fileOf(s), r = rankOf(s);
        int smg = 0, seg = 0;
        const bool isolated = (f == 0 || !pawnFiles[c][f - 1]) && (f == 7 || !pawnFiles[c][f + 1]);
        if (isolated) smg -= 12, seg -= 15;
        bool passed = true;
        for (int df = -1; df <= 1 && passed; ++df) {
            const int ff = f + df;
            if (ff < 0 || ff > 7 || !pawnFiles[o][ff]) continue;
            if (c == White ? pawnMaxRank[o][ff] > r : pawnMinRank[o][ff] < r) passed = false;
        }
        if (passed) {
            const int rel = c == White ? r : 7 - r;
            smg += PassedMg[rel];
            seg += PassedEg[rel];
        }
        mg[c] += smg * p.pawnStructure / 100;
        eg[c] += seg * p.pawnStructure / 100;
    }
    for (int c = 0; c < 2; ++c)
        for (int f = 0; f < 8; ++f)
            if (pawnFiles[c][f] > 1) {
                mg[c] -= 12 * (pawnFiles[c][f] - 1) * p.pawnStructure / 100;
                eg[c] -= 20 * (pawnFiles[c][f] - 1) * p.pawnStructure / 100;
            }

    for (Square s = 0; s < 64; ++s) {
        const Piece pc = pos.at(s);
        if (pc == NoPiece) continue;
        const Color c = colorOf(pc), o = ~c;
        const PieceType t = typeOf(pc);
        if (t == Pawn || t == King) continue;
        const int f = fileOf(s), r = rankOf(s);

        const int mob = (mobility(pos, s, t, c) - MobilityBase[t]) * MobilityWeight[t] * p.mobility / 100;
        mg[c] += mob;
        eg[c] += mob;

        if (t == Rook) {
            if (!pawnFiles[c][f]) {
                const int bonus = pawnFiles[o][f] ? 12 : 25;
                mg[c] += bonus;
                eg[c] += bonus / 2;
            }
            if ((c == White ? r : 7 - r) == 6) mg[c] += 15, eg[c] += 20;
        }

        const Square ek = pos.kingSquare(o);
        const int dist = std::max(std::abs(fileOf(ek) - f), std::abs(rankOf(ek) - r));
        if (dist <= 2) mg[c] += AttackWeight[t] * (3 - dist) / 2 * p.kingAttack / 100;
    }

    for (int c = 0; c < 2; ++c) {
        const Square k = pos.kingSquare(Color(c));
        const int kf = fileOf(k), kr = rankOf(k);
        const int home = c == White ? 0 : 7, dir = c == White ? 1 : -1;
        if (kr != home && kr != home + dir) continue;
        int shield = 0;
        for (int df = -1; df <= 1; ++df) {
            const int ff = kf + df;
            if (ff < 0 || ff > 7) continue;
            if (onBoard(ff, kr + dir) && pos.at(makeSquare(ff, kr + dir)) == makePiece(Color(c), Pawn)) shield += 12;
            else if (onBoard(ff, kr + 2 * dir) && pos.at(makeSquare(ff, kr + 2 * dir)) == makePiece(Color(c), Pawn))
                shield += 6;
            else shield -= 10;
        }
        mg[c] += shield * p.kingSafety / 100;
    }

    for (Square s : {27, 28, 35, 36}) {
        const Piece pc = pos.at(s);
        if (pc != NoPiece) mg[colorOf(pc)] += 10 * p.center / 100;
    }

    int score = (mg[White] - mg[Black]) * phase / 24 + (eg[White] - eg[Black]) * (24 - phase) / 24;

    const int diff = material[White] - material[Black];
    if (p.tradeWhenAhead && std::abs(diff) > 150)
        score += (diff > 0 ? 1 : -1) * p.tradeWhenAhead * (24 - phase) / 8;

    if (phase <= 8 && std::abs(diff) >= 300) {
        const Color strong = diff > 0 ? White : Black;
        const Square wk = pos.kingSquare(strong), lk = pos.kingSquare(~strong);
        const int centerDist = std::max(3 - fileOf(lk), fileOf(lk) - 4) + std::max(3 - rankOf(lk), rankOf(lk) - 4);
        const int kingDist = std::abs(fileOf(wk) - fileOf(lk)) + std::abs(rankOf(wk) - rankOf(lk));
        const int mop = centerDist * 12 + (14 - kingDist) * 5;
        score += strong == White ? mop : -mop;
    }

    score += pos.sideToMove() == White ? 10 : -10;
    return pos.sideToMove() == White ? score : -score;
}

}
