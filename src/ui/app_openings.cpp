#include "ui/app.h"

#include "core/notation.h"
#include "ui/sound.h"
#include "ui/ui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>

namespace ui {

using chess::Move;
using chess::Position;
using training::OpeningCard;

namespace {
constexpr float M = 24;
constexpr float PanelL = 430;

void playMoveSound(Move m) { sound::play(m.isCapture() ? sound::Id::Capture : sound::Id::Move); }
}

void App::loadOpenings() {
    reps_ = training::loadRepertoires(assetsDir_ / "openings" / "openings.txt", myOpeningsFile_);
    repTrees_.clear();
    for (const auto& r : reps_) repTrees_.push_back(training::buildTree(r));
    for (const auto& r : profile_.openingReviews()) opCards_[r.key] = {r.step, r.due};
}

App::OpeningStats App::openingStats(int rep) const {
    OpeningStats s;
    const char me = reps_[rep].black ? 'b' : 'w';
    const int day = storage::Profile::today();
    for (const auto& [key, moves] : repTrees_[rep].next) {
        if (key[key.find(' ') + 1] != me) continue;
        ++s.total;
        const auto it = opCards_.find(reps_[rep].id + "|" + key);
        if (it == opCards_.end()) ++s.fresh;
        else if (it->second.due <= day) ++s.review;
        else ++s.learned;
    }
    return s;
}

bool App::openingDue(const std::string& key) const {
    const auto it = opCards_.find(reps_[opRep_].id + "|" + key);
    return it == opCards_.end() || it->second.due <= storage::Profile::today();
}

int App::openingDueBelow(Position& p, std::unordered_set<std::string>& seen) const {
    const auto* list = repTrees_[opRep_].at(p);
    const std::string key = training::positionKey(p);
    if (!list || !seen.insert(key).second) return 0;
    int n = p.sideToMove() == openingColor() && openingDue(key);
    for (const auto& m : *list) {
        p.makeMove(m.move);
        n += openingDueBelow(p, seen);
        p.unmakeMove();
    }
    return n;
}

std::string App::openingCardKey() const { return reps_[opRep_].id + "|" + training::positionKey(opPos_); }

void App::saveOpeningCard(const std::string& key, OpeningCard c) {
    opCards_[key] = c;
    profile_.setOpeningReview({key, c.step, c.due});
}

void App::openRepertoire(int rep) {
    opRep_ = rep;
    opMode_ = OpMode::View;
    opScroll_ = 0;
    opRuns_ = 0;
    screen_ = Screen::Openings;
    selectOpeningLine(reps_[rep].lines.empty() ? -1 : 0);
}

void App::selectOpeningLine(int line) {
    opLine_ = line;
    opMoves_.clear();
    opPly_ = 0;
    board_.reset();
    if (line < 0) return;
    const auto& l = reps_[opRep_].lines[line];
    opMoves_ = training::lineMoves(l);
    while (opPly_ < opMoves_.size() && l.notes[opPly_].empty()) ++opPly_;
}

void App::reviewOpenings() {
    int best = -1, bestScore = -1;
    for (int i = 0; i < int(reps_.size()); ++i) {
        const auto s = openingStats(i);
        const int score = s.review * 1000 + s.fresh;
        if (!reps_[i].lines.empty() && score > bestScore) best = i, bestScore = score;
    }
    if (best < 0) return;
    openRepertoire(best);
    startOpeningDrill();
}

void App::startOpeningDrill() {
    opMode_ = OpMode::Drill;
    opPos_ = Position();
    opMoves_.clear();
    opNote_.clear();
    opReply_.clear();
    board_.reset();
    openingStep();
}

void App::openingStep() {
    opShow_ = opNew_ = opFailed_ = false;
    if (!repTrees_[opRep_].at(opPos_)) {
        opState_ = OpState::Done;
        ++opRuns_;
        sound::play(sound::Id::GameStart);
        progress("opening");
    } else if (opPos_.sideToMove() == openingColor()) {
        opState_ = OpState::Ask;
        opShow_ = opNew_ = !opCards_.count(openingCardKey());
    } else {
        opState_ = OpState::Reply;
        opTimer_ = GetTime() + (opMoves_.empty() ? 0.3 : 0.7);
    }
}

void App::openingReply() {
    const auto& list = *repTrees_[opRep_].at(opPos_);
    std::vector<int> weights;
    for (const auto& m : list) {
        std::unordered_set<std::string> seen;
        opPos_.makeMove(m.move);
        weights.push_back(openingDueBelow(opPos_, seen));
        opPos_.unmakeMove();
    }
    if (std::all_of(weights.begin(), weights.end(), [](int w) { return w == 0; })) std::fill(weights.begin(), weights.end(), 1);
    const auto& pick = list[std::discrete_distribution<size_t>(weights.begin(), weights.end())(rng_)];
    opReply_ = moveName(opPos_, pick.move) + (pick.note.empty() ? "" : " — " + pick.note);
    const Position before = opPos_;
    opPos_.makeMove(pick.move);
    opMoves_.push_back(pick.move);
    if (settings_.animate) board_.animate(before, pick.move);
    playMoveSound(pick.move);
    openingStep();
}

void App::openingFail() {
    const std::string key = openingCardKey();
    const auto it = opCards_.find(key);
    saveOpeningCard(key, training::reviewCard(it == opCards_.end() ? OpeningCard{} : it->second, false, storage::Profile::today()));
    opFailed_ = opShow_ = true;
}

void App::openingMove(Move m) {
    const auto& list = *repTrees_[opRep_].at(opPos_);
    const auto it = std::find_if(list.begin(), list.end(), [&](const training::OpeningMove& o) { return o.move == m; });
    if (it == list.end()) {
        sound::play(sound::Id::Illegal);
        board_.reset();
        openingFail();
        return;
    }
    const std::string key = openingCardKey();
    const int day = storage::Profile::today();
    const auto card = opCards_.find(key);
    if (!opFailed_ && (card == opCards_.end() || card->second.due <= day))
        saveOpeningCard(key, training::reviewCard(card == opCards_.end() ? OpeningCard{} : card->second, true, day));
    opNote_ = moveName(opPos_, m) + (it->note.empty() ? "" : " — " + it->note);
    opPos_.makeMove(m);
    opMoves_.push_back(m);
    playMoveSound(m);
    openingStep();
}

void App::addOpeningLines(std::vector<training::OpeningLine> lines) {
    if (lines.empty()) return;
    auto& rep = reps_[opRep_];
    for (auto& l : lines) rep.lines.push_back(std::move(l));
    repTrees_[opRep_] = training::buildTree(rep);
    training::saveCustomLines(reps_, myOpeningsFile_);
    selectOpeningLine(int(rep.lines.size()) - 1);
    opScroll_ = 1e9f;
}

std::string App::openingMovesText(const std::vector<Move>& moves, size_t count, float maxWidth) const {
    Position p;
    std::vector<std::string> tok;
    for (size_t i = 0; i < count && i < moves.size(); ++i) {
        tok.push_back((i % 2 == 0 ? std::to_string(i / 2 + 1) + "." : "") + moveName(p, moves[i]));
        p.makeMove(moves[i]);
    }
    std::string s;
    for (size_t i = tok.size(); i-- > 0;) {
        const std::string next = tok[i] + (s.empty() ? "" : " ") + s;
        if (measure("… " + next, 17).x > maxWidth) return "… " + s;
        s = next;
    }
    return s;
}

void App::drawOpeningList() {
    const Theme& t = theme();
    const float W = screenW();
    const float left = 56, top = 40;

    int review = 0;
    std::vector<OpeningStats> stats;
    for (int i = 0; i < int(reps_.size()); ++i) stats.push_back(openingStats(i)), review += stats.back().review;

    text("Дебюты", {left, top}, 34, t.text, true);
    text("Программа играет за соперника, ты вспоминаешь свои ходы. Ошибся — позиция вернётся раньше.", {left, top + 48}, 16, t.sub);
    if (button({W - left - 130, top + 6, 130, 46}, "‹ В меню", Style::Ghost) || (inputEnabled() && IsKeyPressed(KEY_ESCAPE)))
        screen_ = Screen::Menu;
    if (button({W - left - 130 - 12 - 240, top + 6, 240, 46}, review ? "Повторить, что пора: " + std::to_string(review) : "Учить новое",
               Style::Primary, true, 16))
        reviewOpenings();

    constexpr int Cols = 2;
    constexpr float Gap = 16, CardH = 156;
    const float colW = (W - 2 * left - Gap) / Cols;
    for (size_t i = 0; i < reps_.size(); ++i) {
        const auto& rep = reps_[i];
        const OpeningStats& s = stats[i];
        const Rectangle r{left + (i % Cols) * (colW + Gap), top + 96 + (i / Cols) * (CardH + Gap), colW, CardH};
        const bool hot = hovered(r);
        roundRect(r, 12, hot ? mix(t.panel, t.text, 0.06f) : t.panel);
        if (hot) setHandCursor();
        text(rep.title, {r.x + 20, r.y + 16}, 21, t.text, true);
        const std::string side = rep.black ? "за чёрных" : "за белых";
        badge({r.x + r.width - 20 - measure(side, 13).x - 16, r.y + 18}, side, t.panel2, t.sub, 13);
        beginClip({r.x, r.y + 48, r.width, 62});
        textWrapped(rep.text, {r.x + 20, r.y + 50, r.width - 40, 0}, 14, t.sub);
        endClip();
        const std::string info = rep.lines.empty()
                                     ? "Пока пусто"
                                     : "Веток: " + std::to_string(rep.lines.size()) + " · выучено " + std::to_string(s.learned) +
                                           " из " + std::to_string(s.total) +
                                           (s.review ? " · повторить " + std::to_string(s.review) : "");
        text(info, {r.x + 20, r.y + CardH - 40}, 15, s.review ? t.accent : t.text);
        roundRect({r.x + 20, r.y + CardH - 14, r.width - 40, 6}, 3, t.panel2);
        if (s.learned) roundRect({r.x + 20, r.y + CardH - 14, (r.width - 40) * float(s.learned) / float(s.total), 6}, 3, t.good);
        if (clicked(r)) openRepertoire(int(i));
    }
}

void App::drawOpeningView(Rectangle boardRect, float px) {
    const Theme& t = theme();
    const float H = screenH();
    auto& rep = reps_[opRep_];
    const OpeningStats st = openingStats(opRep_);

    Position before, pos;
    for (size_t i = 0; i < opPly_; ++i) {
        before = pos;
        pos.makeMove(opMoves_[i]);
    }
    std::string head = rep.lines.empty() ? "Здесь пока пусто" : "Начальная позиция", note;
    if (rep.lines.empty()) note = "Добавь ветку ходами на доске или скопируй PGN и нажми «Вставить PGN».";
    else if (opPly_ == 0) note = "Листай ходы кнопками ‹ › или стрелками на клавиатуре.";
    if (opPly_ > 0) {
        const Move m = opMoves_[opPly_ - 1];
        head = std::to_string((opPly_ + 1) / 2) + (opPly_ % 2 ? ". " : "... ") + moveName(before, m) +
               (before.sideToMove() == openingColor() ? " — твой ход" : " — ход соперника");
        if (const auto* list = repTrees_[opRep_].at(before))
            for (const auto& o : *list)
                if (o.move == m) note = o.note;
    }

    BoardOptions opt;
    opt.flipped = rep.black;
    opt.coords = settings_.showCoords;
    opt.animate = settings_.animate;
    board_.setHintArrows({});
    board_.update(boardRect, pos, false, pieces_.get(settings_.pieceSet), opt);
    text(openingMovesText(opMoves_, opPly_, boardRect.width), {boardRect.x, M + 18}, 17, t.sub);

    float py = M;
    text(std::string("Дебюты · ") + (rep.black ? "за чёрных" : "за белых"), {px, py}, 15, t.sub);
    text(rep.title, {px, py + 22}, 26, t.text, true);
    text("Позиций: " + std::to_string(st.total) + " · выучено " + std::to_string(st.learned) + " · повторить " +
             std::to_string(st.review) + " · новых " + std::to_string(st.fresh),
         {px, py + 62}, 15, t.sub);
    py += 92;

    const float noteH = 150, buttonsH = 46 + 8 + 40 + 8 + 40;
    const Rectangle lc{px, py, PanelL, std::max(110.0f, H - M - buttonsH - noteH - 24 - py)};
    card(lc);
    constexpr float RowH = 30;
    const bool inList = hovered(lc);
    if (inList) opScroll_ -= GetMouseWheelMove() * 60;
    opScroll_ = std::clamp(opScroll_, 0.0f, std::max(0.0f, rep.lines.size() * RowH + 12 - lc.height));
    beginClip(lc);
    int pick = -1;
    for (size_t i = 0; i < rep.lines.size(); ++i) {
        const Rectangle rr{lc.x + 6, lc.y + 6 + i * RowH - opScroll_, lc.width - 12, RowH - 2};
        if (rr.y + rr.height < lc.y || rr.y > lc.y + lc.height) continue;
        const bool hot = inList && hovered(rr);
        if (int(i) == opLine_) roundRect(rr, 8, mix(t.panel, t.accent, 0.3f));
        else if (hot) roundRect(rr, 8, mix(t.panel, t.text, 0.07f));
        if (hot) setHandCursor();
        text(rep.lines[i].title, {rr.x + 10, rr.y + 6}, 15, t.text);
        if (hot && clicked(rr)) pick = int(i);
    }
    endClip();
    if (pick >= 0) selectOpeningLine(pick);

    const Rectangle nc{px, lc.y + lc.height + 12, PanelL, noteH};
    card(nc);
    text(head, {nc.x + 18, nc.y + 14}, 18, t.text, true);
    textWrapped(note, {nc.x + 18, nc.y + 46, nc.width - 36, 0}, 15, t.sub);

    py = nc.y + nc.height + 12;
    const bool keys = inputEnabled();
    if ((button({px, py, 52, 46}, "‹", Style::Secondary, opPly_ > 0) || (keys && IsKeyPressed(KEY_LEFT))) && opPly_ > 0) {
        --opPly_;
        board_.reset();
    }
    if ((button({px + 60, py, 52, 46}, "›", Style::Secondary, opPly_ < opMoves_.size()) || (keys && IsKeyPressed(KEY_RIGHT))) &&
        opPly_ < opMoves_.size()) {
        if (settings_.animate) board_.animate(pos, opMoves_[opPly_]);
        playMoveSound(opMoves_[opPly_]);
        ++opPly_;
    }
    const int todo = st.review + st.fresh;
    if (button({px + 120, py, PanelL - 120, 46}, todo ? "Тренировать (" + std::to_string(todo) + ")" : "Тренировать", Style::Primary,
               !rep.lines.empty()) ||
        (keys && !rep.lines.empty() && IsKeyPressed(KEY_ENTER))) {
        startOpeningDrill();
        return;
    }
    py += 54;
    const float bw = (PanelL - 16) / 3;
    if (button({px, py, bw, 40}, "Добавить ветку", Style::Secondary, true, 15)) {
        opBase_ = opPly_;
        opMoves_.resize(opPly_);
        opPos_ = pos;
        opMode_ = OpMode::Add;
        board_.reset();
        return;
    }
    if (button({px + bw + 8, py, bw, 40}, "Вставить PGN", Style::Secondary, true, 15)) {
        const char* clip = GetClipboardText();
        auto lines = training::linesFromPGN(clip ? clip : "");
        if (lines.empty()) {
            toast("В буфере обмена нет PGN с ходами — сначала скопируй его");
        } else {
            toast("Добавлено веток: " + std::to_string(lines.size()));
            addOpeningLines(std::move(lines));
        }
        return;
    }
    const bool custom = opLine_ >= 0 && rep.lines[opLine_].custom;
    const bool confirming = GetTime() < opDeleteUntil_;
    if (button({px + 2 * (bw + 8), py, bw, 40}, confirming ? "Точно удалить?" : "Удалить ветку", Style::Ghost, custom, 15)) {
        if (confirming) {
            opDeleteUntil_ = 0;
            rep.lines.erase(rep.lines.begin() + opLine_);
            repTrees_[opRep_] = training::buildTree(rep);
            training::saveCustomLines(reps_, myOpeningsFile_);
            selectOpeningLine(std::min(opLine_, int(rep.lines.size()) - 1));
            return;
        }
        opDeleteUntil_ = GetTime() + 3;
    }
    py += 48;
    if (button({px, py, PanelL, 40}, "‹ К дебютам", Style::Ghost) || (keys && IsKeyPressed(KEY_ESCAPE))) opRep_ = -1;
}

void App::drawOpeningAdd(Rectangle boardRect, float px) {
    const Theme& t = theme();
    BoardOptions opt;
    opt.flipped = reps_[opRep_].black;
    opt.coords = settings_.showCoords;
    opt.legalMoves = settings_.showLegalMoves;
    opt.animate = settings_.animate;
    board_.setHintArrows({});
    const Move m = board_.update(boardRect, opPos_, true, pieces_.get(settings_.pieceSet), opt);
    if (!m.isNone()) {
        opPos_.makeMove(m);
        opMoves_.push_back(m);
        playMoveSound(m);
    }
    text(openingMovesText(opMoves_, opMoves_.size(), boardRect.width), {boardRect.x, M + 18}, 17, t.sub);

    float py = M;
    text(reps_[opRep_].title, {px, py}, 15, t.sub);
    text("Новая ветка", {px, py + 22}, 26, t.text, true);
    py += 70;
    const Rectangle ic{px, py, PanelL, 190};
    card(ic);
    float y = ic.y + 18;
    y += textWrapped("Делай ходы на доске за обе стороны: и за себя, и за соперника. Ветка начинается с позиции, которая была "
                     "на доске, и должна заканчиваться твоим ходом.",
                     {ic.x + 18, y, ic.width - 36, 0}, 16, t.text) + 10;
    const size_t added = opMoves_.size() - opBase_;
    const bool ready = added > 0 && opPos_.sideToMove() != openingColor();
    textWrapped(added == 0 ? "Пока ни одного нового хода" : ready ? "Можно сохранять" : "Теперь сделай свой ход",
                {ic.x + 18, y, ic.width - 36, 0}, 16, ready ? t.good : t.sub, true);

    py = ic.y + ic.height + 12;
    const float bw = (PanelL - 8) / 2;
    if (button({px, py, bw, 46}, "Отменить ход", Style::Secondary, added > 0, 15)) {
        opPos_.unmakeMove();
        opMoves_.pop_back();
        board_.reset();
    }
    if (button({px + bw + 8, py, bw, 46}, "Сохранить ветку", Style::Primary, ready, 15)) {
        training::OpeningLine l;
        Position p;
        for (const Move mv : opMoves_) {
            l.moves.push_back(chess::toSAN(p, mv));
            l.notes.emplace_back();
            p.makeMove(mv);
        }
        l.custom = true;
        l.title = training::customLineTitle(l.moves);
        opMode_ = OpMode::View;
        addOpeningLines({l});
        toast("Ветка сохранена");
        return;
    }
    py += 54;
    if (button({px, py, PanelL, 40}, "Отмена", Style::Ghost) || (inputEnabled() && IsKeyPressed(KEY_ESCAPE))) {
        opMode_ = OpMode::View;
        const size_t ply = opBase_;
        selectOpeningLine(opLine_);
        opPly_ = std::min(ply, opMoves_.size());
    }
}

void App::drawOpeningDrill(Rectangle boardRect, float px) {
    const Theme& t = theme();
    const float H = screenH();
    if (opState_ == OpState::Reply && GetTime() >= opTimer_) openingReply();

    BoardOptions opt;
    opt.flipped = reps_[opRep_].black;
    opt.coords = settings_.showCoords;
    opt.legalMoves = settings_.showLegalMoves;
    opt.animate = settings_.animate;
    std::vector<BoardView::Arrow> arrows;
    if (opState_ == OpState::Ask && opShow_) {
        const Move e = repTrees_[opRep_].at(opPos_)->front().move;
        arrows.push_back({e.from, e.to, ::Color{40, 120, 230, 210}});
    }
    board_.setHintArrows(std::move(arrows));
    const Move m = board_.update(boardRect, opPos_, opState_ == OpState::Ask, pieces_.get(settings_.pieceSet), opt);
    if (!m.isNone()) openingMove(m);
    text(openingMovesText(opMoves_, opMoves_.size(), boardRect.width), {boardRect.x, M + 18}, 17, t.sub);

    const bool ask = opState_ == OpState::Ask, done = opState_ == OpState::Done;
    std::string title = "Верно!";
    ::Color tc = t.good;
    if (done) title = "Ветка пройдена!";
    else if (ask && opFailed_) title = "Запомни этот ход", tc = t.danger;
    else if (ask && opNew_) title = "Новый ход — запомни", tc = t.accent;
    else if (ask) title = "Твой ход", tc = t.text;
    else if (opMoves_.empty()) title = "Начинаем", tc = t.text;

    float py = M;
    text("Тренировка · " + reps_[opRep_].title, {px, py}, 15, t.sub);
    text(title, {px, py + 22}, 26, tc, true);
    py += 70;

    const Rectangle ic{px, py, PanelL, std::max(200.0f, H - M - 108 - py)};
    card(ic);
    float y = ic.y + 18;
    const Rectangle tr{ic.x + 18, 0, ic.width - 36, 0};
    auto para = [&](const std::string& s, float size, ::Color c, bool bold = false) {
        y += textWrapped(s, {tr.x, y, tr.width, 0}, size, c, bold) + 10;
    };
    if (!opNote_.empty()) para("Ты: " + opNote_, 15, t.sub);
    if (!opReply_.empty()) para("Соперник: " + opReply_, 15, t.sub);
    if (ask && opShow_) {
        const auto& e = repTrees_[opRep_].at(opPos_)->front();
        para((opFailed_ ? "Правильно: " : "Сыграй: ") + moveName(opPos_, e.move), 19, opFailed_ ? t.danger : t.accent, true);
        if (!e.note.empty()) para(e.note, 17, t.text);
        if (opFailed_) para("Сделай этот ход на доске. Позиция вернётся в следующих заходах, пока не запомнится.", 15, t.sub);
    } else if (ask) {
        para("Вспомни свой ход в этой позиции и сыграй его на доске.", 17, t.text);
    } else if (done) {
        const OpeningStats st = openingStats(opRep_);
        para("Пройдено веток за этот заход: " + std::to_string(opRuns_) + ".", 17, t.text);
        para(st.review + st.fresh ? "Осталось повторить позиций: " + std::to_string(st.review) + ", новых: " + std::to_string(st.fresh) + "."
                                  : "На сегодня в этом репертуаре всё повторено.",
             16, st.review + st.fresh ? t.sub : t.good);
    }

    py = ic.y + ic.height + 12;
    const float bw = (PanelL - 8) / 2;
    if (button({px, py, bw, 46}, "Показать ход", Style::Secondary, ask && !opShow_, 15)) openingFail();
    if (button({px + bw + 8, py, bw, 46}, done ? "Ещё ветка" : "Заново", done ? Style::Primary : Style::Secondary, true, 15) ||
        (done && inputEnabled() && IsKeyPressed(KEY_ENTER))) {
        startOpeningDrill();
        return;
    }
    py += 54;
    if (button({px, py, PanelL, 40}, "‹ К репертуару", Style::Ghost) || (inputEnabled() && IsKeyPressed(KEY_ESCAPE))) {
        opMode_ = OpMode::View;
        selectOpeningLine(opLine_);
    }
}

void App::drawOpenings() {
    if (opRep_ < 0) {
        drawOpeningList();
        return;
    }
    const float W = screenW(), H = screenH();
    const float availW = W - 3 * M - PanelL, availH = H - 2 * M - 56;
    const float boardSize = std::floor(std::min(availW, availH) / 8) * 8;
    const Rectangle boardRect{M + std::floor((availW - boardSize) / 2), M + 56, boardSize, boardSize};
    const float px = W - M - PanelL;
    if (opMode_ == OpMode::Drill) drawOpeningDrill(boardRect, px);
    else if (opMode_ == OpMode::Add) drawOpeningAdd(boardRect, px);
    else drawOpeningView(boardRect, px);
}

}
