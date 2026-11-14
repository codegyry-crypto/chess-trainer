#include "analysis/weakness.h"

#include "core/notation.h"

#include <algorithm>
#include <functional>
#include <sstream>

namespace analysis {
using namespace chess;

namespace {
int value(Piece p) {
    static const int v[7] = {0, 1, 3, 3, 5, 9, 0};
    return v[typeOf(p)];
}

template <size_t N>
int argmax(const int (&a)[N], int from = 0) {
    int best = from;
    for (int i = from; i < int(N); ++i)
        if (a[i] > a[best]) best = i;
    return best;
}

Move parseOr(const Position& p, const std::string& uci) {
    Position c = p;
    return uci.empty() ? Move{} : parseUCI(c, uci);
}

bool givesCheck(const Position& p, Move m) {
    Position c = p;
    c.makeMove(m);
    return c.inCheck();
}

bool isError(Grade g) { return g == Grade::Mistake || g == Grade::Miss || g == Grade::Blunder; }
}

int positionPhase(const Position& p) {
    int pieces = 0;
    for (int s = 0; s < 64; ++s) {
        const PieceType t = typeOf(p.at(Square(s)));
        pieces += t >= Knight && t <= Queen;
    }
    if (pieces <= 6) return 2;
    return p.fullmoveNumber() <= 10 ? 0 : 1;
}

GameMistakes findMistakes(const std::string& startFen, const std::vector<Move>& moves, const std::vector<Eval>& evals,
                          Color me) {
    GameMistakes out;
    const auto grades = reviewMoves(evals, moves, startFen);
    const int sign = me == White ? 1 : -1;
    Position pos;
    if (!startFen.empty()) pos.setFEN(startFen);
    std::string prevFen;
    for (size_t i = 0; i < moves.size(); ++i) {
        const Position before = pos;
        const std::string fen = pos.fen();
        pos.makeMove(moves[i]);
        const Grade g = grades[i].grade;
        const bool mine = before.sideToMove() == me;
        if (mine && g != Grade::None && g != Grade::Book && g != Grade::Forced) ++out.moves[positionPhase(before)];
        if (mine && isError(g)) {
            const Eval& e0 = evals[i];
            const Eval& e1 = evals[i + 1];
            Mistake k;
            k.ply = int(i);
            k.moveNumber = before.fullmoveNumber();
            k.phase = positionPhase(before);
            k.piece = typeOf(before.at(moves[i].from));
            k.grade = g;
            k.winLoss = grades[i].winLoss;
            k.fen = prevFen;
            k.prevUci = i > 0 ? toUCI(moves[i - 1]) : "";
            k.bestUci = e0.bestUci;
            const Move best = parseOr(before, e0.bestUci);
            const Move reply = parseOr(pos, e1.bestUci);
            const Piece target = reply.isNone() ? NoPiece : pos.at(reply.to);
            const bool hung = target != NoPiece && colorOf(target) == me && value(target) >= 3 &&
                              (value(target) > value(pos.at(reply.from)) || !pos.isAttacked(reply.to, me));
            const bool tactical = !best.isNone() && (best.isCapture() || givesCheck(before, best));
            if (e0.mate && e0.mateIn * sign > 0) k.kind = ErrorKind::MissedMate;
            else if (e1.mate && e1.mateIn * sign < 0) k.kind = ErrorKind::AllowedMate;
            else if (hung) k.kind = ErrorKind::HungPiece;
            else if (tactical) k.kind = ErrorKind::MissedTactic;
            const bool clear = !e0.hasSecond || winPercent(sign * e0.cp) - winPercent(sign * e0.secondCp) >= 10;
            k.puzzle = i > 0 && !best.isNone() && clear && (tactical || k.kind == ErrorKind::MissedMate || k.winLoss >= 20);
            out.mistakes.push_back(k);
        }
        prevFen = fen;
    }
    return out;
}

std::string mistakeKey(const Mistake& m) { return "mine-" + std::to_string(std::hash<std::string>{}(m.fen + m.bestUci)); }

const char* kindTheme(ErrorKind k) {
    static const char* t[ErrorKindCount] = {"hangingPiece", "mateIn2", "mateIn2", "fork", ""};
    return t[int(k)];
}

std::string encodeMistakes(const GameMistakes& g) {
    std::ostringstream o;
    o << g.moves[0] << ' ' << g.moves[1] << ' ' << g.moves[2] << '\n';
    for (const Mistake& m : g.mistakes)
        o << m.ply << ' ' << m.moveNumber << ' ' << m.phase << ' ' << int(m.kind) << ' ' << m.piece << ' '
          << int(m.grade) << ' ' << int(m.winLoss) << '\n';
    return o.str();
}

GameMistakes decodeMistakes(const std::string& s) {
    GameMistakes g;
    std::istringstream in(s);
    in >> g.moves[0] >> g.moves[1] >> g.moves[2];
    Mistake m;
    int kind, grade, loss;
    while (in >> m.ply >> m.moveNumber >> m.phase >> kind >> m.piece >> grade >> loss) {
        if (m.phase < 0 || m.phase > 2 || kind < 0 || kind >= ErrorKindCount || m.piece < 0 || m.piece > 6) continue;
        m.kind = ErrorKind(kind);
        m.grade = Grade(grade);
        m.winLoss = loss;
        g.mistakes.push_back(m);
    }
    return g;
}

void addToReport(WeaknessReport& r, const GameMistakes& g) {
    ++r.games;
    for (int p = 0; p < 3; ++p) r.moves[p] += g.moves[p];
    for (const Mistake& m : g.mistakes) {
        ++r.errors;
        ++r.phase[m.phase];
        ++r.kind[int(m.kind)];
        ++r.piece[m.piece];
        ++r.bucket[std::clamp((m.moveNumber - 1) / 10, 0, MoveBuckets - 1)];
    }
}

std::string weaknessPhrase(const WeaknessReport& r) {
    if (r.errors == 0) return "";
    static const char* verbs[ErrorKindCount] = {"теряешь фигуры", "упускаешь мат", "пропускаешь мат",
                                                "упускаешь тактику", "ошибаешься в спокойных позициях"};
    static const char* pieces[7] = {"", "пешки", "коня", "слона", "ладьи", "ферзя", "короля"};
    static const char* phases[3] = {"в дебюте", "в миттельшпиле", "в эндшпиле"};
    const int b = argmax(r.bucket);
    const std::string when = b == MoveBuckets - 1 ? "после 40-го хода" : std::string("на ") + bucketName(b) + " ходу";
    const int ph = argmax(r.phase);
    return std::string("Чаще всего ") + verbs[argmax(r.kind)] + " " + when + ", обычно ходом " +
           pieces[argmax(r.piece, 1)] + ". Больше всего ошибок " + phases[ph] + " — " + std::to_string(r.phase[ph]) +
           " из " + std::to_string(r.errors) + ".";
}

std::string weakestTheme(const WeaknessReport& r) {
    if (r.errors < 3) return "";
    const ErrorKind k = ErrorKind(argmax(r.kind));
    if (k == ErrorKind::Other) return argmax(r.phase) == 2 ? "endgame" : "";
    return kindTheme(k);
}

const char* errorKindName(ErrorKind k) {
    static const char* n[ErrorKindCount] = {"Зевок фигуры", "Упущенный мат", "Пропущенный мат",
                                            "Упущенная тактика", "Позиционная ошибка"};
    return n[int(k)];
}

const char* phaseName(int phase) {
    static const char* n[3] = {"Дебют", "Миттельшпиль", "Эндшпиль"};
    return n[phase];
}

const char* bucketName(int bucket) {
    static const char* n[MoveBuckets] = {"1–10", "11–20", "21–30", "31–40", "41+"};
    return n[bucket];
}
}
