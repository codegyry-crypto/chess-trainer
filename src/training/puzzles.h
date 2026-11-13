#pragma once
#include "core/position.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace training {

struct Puzzle {
    std::string id, fen;
    std::vector<std::string> moves;
    int rating = 0;
    std::vector<std::string> themes;
};

std::vector<Puzzle> loadPuzzles(const std::filesystem::path& csv);
Puzzle parsePuzzle(const std::string& id, const std::string& fen, const std::string& moves, int rating,
                   const std::string& themes);
std::string joinMoves(const std::vector<std::string>& moves);

struct ImportProgress {
    std::atomic<int> stage{0};
    std::atomic<int64_t> done{0}, total{0};
    std::atomic<bool> cancel{false};
};
bool downloadFile(const std::wstring& host, const std::wstring& path, const std::filesystem::path& to, ImportProgress& p);
bool importPuzzles(const std::filesystem::path& zst, const std::filesystem::path& csv,
                   const std::unordered_set<std::string>& have, size_t limit, std::vector<Puzzle>& out,
                   ImportProgress& p);

std::string themeName(const std::string& id);
const std::vector<std::string>& pickerThemes();

class PuzzleRun {
public:
    enum class Result { Continue, Solved, Wrong };

    bool start(const Puzzle& p, bool setup = true);
    void applySetup();
    Result play(chess::Move m);
    void applyReply();

    chess::Position& position() { return pos_; }
    const Puzzle& puzzle() const { return puzzle_; }
    chess::Move expected();
    chess::Move setupMove();
    chess::Color playerColor() const { return player_; }
    bool setupDone() const { return step_ > 0; }

private:
    Puzzle puzzle_;
    chess::Position pos_;
    chess::Color player_ = chess::White;
    size_t step_ = 0;
};

}
