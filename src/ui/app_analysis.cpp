#include "ui/app.h"

#include "core/notation.h"
#include "core/pgn.h"
#include "ui/grades.h"
#include "ui/sound.h"
#include "ui/ui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>

namespace ui {
namespace fs = std::filesystem;
using analysis::Grade;
using chess::Move;
using chess::Position;

namespace {
constexpr float M = 24;
constexpr float PanelA = 430;
constexpr float BarH = 44;

bool isBad(Grade g) { return isBadGrade(g); }
bool isError(Grade g) { return isBadGrade(g) && g != Grade::Inaccuracy; }

}

Position App::reviewPosition(int ply) const {
    Position p;
    if (!review_.startFen.empty()) p.setFEN(review_.startFen);
    for (int i = 0; i < ply && i < int(review_.moves.size()); ++i) p.makeMove(review_.moves[i]);
    return p;
}

std::string App::reviewMoveLabel(int i) const {
    const int plyFromStart = i + (review_.firstMover == chess::Black ? 1 : 0);
    const int number = review_.firstMoveNumber + plyFromStart / 2;
    const bool white = plyFromStart % 2 == 0;
    const auto sans = sanList(review_.moves, review_.sanEn, review_.sanRu);
    return std::to_string(number) + (white ? ". " : "... ") + sans[i];
}

std::vector<std::string> App::sanList(const std::vector<chess::Move>& moves, const std::vector<std::string>& en,
                                      const std::vector<std::string>& ru) const {
    if (!settings_.squareNotation) return settings_.russianNotation ? ru : en;
    std::vector<std::string> out;
    for (const chess::Move& m : moves) out.push_back(chess::toSquares(m));
    return out;
}

std::string App::moveName(chess::Position& p, chess::Move m) const {
    if (settings_.squareNotation) return chess::toSquares(m);
    return chess::toSAN(p, m, settings_.russianNotation ? chess::RussianLetters : chess::EnglishLetters);
}

void App::openReview(const chess::Game& game, int myColor, Screen returnTo) {
    cancelRetry();
    analyzer_.cancel();
    review_ = Review{};
    review_.loaded = true;
    review_.startFen = game.startFen;
    review_.moves = game.moves;
    review_.white = game.tag("White").empty() ? "Белые" : game.tag("White");
    review_.black = game.tag("Black").empty() ? "Чёрные" : game.tag("Black");
    review_.result = game.result;
    review_.myColor = myColor;
    Position p = game.startPosition();
    review_.firstMover = p.sideToMove();
    review_.firstMoveNumber = p.fullmoveNumber();
    for (const Move& m : game.moves) {
        review_.sanEn.push_back(chess::toSAN(p, m, chess::EnglishLetters));
        review_.sanRu.push_back(chess::toSAN(p, m, chess::RussianLetters));
        p.makeMove(m);
    }
    reviewPly_ = 0;
    reviewShownPly_ = 0;
    reviewListPly_ = -1;
    reviewScroll_ = 0;
    reviewFlipped_ = myColor == 1;
    reviewReturn_ = returnTo;
    gradesVersion_ = -1;
    board_.reset();
    if (stockfishExe_.empty()) toast("Stockfish не найден — оценки ходов недоступны");
    screen_ = Screen::Analysis;
}

bool App::openReviewFromClipboard() {
    const char* clip = GetClipboardText();
    if (!clip || !*clip) {
        toast("Буфер обмена пуст — скопируй PGN партии (например, с Lichess)");
        return false;
    }
    std::string err;
    const auto games = chess::parsePGN(clip, &err);
    if (games.empty() || games[0].moves.empty()) {
        toast(err.empty() ? "В буфере нет партии в формате PGN" : "Не получилось прочитать PGN: " + err);
        return false;
    }
    openReview(games[0], -1, screen_ == Screen::Analysis ? reviewReturn_ : screen_);
    return true;
}

void App::saveMistakesAsPuzzles() {
    review_.mistakesSaved = true;
    progress("review");
    if (review_.myColor < 0) return;
    const int added = addMistakePuzzles(analysis::findMistakes(review_.startFen, review_.moves, evals_,
                                                               review_.myColor == 0 ? chess::White : chess::Black));
    if (added) toast("Твои ошибки (" + std::to_string(added) + ") добавлены в «Задачи → Мои ошибки»");
}

void App::cancelRetry() {
    if (retryFuture_.valid()) retryFuture_.wait();
    retryFuture_ = {};
    retry_ = false;
}

void App::startRetry(int moveIndex) {
    cancelRetry();
    retry_ = true;
    retryMove_ = moveIndex;
    retryPos_ = reviewPosition(moveIndex);
    retryMoved_ = false;
    retryShowAnswer_ = false;
    retryState_ = 0;
    retryMsg_.clear();
    board_.reset();
}

analysis::Eval App::quickEval(const std::string& startFen, const std::vector<std::string>& uciMoves, int ms) {
    std::lock_guard<std::mutex> lock(helperMutex_);
    if (!helperEngine_.running()) {
        if (!helperEngine_.start(stockfishExe_)) return {};
        helperEngine_.setOption("Hash", "32");
    }
    helperEngine_.setOption("UCI_LimitStrength", "false");
    const uci::Analysis a = helperEngine_.analyse(startFen.empty() ? "startpos" : startFen, uciMoves, 0, ms);
    Position p;
    if (!startFen.empty()) p.setFEN(startFen);
    for (const std::string& u : uciMoves) p.makeMove(chess::parseUCI(p, u));
    analysis::Eval e = analysis::terminalEval(p);
    if (!e.known) e = analysis::evalFromAnalysis(a, p.sideToMove());
    return e;
}

void App::handleRetryMove(Move m) {
    retryPos_.makeMove(m);
    retryMoved_ = true;
    retryShowAnswer_ = false;
    const analysis::Eval& prev = evals_[retryMove_];
    if (chess::toUCI(m) == prev.bestUci) {
        retryState_ = 2;
        retryMsg_ = "Верно! Это лучший ход.";
        sound::play(sound::Id::GameStart);
        return;
    }
    retryState_ = 1;
    retryMsg_ = "Проверяю ход…";
    std::vector<std::string> uci;
    for (int i = 0; i < retryMove_; ++i) uci.push_back(chess::toUCI(review_.moves[i]));
    uci.push_back(chess::toUCI(m));
    const std::string fen = review_.startFen;
    retryFuture_ = std::async(std::launch::async, [this, fen, uci]() { return quickEval(fen, uci, 500); });
}

void App::pollRetry() {
    if (!retryFuture_.valid() || retryFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const analysis::Eval after = retryFuture_.get();
    const chess::Color mover = retryMove_ % 2 == 0 ? review_.firstMover : ~review_.firstMover;
    const int sign = mover == chess::White ? 1 : -1;
    const double loss =
        analysis::winPercent(sign * evals_[retryMove_].cp) - analysis::winPercent(sign * after.cp);
    const double playedLoss = grades_[retryMove_].winLoss;
    if (!after.known) {
        retryState_ = 3;
        retryMsg_ = "Не получилось проверить ход";
    } else if (loss < 4) {
        retryState_ = 2;
        retryMsg_ = "Тоже хороший ход! Оценка " + analysis::formatEval(after);
        sound::play(sound::Id::GameStart);
    } else if (loss < playedLoss - 3) {
        retryState_ = 3;
        retryMsg_ = "Лучше, чем в партии, но есть ход сильнее. Оценка " + analysis::formatEval(after);
    } else {
        retryState_ = 3;
        retryMsg_ = "Не то — оценка " + analysis::formatEval(after) + ". Попробуй ещё";
        sound::play(sound::Id::Illegal);
    }
}

void App::drawEvalBar(Rectangle r, const analysis::Eval& e, bool flipped) {
    DrawRectangleRec(r, ::Color{40, 40, 44, 255});
    const float w = e.known ? float(analysis::winPercent(e.cp) / 100.0) : 0.5f;
    const float hw = r.height * w;
    const Rectangle white = flipped ? Rectangle{r.x, r.y, r.width, hw} : Rectangle{r.x, r.y + r.height - hw, r.width, hw};
    DrawRectangleRec(white, ::Color{236, 236, 236, 255});
    DrawLineEx({r.x, r.y + r.height / 2}, {r.x + r.width, r.y + r.height / 2}, 1, ::Color{128, 128, 128, 160});
    if (!e.known) return;
    std::string s = analysis::formatEval(e);
    if (!s.empty() && (s[0] == '+')) s = s.substr(1);
    const bool whiteBetter = e.cp >= 0;
    const float fs = 12;
    const Vector2 m = measure(s, fs, true);
    const bool atBottom = whiteBetter != flipped;
    const float ty = atBottom ? r.y + r.height - m.y - 4 : r.y + 4;
    text(s, {r.x + (r.width - m.x) / 2, ty}, fs, whiteBetter ? ::Color{40, 40, 44, 255} : ::Color{236, 236, 236, 255},
         true);
}

void App::drawEvalGraph(Rectangle r) {
    const Theme& t = theme();
    card(r);
    const Rectangle plot{r.x + 12, r.y + 12, r.width - 24, r.height - 24};
    DrawRectangleRec(plot, ::Color{48, 48, 54, 255});
    const int n = int(review_.moves.size());
    if (n == 0) return;
    auto valueAt = [&](float pos) -> float {
        const int i0 = std::clamp(int(std::floor(pos)), 0, n), i1 = std::clamp(i0 + 1, 0, n);
        if (!evals_[i0].known) return -1;
        const float a = float(analysis::winPercent(evals_[i0].cp));
        if (!evals_[i1].known) return a;
        const float b = float(analysis::winPercent(evals_[i1].cp));
        return a + (b - a) * (pos - i0);
    };
    for (int x = 0; x < int(plot.width); ++x) {
        const float v = valueAt(x * n / plot.width);
        if (v < 0) continue;
        const float y = plot.y + plot.height * (1 - v / 100);
        DrawRectangleRec({plot.x + x, y, 1, plot.y + plot.height - y}, ::Color{226, 226, 226, 255});
    }
    DrawLineEx({plot.x, plot.y + plot.height / 2}, {plot.x + plot.width, plot.y + plot.height / 2}, 1,
               ::Color{140, 140, 140, 140});
    for (int i = 0; i < n && i < int(grades_.size()); ++i) {
        const Grade g = grades_[i].grade;
        if (!(isBad(g) || g == Grade::Brilliant || g == Grade::Great) || !evals_[i + 1].known) continue;
        const float x = plot.x + (i + 1) * plot.width / n;
        const float y = plot.y + plot.height * float(1 - analysis::winPercent(evals_[i + 1].cp) / 100);
        DrawCircleV({x, y}, g == Grade::Inaccuracy ? 3.5f : 5.0f, gradeColor(g));
    }
    const float cx = plot.x + reviewPly_ * plot.width / n;
    DrawLineEx({cx, plot.y}, {cx, plot.y + plot.height}, 2, t.accent);
    if (hovered(plot)) {
        setHandCursor();
        const float hx = mouse().x;
        DrawLineEx({hx, plot.y}, {hx, plot.y + plot.height}, 1, fade(t.accent, 0.5f));
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            reviewPly_ = std::clamp(int(std::lround((hx - plot.x) / plot.width * n)), 0, n);
    }
}

void App::drawReviewMoves(Rectangle r) {
    const Theme& t = theme();
    card(r);
    const auto sans = sanList(review_.moves, review_.sanEn, review_.sanRu);
    const float rowH = 30;
    const Rectangle area{r.x + 8, r.y + 8, r.width - 16, r.height - 16};
    const int offset = review_.firstMover == chess::Black ? 1 : 0;
    const int total = int(sans.size()) + offset;
    const int rows = (total + 1) / 2;
    const float maxScroll = std::max(0.0f, rows * rowH - area.height);
    if (reviewPly_ != reviewListPly_) {
        reviewListPly_ = reviewPly_;
        if (reviewPly_ > 0) {
            const float rowTop = ((reviewPly_ - 1 + offset) / 2) * rowH;
            if (rowTop < reviewScroll_) reviewScroll_ = rowTop;
            if (rowTop + rowH > reviewScroll_ + area.height) reviewScroll_ = rowTop + rowH - area.height;
        }
    }
    if (hovered(area)) reviewScroll_ -= GetMouseWheelMove() * rowH * 2;
    reviewScroll_ = std::clamp(reviewScroll_, 0.0f, maxScroll);

    beginClip(area);
    const float colW = (area.width - 48) / 2;
    for (int row = 0; row < rows; ++row) {
        const float y = area.y + row * rowH - reviewScroll_;
        if (y + rowH < area.y || y > area.y + area.height) continue;
        if (row % 2) DrawRectangleRec({area.x, y, area.width, rowH}, fade(t.panel2, 0.35f));
        text(std::to_string(review_.firstMoveNumber + row) + ".", {area.x + 10, y + 5}, 15, t.sub);
        for (int k = 0; k < 2; ++k) {
            const int i = row * 2 + k - offset;
            if (i < 0 || i >= int(sans.size())) continue;
            const Rectangle cell{area.x + 48 + k * colW, y + 2, colW - 6, rowH - 4};
            const bool current = reviewPly_ == i + 1;
            const bool hot = hovered(cell) && hovered(area);
            if (current) roundRect(cell, 6, t.hl);
            else if (hot) roundRect(cell, 6, t.panel2);
            const Grade g = i < int(grades_.size()) ? grades_[i].grade : Grade::None;
            const std::string s = sans[i] + analysis::gradeSymbol(g);
            text(s, {cell.x + 8, cell.y + 4}, 16, isBad(g) ? gradeColor(g) : (current ? t.hlText : t.text),
                 current || isBad(g));
            if (g != Grade::None) drawGradeIcon({cell.x + cell.width - 14, cell.y + cell.height / 2}, 8, g);
            if (hot) setHandCursor();
            if (clicked(cell) && hovered(area)) reviewPly_ = i + 1;
        }
    }
    endClip();
}

void App::drawAnalysis() {
    const Theme& t = theme();
    const float W = screenW(), H = screenH();

    if (!review_.loaded) {
        const Rectangle c{(W - 560) / 2, (H - 260) / 2, 560, 260};
        card(c, 16);
        textCentered("Разбор партии", {c.x, c.y + 28, c.width, 36}, 26, t.text, true);
        textWrapped("Сыграй партию с ботом или скопируй PGN партии (например, с Lichess: «Поделиться» → PGN) "
                    "и вставь его сюда.",
                    {c.x + 40, c.y + 80, c.width - 80, 80}, 16, t.sub);
        if (button({c.x + 40, c.y + 180, 300, 48}, "Вставить PGN из буфера", Style::Primary)) openReviewFromClipboard();
        if (button({c.x + 356, c.y + 180, 164, 48}, "Назад", Style::Ghost) || IsKeyPressed(KEY_ESCAPE))
            screen_ = Screen::Menu;
        return;
    }

    if (!stockfishExe_.empty() && (analyzer_.moves() != review_.moves || analyzer_.fen() != review_.startFen))
        analyzer_.start(stockfishExe_, review_.startFen, review_.moves, 250);
    if (analyzer_.version() != gradesVersion_) {
        gradesVersion_ = analyzer_.version();
        evals_ = analyzer_.snapshot();
        evals_.resize(review_.moves.size() + 1);
        grades_ = analysis::reviewMoves(evals_, review_.moves, review_.startFen);
        summary_ = analysis::summarize(grades_, review_.firstMover);
    }
    if (!analyzer_.running() && analyzer_.done() == analyzer_.total() && !review_.mistakesSaved)
        saveMistakesAsPuzzles();
    pollRetry();
    const int n = int(review_.moves.size());
    reviewPly_ = std::clamp(reviewPly_, 0, n);

    if (inputEnabled() && !retry_) {
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) reviewPly_ = std::max(0, reviewPly_ - 1);
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) reviewPly_ = std::min(n, reviewPly_ + 1);
        if (IsKeyPressed(KEY_HOME) || IsKeyPressed(KEY_UP)) reviewPly_ = 0;
        if (IsKeyPressed(KEY_END) || IsKeyPressed(KEY_DOWN)) reviewPly_ = n;
        if (IsKeyPressed(KEY_F)) reviewFlipped_ = !reviewFlipped_;
        if (IsKeyPressed(KEY_ESCAPE)) screen_ = reviewReturn_;
    } else if (inputEnabled() && retry_ && IsKeyPressed(KEY_ESCAPE)) {
        cancelRetry();
    }

    const float gap = 8, barW = 24;
    const float availW = W - 3 * M - PanelA - barW - 10;
    const float availH = H - 2 * M - 2 * (BarH + gap);
    const float boardSize = std::floor(std::min(availW, availH) / 8) * 8;
    const float bx = M + barW + 10 + std::floor((availW - boardSize) / 2);
    const float by = M + BarH + gap;
    const Rectangle boardRect{bx, by, boardSize, boardSize};
    const bool flipped = reviewFlipped_;

    BoardOptions opt;
    opt.flipped = flipped;
    opt.coords = settings_.showCoords;
    opt.legalMoves = settings_.showLegalMoves;
    opt.lastMove = settings_.highlightLastMove;
    opt.animate = settings_.animate;
    PieceSet& set = pieces_.get(settings_.pieceSet);

    std::vector<BoardView::Arrow> hints;
    const ::Color orange{255, 170, 0, 190}, blue{40, 120, 230, 200};
    analysis::Eval shownEval;
    const Grade shownGrade =
        !retry_ && reviewPly_ > 0 && reviewPly_ - 1 < int(grades_.size()) ? grades_[reviewPly_ - 1].grade : Grade::None;
    if (shownGrade != Grade::None) opt.lastMoveTint = fade(gradeColor(shownGrade), 0.5f);
    if (retry_) {
        shownEval = evals_[retryMove_];
        if (retryShowAnswer_ && !evals_[retryMove_].bestUci.empty()) {
            Position p = reviewPosition(retryMove_);
            const Move best = chess::parseUCI(p, evals_[retryMove_].bestUci);
            if (!best.isNone()) hints.push_back({best.from, best.to, blue});
        }
        board_.setHintArrows(std::move(hints));
        const bool canMove = !retryMoved_;
        const Move m = board_.update(boardRect, retryPos_, canMove, set, opt);
        if (!m.isNone()) {
            if (m.isCapture()) sound::play(sound::Id::Capture);
            else sound::play(sound::Id::Move);
            handleRetryMove(m);
        }
    } else {
        if (reviewPly_ > 0) {
            const Move played = review_.moves[reviewPly_ - 1];
            hints.push_back({played.from, played.to, orange});
            const Grade g = reviewPly_ - 1 < int(grades_.size()) ? grades_[reviewPly_ - 1].grade : Grade::None;
            if (isBad(g) && !evals_[reviewPly_ - 1].bestUci.empty()) {
                Position p = reviewPosition(reviewPly_ - 1);
                const Move best = chess::parseUCI(p, evals_[reviewPly_ - 1].bestUci);
                if (!best.isNone()) hints.push_back({best.from, best.to, blue});
            }
        }
        board_.setHintArrows(std::move(hints));
        Position shown = reviewPosition(reviewPly_);
        board_.update(boardRect, shown, false, set, opt);
        if (reviewPly_ > 0 &&
            drawSquareGrade(board_.squareRect(review_.moves[reviewPly_ - 1].to), reviewPly_ - 1, shownGrade,
                            reviewAnim_) &&
            shownGrade == Grade::Brilliant)
            sound::play(sound::Id::Brilliant);
        shownEval = evals_[reviewPly_];
        if (settings_.animate && reviewPly_ == reviewShownPly_ + 1)
            board_.animate(reviewPosition(reviewShownPly_), review_.moves[reviewShownPly_]);
        else if (settings_.animate && reviewPly_ == reviewShownPly_ - 1)
            board_.animateBack(reviewPosition(reviewPly_), review_.moves[reviewPly_]);
        reviewShownPly_ = reviewPly_;
    }
    drawEvalBar({bx - barW - 10, by, barW, boardSize}, shownEval, flipped);

    auto playerBar = [&](Rectangle r, chess::Color c) {
        const int ci = c == chess::White ? 0 : 1;
        DrawCircleV({r.x + 14, r.y + r.height / 2}, 10,
                    c == chess::White ? ::Color{240, 240, 240, 255} : ::Color{30, 30, 34, 255});
        DrawCircleLinesV({r.x + 14, r.y + r.height / 2}, 10, t.border);
        const std::string& name = c == chess::White ? review_.white : review_.black;
        text(name, {r.x + 34, r.y + 10}, 18, t.text, true);
        if (summary_.reviewed > 0) {
            const std::string acc = "точность " + std::to_string(int(std::lround(summary_.accuracy[ci]))) + "%";
            text(acc, {r.x + 44 + measure(name, 18, true).x, r.y + 13}, 15, t.sub);
        }
    };
    const chess::Color topSide = flipped ? chess::White : chess::Black;
    playerBar({bx, M, boardSize, BarH}, topSide);
    playerBar({bx, by + boardSize + gap, boardSize, BarH}, ~topSide);

    const float px = W - M - PanelA;
    float py = M;

    const Rectangle sc{px, py, PanelA, 132};
    card(sc);
    text("Разбор партии", {sc.x + 18, sc.y + 12}, 20, t.text, true);
    if (analyzer_.running()) {
        const std::string pr = "Stockfish: " + std::to_string(analyzer_.done()) + " из " +
                               std::to_string(analyzer_.total());
        text(pr, {sc.x + sc.width - 18 - measure(pr, 14).x, sc.y + 16}, 14, t.sub);
        const float k = analyzer_.total() ? float(analyzer_.done()) / analyzer_.total() : 0;
        roundRect({sc.x + 18, sc.y + 40, sc.width - 36, 4}, 2, mix(t.bg, t.text, 0.08f));
        roundRect({sc.x + 18, sc.y + 40, std::max(4.0f, (sc.width - 36) * k), 4}, 2, t.accent);
    } else if (!analyzer_.error().empty()) {
        text(analyzer_.error(), {sc.x + 18, sc.y + 40}, 13, t.danger);
    }
    const float colW = 25, x0 = sc.x + sc.width - 14 - (analysis::GradeCount - 1) * colW;
    for (int c = 0; c < 2; ++c) {
        const float ry = sc.y + 76 + c * 26;
        const std::string& name = c == 0 ? review_.white : review_.black;
        DrawCircleV({sc.x + 25, ry + 11}, 6, c == 0 ? ::Color{240, 240, 240, 255} : ::Color{30, 30, 34, 255});
        text(name.size() > 20 ? name.substr(0, 20) : name, {sc.x + 37, ry + 2}, 15, t.text);
        if (summary_.reviewed > 0) {
            const std::string acc = std::to_string(int(std::lround(summary_.accuracy[c]))) + "%";
            text(acc, {x0 - 8 - measure(acc, 15, true).x, ry + 2}, 15, t.text, true);
        }
        for (int k = 1; k < analysis::GradeCount; ++k) {
            const int cnt = summary_.count[c][k];
            textCentered(cnt ? std::to_string(cnt) : "·", {x0 + (k - 1) * colW, ry, colW, 22}, 15,
                         cnt ? gradeColor(Grade(k)) : t.sub, cnt > 0);
        }
    }
    for (int k = 1; k < analysis::GradeCount; ++k) {
        const Vector2 c{x0 + (k - 1) * colW + colW / 2, sc.y + 62};
        drawGradeIcon(c, 9, Grade(k));
        if (CheckCollisionPointCircle(mouse(), c, 10)) {
            const std::string name = analysis::gradeName(Grade(k));
            const Vector2 m = measure(name, 14, true);
            const Rectangle tip{std::min(c.x - m.x / 2 - 8, sc.x + sc.width - m.x - 16), c.y + 14, m.x + 16, 26};
            roundRect(tip, 6, t.panel2);
            text(name, {tip.x + 8, tip.y + 5}, 14, gradeColor(Grade(k)), true);
        }
    }
    py += sc.height + 10;

    const Rectangle ic{px, py, PanelA, 170};
    card(ic);
    if (retry_) {
        const std::string label = reviewMoveLabel(retryMove_);
        text("Найди ход лучше, чем " + label, {ic.x + 18, ic.y + 14}, 18, t.text, true);
        ::Color mc = t.sub;
        if (retryState_ == 2) mc = t.good;
        if (retryState_ == 3) mc = t.danger;
        textWrapped(retryMsg_.empty() ? "Сделай ход на доске." : retryMsg_, {ic.x + 18, ic.y + 48, ic.width - 36, 50},
                    16, mc, retryState_ >= 2);
        const float bw = (ic.width - 36 - 16) / 3;
        if (button({ic.x + 18, ic.y + 112, bw, 42}, "Ещё раз", Style::Secondary, retryMoved_ && retryState_ != 1, 15))
            startRetry(retryMove_);
        if (button({ic.x + 18 + bw + 8, ic.y + 112, bw, 42}, "Ответ", Style::Secondary, true, 15)) {
            if (retryMoved_) startRetry(retryMove_);
            retryShowAnswer_ = true;
        }
        if (button({ic.x + 18 + 2 * (bw + 8), ic.y + 112, bw, 42}, "Готово", Style::Primary, retryState_ != 1, 15))
            cancelRetry();
    } else if (reviewPly_ == 0) {
        text("Начальная позиция", {ic.x + 18, ic.y + 14}, 19, t.text, true);
        text(analysis::formatEval(evals_[0]), {ic.x + ic.width - 90, ic.y + 14}, 19, t.text, true);
        textWrapped("Листай ходы стрелками ← → или кликай по ходам и по графику. Оранжевая стрелка — сыгранный ход, "
                    "синяя — лучший.",
                    {ic.x + 18, ic.y + 50, ic.width - 36, 80}, 15, t.sub);
    } else {
        const int i = reviewPly_ - 1;
        const Grade g = i < int(grades_.size()) ? grades_[i].grade : Grade::None;
        const std::string label = reviewMoveLabel(i) + analysis::gradeSymbol(g);
        text(label, {ic.x + 18, ic.y + 14}, 20, isBad(g) ? gradeColor(g) : t.text, true);
        if (g != Grade::None) drawGradeIcon({ic.x + 28, ic.y + 54}, 10, g);
        const std::string ev = analysis::formatEval(evals_[reviewPly_]);
        text(ev, {ic.x + ic.width - 18 - measure(ev, 20, true).x, ic.y + 14}, 20, t.text, true);
        if (g != Grade::None)
            text(analysis::gradeName(g), {ic.x + 46, ic.y + 45}, 15, gradeColor(g), true);
        float ly = ic.y + 70;
        if (isBad(g) && !evals_[i].bestUci.empty()) {
            Position p = reviewPosition(i);
            const Move best = chess::parseUCI(p, evals_[i].bestUci);
            const std::string bestSan =
                best.isNone() ? evals_[i].bestUci : moveName(p, best);
            text("Лучше было: " + bestSan + "  (" + analysis::formatEval(evals_[i]) + ")", {ic.x + 18, ly}, 16, t.text);
            ly += 26;
        }
        const analysis::Eval& cur = evals_[reviewPly_];
        if (!cur.pv.empty()) {
            Position p = reviewPosition(reviewPly_);
            std::string line;
            for (size_t k = 0; k < cur.pv.size() && k < 6; ++k) {
                const Move m = chess::parseUCI(p, cur.pv[k]);
                if (m.isNone()) break;
                line += moveName(p, m) + " ";
                p.makeMove(m);
            }
            textWrapped("Дальше лучше всего: " + line, {ic.x + 18, ly, ic.width - 36, 40}, 14, t.sub);
        }
        if (isBad(g) && !evals_[i].bestUci.empty() &&
            button({ic.x + 18, ic.y + 124, 200, 36}, "Найти ход лучше", Style::Primary, true, 15))
            startRetry(i);
    }
    py += ic.height + 10;

    drawEvalGraph({px, py, PanelA, 110});
    py += 110 + 10;

    const float navH = 48 + 10 + 44;
    drawReviewMoves({px, py, PanelA, H - M - py - navH});
    py = H - M - navH + 6;

    const float nw = (PanelA - 3 * 6 - 140) / 4;
    const char* labels[4] = {"«", "‹", "›", "»"};
    const int targets[4] = {0, reviewPly_ - 1, reviewPly_ + 1, n};
    for (int k = 0; k < 4; ++k) {
        const bool en = !retry_ && ((k < 2) ? reviewPly_ > 0 : reviewPly_ < n);
        if (button({px + k * (nw + 6), py, nw, 40}, labels[k], Style::Secondary, en, 20)) reviewPly_ = targets[k];
    }
    if (button({px + PanelA - 134, py, 134, 40}, "Ошибка ›", Style::Secondary, !retry_, 15)) {
        for (int k = 1; k <= n; ++k) {
            const int i = (reviewPly_ - 1 + k) % n;
            const chess::Color mover = i % 2 == 0 ? review_.firstMover : ~review_.firstMover;
            const bool mine = review_.myColor < 0 || (review_.myColor == 0) == (mover == chess::White);
            if (mine && i < int(grades_.size()) && isError(grades_[i].grade)) {
                reviewPly_ = i + 1;
                break;
            }
        }
    }
    py += 50;
    const float bw = (PanelA - 16) / 3;
    if (button({px, py, bw, 40}, "Перевернуть", Style::Ghost, true, 15)) reviewFlipped_ = !reviewFlipped_;
    if (button({px + bw + 8, py, bw, 40}, "Вставить PGN", Style::Ghost, true, 15)) openReviewFromClipboard();
    if (button({px + 2 * (bw + 8), py, bw, 40}, "‹ Назад", Style::Ghost, true, 15)) {
        cancelRetry();
        screen_ = reviewReturn_;
    }
}

}
