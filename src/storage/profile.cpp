#include "storage/profile.h"

#include "rating/elo.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>

namespace storage {
namespace {
struct Stmt {
    sqlite3_stmt* s = nullptr;
    Stmt(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &s, nullptr); }
    ~Stmt() { sqlite3_finalize(s); }
    Stmt& bind(int i, const std::string& v) {
        sqlite3_bind_text(s, i, v.c_str(), -1, SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& bind(int i, int v) {
        sqlite3_bind_int(s, i, v);
        return *this;
    }
    Stmt& bind(int i, double v) {
        sqlite3_bind_double(s, i, v);
        return *this;
    }
    bool step() { return sqlite3_step(s) == SQLITE_ROW; }
    bool run() { return sqlite3_step(s) == SQLITE_DONE; }
    std::string text(int col) const {
        const unsigned char* t = sqlite3_column_text(s, col);
        return t ? reinterpret_cast<const char*>(t) : "";
    }
    int integer(int col) const { return sqlite3_column_int(s, col); }
    double real(int col) const { return sqlite3_column_double(s, col); }
};

const char* Schema = R"SQL(
CREATE TABLE IF NOT EXISTS profile (key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS puzzle_done (id TEXT PRIMARY KEY);
CREATE TABLE IF NOT EXISTS srs (key TEXT PRIMARY KEY, fen TEXT, moves TEXT, rating INTEGER, themes TEXT,
    source TEXT, step INTEGER, due INTEGER);
CREATE TABLE IF NOT EXISTS games (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    date TEXT, opponent TEXT, bot_id TEXT, opponent_rating INTEGER,
    color TEXT, score REAL, reason TEXT, time_control TEXT,
    rated INTEGER, rating_before INTEGER, rating_after INTEGER, pgn TEXT);
)SQL";

std::string nowString() {
    char buf[32];
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

}

Profile::~Profile() {
    if (db_) sqlite3_close(db_);
}

bool Profile::open(const std::filesystem::path& file, std::string* error) {
    if (db_) sqlite3_close(db_), db_ = nullptr;
    const std::u8string u8 = file.u8string();
    if (sqlite3_open(reinterpret_cast<const char*>(u8.c_str()), &db_) != SQLITE_OK) {
        if (error) *error = db_ ? sqlite3_errmsg(db_) : "не удалось открыть базу";
        if (db_) sqlite3_close(db_), db_ = nullptr;
        return false;
    }
    char* err = nullptr;
    if (sqlite3_exec(db_, Schema, nullptr, nullptr, &err) != SQLITE_OK) {
        if (error) *error = err ? err : "ошибка схемы";
        sqlite3_free(err);
        return false;
    }
    loadState();
    return true;
}

void Profile::setValue(const std::string& key, const std::string& value) {
    Stmt(db_, "INSERT OR REPLACE INTO profile(key, value) VALUES(?, ?)").bind(1, key).bind(2, value).run();
}

void Profile::loadState() {
    rating_ = rating::StartRating;
    peak_ = 0;
    ratedGames_ = 0;
    puzzleRating_ = 1000;
    puzzlesPlayed_ = stormBest_ = 0;
    Stmt q(db_, "SELECT key, value FROM profile");
    while (q.step()) {
        const std::string k = q.text(0), v = q.text(1);
        if (k == "rating") rating_ = std::atoi(v.c_str());
        if (k == "peak") peak_ = std::atoi(v.c_str());
        if (k == "puzzle_rating") puzzleRating_ = std::atoi(v.c_str());
        if (k == "puzzles_played") puzzlesPlayed_ = std::atoi(v.c_str());
        if (k == "storm_best") stormBest_ = std::atoi(v.c_str());
    }
    Stmt c(db_, "SELECT COUNT(*) FROM games WHERE rated = 1");
    if (c.step()) ratedGames_ = c.integer(0);
}

int Profile::recordPuzzle(int puzzleRating, bool solved) {
    const int delta = rating::ratingDelta(puzzleRating_, puzzleRating, solved ? 1.0 : 0.0, puzzlesPlayed_);
    puzzleRating_ = std::max(100, puzzleRating_ + delta);
    ++puzzlesPlayed_;
    setValue("puzzle_rating", std::to_string(puzzleRating_));
    setValue("puzzles_played", std::to_string(puzzlesPlayed_));
    return delta;
}

void Profile::markPuzzleDone(const std::string& id) {
    Stmt(db_, "INSERT OR IGNORE INTO puzzle_done(id) VALUES(?)").bind(1, id).run();
}

std::vector<std::string> Profile::donePuzzles() const {
    std::vector<std::string> out;
    if (!db_) return out;
    Stmt q(db_, "SELECT id FROM puzzle_done");
    while (q.step()) out.push_back(q.text(0));
    return out;
}

std::string Profile::value(const std::string& key) const {
    if (!db_) return "";
    Stmt q(db_, "SELECT value FROM profile WHERE key = ?");
    q.bind(1, key);
    return q.step() ? q.text(0) : "";
}

void Profile::setStormBest(int score) {
    stormBest_ = std::max(stormBest_, score);
    setValue("storm_best", std::to_string(stormBest_));
}

int Profile::today() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    using namespace std::chrono;
    const year_month_day ymd{year{tm.tm_year + 1900}, month{unsigned(tm.tm_mon + 1)}, day{unsigned(tm.tm_mday)}};
    return int(sys_days(ymd).time_since_epoch().count());
}

void Profile::srsAdd(SrsItem item, int day) {
    Stmt ins(db_, "INSERT OR IGNORE INTO srs(key, fen, moves, rating, themes, source, step, due) VALUES(?,?,?,?,?,?,0,?)");
    ins.bind(1, item.key).bind(2, item.fen).bind(3, item.moves).bind(4, item.rating).bind(5, item.themes);
    ins.bind(6, item.source).bind(7, day + 1).run();
}

std::vector<SrsItem> Profile::srsItems(int day, bool dueOnly, const std::string& source) const {
    std::vector<SrsItem> out;
    if (!db_) return out;
    Stmt q(db_, "SELECT key, fen, moves, rating, themes, source, step, due FROM srs "
                "WHERE (? = 0 OR due <= ?) AND (? = '' OR source = ?) ORDER BY due");
    q.bind(1, dueOnly ? 1 : 0).bind(2, day).bind(3, source).bind(4, source);
    while (q.step())
        out.push_back({q.text(0), q.text(1), q.text(2), q.text(4), q.text(5), q.integer(3), q.integer(6), q.integer(7)});
    return out;
}

void Profile::srsResult(const std::string& key, bool solved, int day) {
    static const int intervals[] = {1, 3, 7, 14};
    Stmt q(db_, "SELECT step FROM srs WHERE key = ?");
    q.bind(1, key);
    if (!q.step()) return;
    const int step = solved ? q.integer(0) + 1 : 0;
    if (step >= 4) {
        Stmt(db_, "DELETE FROM srs WHERE key = ?").bind(1, key).run();
        return;
    }
    Stmt(db_, "UPDATE srs SET step = ?, due = ? WHERE key = ?").bind(1, step).bind(2, day + intervals[step]).bind(3, key).run();
}

Streak Profile::streak() const {
    const std::string day = value("streak_day");
    return {counter("streak"), counter("streak_best"), day.empty() ? -1 : std::atoi(day.c_str())};
}

void Profile::markActivity(int day) {
    const Streak s = advanceStreak(streak(), day);
    setValue("streak", std::to_string(s.current));
    setValue("streak_best", std::to_string(s.best));
    setValue("streak_day", std::to_string(s.lastDay));
}

std::vector<PlanItem> Profile::plan(int day) const {
    if (value("plan_day") != std::to_string(day)) return {};
    return decodePlan(value("plan"));
}

void Profile::setPlan(int day, const std::vector<PlanItem>& plan) {
    setValue("plan_day", std::to_string(day));
    setValue("plan", encodePlan(plan));
}

int Profile::counter(const std::string& key) const { return std::atoi(value(key).c_str()); }

void Profile::bump(const std::string& key) { setValue(key, std::to_string(counter(key) + 1)); }

bool Profile::unlock(const std::string& achievement, int day) {
    if (!value("ach:" + achievement).empty()) return false;
    setValue("ach:" + achievement, std::to_string(day));
    return true;
}

bool Profile::calibrated() const { return ratedGames_ >= rating::CalibrationGames; }
int Profile::calibrationLeft() const { return std::max(0, rating::CalibrationGames - ratedGames_); }

std::vector<rating::CalibrationGame> Profile::calibrationGames() const {
    std::vector<rating::CalibrationGame> cal;
    Stmt q(db_, "SELECT opponent_rating, score FROM games WHERE rated = 1 ORDER BY id LIMIT ?");
    q.bind(1, rating::CalibrationGames);
    while (q.step()) cal.push_back({q.integer(0), q.real(1)});
    return cal;
}

RatingChange Profile::recordGame(GameRecord g) {
    RatingChange ch;
    if (g.date.empty()) g.date = nowString();
    ch.before = rating_;
    ch.levelBefore = rating::levelFor(rating_).number;
    if (g.rated) {
        ch.rated = true;
        if (!calibrated()) {
            std::vector<rating::CalibrationGame> cal = calibrationGames();
            cal.push_back({g.opponentRating, g.score});
            rating_ = rating::performanceRating(cal);
            ch.calibrationPlayed = int(cal.size());
            ch.calibrationDone = ch.calibrationPlayed >= rating::CalibrationGames;
            ch.calibrating = !ch.calibrationDone;
        } else {
            rating_ = std::max(100, rating_ + rating::ratingDelta(rating_, g.opponentRating, g.score, ratedGames_));
        }
        ++ratedGames_;
        if (!ch.calibrating) peak_ = std::max(peak_, rating_);
        setValue("rating", std::to_string(rating_));
        setValue("peak", std::to_string(peak_));
    }
    ch.after = rating_;
    ch.delta = ch.after - ch.before;
    ch.levelAfter = rating::levelFor(rating_).number;
    g.ratingBefore = ch.before;
    g.ratingAfter = ch.after;

    Stmt ins(db_, "INSERT INTO games(date, opponent, bot_id, opponent_rating, color, score, reason, time_control, "
                  "rated, rating_before, rating_after, pgn) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, g.date).bind(2, g.opponent).bind(3, g.botId).bind(4, g.opponentRating).bind(5, g.color);
    ins.bind(6, g.score).bind(7, g.reason).bind(8, g.timeControl).bind(9, g.rated ? 1 : 0);
    ins.bind(10, g.ratingBefore).bind(11, g.ratingAfter).bind(12, g.pgn);
    ins.run();
    return ch;
}

std::vector<GameRecord> Profile::recentGames(int limit) const {
    std::vector<GameRecord> out;
    if (!db_) return out;
    Stmt q(db_, "SELECT id, date, opponent, bot_id, opponent_rating, color, score, reason, time_control, rated, "
                "rating_before, rating_after, pgn FROM games ORDER BY id DESC LIMIT ?");
    q.bind(1, limit);
    while (q.step()) {
        GameRecord g;
        g.id = q.integer(0);
        g.date = q.text(1);
        g.opponent = q.text(2);
        g.botId = q.text(3);
        g.opponentRating = q.integer(4);
        g.color = q.text(5);
        g.score = q.real(6);
        g.reason = q.text(7);
        g.timeControl = q.text(8);
        g.rated = q.integer(9) != 0;
        g.ratingBefore = q.integer(10);
        g.ratingAfter = q.integer(11);
        g.pgn = q.text(12);
        out.push_back(std::move(g));
    }
    return out;
}

std::vector<std::pair<std::string, int>> Profile::ratingHistory() const {
    std::vector<std::pair<std::string, int>> out;
    if (!db_) return out;
    Stmt q(db_, "SELECT date, rating_after FROM games WHERE rated = 1 ORDER BY id");
    while (q.step()) out.emplace_back(q.text(0), q.integer(1));
    return out;
}

Stats Profile::stats() const {
    Stats s;
    if (!db_) return s;
    Stmt q(db_, "SELECT score FROM games ORDER BY id");
    int run = 0;
    while (q.step()) {
        const double sc = q.real(0);
        ++s.games;
        if (sc > 0.75) {
            ++s.wins;
            run = run > 0 ? run + 1 : 1;
        } else if (sc < 0.25) {
            ++s.losses;
            run = run < 0 ? run - 1 : -1;
        } else {
            ++s.draws;
            run = 0;
        }
        s.bestStreak = std::max(s.bestStreak, run);
    }
    s.streak = run;
    return s;
}

}
