#pragma once
#include "analysis/review.h"
#include "core/types.h"
#include "uci/uci_engine.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace analysis {
class Analyzer {
public:
    ~Analyzer();

    void start(const std::filesystem::path& engineExe, const std::string& startFen,
               const std::vector<chess::Move>& moves, int movetimeMs, int threads = 2);
    void cancel();

    bool running() const { return running_; }
    int done() const { return done_; }
    int total() const { return total_; }
    int version() const { return version_; }
    const std::string& fen() const { return fen_; }
    const std::vector<chess::Move>& moves() const { return moves_; }
    std::vector<Eval> snapshot() const;
    std::string error() const;

private:
    void run(std::filesystem::path exe, int movetimeMs, int threads);

    mutable std::mutex mutex_;
    std::vector<Eval> evals_;
    std::string error_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::atomic<int> done_{0};
    std::atomic<int> total_{0};
    std::atomic<int> version_{0};
    std::string fen_;
    std::vector<chess::Move> moves_;
    uci::Engine engine_;
    std::filesystem::path engineExe_;
};

}
