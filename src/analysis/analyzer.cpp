#include "analysis/analyzer.h"

#include "core/notation.h"
#include "core/position.h"

#include <algorithm>

namespace analysis {
using namespace chess;

Analyzer::~Analyzer() {
    cancel();
    engine_.stop();
}

void Analyzer::cancel() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    stop_ = false;
    running_ = false;
}

void Analyzer::start(const std::filesystem::path& engineExe, const std::string& startFen,
                     const std::vector<Move>& moves, int movetimeMs, int threads) {
    cancel();
    size_t keep = 0;
    if (startFen == fen_)
        while (keep < evals_.size() && keep <= moves.size() && (keep == 0 || moves_[keep - 1] == moves[keep - 1]))
            ++keep;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        evals_.resize(keep);
        evals_.resize(moves.size() + 1);
        error_.clear();
    }
    fen_ = startFen;
    moves_ = moves;
    done_ = int(std::count_if(evals_.begin(), evals_.end(), [](const Eval& e) { return e.known; }));
    total_ = int(moves.size() + 1);
    ++version_;
    running_ = true;
    thread_ = std::thread(&Analyzer::run, this, engineExe, movetimeMs, threads);
}

std::vector<Eval> Analyzer::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return evals_;
}

std::string Analyzer::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

void Analyzer::run(std::filesystem::path exe, int movetimeMs, int threads) {
    if (!engine_.running() || engineExe_ != exe) {
        std::string err;
        if (!engine_.start(exe, &err)) {
            std::lock_guard<std::mutex> lock(mutex_);
            error_ = "Stockfish не запустился: " + err;
            running_ = false;
            return;
        }
        engineExe_ = exe;
        engine_.setOption("Hash", "64");
    }
    engine_.setOption("Threads", std::to_string(threads));
    engine_.newGame();

    Position pos;
    if (!fen_.empty()) pos.setFEN(fen_);
    std::vector<std::string> uciMoves;
    for (size_t i = 0; i <= moves_.size() && !stop_; ++i) {
        if (i > 0) {
            pos.makeMove(moves_[i - 1]);
            uciMoves.push_back(toUCI(moves_[i - 1]));
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (evals_[i].known) continue;
        }
        Eval e = terminalEval(pos);
        if (!e.known) {
            const uci::Analysis a =
                engine_.analyse(fen_.empty() ? "startpos" : fen_, uciMoves, 0, movetimeMs, 2, &stop_);
            if (stop_) break;
            e = evalFromAnalysis(a, pos.sideToMove());
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            evals_[i] = e;
        }
        ++done_;
        ++version_;
    }
    running_ = false;
}

}
