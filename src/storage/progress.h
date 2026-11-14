#pragma once
#include <string>
#include <utility>
#include <vector>

namespace storage {
struct SrsItem;

struct PlanItem {
    std::string kind, title, arg;
    int minutes = 0, target = 1, done = 0;
    bool needsPrev = false;
    bool complete() const { return done >= target; }
};

struct PlanInput {
    std::string theme, themeName;
    std::string lesson, lessonTitle;
    std::string endgame, endgameTitle;
    std::string weakTheme, weakThemeName;
};

std::vector<PlanItem> buildPlan(const PlanInput& in);
std::string encodePlan(const std::vector<PlanItem>& plan);
std::vector<PlanItem> decodePlan(const std::string& s);
bool planComplete(const std::vector<PlanItem>& plan);
int planMinutes(const std::vector<PlanItem>& plan);
bool advancePlan(std::vector<PlanItem>& plan, const std::string& kind, const std::string& arg);

struct Streak {
    int current = 0, best = 0, lastDay = -1;
};
Streak advanceStreak(Streak s, int day);
int liveStreak(const Streak& s, int day);

struct Achievement {
    const char* id;
    const char* title;
    const char* desc;
};
struct AchievementStats {
    int wins = 0, rating = 0, puzzlesSolved = 0, puzzleRating = 0, stormBest = 0;
    bool calibrated = false;
    int lessonsDone = 0, lessonsTotal = 0, endgamesDone = 0, endgamesTotal = 0;
    int bestStreak = 0, plansDone = 0;
};
const std::vector<Achievement>& achievements();
std::vector<std::string> earnedAchievements(const AchievementStats& s);

std::vector<std::pair<std::string, int>> weakThemes(const std::vector<SrsItem>& items, const std::vector<std::string>& allowed);
int fenPhase(const std::string& fen);
}
