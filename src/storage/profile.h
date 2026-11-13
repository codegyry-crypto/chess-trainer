#pragma once
#include "rating/elo.h"
#include "storage/progress.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;

namespace storage {
struct GameRecord {
    int64_t id = 0;
    std::string date;
    std::string opponent;
    std::string botId;
    int opponentRating = 0;
    std::string color;
    double score = 0;
    std::string reason;
    std::string timeControl;
    bool rated = false;
    int ratingBefore = 0;
    int ratingAfter = 0;
    std::string pgn;
};

struct RatingChange {
    bool rated = false;
    bool calibrating = false;
    bool calibrationDone = false;
    int calibrationPlayed = 0;
    int before = 0, after = 0, delta = 0;
    int levelBefore = 0, levelAfter = 0;
};

struct SrsItem {
    std::string key, fen, moves, themes, source;
    int rating = 0;
    int step = 0;
    int due = 0;
};

struct Stats {
    int games = 0, wins = 0, draws = 0, losses = 0;
    int streak = 0;
    int bestStreak = 0;
};

class Profile {
public:
    Profile() = default;
    ~Profile();
    Profile(const Profile&) = delete;
    Profile& operator=(const Profile&) = delete;

    bool open(const std::filesystem::path& file, std::string* error = nullptr);
    bool isOpen() const { return db_ != nullptr; }

    int rating() const { return rating_; }
    int peak() const { return peak_; }
    int ratedGames() const { return ratedGames_; }
    bool calibrated() const;
    int calibrationLeft() const;
    int calibrationPlayed() const { return rating::CalibrationGames - calibrationLeft(); }
    std::vector<rating::CalibrationGame> calibrationGames() const;

    RatingChange recordGame(GameRecord g);

    int puzzleRating() const { return puzzleRating_; }
    int puzzlesPlayed() const { return puzzlesPlayed_; }
    int recordPuzzle(int puzzleRating, bool solved);
    int stormBest() const { return stormBest_; }
    void setStormBest(int score);

    void markPuzzleDone(const std::string& id);
    std::vector<std::string> donePuzzles() const;
    std::string value(const std::string& key) const;
    void setValue(const std::string& key, const std::string& value);

    static int today();
    void srsAdd(SrsItem item, int day);
    std::vector<SrsItem> srsItems(int day, bool dueOnly, const std::string& source) const;
    void srsResult(const std::string& key, bool solved, int day);

    std::vector<GameRecord> recentGames(int limit) const;
    std::vector<std::pair<std::string, int>> ratingHistory() const;
    Stats stats() const;

    Streak streak() const;
    void markActivity(int day);
    std::vector<PlanItem> plan(int day) const;
    void setPlan(int day, const std::vector<PlanItem>& plan);
    int counter(const std::string& key) const;
    void bump(const std::string& key);
    bool unlock(const std::string& achievement, int day);

private:
    void loadState();

    sqlite3* db_ = nullptr;
    int rating_ = 0;
    int peak_ = 0;
    int ratedGames_ = 0;
    int puzzleRating_ = 0, puzzlesPlayed_ = 0, stormBest_ = 0;
};

}
