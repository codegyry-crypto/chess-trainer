#include "ui/app.h"

#include "ui/fx.h"
#include "ui/ui.h"

#include <raylib.h>

#include <algorithm>

namespace ui {
namespace {
constexpr float M = 24;

void checkMark(Vector2 c, float r, ::Color col) {
    DrawLineEx({c.x - r * 0.45f, c.y}, {c.x - r * 0.1f, c.y + r * 0.35f}, 3, col);
    DrawLineEx({c.x - r * 0.1f, c.y + r * 0.35f}, {c.x + r * 0.5f, c.y - r * 0.35f}, 3, col);
}
}

std::vector<storage::PlanItem>& App::todayPlan() {
    const int day = storage::Profile::today();
    if (planDay_ == day) return plan_;
    planDay_ = day;
    plan_ = profile_.plan(day);
    if (!plan_.empty()) return plan_;
    storage::PlanInput in;
    const auto& themes = training::pickerThemes();
    const auto weak = storage::weakThemes(profile_.srsItems(day, false, ""), themes);
    in.theme = weak.empty() ? themes[size_t(day) % themes.size()] : weak[0].first;
    in.themeName = training::themeName(in.theme);
    in.weakTheme = analysis::weakestTheme(weaknessReport());
    in.weakThemeName = training::themeName(in.weakTheme);
    for (const auto& sec : lessons_)
        for (const auto& l : sec.lessons)
            if (in.lesson.empty() && !lessonsDone_.count(l.id)) in.lesson = l.id, in.lessonTitle = l.title;
    const auto& egs = training::endgames();
    const training::Endgame* eg = nullptr;
    for (const auto& e : egs)
        if (!eg && !egDone_.count(e.id)) eg = &e;
    if (!eg && !egs.empty()) eg = &egs[size_t(day) % egs.size()];
    if (eg) in.endgame = eg->id, in.endgameTitle = eg->title;
    plan_ = storage::buildPlan(in);
    if (profile_.isOpen()) profile_.setPlan(day, plan_);
    return plan_;
}

void App::progress(const std::string& kind, const std::string& arg) {
    if (!profile_.isOpen()) return;
    const int day = storage::Profile::today();
    profile_.markActivity(day);
    auto& plan = todayPlan();
    const bool wasDone = storage::planComplete(plan);
    const auto before = plan;
    if (storage::advancePlan(plan, kind, arg)) {
        profile_.setPlan(day, plan);
        if (!wasDone && storage::planComplete(plan)) {
            profile_.bump("plans_done");
            toast("План дня выполнен! Серия: " + std::to_string(profile_.streak().current) + " дн.");
        } else {
            for (size_t i = 0; i < plan.size(); ++i)
                if (plan[i].complete() && !before[i].complete()) toast("План дня: «" + plan[i].title + "» — готово");
        }
    }
    checkAchievements();
}

void App::checkAchievements() {
    storage::AchievementStats s;
    s.wins = profile_.stats().wins;
    s.rating = profile_.rating();
    s.calibrated = profile_.calibrated();
    s.puzzlesSolved = profile_.counter("puzzles_solved");
    s.puzzleRating = profile_.puzzleRating();
    s.stormBest = profile_.stormBest();
    s.lessonsDone = int(lessonsDone_.size());
    s.lessonsTotal = int(lessonCount());
    s.endgamesDone = int(egDone_.size());
    s.endgamesTotal = int(training::endgames().size());
    s.bestStreak = profile_.streak().best;
    s.plansDone = profile_.counter("plans_done");
    const int day = storage::Profile::today();
    for (const auto& id : storage::earnedAchievements(s))
        if (profile_.unlock(id, day))
            for (const auto& a : storage::achievements())
                if (id == a.id) {
                    toast(std::string("Новое достижение: «") + a.title + "»");
                    fx::celebrate(fx::Kind::Achievement, {screenW() / 2, 150}, a.title);
                }
}

void App::openLastReview(Screen returnTo) {
    const auto last = profile_.recentGames(1);
    std::string err;
    const auto games = last.empty() ? std::vector<chess::Game>{} : chess::parsePGN(last[0].pgn, &err);
    if (!games.empty() && !games[0].moves.empty()) {
        openReview(games[0], last[0].color == "w" ? 0 : 1, returnTo);
        return;
    }
    review_ = Review{};
    reviewReturn_ = returnTo;
    screen_ = Screen::Analysis;
}

void App::startPlanItem(const storage::PlanItem& item) {
    if (item.kind == "puzzles") {
        puzzleTheme_ = item.arg;
        openPuzzles(PuzzleMode::Theme);
    }
    if (item.kind == "lesson")
        for (size_t s = 0; s < lessons_.size(); ++s)
            for (size_t i = 0; i < lessons_[s].lessons.size(); ++i)
                if (lessons_[s].lessons[i].id == item.arg) openLesson(int(s), int(i));
    if (item.kind == "endgame")
        for (size_t i = 0; i < training::endgames().size(); ++i)
            if (training::endgames()[i].id == item.arg) openEndgame(int(i));
    if (item.kind == "opening") reviewOpenings();
    if (item.kind == "rated") openRated();
    if (item.kind == "review") openLastReview(Screen::Plan);
}

void App::drawPlan() {
    const Theme& t = theme();
    const float W = (float)screenW(), H = (float)screenH();
    if (button({M + 8, M + 4, 120, 42}, "‹ Назад", Style::Ghost) || (inputEnabled() && IsKeyPressed(KEY_ESCAPE)))
        screen_ = Screen::Menu;
    const auto plan = todayPlan();
    const float w = std::min(900.0f, W - 2 * M - 32), left = std::floor((W - w) / 2);
    float y = M + 60;
    text("План дня", {left, y}, 30, t.text, true);
    text("Около " + std::to_string(storage::planMinutes(plan)) +
             " минут. План собран на сегодня, пункты отмечаются сами, когда ты их проходишь.",
         {left, y + 44}, 16, t.sub);
    const int streak = storage::liveStreak(profile_.streak(), storage::Profile::today());
    streakFlame({left + w, y + 4}, streak, 24);

    y += 90;
    const float rowH = std::min(88.0f, (H - y - M - 90) / std::max<float>(1, float(plan.size())) - 10);
    bool firstOpen = true;
    for (size_t i = 0; i < plan.size(); ++i) {
        const storage::PlanItem& it = plan[i];
        const Rectangle r{left, y, w, rowH};
        card(r);
        const Vector2 c{r.x + 36, r.y + rowH / 2};
        if (it.complete()) {
            DrawCircleV(c, 16, t.good);
            checkMark(c, 16, t.bg);
        } else {
            DrawCircleLinesV(c, 16, t.border);
        }
        text(it.title, {r.x + 68, r.y + rowH / 2 - 24}, 20, it.complete() ? t.sub : t.text, true);
        const std::string sub = it.target > 1 ? std::to_string(std::min(it.done, it.target)) + " из " +
                                                    std::to_string(it.target) + " · около " + std::to_string(it.minutes) + " мин"
                                              : "Около " + std::to_string(it.minutes) + " мин";
        text(sub, {r.x + 68, r.y + rowH / 2 + 4}, 15, t.sub);
        const Rectangle br{r.x + w - 170, r.y + (rowH - 42) / 2, 150, 42};
        const bool locked = it.needsPrev && i > 0 && !plan[i - 1].complete();
        if (it.complete()) {
            textCentered("Готово", br, 17, t.good, true);
        } else if (button(br, locked ? "После партии" : "Начать", firstOpen ? Style::Primary : Style::Secondary, !locked)) {
            startPlanItem(it);
        }
        if (!it.complete()) firstOpen = false;
        y += rowH + 10;
    }
    if (storage::planComplete(plan)) {
        const Rectangle r{left, y + 6, w, 64};
        roundRect(r, 12, fade(t.good, 0.15f));
        textCentered("План на сегодня выполнен! Завтра будет новый.", r, 19, t.good, true);
    }
}

void App::drawProfileAchievements(float top) {
    const Theme& t = theme();
    const float W = (float)screenW(), x0 = M + 16, inner = W - 2 * x0;
    const auto& all = storage::achievements();
    int got = 0;
    for (const auto& a : all) got += !profile_.value(std::string("ach:") + a.id).empty();
    text("Открыто " + std::to_string(got) + " из " + std::to_string(all.size()), {x0, top}, 17, t.sub);
    const int cols = std::max(1, int(inner / 300));
    const float gap = 12, cw = (inner - (cols - 1) * gap) / cols, ch = 84;
    for (size_t i = 0; i < all.size(); ++i) {
        const bool on = !profile_.value(std::string("ach:") + all[i].id).empty();
        const Rectangle r{x0 + (i % cols) * (cw + gap), top + 36 + (i / cols) * (ch + gap), cw, ch};
        card(r);
        if (on) roundRectLines(r, 12, 2, t.accent);
        const Vector2 c{r.x + 40, r.y + ch / 2};
        DrawCircleV(c, 22, on ? t.accent : t.panel2);
        if (on) checkMark(c, 22, t.accentText);
        text(all[i].title, {r.x + 76, r.y + 18}, 18, on ? t.text : t.sub, true);
        text(all[i].desc, {r.x + 76, r.y + 46}, 14, t.sub);
    }
}
}
