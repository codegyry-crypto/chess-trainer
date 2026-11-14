#include "analysis/explain.h"

#include "core/notation.h"

#include <algorithm>

namespace analysis {
using namespace chess;

namespace {
const int Value[7] = {0, 1, 3, 3, 5, 9, 100};
const char* Nom[7] = {"", "пешка", "конь", "слон", "ладья", "ферзь", "король"};
const char* Acc[7] = {"", "пешку", "коня", "слона", "ладью", "ферзя", "короля"};

int value(Piece p) { return Value[typeOf(p)]; }
std::string it(Piece p) { return typeOf(p) == Pawn || typeOf(p) == Rook ? "её" : "его"; }
std::string at(Piece p, Square s) { return std::string(Acc[typeOf(p)]) + " на " + squareName(s); }

int balance(const Position& p, Color c) {
    int b = 0;
    for (Square s = 0; s < 64; ++s)
        if (p.at(s) != NoPiece && typeOf(p.at(s)) != King) b += colorOf(p.at(s)) == c ? value(p.at(s)) : -value(p.at(s));
    return b;
}

std::vector<Move> parse(Position p, const std::vector<std::string>& uci) {
    std::vector<Move> out;
    for (const std::string& u : uci) {
        const Move m = parseUCI(p, u);
        if (m.isNone()) break;
        out.push_back(m);
        p.makeMove(m);
    }
    return out;
}

std::string text(Position p, const std::vector<Move>& moves, size_t n, const MoveNamer& name) {
    std::string s;
    for (size_t k = 0; k < moves.size() && k < n; ++k) {
        s += (k ? " " : "") + name(p, moves[k]);
        p.makeMove(moves[k]);
    }
    return s;
}

int mateLength(Position p, const std::vector<Move>& moves) {
    for (const Move& m : moves) p.makeMove(m);
    return !moves.empty() && p.inCheck() && p.legalMoves().empty() ? int(moves.size() + 1) / 2 : 0;
}

struct Gain {
    int value = 0;
    PieceType top = NoPieceType;
    size_t plies = 0;
};

Gain gain(Position p, const std::vector<Move>& moves, Color c) {
    const int start = balance(p, c);
    Gain g;
    PieceType top = NoPieceType;
    for (size_t k = 0; k < moves.size() && k < 6; ++k) {
        const Piece taken = p.at(moves[k].to);
        if (taken != NoPiece && colorOf(taken) != c && Value[typeOf(taken)] > Value[top]) top = typeOf(taken);
        p.makeMove(moves[k]);
        if (k + 1 == moves.size() || !(moves[k + 1].isCapture() || moves[k + 1].isPromotion()))
            g = {balance(p, c) - start, top, k + 1};
    }
    return g;
}

std::string gainName(const Gain& g) {
    if (g.value == 1) return "пешку";
    if (g.top != NoPieceType && Value[g.top] <= g.value + 1) return Acc[g.top];
    return "материал";
}

int minAttacker(const Position& p, Square s, Color by) {
    std::vector<Square> a;
    p.attackers(s, by, &a);
    int v = 1000;
    for (Square x : a) v = std::min(v, value(p.at(x)));
    return v;
}

bool hanging(const Position& p, Square s) {
    const Piece x = p.at(s);
    if (x == NoPiece || typeOf(x) == King || !p.isAttacked(s, ~colorOf(x))) return false;
    return !p.isAttacked(s, colorOf(x)) || minAttacker(p, s, ~colorOf(x)) < value(x);
}

bool attacksFrom(const Position& p, Square from, Square target) {
    std::vector<Square> a;
    p.attackers(target, colorOf(p.at(from)), &a);
    return std::find(a.begin(), a.end(), from) != a.end();
}

std::string fork(const Position& p, Square f) {
    const Piece a = p.at(f);
    std::vector<Square> targets;
    for (Square s = 0; s < 64; ++s) {
        const Piece t = p.at(s);
        if (t == NoPiece || colorOf(t) == colorOf(a) || value(t) < 3 || !attacksFrom(p, f, s)) continue;
        if (typeOf(t) == King || value(t) > value(a) || !p.isAttacked(s, colorOf(t))) targets.push_back(s);
    }
    if (targets.size() < 2) return {};
    std::sort(targets.begin(), targets.end(), [&](Square x, Square y) { return value(p.at(x)) > value(p.at(y)); });
    return std::string(Nom[typeOf(a)]) + " на " + squareName(f) + " нападает на " + at(p.at(targets[0]), targets[0]) +
           " и " + at(p.at(targets[1]), targets[1]);
}

std::string forkWord(const Position& p, Square f) {
    return typeOf(p.at(f)) == Knight || typeOf(p.at(f)) == Pawn ? "вилка" : "двойной удар";
}

std::string pin(const Position& p, Square f) {
    const Piece a = p.at(f);
    const PieceType t = typeOf(a);
    if (t != Bishop && t != Rook && t != Queen) return {};
    static const int dirs[8][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int d = t == Rook ? 4 : 0; d < (t == Bishop ? 4 : 8); ++d) {
        Square first = NoSquare;
        for (int x = fileOf(f) + dirs[d][0], y = rankOf(f) + dirs[d][1]; onBoard(x, y); x += dirs[d][0], y += dirs[d][1]) {
            const Square s = makeSquare(x, y);
            const Piece q = p.at(s);
            if (q == NoPiece) continue;
            if (colorOf(q) == colorOf(a)) break;
            if (first == NoSquare) {
                if (typeOf(q) == King) break;
                first = s;
                continue;
            }
            const Piece b = p.at(first);
            if (typeOf(q) == King || value(q) > value(b))
                return "связывает " + at(b, first) + " — за " + (it(b) == "её" ? "ней " : "ним ") + Nom[typeOf(q)] +
                       " на " + squareName(s);
            break;
        }
    }
    return {};
}

Square mateThreat(const Position& after) {
    if (after.inCheck()) return NoSquare;
    std::string fen = after.fen();
    const size_t s1 = fen.find(' '), s2 = fen.find(' ', s1 + 1), s3 = fen.find(' ', s2 + 1), s4 = fen.find(' ', s3 + 1);
    fen = fen.substr(0, s1 + 1) + (after.sideToMove() == White ? "b" : "w") + fen.substr(s2, s3 - s2) + " -" +
          fen.substr(s4);
    Position q;
    if (!q.setFEN(fen)) return NoSquare;
    for (const Move& m : q.legalMoves()) {
        q.makeMove(m);
        const bool mate = q.inCheck() && q.legalMoves().empty();
        q.unmakeMove();
        if (mate) return m.to;
    }
    return NoSquare;
}

std::string mateText(Position p, const std::vector<Move>& moves, int n, const MoveNamer& name) {
    if (n == 1) return "мат: " + text(p, moves, 1, name);
    return "мат в " + std::to_string(n) + (n <= 3 ? ": " + text(p, moves, moves.size(), name) : "");
}
}

std::string whyBad(const Position& before, Move played, const std::vector<std::string>& reply, const MoveNamer& name) {
    Position a = before;
    const std::string pname = name(a, played), head = "Ход " + pname;
    a.makeMove(played);
    const std::vector<Move> r = parse(a, reply);
    if (r.empty()) return {};
    if (const int n = mateLength(a, r)) return head + " позволяет " + mateText(a, r, n, name) + ".";

    std::vector<Move> line{played};
    line.insert(line.end(), r.begin(), r.end());
    const Color me = before.sideToMove();
    const Gain g = gain(before, line, ~me);
    if (g.value < 1) return {};

    const Piece x = a.at(r[0].to), attacker = a.at(r[0].from);
    if (x != NoPiece && hanging(a, r[0].to)) {
        const std::string tail = " — " + std::string(Nom[typeOf(attacker)]) + " с " + squareName(r[0].from) + " бьёт " + it(x) + ".";
        if (r[0].to == played.to) return head + " поставил " + Acc[typeOf(x)] + " под удар" + tail;
        return head + " оставил " + at(x, r[0].to) + (a.isAttacked(r[0].to, me) ? " под ударом" : " без защиты") + tail;
    }
    Position c = a;
    c.makeMove(r[0]);
    const std::string f = fork(c, r[0].to);
    if (!f.empty()) return head + " позволяет " + (forkWord(c, r[0].to) == "вилка" ? "вилку" : "двойной удар") + ": " + f + ".";
    const std::string pn = pin(c, r[0].to);
    if (!pn.empty()) return head + " позволяет связку: " + std::string(Nom[typeOf(attacker)]) + " " + pn + ".";
    return "После хода " + pname + " соперник выигрывает " + gainName(g) + ": " +
           text(a, r, g.plies - 1, name) + ".";
}

std::string whyGood(const Position& before, const std::vector<std::string>& line, const MoveNamer& name) {
    const std::vector<Move> moves = parse(before, line);
    if (moves.empty()) return {};
    if (const int n = mateLength(before, moves)) return n == 1 ? "мат" : mateText(before, moves, n, name);

    const Move m = moves[0];
    const Color me = before.sideToMove();
    Position a = before;
    a.makeMove(m);
    const std::string f = fork(a, m.to);
    const std::string pn = pin(a, m.to);
    const Gain g = gain(before, moves, me);
    if (g.value >= 1) {
        if (!f.empty()) return forkWord(a, m.to) + ": " + f;
        if (!pn.empty()) return pn;
        const Piece x = before.at(m.to);
        if (x != NoPiece && !before.isAttacked(m.to, ~me))
            return "забирает " + at(x, m.to) + " — " + (it(x) == "её" ? "она" : "он") + " без защиты";
        return "выигрывает " + gainName(g) + ": " + text(before, moves, g.plies, name);
    }
    const Square t = mateThreat(a);
    if (t != NoSquare) return "угроза мата на " + squareName(t);
    if (!pn.empty()) return pn;
    if (hanging(before, m.from) && !hanging(a, m.to))
        return "уводит " + at(before.at(m.from), m.from) + " из-под удара";
    for (Square s = 0; s < 64; ++s)
        if (s != m.from && before.at(s) != NoPiece && colorOf(before.at(s)) == me && hanging(before, s) &&
            a.at(s) == before.at(s) && a.isAttacked(s, ~me) && !hanging(a, s))
            return "защищает " + at(a.at(s), s);
    return {};
}

}
