#include "storage/progress.h"

#include "storage/profile.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <sstream>

namespace storage {

std::vector<PlanItem> buildPlan(const PlanInput& in) {
    std::vector<PlanItem> p;
    p.push_back({"puzzles", "10 задач: " + in.themeName, in.theme, 15, 10});
    if (!in.lesson.empty()) p.push_back({"lesson", "Урок: " + in.lessonTitle, in.lesson, 10});
    if (!in.endgame.empty()) p.push_back({"endgame", "Эндшпиль: " + in.endgameTitle, in.endgame, 10});
    p.push_back({"rated", "Рейтинговая партия", "", 15});
    p.push_back({"review", "Разбор этой партии", "", 5, 1, 0, true});
    return p;
}

std::string encodePlan(const std::vector<PlanItem>& plan) {
    std::string s;
    for (const PlanItem& i : plan)
        s += i.kind + '\t' + i.title + '\t' + i.arg + '\t' + std::to_string(i.minutes) + '\t' +
             std::to_string(i.target) + '\t' + std::to_string(i.done) + '\t' + (i.needsPrev ? "1" : "0") + '\n';
    return s;
}

std::vector<PlanItem> decodePlan(const std::string& s) {
    std::vector<PlanItem> out;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        std::istringstream ls(line);
        std::string x;
        while (std::getline(ls, x, '\t')) f.push_back(x);
        if (f.size() < 7) continue;
        out.push_back({f[0], f[1], f[2], std::atoi(f[3].c_str()), std::atoi(f[4].c_str()), std::atoi(f[5].c_str()),
                       f[6] == "1"});
    }
    return out;
}

bool planComplete(const std::vector<PlanItem>& plan) {
    return !plan.empty() && std::all_of(plan.begin(), plan.end(), [](const PlanItem& i) { return i.complete(); });
}

int planMinutes(const std::vector<PlanItem>& plan) {
    int m = 0;
    for (const PlanItem& i : plan) m += i.minutes;
    return m;
}

bool advancePlan(std::vector<PlanItem>& plan, const std::string& kind, const std::string& arg) {
    for (size_t k = 0; k < plan.size(); ++k) {
        PlanItem& i = plan[k];
        if (i.kind != kind || i.complete() || (!i.arg.empty() && i.arg != arg)) continue;
        if (i.needsPrev && k > 0 && !plan[k - 1].complete()) return false;
        ++i.done;
        return true;
    }
    return false;
}

Streak advanceStreak(Streak s, int day) {
    if (s.lastDay == day) return s;
    s.current = s.lastDay == day - 1 ? s.current + 1 : 1;
    s.best = std::max(s.best, s.current);
    s.lastDay = day;
    return s;
}

int liveStreak(const Streak& s, int day) { return s.lastDay >= day - 1 ? s.current : 0; }

const std::vector<Achievement>& achievements() {
    static const std::vector<Achievement> a = {
        {"first_win", "Первая победа", "Выиграй партию у бота"},
        {"calibrated", "Калибровка", "Сыграй 5 калибровочных партий"},
        {"level5", "Уровень 5", "Набери 1200 Elo"},
        {"level8", "Уровень 8", "Набери 1700 Elo"},
        {"puzzles10", "10 задач", "Реши 10 задач"},
        {"puzzles100", "100 задач", "Реши 100 задач"},
        {"puzzles500", "500 задач", "Реши 500 задач"},
        {"puzzle1500", "Тактик", "Рейтинг задач 1500"},
        {"storm20", "Штурм 20", "Реши 20 задач в «Штурме»"},
        {"lesson1", "Первый урок", "Пройди урок курса"},
        {"course", "Курс пройден", "Пройди все уроки"},
        {"endgames", "Эндшпили", "Пройди все позиции эндшпиля"},
        {"streak3", "3 дня подряд", "Тренируйся 3 дня подряд"},
        {"streak7", "Неделя", "Тренируйся 7 дней подряд"},
        {"streak30", "Месяц", "Тренируйся 30 дней подряд"},
        {"plan1", "План дня", "Выполни план дня"},
    };
    return a;
}

std::vector<std::string> earnedAchievements(const AchievementStats& s) {
    const bool got[] = {s.wins >= 1,
                        s.calibrated,
                        s.calibrated && s.rating >= 1200,
                        s.calibrated && s.rating >= 1700,
                        s.puzzlesSolved >= 10,
                        s.puzzlesSolved >= 100,
                        s.puzzlesSolved >= 500,
                        s.puzzleRating >= 1500,
                        s.stormBest >= 20,
                        s.lessonsDone >= 1,
                        s.lessonsTotal > 0 && s.lessonsDone >= s.lessonsTotal,
                        s.endgamesTotal > 0 && s.endgamesDone >= s.endgamesTotal,
                        s.bestStreak >= 3,
                        s.bestStreak >= 7,
                        s.bestStreak >= 30,
                        s.plansDone >= 1};
    std::vector<std::string> out;
    for (size_t i = 0; i < achievements().size(); ++i)
        if (got[i]) out.push_back(achievements()[i].id);
    return out;
}

std::vector<std::pair<std::string, int>> weakThemes(const std::vector<SrsItem>& items, const std::vector<std::string>& allowed) {
    std::map<std::string, int> count;
    for (const SrsItem& it : items) {
        if (it.source != "lichess") continue;
        std::istringstream in(it.themes);
        std::string th;
        while (in >> th)
            if (std::find(allowed.begin(), allowed.end(), th) != allowed.end()) ++count[th];
    }
    std::vector<std::pair<std::string, int>> out(count.begin(), count.end());
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}

int fenPhase(const std::string& fen) {
    const std::string board = fen.substr(0, fen.find(' '));
    const auto pieces = std::count_if(board.begin(), board.end(), [](char c) {
        return std::string("nbrqNBRQ").find(c) != std::string::npos;
    });
    if (pieces <= 6) return 2;
    const auto sp = fen.find_last_of(' ');
    return sp != std::string::npos && std::atoi(fen.c_str() + sp + 1) <= 10 ? 0 : 1;
}
}
