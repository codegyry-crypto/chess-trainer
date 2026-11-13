#include "ui/app.h"

#include "ui/sound.h"
#include "ui/ui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>

namespace ui {

using chess::Move;
using training::PuzzleRun;

namespace {
constexpr float M = 24;
constexpr float PanelL = 430;
}

size_t App::lessonCount() const {
    size_t n = 0;
    for (const auto& s : lessons_) n += s.lessons.size();
    return n;
}

void App::openLesson(int sec, int idx) {
    lessonSec_ = sec;
    lessonIdx_ = idx;
    screen_ = Screen::Lessons;
    setLessonStep(0);
}

void App::setLessonStep(int step) {
    lessonStep_ = step;
    board_.reset();
    puzzleHint_ = 0;
    const training::LessonStep& s = lesson().steps[step];
    training::Puzzle p;
    p.id = lesson().id;
    p.fen = s.fen;
    p.moves = s.solution;
    run_.start(p, false);
    puzzleState_ = s.solution.empty() ? PuzzleState::Solved : PuzzleState::Play;
}

void App::lessonMove(Move m) {
    const PuzzleRun::Result r = run_.play(m);
    if (r == PuzzleRun::Result::Wrong) {
        sound::play(sound::Id::Illegal);
        board_.reset();
        puzzleState_ = PuzzleState::Wrong;
        return;
    }
    puzzleHint_ = 0;
    sound::play(m.isCapture() ? sound::Id::Capture : sound::Id::Move);
    if (r == PuzzleRun::Result::Solved) {
        sound::play(sound::Id::GameStart);
        puzzleState_ = PuzzleState::Solved;
        return;
    }
    puzzleState_ = PuzzleState::Reply;
    puzzleTimer_ = GetTime() + 0.5;
}

void App::drawLessonList() {
    const Theme& t = theme();
    const float W = screenW(), H = screenH();
    const float left = 56, top = 40;

    text("Уроки", {left, top}, 34, t.text, true);
    const size_t total = lessonCount();
    text("Пройдено " + std::to_string(lessonsDone_.size()) + " из " + std::to_string(total), {left, top + 48}, 16, t.sub);
    roundRect({left, top + 76, 320, 8}, 4, t.panel2);
    if (total) roundRect({left, top + 76, 320.0f * float(lessonsDone_.size()) / float(total), 8}, 4, t.good);

    int nextSec = -1, nextIdx = -1;
    for (int s = 0; s < int(lessons_.size()) && nextSec < 0; ++s)
        for (int i = 0; i < int(lessons_[s].lessons.size()); ++i)
            if (!lessonsDone_.count(lessons_[s].lessons[i].id)) {
                nextSec = s, nextIdx = i;
                break;
            }
    const float bw = 380;
    if (nextSec >= 0 && button({W - left - bw, top + 6, bw, 46}, "Продолжить: " + lessons_[nextSec].lessons[nextIdx].title,
                               Style::Primary, true, 16))
        openLesson(nextSec, nextIdx);
    if (button({W - left - bw - 12 - 130, top + 6, 130, 46}, "‹ В меню", Style::Ghost) ||
        (inputEnabled() && IsKeyPressed(KEY_ESCAPE)))
        screen_ = Screen::Menu;

    const Rectangle area{left - 8, top + 100, W - 2 * left + 16, H - top - 100};
    const bool inArea = CheckCollisionPointRec(mouse(), area);
    constexpr int Cols = 3;
    constexpr float Gap = 16, RowH = 34, HeadH = 48;
    const float colW = (area.width - 16 - (Cols - 1) * Gap) / Cols;
    float colY[Cols] = {0, 0, 0};
    std::vector<Rectangle> cards;
    for (const auto& sec : lessons_) {
        const int c = int(std::min_element(colY, colY + Cols) - colY);
        cards.push_back({area.x + 8 + c * (colW + Gap), colY[c], colW, HeadH + RowH * sec.lessons.size() + 10});
        colY[c] += cards.back().height + Gap;
    }
    const float contentH = *std::max_element(colY, colY + Cols);
    if (inArea) lessonScroll_ -= GetMouseWheelMove() * 60;
    lessonScroll_ = std::clamp(lessonScroll_, 0.0f, std::max(0.0f, contentH - area.height));

    beginClip(area);
    for (size_t s = 0; s < lessons_.size(); ++s) {
        const auto& sec = lessons_[s];
        Rectangle r = cards[s];
        r.y += area.y - lessonScroll_;
        card(r);
        int done = 0;
        for (const auto& l : sec.lessons) done += int(lessonsDone_.count(l.id));
        text(std::to_string(s + 1) + ". " + sec.title, {r.x + 18, r.y + 15}, 19, t.text, true);
        const std::string cnt = std::to_string(done) + "/" + std::to_string(sec.lessons.size());
        text(cnt, {r.x + r.width - 18 - measure(cnt, 15).x, r.y + 18}, 15, done == int(sec.lessons.size()) ? t.good : t.sub);
        for (size_t i = 0; i < sec.lessons.size(); ++i) {
            const auto& l = sec.lessons[i];
            const Rectangle rr{r.x + 8, r.y + HeadH + i * RowH, r.width - 16, RowH - 2};
            const bool hot = inArea && hovered(rr);
            if (hot) {
                roundRect(rr, 8, mix(t.panel, t.text, 0.07f));
                setHandCursor();
            }
            const Vector2 c{rr.x + 18, rr.y + rr.height / 2};
            if (lessonsDone_.count(l.id)) {
                DrawCircleV(c, 9, t.good);
                DrawLineEx({c.x - 4.5f, c.y}, {c.x - 1, c.y + 3.5f}, 2.2f, t.bg);
                DrawLineEx({c.x - 1, c.y + 3.5f}, {c.x + 5, c.y - 3.5f}, 2.2f, t.bg);
            } else {
                DrawCircleLinesV(c, 8, t.sub);
            }
            text(l.title, {rr.x + 38, rr.y + 7}, 16, t.text);
            if (hot && clicked(rr)) openLesson(int(s), int(i));
        }
    }
    endClip();
}

void App::drawLessons() {
    if (lessonIdx_ < 0) {
        drawLessonList();
        return;
    }
    const Theme& t = theme();
    const float W = screenW(), H = screenH();
    const training::Lesson& l = lesson();
    const training::LessonStep& st = l.steps[lessonStep_];
    const bool task = !st.solution.empty();
    const bool last = lessonStep_ + 1 == int(l.steps.size());

    if (puzzleState_ == PuzzleState::Reply && GetTime() >= puzzleTimer_) {
        const chess::Position before = run_.position();
        const Move m = run_.expected();
        run_.applyReply();
        if (settings_.animate) board_.animate(before, m);
        sound::play(m.isCapture() ? sound::Id::Capture : sound::Id::Move);
        puzzleState_ = PuzzleState::Play;
    }
    const bool solved = puzzleState_ == PuzzleState::Solved;
    if (solved && last && lessonsDone_.insert(l.id).second && shotFile_.empty()) profile_.setValue("lesson:" + l.id, "1");

    const float availW = W - 3 * M - PanelL, availH = H - 2 * M - 56;
    const float boardSize = std::floor(std::min(availW, availH) / 8) * 8;
    const Rectangle boardRect{M + std::floor((availW - boardSize) / 2), M + 56, boardSize, boardSize};
    BoardOptions opt;
    opt.flipped = task && run_.playerColor() == chess::Black;
    opt.coords = settings_.showCoords;
    opt.legalMoves = settings_.showLegalMoves;
    opt.animate = settings_.animate;

    const bool active = task && (puzzleState_ == PuzzleState::Play || puzzleState_ == PuzzleState::Wrong);
    std::vector<BoardView::Arrow> arrows;
    if (!task || solved)
        for (const auto& a : st.arrows)
            arrows.push_back({chess::parseSquare(a.substr(0, 2)), chess::parseSquare(a.substr(2, 2)), ::Color{60, 170, 90, 210}});
    if (active && puzzleHint_ > 0) {
        const Move e = run_.expected();
        arrows.push_back({e.from, puzzleHint_ >= 2 ? e.to : e.from, ::Color{40, 120, 230, 210}});
    }
    board_.setHintArrows(std::move(arrows));
    const Move m = board_.update(boardRect, run_.position(), active, pieces_.get(settings_.pieceSet), opt);
    if (!m.isNone()) lessonMove(m);
    if (task)
        text(run_.playerColor() == chess::White ? "Ход белых — найди лучший ход" : "Ход чёрных — найди лучший ход",
             {boardRect.x, M + 14}, 22, t.text, true);

    const float px = W - M - PanelL;
    float py = M;
    text(std::to_string(lessonSec_ + 1) + ". " + lessons_[lessonSec_].title, {px, py}, 15, t.sub);
    text(l.title, {px, py + 22}, 26, t.text, true);
    const int n = int(l.steps.size());
    for (int i = 0; i < n; ++i)
        DrawCircleV({px + 7 + i * 22.0f, py + 76}, 6,
                    i < lessonStep_ || (i == lessonStep_ && solved) ? t.good : i == lessonStep_ ? t.accent : t.panel2);
    text("Шаг " + std::to_string(lessonStep_ + 1) + " из " + std::to_string(n), {px + n * 22.0f + 8, py + 67}, 14, t.sub);
    py += 96;

    const float buttonsH = 108.0f + (last && !l.practiceFen.empty() ? 58 : 0);
    const Rectangle ic{px, py, PanelL, std::max(200.0f, H - M - buttonsH - py)};
    card(ic);
    float y = ic.y + 18;
    y += textWrapped(st.text, {ic.x + 18, y, ic.width - 36, 0}, 18, t.text) + 12;
    if (task) {
        std::string status = "Сделай ход на доске";
        ::Color sc = t.sub;
        if (puzzleState_ == PuzzleState::Reply) status = "Верно! Продолжай", sc = t.good;
        if (puzzleState_ == PuzzleState::Wrong) status = "Не тот ход. Подумай ещё", sc = t.danger;
        if (solved) status = "Верно!", sc = t.good;
        y += textWrapped(status, {ic.x + 18, y, ic.width - 36, 0}, 19, sc, true) + 6;
        if (solved) textWrapped(st.after, {ic.x + 18, y, ic.width - 36, 0}, 16, t.sub);
    }
    if (last && solved) {
        const std::string tail = l.practiceFen.empty() ? "Урок пройден!" : "Урок пройден! Закрепи с ботом: " + l.practiceGoal;
        textWrapped(tail, {ic.x + 18, ic.y + ic.height - 60, ic.width - 36, 0}, 16, t.good, true);
    }

    py = ic.y + ic.height + 12;
    int nextSec = lessonSec_, nextIdx = lessonIdx_ + 1;
    if (nextIdx >= int(lessons_[nextSec].lessons.size())) ++nextSec, nextIdx = 0;
    const bool hasNext = nextSec < int(lessons_.size());
    const float bw = (PanelL - 16) / 3;
    if (task && button({px, py, bw, 46}, puzzleHint_ ? "Ход" : "Подсказка", Style::Secondary, active && puzzleHint_ < 2, 15))
        ++puzzleHint_;
    if (button({px + bw + 8, py, bw, 46}, "‹ Назад", Style::Secondary, lessonStep_ > 0, 15)) setLessonStep(lessonStep_ - 1);
    const bool canNext = solved && (!last || hasNext);
    if (button({px + 2 * (bw + 8), py, bw, 46}, last ? "Следующий урок" : "Дальше ›", Style::Primary, canNext, 15) ||
        (canNext && inputEnabled() && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_RIGHT)))) {
        if (!last) setLessonStep(lessonStep_ + 1);
        else openLesson(nextSec, nextIdx);
        return;
    }
    py += 58;
    if (last && !l.practiceFen.empty()) {
        if (button({px, py, PanelL, 46}, "Практика с ботом", solved ? Style::Primary : Style::Secondary,
                   solved, 15)) {
            startGame(timeControls()[0], l.practiceFen, engine::botById(l.practiceBot),
                      l.practiceBlack ? chess::Black : chess::White);
            return;
        }
        py += 58;
    }
    if (button({px, py, PanelL, 42}, "‹ К урокам", Style::Ghost) || (inputEnabled() && IsKeyPressed(KEY_ESCAPE)))
        lessonIdx_ = -1;
}

}
