#pragma once
#include "analysis/analyzer.h"
#include "analysis/review.h"
#include "core/pgn.h"
#include "engine/bots.h"
#include "uci/uci_engine.h"
#include "engine/search.h"
#include "storage/profile.h"
#include "training/endgames.h"
#include "training/lessons.h"
#include "training/puzzles.h"
#include "ui/board_view.h"
#include "ui/game_session.h"
#include "ui/grades.h"
#include "ui/pieces.h"
#include "ui/settings.h"
#include "ui/theme.h"

#include <atomic>
#include <filesystem>
#include <future>
#include <mutex>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace ui {
class App {
public:
    enum class PuzzleMode { Rated, Theme, Daily, Mine, Repeat, Storm };
    int run(int argc, char** argv);

private:
    enum class Screen { Menu, Game, Settings, Profile, Analysis, Puzzles, Lessons, Endgames, Plan };
    enum class PuzzleState { Empty, Setup, Play, Reply, Solved, Wrong };
    enum class SettingsTab { Look, Pieces, Game, Sound, About };

    void applySettings();
    void setupShot(const std::string& screen);
    void frame();

    void drawMenu();
    bool drawUpdateBanner(Rectangle r);
    void openPuzzles(PuzzleMode mode);
    bool pickPuzzle(training::Puzzle& out);
    void nextPuzzle();
    void finishPuzzle(bool solved);
    void puzzleMove(chess::Move m);
    void drawPuzzles();
    void startPuzzleImport();
    void pollPuzzleImport();
    std::string importLabel() const;
    void saveMistakesAsPuzzles();
    chess::Color coachSide() const;
    void coachUpdate();
    void nextHint();
    std::vector<BoardView::Arrow> coachArrows();
    bool coachIntercept(chess::Move m);
    void drawCoachCard(Rectangle r);
    void drawBlunderWarning(Rectangle boardRect);
    void drawProfile();
    void drawProfileOverview(float top);
    void drawProfileGames(float top);
    void drawProfileAchievements(float top);
    std::vector<storage::PlanItem>& todayPlan();
    void progress(const std::string& kind, const std::string& arg = {});
    void checkAchievements();
    void startPlanItem(const storage::PlanItem& item);
    void openLastReview(Screen returnTo);
    void drawPlan();
    void openLesson(int sec, int idx);
    void setLessonStep(int step);
    void lessonMove(chess::Move m);
    void drawLessons();
    void drawLessonList();
    const training::Lesson& lesson() const { return lessons_[lessonSec_].lessons[lessonIdx_]; }
    size_t lessonCount() const;
    const training::Endgame& endgame() const;
    int egScore(size_t i) const;
    void openEndgame(int idx);
    void restartEndgame(int keepMoves);
    void askEndgameEngine();
    void finishEndgame(training::Verdict v, const std::string& msg);
    void endgameMove(chess::Move m);
    void pollEndgame();
    void drawEndgameList();
    void drawEndgames();

    void drawAnalysis();
    void drawEvalBar(Rectangle r, const analysis::Eval& e, bool flipped);
    void drawEvalGraph(Rectangle r);
    void drawReviewMoves(Rectangle r);
    void openReview(const chess::Game& game, int myColor, Screen returnTo);
    bool openReviewFromClipboard();
    chess::Position reviewPosition(int ply) const;
    std::string reviewMoveLabel(int moveIndex) const;
    void startRetry(int moveIndex);
    void cancelRetry();
    void handleRetryMove(chess::Move m);
    void pollRetry();
    analysis::Eval quickEval(const std::string& startFen, const std::vector<std::string>& uciMoves, int ms);
    void drawRatingBlock(Rectangle r);
    void recordFinishedGame();
    void forfeitIfRated();
    void openRated();
    void findOpponent();
    void startRated();
    void drawRatedModal();
    void drawNewGameModal();
    void drawGame();
    void drawSettings();
    void drawSettingsLook(Rectangle r);
    void drawSettingsPieces(Rectangle r);
    void drawSettingsGame(Rectangle r);
    void drawSettingsSound(Rectangle r);
    void drawSettingsAbout(Rectangle r);

    void drawPlayerBar(Rectangle r, chess::Color c, const chess::Position& shown);
    void drawStatusCard(Rectangle r);
    void drawMoveList(Rectangle r);
    void updateLiveGrades();
    analysis::Grade liveGrade(int index) const;
    void drawGameOver(Rectangle boardRect);
    void handleGameKeys();
    void animateNavigation();

    void startGame(const TimeControl& tc, const std::string& fen = {}, const engine::BotProfile* practiceBot = nullptr,
                   chess::Color practiceColor = chess::White, bool rated = false);
    void updateBot();
    void cancelBot();
    void undoMove();
    void offerDraw();
    bool botThinking() const { return botFuture_.valid(); }
    bool humanToMove() const { return !bot_ || session_.position().sideToMove() == humanColor_; }
    std::string resultTitle() const;
    void playUserMove(chess::Move m);
    void moveSound(chess::Move m);
    void savePGN();
    bool boardFlipped() const;

    std::filesystem::path assetsDir_;
    Settings settings_;
    PieceLibrary pieces_;
    BoardView board_;
    GameSession session_;
    std::vector<std::string> credits_;

    Screen screen_ = Screen::Menu;
    bool newGameOpen_ = false;
    bool ratedOpen_ = false;
    int ratedPhase_ = 0;
    double ratedSearchUntil_ = 0;
    engine::BotProfile foundBot_, ratedBot_;
    chess::Color foundColor_ = chess::White;
    bool gameOverDismissed_ = false;
    bool flipped_ = false;
    SettingsTab tab_ = SettingsTab::Pieces;
    Screen settingsReturn_ = Screen::Menu;
    float moveScroll_ = 0;
    size_t lastMoveCount_ = 0;
    int listViewPly_ = -1;
    int shownPly_ = 0;
    size_t shownMoves_ = 0;
    double resignConfirmUntil_ = 0;
    bool lowTimeWarned_[2] = {false, false};
    bool wasOver_ = false;
    bool quit_ = false;

    storage::Profile profile_;
    bool rated_ = false;
    bool recorded_ = false;
    bool hasChange_ = false;
    storage::RatingChange lastChange_;

    std::filesystem::path stockfishExe_;
    analysis::Analyzer analyzer_;
    uci::Engine helperEngine_;
    std::mutex helperMutex_;

    struct Review {
        bool loaded = false;
        std::string startFen;
        std::vector<chess::Move> moves;
        std::vector<std::string> sanEn, sanRu;
        std::string white, black, result;
        int myColor = -1;
        chess::Color firstMover = chess::White;
        int firstMoveNumber = 1;
        bool mistakesSaved = false;
    };
    Review review_;
    int reviewPly_ = 0, reviewShownPly_ = 0, reviewListPly_ = -1;
    float reviewScroll_ = 0;
    bool reviewFlipped_ = false;
    Screen reviewReturn_ = Screen::Menu;
    std::vector<analysis::Eval> evals_;
    std::vector<analysis::MoveReview> grades_;
    analysis::Summary summary_;
    int gradesVersion_ = -1;
    GradeAnim reviewAnim_, liveAnim_;
    std::vector<analysis::MoveReview> liveGrades_;
    int liveVersion_ = -1;
    bool retry_ = false;
    int retryMove_ = 0;
    chess::Position retryPos_;
    bool retryMoved_ = false, retryShowAnswer_ = false;
    int retryState_ = 0;
    std::string retryMsg_;
    std::future<analysis::Eval> retryFuture_;
    std::string shotFile_;

    const engine::BotProfile* bot_ = nullptr;
    chess::Color humanColor_ = chess::White;
    engine::Searcher searcher_;
    std::future<engine::BotMove> botFuture_;
    std::atomic<bool> botStop_{false};
    size_t botForMoves_ = 0;
    double botReadyAt_ = 0;
    int lastBotScore_ = 0;
    bool autoPlay_ = false;

    std::vector<training::Puzzle> puzzles_;
    std::future<std::vector<training::Puzzle>> puzzlesLoading_;
    training::PuzzleRun run_;
    PuzzleMode pmode_ = PuzzleMode::Rated;
    PuzzleState puzzleState_ = PuzzleState::Empty;
    std::string puzzleTheme_ = "fork";
    std::unordered_set<std::string> puzzleDone_;
    std::filesystem::path morePuzzlesFile_;
    training::ImportProgress importProgress_;
    std::future<std::vector<training::Puzzle>> importFuture_;
    std::future<std::string> updateCheck_;
    std::future<bool> updateDownload_;
    training::ImportProgress updateProgress_;
    std::string updateTag_;
    bool updateChecked_ = false;
    double puzzleTimer_ = 0, stormEnd_ = 0;
    int puzzleHint_ = 0, puzzleDelta_ = 0, stormScore_ = 0, stormErrors_ = 0;
    bool puzzleFailed_ = false;

    std::vector<training::Section> lessons_;
    std::unordered_set<std::string> lessonsDone_;
    std::vector<storage::PlanItem> plan_;
    int planDay_ = -1;
    int profileTab_ = 0;
    int lessonSec_ = 0, lessonIdx_ = -1, lessonStep_ = 0;
    float lessonScroll_ = 0;

    int egIdx_ = -1, egMistake_ = 0;
    GameSession eg_;
    chess::Color egColor_ = chess::White;
    std::vector<analysis::Eval> egEvals_;
    std::future<analysis::Eval> egFuture_;
    training::Verdict egVerdict_ = training::Verdict::Continue;
    std::string egMsg_;
    bool egPromoted_ = false, egShowMistake_ = false;
    std::unordered_set<std::string> egDone_;

    bool coachOn_ = false;
    engine::Searcher checkSearcher_, hintSearcher_;
    int coachPly_ = -1, hintForPly_ = -1, hintLevel_ = 0;
    bool showThreats_ = false, hintReady_ = false, hintMate_ = false;
    chess::Move hintMove_, pendingMove_;
    std::future<engine::Result> hintFuture_;
    std::string warning_;
    std::mt19937 rng_{std::random_device{}()};
};

}
