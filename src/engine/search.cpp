#include "engine/search.h"

#include <algorithm>
#include <chrono>

namespace engine {
using namespace chess;

namespace {
int64_t nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int toTT(int score, int ply) {
    if (score > MateBound) return score + ply;
    if (score < -MateBound) return score - ply;
    return score;
}
int fromTT(int score, int ply) {
    if (score > MateBound) return score - ply;
    if (score < -MateBound) return score + ply;
    return score;
}

enum : uint8_t { Exact = 1, Lower = 2, Upper = 3 };

}

Searcher::Searcher(int ttSizeMb) {
    size_t entries = 1;
    while (entries * 2 * sizeof(TTEntry) <= size_t(ttSizeMb) * 1024 * 1024) entries *= 2;
    tt_.resize(entries);
    ttMask_ = entries - 1;
    for (auto& b : moveBuf_) b.reserve(64);
    for (auto& b : scoreBuf_) b.reserve(64);
}

void Searcher::clear() {
    std::fill(tt_.begin(), tt_.end(), TTEntry{});
    for (auto& k : killers_) k[0] = k[1] = Move::none();
    for (auto& h : history_)
        for (int& v : h) v = 0;
}

void Searcher::begin(const Limits& limits, const EvalParams& params, std::atomic<bool>* stop) {
    limits_ = limits;
    params_ = params;
    externalStop_ = stop;
    stopped_ = false;
    nodes_ = 0;
    startUs_ = nowUs();
    for (auto& k : killers_) k[0] = k[1] = Move::none();
    for (auto& h : history_)
        for (int& v : h) v /= 8;
}

void Searcher::checkLimits() {
    if (externalStop_ && externalStop_->load(std::memory_order_relaxed)) stopped_ = true;
    if (limits_.timeMs > 0 && nowUs() - startUs_ >= int64_t(limits_.timeMs) * 1000) stopped_ = true;
    if (limits_.maxNodes && nodes_ >= limits_.maxNodes) stopped_ = true;
}

void Searcher::orderMoves(Position& pos, std::vector<Move>& moves, Move ttMove, int ply) {
    std::vector<int>& sc = scoreBuf_[ply];
    sc.resize(moves.size());
    for (size_t i = 0; i < moves.size(); ++i) {
        const Move m = moves[i];
        int s;
        if (!ttMove.isNone() && m == ttMove) s = 10'000'000;
        else if (m.isCapture()) {
            const PieceType victim = m.isEnPassant() ? Pawn : typeOf(pos.at(m.to));
            s = 1'000'000 + pieceValue(victim) * 10 - pieceValue(typeOf(pos.at(m.from)));
        } else if (m.isPromotion()) s = 900'000 + m.promo;
        else if (m == killers_[ply][0]) s = 800'000;
        else if (m == killers_[ply][1]) s = 700'000;
        else s = history_[m.from][m.to];
        sc[i] = s;
    }
    for (size_t i = 1; i < moves.size(); ++i) {
        const Move m = moves[i];
        const int s = sc[i];
        size_t j = i;
        while (j > 0 && sc[j - 1] < s) {
            moves[j] = moves[j - 1];
            sc[j] = sc[j - 1];
            --j;
        }
        moves[j] = m;
        sc[j] = s;
    }
}

int Searcher::quiesce(Position& pos, int alpha, int beta, int ply, int qdepth) {
    if ((++nodes_ & 1023) == 0) checkLimits();
    if (stopped_) return 0;
    if (ply >= MaxPly - 1) return evaluate(pos, params_);

    const bool inCheck = pos.inCheck();
    int best = -Inf;
    int standPat = 0;
    if (!inCheck) {
        standPat = evaluate(pos, params_);
        if (standPat >= beta) return standPat;
        if (standPat > alpha) alpha = standPat;
        best = standPat;
        if (qdepth >= 12) return standPat;
    }

    std::vector<Move>& moves = moveBuf_[ply];
    pos.generateLegal(moves, !inCheck);
    if (moves.empty()) return inCheck ? -Mate + ply : best;
    orderMoves(pos, moves, Move::none(), ply);

    for (size_t i = 0; i < moves.size(); ++i) {
        const Move m = moves[i];
        if (!inCheck && !m.isPromotion()) {
            const PieceType victim = m.isEnPassant() ? Pawn : typeOf(pos.at(m.to));
            if (standPat + pieceValue(victim) + 200 < alpha) continue;
        }
        pos.makeMove(m);
        const int score = -quiesce(pos, -beta, -alpha, ply + 1, qdepth + 1);
        pos.unmakeMove();
        if (stopped_) return 0;
        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) break;
            }
        }
    }
    return best;
}

int Searcher::negamax(Position& pos, int depth, int alpha, int beta, int ply, bool pvNode) {
    if ((++nodes_ & 1023) == 0) checkLimits();
    if (stopped_) return 0;

    if (ply > 0) {
        if (pos.isRepetition() || pos.halfmoveClock() >= 100 || pos.isInsufficientMaterial()) return 0;
        alpha = std::max(alpha, -Mate + ply);
        beta = std::min(beta, Mate - ply - 1);
        if (alpha >= beta) return alpha;
    }
    if (ply >= MaxPly - 1) return evaluate(pos, params_);

    const bool inCheck = pos.inCheck();
    if (inCheck) ++depth;
    if (depth <= 0) return limits_.quiescence ? quiesce(pos, alpha, beta, ply, 0) : evaluate(pos, params_);

    TTEntry* e = probe(pos.hash());
    Move ttMove = Move::none();
    if (e->key == pos.hash()) {
        ttMove = e->move;
        if (!pvNode && e->depth >= depth) {
            const int s = fromTT(e->score, ply);
            if (e->flag == Exact || (e->flag == Lower && s >= beta) || (e->flag == Upper && s <= alpha)) return s;
        }
    }

    if (!pvNode && !inCheck && depth <= 3 && std::abs(beta) < MateBound) {
        const int staticEval = evaluate(pos, params_);
        if (staticEval - 110 * depth >= beta) return staticEval;
    }

    std::vector<Move>& moves = moveBuf_[ply];
    pos.generateLegal(moves);
    if (moves.empty()) return inCheck ? -Mate + ply : 0;
    orderMoves(pos, moves, ttMove, ply);

    const int origAlpha = alpha;
    int best = -Inf;
    Move bestMove = moves[0];
    for (size_t i = 0; i < moves.size(); ++i) {
        const Move m = moves[i];
        const bool quiet = !m.isCapture() && !m.isPromotion();
        pos.makeMove(m);
        int score;
        if (i == 0) {
            score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, pvNode);
        } else {
            int r = 0;
            if (depth >= 3 && i >= 3 && quiet && !inCheck && !pos.inCheck()) r = i >= 8 ? 2 : 1;
            score = -negamax(pos, depth - 1 - r, -alpha - 1, -alpha, ply + 1, false);
            if (score > alpha && r) score = -negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1, false);
            if (score > alpha && score < beta) score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, true);
        }
        pos.unmakeMove();
        if (stopped_) return 0;

        if (score > best) {
            best = score;
            bestMove = m;
            if (ply == 0) rootBest_ = m;
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) {
                    if (quiet) {
                        if (!(killers_[ply][0] == m)) killers_[ply][1] = killers_[ply][0], killers_[ply][0] = m;
                        history_[m.from][m.to] += depth * depth;
                        if (history_[m.from][m.to] > 600'000)
                            for (auto& h : history_)
                                for (int& v : h) v /= 2;
                    }
                    break;
                }
            }
        }
    }

    e->key = pos.hash();
    e->move = bestMove;
    e->score = int16_t(toTT(best, ply));
    e->depth = int8_t(std::min(depth, 120));
    e->flag = best >= beta ? Lower : (best > origAlpha ? Exact : Upper);
    return best;
}

std::vector<Move> Searcher::extractPv(Position pos, int maxLen) {
    std::vector<Move> pv;
    std::vector<Move> legal;
    for (int i = 0; i < maxLen; ++i) {
        const TTEntry* e = probe(pos.hash());
        if (e->key != pos.hash() || e->move.isNone()) break;
        pos.generateLegal(legal);
        if (std::find(legal.begin(), legal.end(), e->move) == legal.end()) break;
        Move m = *std::find(legal.begin(), legal.end(), e->move);
        pv.push_back(m);
        pos.makeMove(m);
        if (pos.isRepetition()) break;
    }
    return pv;
}

Result Searcher::search(Position pos, const Limits& limits, const EvalParams& params, std::atomic<bool>* stop) {
    begin(limits, params, stop);
    Result res;
    std::vector<Move> rootMoves;
    pos.generateLegal(rootMoves);
    if (rootMoves.empty()) return res;
    res.best = rootMoves[0];
    if (rootMoves.size() == 1) {
        res.depth = 1;
        res.score = evaluate(pos, params);
        res.pv = {res.best};
        return res;
    }

    rootBest_ = rootMoves[0];
    for (int depth = 1; depth <= limits.maxDepth; ++depth) {
        const int score = negamax(pos, depth, -Inf, Inf, 0, true);
        if (stopped_ && depth > 1) break;
        res.best = rootBest_;
        res.score = score;
        res.depth = depth;
        if (stopped_) break;
        if (std::abs(score) > MateBound && Mate - std::abs(score) <= depth) break;
        if (limits.timeMs > 0 && (nowUs() - startUs_) > int64_t(limits.timeMs) * 1000 * 45 / 100) break;
    }
    for (const Move& m : rootMoves)
        if (m == res.best) res.best = m;
    res.pv = extractPv(pos, std::max(1, res.depth));
    if (res.pv.empty() || !(res.pv[0] == res.best)) res.pv = {res.best};
    res.nodes = nodes_;
    res.seconds = (nowUs() - startUs_) / 1e6;
    return res;
}

std::vector<RootMove> Searcher::scoreRootMoves(Position pos, int depth, const Limits& limits, const EvalParams& params,
                                               std::atomic<bool>* stop) {
    begin(limits, params, stop);
    std::vector<Move> rootMoves;
    pos.generateLegal(rootMoves);
    std::vector<RootMove> out;
    for (const Move& m : rootMoves) {
        pos.makeMove(m);
        const int s = depth <= 1 ? (limits.quiescence ? -quiesce(pos, -Inf, Inf, 1, 0) : -evaluate(pos, params))
                                 : -negamax(pos, depth - 1, -Inf, Inf, 1, true);
        pos.unmakeMove();
        if (stopped_) break;
        out.push_back({m, s});
    }
    std::stable_sort(out.begin(), out.end(), [](const RootMove& a, const RootMove& b) { return a.score > b.score; });
    return out;
}

}
