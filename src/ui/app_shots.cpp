#include "ui/app.h"

#include "core/notation.h"

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
    newGameOpen_ = screen == "newgame";
    if (screen == "rated") {
        ratedOpen_ = true;
        ratedPhase_ = 2;
        findOpponent();
    }

    const bool profileShot = screen == "profile" || screen == "profilegames" || screen == "achievements" || screen == "weakness";
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
        if (screen == "weakness" || screen == "plan") {
            const int spec[][4] = {{2, 0, 2, 2},  {14, 1, 0, 2},  {17, 1, 0, 2}, {19, 1, 3, 3}, {23, 1, 0, 2},
                                   {12, 1, 3, 5}, {28, 1, 4, 4},  {33, 2, 4, 6}, {45, 2, 2, 1}, {16, 1, 1, 2},
                                   {21, 1, 0, 2}, {9, 0, 0, 3},   {38, 2, 4, 1}, {25, 1, 0, 2}};
            int k = 0;
            for (const auto& g : profile_.recentGames(14)) {
                analysis::GameMistakes gm;
                gm.moves[0] = 10, gm.moves[1] = 18, gm.moves[2] = 7;
                analysis::Mistake m;
                m.moveNumber = spec[k][0], m.phase = spec[k][1], m.kind = analysis::ErrorKind(spec[k][2]);
                m.piece = spec[k][3], m.grade = analysis::Grade::Blunder, m.winLoss = 30;
                gm.mistakes.push_back(m);
                profile_.setValue("weak:" + std::to_string(g.id), analysis::encodeMistakes(gm));
                ++k;
            }
            for (int i = 0; i < 6; ++i) profile_.srsAdd({"m" + std::to_string(i), "", "", "mine", "mine"}, storage::Profile::today());
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
        profileTab_ = screen == "profilegames" ? 1 : screen == "achievements" ? 2 : screen == "weakness" ? 3 : 0;
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
}

}
