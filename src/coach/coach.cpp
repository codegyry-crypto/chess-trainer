#include "coach/coach.h"

#include "core/notation.h"
#include "engine/eval.h"

#include <algorithm>
#include <cstdio>

namespace coach {

using namespace chess;

std::string pieceName(PieceType t) {
    const char* names[7] = {"", "пешка", "конь", "слон", "ладья", "ферзь", "король"};
    return names[t];
}

std::string pieceAcc(PieceType t) {
    const char* names[7] = {"", "пешку", "коня", "слона", "ладью", "ферзя", "короля"};
    return names[t];
}

std::vector<Threat> threats(const Position& pos, Color victim) {
    std::vector<Threat> out;
    std::vector<Square> att;
    for (Square s = 0; s < 64; ++s) {
        const Piece p = pos.at(s);
        if (p == NoPiece || colorOf(p) != victim || typeOf(p) == King) continue;
        att.clear();
        if (!pos.attackers(s, ~victim, &att)) continue;
        const Square cheapest = *std::min_element(att.begin(), att.end(), [&](Square a, Square b) {
            return engine::pieceValue(typeOf(pos.at(a))) < engine::pieceValue(typeOf(pos.at(b)));
        });
        const bool defended = pos.isAttacked(s, victim);
        if (!defended || engine::pieceValue(typeOf(pos.at(cheapest))) < engine::pieceValue(typeOf(p)))
            out.push_back({cheapest, s});
    }
    return out;
}

MoveCheck checkMove(const Position& pos, Move m, engine::Searcher& s) {
    MoveCheck res;
    Position p = pos;
    engine::Limits lim;
    lim.timeMs = 300;
    const auto scored = s.scoreRootMoves(p, 3, lim);
    const auto mine = std::find_if(scored.begin(), scored.end(), [&](const auto& r) { return r.move == m; });
    if (scored.empty() || mine == scored.end()) return res;
    const int loss = scored.front().score - mine->score;
    if (loss < 150 || mine->score > 500) return res;
    res.blunder = true;

    const Color us = p.sideToMove();
    p.makeMove(m);
    for (const Move& reply : p.legalMoves()) {
        Position q = p;
        const std::string san = toSAN(q, reply, RussianLetters);
        if (!san.empty() && san.back() == '#') {
            res.reason = "После этого хода тебе ставят мат: " + san;
            return res;
        }
    }
    for (const Threat& t : threats(p, us)) {
        const PieceType pt = typeOf(p.at(t.to));
        if (pt == Pawn) continue;
        res.reason = p.isAttacked(t.to, us) ? "Твоего " + pieceAcc(pt) + " на " + squareName(t.to) + " может забрать " +
                                                  pieceName(typeOf(p.at(t.from)))
                                            : "Ты оставляешь " + pieceAcc(pt) + " на " + squareName(t.to) + " без защиты";
        return res;
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "Этот ход теряет примерно %.1f пешки. Есть ход сильнее", loss / 100.0);
    res.reason = buf;
    return res;
}

std::vector<std::string> castlingProblems(Position& pos) {
    std::vector<std::string> out;
    if (pos.chess960()) return out;
    const Color us = pos.sideToMove();
    const int base = us == White ? 0 : 56;
    const Color them = ~us;
    const bool kingHome = pos.at(base + 4) == makePiece(us, King);
    const int rights = pos.castlingRights() >> (us == White ? 0 : 2);
    if (!kingHome || !(rights & 3)) {
        out.push_back(kingHome ? "Рокировки больше не будет: король или обе ладьи уже ходили"
                               : "Рокировки больше не будет: король уже ходил");
        return out;
    }
    const auto legal = pos.legalMoves();
    struct Side {
        const char* name;
        int bit, rook, kingTo;
        std::vector<int> between, path;
    };
    const Side sides[2] = {{"Короткой", 1, base + 7, base + 6, {base + 5, base + 6}, {base + 5, base + 6}},
                           {"Длинной", 2, base, base + 2, {base + 1, base + 2, base + 3}, {base + 3, base + 2}}};
    for (const Side& sd : sides) {
        const bool possible = std::any_of(legal.begin(), legal.end(), [&](const Move& m) {
            return m.isCastle() && m.to == sd.kingTo;
        });
        if (possible) continue;
        const std::string title = std::string(sd.name) + " рокировки нет: ";
        if (!(rights & sd.bit)) {
            out.push_back(title + "ладья " + squareName(sd.rook) + " уже ходила");
        } else if (std::any_of(sd.between.begin(), sd.between.end(), [&](int s) { return pos.at(s) != NoPiece; })) {
            out.push_back(title + "между королём и ладьёй стоят фигуры");
        } else if (pos.inCheck()) {
            out.push_back(title + "король под шахом");
        } else {
            for (int s : sd.path)
                if (pos.isAttacked(s, them)) {
                    out.push_back(title + "поле " + squareName(s) + " под боем");
                    break;
                }
        }
    }
    return out;
}

std::string hint(Position& pos, Move best, bool mate, int level, bool russian) {
    if (best.isNone()) return "";
    if (level >= 3)
        return "Лучший ход: " + toSAN(pos, best, russian ? RussianLetters : EnglishLetters);
    if (level == 2)
        return "Посмотри на " + pieceAcc(typeOf(pos.at(best.from))) + " на " + squareName(best.from);
    if (mate) return "Здесь есть мат! Найди его";
    if (best.isCapture()) return "Можно выиграть материал: присмотрись к взятиям";
    const std::string san = toSAN(pos, best);
    if (!san.empty() && san.back() == '+') return "Есть сильный шах";
    return "Тактики нет: найди ход, который улучшает позицию";
}

}
