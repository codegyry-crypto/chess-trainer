#include "ui/app.h"

#include "core/notation.h"
#include "ui/sound.h"
#include "ui/ui.h"

#include <raylib.h>

#include <algorithm>

namespace ui {

using chess::Move;
using training::PuzzleRun;

namespace {
constexpr float M = 24;
constexpr float PanelP = 430;
constexpr double StormSeconds = 180;

bool rated(App::PuzzleMode m) { return m == App::PuzzleMode::Rated || m == App::PuzzleMode::Theme; }
}

void App::openPuzzles(PuzzleMode mode) {
    if (puzzles_.empty() && puzzlesLoading_.valid()) puzzles_ = puzzlesLoading_.get();
    pmode_ = mode;
    screen_ = Screen::Puzzles;
    stormScore_ = stormErrors_ = 0;
    stormEnd_ = mode == PuzzleMode::Storm ? GetTime() + StormSeconds : 0;
    nextPuzzle();
}

bool App::pickPuzzle(training::Puzzle& out) {
    const int today = storage::Profile::today();
    if (pmode_ == PuzzleMode::Mine || pmode_ == PuzzleMode::Repeat) {
        auto items = profile_.srsItems(today, pmode_ == PuzzleMode::Repeat, pmode_ == PuzzleMode::Mine ? "mine" : "");
        if (items.empty()) return false;
        const storage::SrsItem& it = items[rng_() % std::min<size_t>(items.size(), 5)];
        out = training::parsePuzzle(it.key, it.fen, it.moves, it.rating, it.themes);
        return true;
    }
    if (puzzles_.empty()) return false;
    if (pmode_ == PuzzleMode::Daily) {
        out = puzzles_[size_t(today) * 7919 % puzzles_.size()];
        return true;
    }
    const int target = pmode_ == PuzzleMode::Storm ? 600 + stormScore_ * 60 : profile_.puzzleRating();
    std::vector<const training::Puzzle*> pool;
    for (int width = 100; pool.size() < 20 && width <= 3000; width *= 2) {
        pool.clear();
        auto it = std::lower_bound(puzzles_.begin(), puzzles_.end(), target - width,
                                   [](const training::Puzzle& p, int r) { return p.rating < r; });
        for (; it != puzzles_.end() && it->rating <= target + width; ++it) {
            const training::Puzzle& p = *it;
            if (puzzleDone_.count(p.id)) continue;
            if (pmode_ == PuzzleMode::Theme &&
                std::find(p.themes.begin(), p.themes.end(), puzzleTheme_) == p.themes.end())
                continue;
            pool.push_back(&p);
        }
    }
    if (pool.empty()) return false;
    out = *pool[rng_() % pool.size()];
    return true;
}

void App::nextPuzzle() {
    board_.reset();
    puzzleHint_ = 0;
    puzzleFailed_ = false;
    puzzleDelta_ = 0;
    training::Puzzle p;
    if (!pickPuzzle(p) || !run_.start(p)) {
        puzzleState_ = PuzzleState::Empty;
        return;
    }
    if (pmode_ != PuzzleMode::Mine && pmode_ != PuzzleMode::Repeat && puzzleDone_.insert(p.id).second)
        profile_.markPuzzleDone(p.id);
    puzzleState_ = PuzzleState::Setup;
    puzzleTimer_ = GetTime() + 0.6;
}

void App::finishPuzzle(bool solved) {
    if (!puzzleFailed_ && rated(pmode_)) puzzleDelta_ = profile_.recordPuzzle(run_.puzzle().rating, solved);
    const int today = storage::Profile::today();
    if (solved && !puzzleFailed_) {
        profile_.bump("puzzles_solved");
        progress("puzzles", pmode_ == PuzzleMode::Theme ? puzzleTheme_ : "");
    }
    if (pmode_ == PuzzleMode::Mine || pmode_ == PuzzleMode::Repeat) profile_.srsResult(run_.puzzle().id, solved && !puzzleFailed_, today);
    else if (!solved && pmode_ != PuzzleMode::Storm)
        profile_.srsAdd({run_.puzzle().id, run_.puzzle().fen, training::joinMoves(run_.puzzle().moves),
                         training::joinMoves(run_.puzzle().themes), "lichess",
                         run_.puzzle().rating},
                        today);
}

void App::startPuzzleImport() {
    if (importFuture_.valid() || puzzles_.empty()) return;
    std::unordered_set<std::string> have;
    for (const auto& p : puzzles_) have.insert(p.id);
    importProgress_.stage = 1;
    importProgress_.done = importProgress_.total = 0;
    importFuture_ = std::async(std::launch::async, [this, have = std::move(have)] {
        namespace fs = std::filesystem;
        std::vector<training::Puzzle> out;
        const fs::path zst = appDataDir() / "lichess_db_puzzle.csv.zst";
        std::error_code ec;
        if (!fs::exists(zst) &&
            !training::downloadFile(L"database.lichess.org", L"/lichess_db_puzzle.csv.zst", zst, importProgress_)) {
            importProgress_.stage = -1;
            return out;
        }
        importProgress_.stage = 2;
        const bool ok = training::importPuzzles(zst, morePuzzlesFile_, have, 150000, out, importProgress_);
        if (importProgress_.cancel) return out;
        if (!ok || out.empty()) fs::remove(zst, ec);
        if (!ok) importProgress_.stage = -1;
        return out;
    });
}

void App::pollPuzzleImport() {
    if (!importFuture_.valid() || importFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto added = importFuture_.get();
    if (added.empty())
        toast(importProgress_.stage < 0 ? "Не получилось скачать задачи. Проверь интернет и попробуй ещё раз"
                                        : "Новых задач пока нет — Lichess пополняет базу каждый месяц");
    else
        toast("Добавлено " + std::to_string(added.size()) + " новых задач с Lichess");
    puzzles_.insert(puzzles_.end(), std::make_move_iterator(added.begin()), std::make_move_iterator(added.end()));
    std::sort(puzzles_.begin(), puzzles_.end(), [](const auto& a, const auto& b) { return a.rating < b.rating; });
    if (screen_ == Screen::Puzzles && puzzleState_ == PuzzleState::Empty && pmode_ != PuzzleMode::Storm) nextPuzzle();
}

std::string App::importLabel() const {
    if (!importFuture_.valid()) return "Загрузить ещё задачи";
    const int64_t done = importProgress_.done, total = importProgress_.total;
    if (importProgress_.stage == 1)
        return "Скачиваю базу Lichess: " + std::to_string(done >> 20) + " из " + (total ? std::to_string(total >> 20) : "?") + " МБ";
    return "Добавляю задачи… " + std::to_string(total ? done * 100 / total : 0) + "%";
}

void App::puzzleMove(Move m) {
    const PuzzleRun::Result r = run_.play(m);
    if (r == PuzzleRun::Result::Wrong) {
        sound::play(sound::Id::Illegal);
        board_.reset();
        if (pmode_ == PuzzleMode::Storm) {
            ++stormErrors_;
            nextPuzzle();
            return;
        }
        if (!puzzleFailed_) finishPuzzle(false);
        puzzleFailed_ = true;
        puzzleState_ = PuzzleState::Wrong;
        return;
    }
    sound::play(m.isCapture() ? sound::Id::Capture : sound::Id::Move);
    if (r == PuzzleRun::Result::Solved) {
        sound::play(sound::Id::GameStart);
        if (pmode_ == PuzzleMode::Storm) {
            ++stormScore_;
            nextPuzzle();
            return;
        }
        finishPuzzle(true);
        puzzleState_ = PuzzleState::Solved;
        return;
    }
    puzzleState_ = PuzzleState::Reply;
    puzzleTimer_ = GetTime() + 0.4;
}

bool App::puzzleBoard(Rectangle boardRect) {
    if ((puzzleState_ == PuzzleState::Setup || puzzleState_ == PuzzleState::Reply) && GetTime() >= puzzleTimer_) {
        const chess::Position before = run_.position();
        const Move m = puzzleState_ == PuzzleState::Setup ? run_.setupMove() : run_.expected();
        if (puzzleState_ == PuzzleState::Setup) run_.applySetup();
        else run_.applyReply();
        if (settings_.animate) board_.animate(before, m);
        sound::play(m.isCapture() ? sound::Id::Capture : sound::Id::Move);
        puzzleState_ = PuzzleState::Play;
    }

    BoardOptions opt;
    opt.flipped = run_.playerColor() == chess::Black;
    opt.coords = settings_.showCoords;
    opt.legalMoves = settings_.showLegalMoves;
    opt.animate = settings_.animate;

    std::vector<BoardView::Arrow> arrows;
    const bool active = puzzleState_ == PuzzleState::Play || puzzleState_ == PuzzleState::Wrong;
    if (active && puzzleHint_ > 0) {
        const Move e = run_.expected();
        arrows.push_back({e.from, puzzleHint_ >= 2 ? e.to : e.from, ::Color{40, 120, 230, 210}});
    }
    board_.setHintArrows(std::move(arrows));
    if (puzzleState_ != PuzzleState::Empty) {
        const Move m = board_.update(boardRect, run_.position(), active && inputEnabled(), pieces_.get(settings_.pieceSet), opt);
        if (!m.isNone()) puzzleMove(m);
    }
    return active;
}

void App::showPuzzleSolution() {
    if (!puzzleFailed_) finishPuzzle(false);
    puzzleFailed_ = true;
    puzzleHint_ = 0;
    board_.reset();
    const chess::Position before = run_.position();
    const Move e = run_.expected();
    if (run_.play(e) == PuzzleRun::Result::Solved) puzzleState_ = PuzzleState::Solved;
    else puzzleState_ = PuzzleState::Reply, puzzleTimer_ = GetTime() + 0.6;
    board_.animate(before, e);
}

void App::drawPuzzles() {
    const Theme& t = theme();
    const float W = screenW(), H = screenH();

    if (pmode_ == PuzzleMode::Storm && (stormEnd_ > 0 && GetTime() >= stormEnd_ || stormErrors_ >= 3)) {
        if (puzzleState_ != PuzzleState::Empty) {
            profile_.setStormBest(stormScore_);
            sound::play(sound::Id::Checkmate);
        }
        puzzleState_ = PuzzleState::Empty;
        stormEnd_ = 0;
    }
    const float availW = W - 3 * M - PanelP, availH = H - 2 * M - 56;
    const float boardSize = std::floor(std::min(availW, availH) / 8) * 8;
    const Rectangle boardRect{M + std::floor((availW - boardSize) / 2), M + 56, boardSize, boardSize};
    const bool active = puzzleBoard(boardRect);
    const bool white = run_.playerColor() == chess::White;
    text(puzzleState_ == PuzzleState::Empty ? "" : (white ? "Ход белых — найди лучший ход" : "Ход чёрных — найди лучший ход"),
         {boardRect.x, M + 14}, 22, t.text, true);

    const float px = W - M - PanelP;
    float py = M;
    struct ModeItem {
        PuzzleMode mode;
        std::string name;
    };
    const size_t due = profile_.srsItems(storage::Profile::today(), true, "").size();
    const ModeItem modes[] = {{PuzzleMode::Rated, "Рейтинговые"},
                              {PuzzleMode::Theme, "По темам"},
                              {PuzzleMode::Daily, "Задача дня"},
                              {PuzzleMode::Mine, "Мои ошибки"},
                              {PuzzleMode::Repeat, due ? "Повтор (" + std::to_string(due) + ")" : "Повтор"},
                              {PuzzleMode::Storm, "Штурм 3 мин"}};
    const float cw = (PanelP - 16) / 3;
    for (int i = 0; i < 6; ++i)
        if (chip({px + (i % 3) * (cw + 8), py + (i / 3) * 52.0f, cw, 44}, modes[i].name, pmode_ == modes[i].mode, true, 15))
            openPuzzles(modes[i].mode);
    py += 2 * 52 + 8;

    if (pmode_ == PuzzleMode::Theme) {
        const auto& themes = training::pickerThemes();
        const float tw = (PanelP - 16) / 3;
        for (size_t i = 0; i < themes.size(); ++i) {
            const Rectangle r{px + (i % 3) * (tw + 8), py + (i / 3) * 40.0f, tw, 34};
            if (chip(r, training::themeName(themes[i]), puzzleTheme_ == themes[i], true, 13)) {
                puzzleTheme_ = themes[i];
                nextPuzzle();
            }
        }
        py += float((themes.size() + 2) / 3) * 40 + 8;
    }

    const Rectangle ic{px, py, PanelP, 250};
    card(ic);
    float y = ic.y + 16;
    if (pmode_ == PuzzleMode::Storm) {
        const double left = std::max(0.0, stormEnd_ - GetTime());
        const std::string clock = std::to_string(int(left) / 60) + ":" + (int(left) % 60 < 10 ? "0" : "") +
                                  std::to_string(int(left) % 60);
        text(stormEnd_ > 0 ? clock : "Время вышло", {ic.x + 18, y}, 34, left < 20 && stormEnd_ > 0 ? t.danger : t.text, true);
        text("Решено: " + std::to_string(stormScore_), {ic.x + 18, y + 50}, 20, t.good, true);
        text("Ошибок: " + std::to_string(stormErrors_) + " из 3", {ic.x + 18, y + 80}, 16, t.sub);
        text("Рекорд: " + std::to_string(profile_.stormBest()), {ic.x + 18, y + 106}, 16, t.sub);
        if (puzzleState_ == PuzzleState::Empty &&
            button({ic.x + 18, ic.y + ic.height - 60, PanelP - 36, 44}, "Начать заново", Style::Primary))
            openPuzzles(PuzzleMode::Storm);
        if (button({px, ic.y + ic.height + 12, PanelP, 42}, "‹ В меню", Style::Ghost)) screen_ = Screen::Menu;
        return;
    }
    const std::string doneLine = "Пройдено " + std::to_string(puzzleDone_.size()) + " из " + std::to_string(puzzles_.size());
    if (puzzleState_ == PuzzleState::Empty) {
        const bool bank = pmode_ == PuzzleMode::Rated || pmode_ == PuzzleMode::Theme || pmode_ == PuzzleMode::Daily;
        const bool all = puzzleDone_.size() >= puzzles_.size();
        const std::string msg = pmode_ == PuzzleMode::Repeat ? "На сегодня повторять нечего. Возвращайся завтра!"
                                : pmode_ == PuzzleMode::Mine
                                    ? "Пока нет задач из твоих ошибок. Разбери партию — ошибки попадут сюда."
                                : all ? "Ты прошёл все задачи! Загрузи новые с Lichess, чтобы продолжить."
                                      : "Задачи этой темы закончились. Выбери другую тему или загрузи новые с Lichess.";
        if (bank && all) text("Все задачи пройдены!", {ic.x + 18, y}, 22, t.good, true);
        textWrapped(msg, {ic.x + 18, y + (bank && all ? 40 : 0), ic.width - 36, 100}, 17, t.sub);
        if (bank) {
            text(doneLine, {ic.x + 18, ic.y + ic.height - 96}, 14, t.sub);
            if (button({ic.x + 18, ic.y + ic.height - 66, PanelP - 36, 48}, importLabel(), Style::Primary,
                       !importFuture_.valid(), 15))
                startPuzzleImport();
        }
        if (button({px, ic.y + ic.height + 12, PanelP, 42}, "‹ В меню", Style::Ghost) ||
            (inputEnabled() && IsKeyPressed(KEY_ESCAPE)))
            screen_ = Screen::Menu;
        return;
    }
    if (rated(pmode_)) text(doneLine, {ic.x + 18, ic.y + ic.height - 30}, 14, t.sub);

    std::string rating = "Твой рейтинг задач: " + std::to_string(profile_.puzzleRating());
    if (puzzleDelta_) rating += puzzleDelta_ > 0 ? "  +" + std::to_string(puzzleDelta_) : "  " + std::to_string(puzzleDelta_);
    text(rating, {ic.x + 18, y}, 17, t.text, true);
    y += 28;
    text(rated(pmode_) ? "Рейтинг задачи: " + std::to_string(run_.puzzle().rating) : "Без рейтинга: тренировка",
         {ic.x + 18, y}, 15, t.sub);
    y += 34;
    std::string status = "Найди лучший ход";
    ::Color sc = t.text;
    if (puzzleState_ == PuzzleState::Solved) status = "Решено!", sc = t.good;
    if (puzzleState_ == PuzzleState::Wrong) status = "Не тот ход. Попробуй ещё или посмотри решение", sc = t.danger;
    if (puzzleState_ == PuzzleState::Reply) status = "Верно! Продолжай", sc = t.good;
    textWrapped(status, {ic.x + 18, y, ic.width - 36, 50}, 19, sc, true);
    y += 58;
    if (puzzleState_ == PuzzleState::Solved || puzzleFailed_) {
        std::string themes;
        for (const auto& th : run_.puzzle().themes)
            if (!training::themeName(th).empty()) themes += (themes.empty() ? "" : " · ") + training::themeName(th);
        textWrapped(themes, {ic.x + 18, y, ic.width - 36, 40}, 14, t.sub);
    }

    py += ic.height + 12;
    const float bw = (PanelP - 16) / 3;
    if (button({px, py, bw, 46}, puzzleHint_ ? "Ход" : "Подсказка", Style::Secondary, active && puzzleHint_ < 2, 15)) {
        if (!puzzleFailed_ && rated(pmode_)) finishPuzzle(false);
        puzzleFailed_ = true;
        ++puzzleHint_;
    }
    if (button({px + bw + 8, py, bw, 46}, "Решение", Style::Secondary, active, 15)) showPuzzleSolution();
    if (button({px + 2 * (bw + 8), py, bw, 46}, "Дальше", Style::Primary, true, 15)) nextPuzzle();
    py += 58;
    if (button({px, py, PanelP, 42}, importLabel(), Style::Ghost, !importFuture_.valid(), 15)) startPuzzleImport();
    py += 50;
    if (button({px, py, PanelP, 42}, "‹ В меню", Style::Ghost) || (inputEnabled() && IsKeyPressed(KEY_ESCAPE)))
        screen_ = Screen::Menu;
}

}
