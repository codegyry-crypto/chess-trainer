#include "analysis/review.h"

#include "core/notation.h"
#include "engine/bots.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace analysis {
using namespace chess;

double winPercent(int cpWhite) {
    const double cp = std::clamp(cpWhite, -1000, 1000);
    return 50.0 + 50.0 * (2.0 / (1.0 + std::exp(-0.00368208 * cp)) - 1.0);
}

int cpFromScore(const uci::Score& s) {
    if (!s.mate) return s.value;
    if (s.value > 0) return 10000 - s.value * 10;
    return -10000 - s.value * 10;
}

Eval evalFromAnalysis(const uci::Analysis& a, Color sideToMove) {
    Eval e;
    if (a.lines.empty()) return e;
    const uci::Line& l = a.lines.front();
    const int sign = sideToMove == White ? 1 : -1;
    e.known = true;
    e.cp = sign * cpFromScore(l.score);
    e.mate = l.score.mate;
    e.mateIn = l.score.mate ? sign * l.score.value : 0;
    e.bestUci = a.bestmove.empty() && !l.pv.empty() ? l.pv.front() : a.bestmove;
    e.pv = l.pv;
    if (a.lines.size() > 1) {
        e.hasSecond = true;
        e.secondCp = sign * cpFromScore(a.lines[1].score);
    }
    return e;
}

Eval terminalEval(Position& pos) {
    Eval e;
    if (!pos.legalMoves().empty()) {
        if (pos.isInsufficientMaterial() || pos.halfmoveClock() >= 100 || pos.isThreefold()) e.known = true;
        return e;
    }
    e.known = true;
    if (pos.inCheck()) {
        e.mate = true;
        e.mateIn = 0;
        e.cp = pos.sideToMove() == White ? -10000 : 10000;
    }
    return e;
}

namespace {
int pieceValue(Piece p) {
    static const int v[7] = {0, 1, 3, 3, 5, 9, 0};
    return v[typeOf(p)];
}
}

bool isSacrifice(const Position& before, Position& after, Move m, Piece captured) {
    const Color mover = ~after.sideToMove();
    for (const Move& c : after.legalMoves()) {
        const Piece target = after.at(c.to);
        if (target == NoPiece || colorOf(target) != mover || pieceValue(target) < 3) continue;
        if (c.to != m.to && before.isAttacked(c.to, ~mover) && !before.isAttacked(c.to, mover)) continue;
        int loss = pieceValue(target);
        if (after.isAttacked(c.to, mover)) loss -= pieceValue(after.at(c.from));
        if (c.to == m.to) loss -= pieceValue(captured);
        if (loss >= 2) return true;
    }
    return false;
}

std::vector<MoveReview> reviewMoves(const std::vector<Eval>& evals, const std::vector<Move>& moves,
                                    const std::string& startFen) {
    std::vector<MoveReview> out(moves.size());
    Position pos;
    if (!startFen.empty()) pos.setFEN(startFen);
    bool inBook = startFen.empty();
    for (size_t i = 0; i < moves.size(); ++i) {
        const Move m = moves[i];
        const int sign = pos.sideToMove() == White ? 1 : -1;
        const bool forced = pos.legalMoves().size() == 1;
        if (inBook) {
            const std::vector<Move> book = engine::bookMoves({moves.begin(), moves.begin() + i});
            inBook = std::find(book.begin(), book.end(), m) != book.end();
        }
        const Piece captured = pos.at(m.to);
        const Position prev = pos;
        pos.makeMove(m);
        MoveReview& r = out[i];
        if (forced || inBook) r.grade = forced ? Grade::Forced : Grade::Book;
        if (i + 1 >= evals.size() || !evals[i].known || !evals[i + 1].known) continue;
        const double before = winPercent(sign * evals[i].cp);
        const double after = winPercent(sign * evals[i + 1].cp);
        const double loss = std::max(0.0, before - after);
        r.winLoss = loss;
        r.accuracy = std::clamp(103.1668 * std::exp(-0.04354 * loss) - 3.1669, 0.0, 100.0);
        if (r.grade != Grade::None) continue;
        const bool best = !evals[i].bestUci.empty() && toUCI(m) == evals[i].bestUci;
        const bool recapture = i > 0 && moves[i - 1].isCapture() && moves[i - 1].to == m.to;
        const double gap = evals[i].hasSecond ? before - winPercent(sign * evals[i].secondCp) : 0;
        if ((best || loss < 2) && after >= 45 && isSacrifice(prev, pos, m, captured)) r.grade = Grade::Brilliant;
        else if (best && gap >= 20 && !recapture) r.grade = Grade::Great;
        else if (best) r.grade = Grade::Best;
        else if (loss < 2) r.grade = Grade::Excellent;
        else if (loss < 5) r.grade = Grade::Good;
        else if (loss < 10) r.grade = Grade::Inaccuracy;
        else if (before >= 80 && after < 65 && after >= 35) r.grade = Grade::Miss;
        else if (loss < 15) r.grade = Grade::Mistake;
        else r.grade = Grade::Blunder;
    }
    return out;
}

Summary summarize(const std::vector<MoveReview>& reviews, Color firstMover) {
    Summary s;
    double sum[2] = {0, 0};
    int n[2] = {0, 0};
    for (size_t i = 0; i < reviews.size(); ++i) {
        const MoveReview& r = reviews[i];
        if (r.grade == Grade::None) continue;
        const int c = ((i % 2 == 0) ? firstMover : ~firstMover) == White ? 0 : 1;
        sum[c] += r.accuracy;
        ++n[c];
        ++s.reviewed;
        ++s.count[c][int(r.grade)];
    }
    for (int c = 0; c < 2; ++c) s.accuracy[c] = n[c] ? sum[c] / n[c] : 0;
    return s;
}

const char* gradeName(Grade g) {
    static const char* names[GradeCount] = {"",           "Бриллиантовый ход", "Отличный ход", "Лучший ход",
                                            "Превосходный ход", "Хороший ход", "Книжный ход", "Неточность",
                                            "Ошибка",     "Упущенный выигрыш", "Зевок",        "Вынужденный ход"};
    return names[int(g)];
}

const char* gradeSymbol(Grade g) {
    switch (g) {
    case Grade::Brilliant: return "!!";
    case Grade::Great: return "!";
    case Grade::Inaccuracy: return "?!";
    case Grade::Mistake: return "?";
    case Grade::Blunder: return "??";
    default: return "";
    }
}

std::string formatEval(const Eval& e) {
    if (!e.known) return "…";
    char buf[32];
    if (e.mate) {
        if (e.mateIn == 0) return e.cp > 0 ? "1-0" : "0-1";
        std::snprintf(buf, sizeof buf, "#%s%d", e.mateIn < 0 ? "−" : "", std::abs(e.mateIn));
        return buf;
    }
    std::snprintf(buf, sizeof buf, "%s%.1f", e.cp > 0 ? "+" : (e.cp < 0 ? "−" : ""), std::abs(e.cp) / 100.0);
    return buf;
}

}
