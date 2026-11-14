#include "ui/app.h"

#include "core/notation.h"
#include "ui/fx.h"
#include "ui/ui.h"

#include <algorithm>
#include <sstream>

namespace ui {

namespace {
std::vector<chess::Move> parseLine(const std::string& sans) {
    chess::Position p;
    std::vector<chess::Move> moves;
    std::istringstream in(sans);
    std::string san;
    while (in >> san) {
        const chess::Move m = chess::parseSAN(p, san);
        if (m.isNone()) break;
        moves.push_back(m);
        p.makeMove(m);
    }
    return moves;
}
}

void App::setupShot(const std::string& screen) {
    auto playLine = [&](const std::string& sans) {
        for (const chess::Move& m : parseLine(sans)) session_.play(m);
    };
    settings_.vsBot = screen == "newgame" || screen == "botplay" || screen == "sfplay" || screen == "elo" ||
                      screen == "coach" || screen == "coachhint";
    newGameOpen_ = screen == "newgame" || screen == "newgame960";
    if (screen == "newgame960" || screen == "play960" || screen == "review960") {
        settings_.vsBot = true;
        chess960_ = true;
        chess960Pos_ = 249;
    }
    if (screen == "play960" || screen == "review960") {
        settings_.botId = "club";
        settings_.colorChoice = 0;
        startGame(timeControls()[2], chess960Fen());
        for (const char* uci : {"c2c3", "c7c6", "d1c2", "d8c7", "c1b1", "c8b8", "g1f3", "g8f6"})
            session_.play(chess::parseUCI(session_.position(), uci));
        if (screen == "review960") openReview(session_.toGame(), 0, Screen::Game);
    }
    if (screen == "rated") {
        ratedOpen_ = true;
        ratedPhase_ = 2;
        findOpponent();
    }

    const bool profileShot = screen == "profile" || screen == "profilegames" || screen == "achievements";
    if (profileShot || screen == "elo" || screen == "menu" || screen == "plan") {
        const int opp[] = {1000, 1200, 1000, 700, 1000, 1000, 1200, 1200, 1000, 1400, 1200, 1200, 1400, 1200};
        const double res[] = {1, 0, 1, 1, 0.5, 1, 0, 1, 1, 0, 1, 1, 0.5, 1};
        for (int i = 0; i < 14; ++i) {
            storage::GameRecord g;
            for (const auto& b : engine::bots())
                if (b.rating == opp[i]) g.opponent = b.name, g.botId = b.id;
            g.opponentRating = opp[i];
            g.color = i % 2 ? "b" : "w";
            g.score = res[i];
            g.reason = res[i] == 0.5 ? "Троекратное повторение" : "Мат";
            g.timeControl = "10+0";
            g.rated = true;
            g.date = "2026-11-" + std::to_string(10 + i / 5) + " 1" + std::to_string(i % 10) + ":00";
            profile_.recordGame(g);
        }
        const int day = storage::Profile::today();
        for (int d = 3; d >= 0; --d) profile_.markActivity(day - d);
        for (int i = 0; i < 37; ++i) profile_.bump("puzzles_solved");
        profile_.srsAdd({"s1", "", "", "fork pin", "lichess"}, day);
        profile_.srsAdd({"s2", "", "", "fork", "lichess"}, day);
        profile_.srsAdd({"s3", "8/5k2/8/3R4/8/2K5/8/6r1 w - - 0 40", "", "mine", "mine"}, day);
        auto& plan = todayPlan();
        for (int i = 0; i < 4; ++i) storage::advancePlan(plan, "puzzles", plan[0].arg);
        storage::advancePlan(plan, "lesson", plan[1].arg);
        checkAchievements();
        if (profileShot) screen_ = Screen::Profile;
        profileTab_ = screen == "profilegames" ? 1 : screen == "achievements" ? 2 : 0;
        if (screen == "plan") screen_ = Screen::Plan;
    }
    if (screen == "about") screen_ = Screen::Settings, tab_ = SettingsTab::About;
    if (screen == "elo") {
        foundBot_ = *engine::botById("positional");
        foundColor_ = chess::White;
        settings_.tcIndex = 4;
        startRated();
        playLine("e4 e5 Bc4 Nc6 Qh5 Nf6 Qxf7");
    }
    if (screen == "botplay" || screen == "sfplay") {
        settings_.botId = screen == "sfplay" ? "grandmaster" : "club";
        settings_.colorChoice = 0;
        autoPlay_ = true;
        startGame(timeControls()[2]);
    }
    if (screen == "coach" || screen == "coachhint") {
        settings_.coachMode = true;
        settings_.botId = "amateur";
        settings_.colorChoice = 0;
        startGame(timeControls()[4]);
        playLine("e4 e5 Qh5 Nc6");
        coachUpdate();
        if (screen == "coach") {
            chess::Position p = session_.position();
            playUserMove(chess::parseSAN(p, "Qxe5+"));
        } else {
            showThreats_ = true;
            nextHint();
            nextHint();
        }
    }
    if (screen == "puzzles") openPuzzles(PuzzleMode::Rated);
    if (screen == "endgames") {
        egDone_ = {"queen", "rook"};
        egIdx_ = -1;
        screen_ = Screen::Endgames;
    }
    if (screen == "endgame") openEndgame(1);
    if (screen == "endgamelost") {
        openEndgame(0);
        egFuture_.wait();
        pollEndgame();
        endgameMove(chess::parseUCI(eg_.position(), "h2d6"));
        egFuture_.wait();
        pollEndgame();
        egShowMistake_ = true;
    }
    if (screen == "openings") screen_ = Screen::Openings;
    if (screen == "opening") openRepertoire(0), opPly_ = 5;
    if (screen == "openingdrill") openRepertoire(2), startOpeningDrill();
    if (screen == "openingempty") openRepertoire(4);
    if (screen == "openingadd") {
        openRepertoire(4);
        opMode_ = OpMode::Add;
        opPos_ = chess::Position();
        for (const chess::Move& m : parseLine("d4 Nf6 c4 g6 Nc3")) opPos_.makeMove(m), opMoves_.push_back(m);
    }
    if (screen == "openingdone" || screen == "openingfail") {
        openRepertoire(3);
        startOpeningDrill();
        for (int i = 0; opState_ != OpState::Done; ++i) {
            if (opState_ == OpState::Reply) {
                openingReply();
            } else if (screen == "openingfail" && i > 6) {
                openingFail();
                break;
            } else {
                openingMove(repTrees_[opRep_].at(opPos_)->front().move);
            }
        }
    }
    if (screen == "lessons") {
        for (const auto& l : lessons_[0].lessons) lessonsDone_.insert(l.id);
        lessonsDone_.insert(lessons_[1].lessons[0].id);
        lessonIdx_ = -1;
        screen_ = Screen::Lessons;
    }
    if (screen == "lesson" || screen == "lessondone") {
        openLesson(1, 0);
        setLessonStep(1);
        if (screen == "lessondone")
            for (size_t i = 0; i < lesson().steps[1].solution.size(); i += 2) {
                if (puzzleState_ == PuzzleState::Reply) run_.applyReply();
                lessonMove(run_.expected());
            }
    }
    if (screen == "puzzlesdone" || screen == "puzzleimport") {
        puzzles_ = puzzlesLoading_.get();
        for (const auto& p : puzzles_) puzzleDone_.insert(p.id);
        openPuzzles(PuzzleMode::Rated);
        if (screen == "puzzleimport") startPuzzleImport();
    }
    if (screen == "menu") puzzles_ = puzzlesLoading_.get();
    if (screen == "puzzletheme") openPuzzles(PuzzleMode::Theme);
    if (screen == "puzzlestorm" || screen == "stormauto") openPuzzles(PuzzleMode::Storm);
    autoPlay_ = autoPlay_ || screen == "stormauto";
    if (screen == "game" || screen == "gameover") {
        startGame(timeControls()[4]);
        playLine(screen == "gameover" ? "f3 e5 g4 Qh4" : "e4 e5 Nf3 Nc6 Bc4 Nf6 Ng5 d5 exd5 Nxd5 Nxf7");
    }
    if (screen == "longgame" || screen == "review") {
        startGame(timeControls()[5]);
        uint32_t rnd = 7;
        for (int i = 0; i < 90 && !session_.over(); ++i) {
            const auto legal = session_.position().legalMoves();
            rnd = rnd * 1664525u + 1013904223u;
            session_.play(legal[(rnd >> 8) % legal.size()]);
        }
        if (screen == "review") session_.setViewPly(31);
    }
    if (screen == "analysis" || screen == "analysisblack") {
        chess::Game g;
        g.setTag("White", "Ты");
        g.setTag("Black", "Любитель");
        g.moves = parseLine(screen == "analysis" ? "e4 e5 Nf3 Nc6 Bc4 Nf6 Ng5 d5 exd5 Nxd5 Nxf7 Kxf7 Qf3+ Ke6 Nc3 Nb4 O-O c6 d4"
                                                 : "e4 e5 Bc4 Nc6 Qh5 Nf6 Qxf7");
        openReview(g, screen == "analysis" ? 0 : 1, Screen::Menu);
        reviewPly_ = std::min(10, int(g.moves.size()));
    }
    const std::pair<const char*, SettingsTab> tabs[] = {{"settings", SettingsTab::Pieces},
                                                        {"settings-look", SettingsTab::Look},
                                                        {"settings-game", SettingsTab::Game},
                                                        {"settings-sound", SettingsTab::Sound},
                                                        {"settings-about", SettingsTab::About}};
    for (const auto& [name, tab] : tabs)
        if (screen == name) screen_ = Screen::Settings, tab_ = tab;
    if (screen == "update") updateTag_ = "v9.9.9", updateChecked_ = true;
    if (screen.starts_with("vision")) {
        const bool square = screen == "vision" || screen == "visionwrong";
        openVision(square ? training::VisionMode::Square : training::VisionMode::Knight);
        if (screen != "visionstart") visEnd_ = GetTime() + 21.4;
        const auto sq = [](const char* s) { return chess::parseSquare(s); };
        if (square) vis_.task = {training::VisionMode::Square, sq("f6"), 1ull << sq("f6")};
        else vis_.task = {training::VisionMode::Knight, sq("b1"), training::visionReach(training::VisionMode::Knight, sq("b1"))};
        vis_.score = 7, vis_.errors = 1;
        if (screen == "visionwrong") vis_.click(sq("f5")), visWrongSq_ = sq("f5"), visPause_ = 1e9;
        if (screen == "visionknight") vis_.click(sq("a3"));
        if (screen == "visionend") visEnd_ = -1;
    }
    if (screen == "blind" || screen == "blindpeek") {
        settings_.vsBot = true;
        settings_.botId = "amateur";
        settings_.colorChoice = 0;
        blindPick_ = true;
        startGame(timeControls()[4]);
        playLine("e4 e5 Nf3 Nc6 Bc4 Bc5");
        if (screen == "blindpeek") peekUntil_ = 1e9;
    }
    if (screen == "blindnew") settings_.vsBot = newGameOpen_ = blindPick_ = true;
    if (screen == "fxpuzzle" || screen == "fxstreak") {
        openPuzzles(PuzzleMode::Rated);
        if (screen == "fxpuzzle") fx::celebrate(fx::Kind::Solved, {420, 330});
        else fx::celebrate(fx::Kind::Streak, {420, 330}, "Серия: 5 подряд!");
    }
    if (screen == "fxbrilliant") {
        setupShot("analysis");
        fx::celebrate(fx::Kind::Brilliant, {screenW() / 2 - 200, 300}, "Бриллиантовый ход!");
    }
    if (screen == "fxachievement") fx::celebrate(fx::Kind::Achievement, {screenW() / 2, 150}, "Первая победа");
}

}
