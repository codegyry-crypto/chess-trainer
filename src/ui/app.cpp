#include "ui/app.h"

#include "core/notation.h"
#include "core/pgn.h"
#include "ui/sound.h"
#include "rating/elo.h"
#include "ui/titlebar.h"
#include "ui/ui.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>

namespace ui {
namespace fs = std::filesystem;
using chess::Move;
using chess::Position;

namespace {
constexpr float M = 24;
constexpr float PanelW = 360;
constexpr float BarH = 52;

Position previewPosition() {
    Position p;
    p.setFEN("r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R b KQkq - 0 5");
    p.makeMove(p.findLegal(chess::parseSquare("d7"), chess::parseSquare("d6")));
    return p;
}

void drawMiniBoard(Rectangle r, const Position& p, PieceSet& set, const Theme& t, bool lastMove = true) {
    const float c = std::floor(r.width / 8);
    for (int sq = 0; sq < 64; ++sq) {
        const int f = chess::fileOf(sq), rk = chess::rankOf(sq);
        const Rectangle cr{r.x + f * c, r.y + (7 - rk) * c, c, c};
        DrawRectangleRec(cr, (f + rk) % 2 ? t.light : t.dark);
        const Move last = p.lastMove();
        if (lastMove && !last.isNone() && (sq == last.from || sq == last.to)) DrawRectangleRec(cr, t.lastMove);
        set.draw(p.at(sq), cr);
    }
}

std::string formatClock(double sec) {
    if (sec < 0) sec = 0;
    char buf[32];
    if (sec < 10) {
        std::snprintf(buf, sizeof buf, "0:%04.1f", sec);
    } else {
        const int s = int(std::ceil(sec));
        if (s >= 3600) std::snprintf(buf, sizeof buf, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
        else std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
    }
    return buf;
}

int pieceValue(chess::PieceType t) {
    switch (t) {
    case chess::Pawn: return 1;
    case chess::Knight:
    case chess::Bishop: return 3;
    case chess::Rook: return 5;
    case chess::Queen: return 9;
    default: return 0;
    }
}

fs::path utf8Path(const char* s) { return fs::path(reinterpret_cast<const char8_t*>(s)); }

std::string readCredit(const fs::path& licenseFile, const std::string& setId) {
    std::ifstream in(licenseFile);
    std::string line, author, license;
    while (std::getline(in, line)) {
        if (line.rfind("Автор: ", 0) == 0) author = line.substr(std::strlen("Автор: "));
        if (line.rfind("Лицензия: ", 0) == 0) license = line.substr(std::strlen("Лицензия: "));
    }
    const size_t paren = license.find(" (");
    if (paren != std::string::npos) license.resize(paren);
    if (author.empty()) return setId;
    return setId + " — " + author + ", " + license;
}

void drawLevelBadge(Vector2 c, float rad, int elo, bool unknown) {
    DrawCircleV(c, rad, ::Color{22, 22, 27, 255});
    const float r0 = rad * 0.74f, r1 = rad * 0.88f;
    DrawRing(c, r0, r1, 135, 405, 48, ::Color{58, 58, 66, 255});
    const Rectangle box{c.x - rad, c.y - rad, 2 * rad, 2 * rad};
    if (unknown) {
        textCentered("?", box, rad * 0.95f, ::Color{170, 170, 178, 255}, true);
        return;
    }
    const rating::Level& L = rating::levelFor(elo);
    DrawRing(c, r0, r1, 135, 135 + 270.0f * L.number / 10.0f, 48, hex(L.color));
    textCentered(std::to_string(L.number), box, rad * 0.9f, hex(L.color), true);
}

void drawProgressBar(Rectangle r, float k, ::Color fill) {
    roundRect(r, r.height / 2, mix(theme().bg, theme().text, 0.08f));
    if (k > 0) roundRect({r.x, r.y, std::max(r.height, r.width * k), r.height}, r.height / 2, fill);
}

std::FILE*& logFile() {
    static std::FILE* f = nullptr;
    return f;
}

}

int App::run(int argc, char** argv) {
    std::string shotScreen, shotTheme, shotPieces;
    bool shotMax = false;
    int shotFrames = 20;
    for (int i = 1; i + 1 < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot")) shotFile_ = argv[++i];
        else if (!std::strcmp(argv[i], "--screen")) shotScreen = argv[++i];
        else if (!std::strcmp(argv[i], "--theme")) shotTheme = argv[++i];
        else if (!std::strcmp(argv[i], "--pieces")) shotPieces = argv[++i];
        else if (!std::strcmp(argv[i], "--max")) shotMax = argv[++i][0] == '1';
        else if (!std::strcmp(argv[i], "--frames")) shotFrames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--log")) logFile() = std::fopen(argv[++i], "w");
    }
    if (logFile())
        SetTraceLogCallback([](int, const char* fmt, va_list args) {
            std::vfprintf(logFile(), fmt, args);
            std::fputc('\n', logFile());
            std::fflush(logFile());
        });

    settings_.load();
    if (!shotTheme.empty()) settings_.theme = shotTheme;
    if (!shotPieces.empty()) settings_.pieceSet = shotPieces;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Шахматный тренер");
    {
        const float k = scale();
        SetWindowMinSize(int(1000 * k), int(680 * k));
        SetWindowSize(int(1280 * k), int(800 * k));
        const int mon = GetCurrentMonitor();
        SetWindowPosition(std::max(0, (GetMonitorWidth(mon) - int(1280 * k)) / 2),
                          std::max(0, (GetMonitorHeight(mon) - int(800 * k)) / 2));
    }
    if (shotFile_.empty() || shotMax) MaximizeWindow();
    SetExitKey(KEY_NULL);

    const fs::path appDir = exeDir();
    std::error_code cwdError;
    fs::current_path(appDir, cwdError);
    // raylib opens files through ANSI fopen, so a relative path keeps it working in folders with Cyrillic names
    assetsDir_ = "assets";
#ifdef CHESS_SOURCE_ASSETS
    if (!fs::exists(assetsDir_)) assetsDir_ = utf8Path(CHESS_SOURCE_ASSETS);
#endif

    ui::init();
    {
        std::string err;
        fs::path dbFile = appDataDir() / "profile.db";
        if (!shotFile_.empty()) {
            dbFile = fs::temp_directory_path() / "chess_trainer_shot_profile.db";
            std::error_code ec;
            fs::remove(dbFile, ec);
        }
        if (!profile_.open(dbFile, &err)) toast("Профиль не открылся: " + err);
        for (auto& id : profile_.donePuzzles()) puzzleDone_.insert(std::move(id));
        morePuzzlesFile_ = shotFile_.empty() ? appDataDir() / "puzzles_more.csv"
                                             : fs::temp_directory_path() / "chess_trainer_shot_more.csv";
        if (!shotFile_.empty()) {
            std::error_code ec;
            fs::remove(morePuzzlesFile_, ec);
        }
    }
    sound::init(assetsDir_ / "sounds");
    pieces_.scan(assetsDir_ / "pieces");
    lessons_ = training::loadLessons(assetsDir_ / "lessons" / "lessons.txt");
    for (const auto& sec : lessons_)
        for (const auto& l : sec.lessons)
            if (!profile_.value("lesson:" + l.id).empty()) lessonsDone_.insert(l.id);
    for (const auto& e : training::endgames())
        if (!profile_.value("endgame:" + e.id).empty()) egDone_.insert(e.id);
    puzzlesLoading_ = std::async(std::launch::async, [base = assetsDir_ / "puzzles" / "puzzles.csv", more = morePuzzlesFile_] {
        auto all = training::loadPuzzles(base);
        auto extra = training::loadPuzzles(more);
        all.insert(all.end(), std::make_move_iterator(extra.begin()), std::make_move_iterator(extra.end()));
        std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.rating < b.rating; });
        return all;
    });
    stockfishExe_ = uci::findStockfish(appDir / "stockfish");
#ifdef CHESS_SOURCE_ASSETS
    if (stockfishExe_.empty()) stockfishExe_ = uci::findStockfish(utf8Path(CHESS_SOURCE_ASSETS) / ".." / "third_party" / "stockfish");
#endif
    for (const auto& s : pieces_.sets())
        credits_.push_back(readCredit(assetsDir_ / "pieces" / s->id() / "LICENSE.txt", s->id()));
    for (const char* snd : {"wood", "marble", "sfx", "piano", "futuristic", "nes", "lisp"})
        if (fs::exists(assetsDir_ / "sounds" / snd / "LICENSE.txt"))
            credits_.push_back("звуки " + readCredit(assetsDir_ / "sounds" / snd / "LICENSE.txt", snd));
    credits_.push_back("щелчки кнопок — cabled_mess (Freesound), CC0; звук ошибки — Kenney, CC0");
    applySettings();

    const std::u8string iconPath = (assetsDir_ / "icon" / "app.png").u8string();
    Image icon = LoadImage(reinterpret_cast<const char*>(iconPath.c_str()));
    if (!icon.data) icon = pieces_.get("cburnett").renderImage(chess::WKnight, 64);
    SetWindowIcon(icon);
    UnloadImage(icon);

    if (!shotScreen.empty()) setupShot(shotScreen);

    int frames = 0;
    while (!WindowShouldClose() && !quit_) {
        frame();
        if (!shotFile_.empty() && !importFuture_.valid() && ++frames == shotFrames) {
            const int w = GetRenderWidth(), h = GetRenderHeight();
            Image im{rlReadScreenPixels(w, h), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            ExportImage(im, shotFile_.c_str());
            UnloadImage(im);
            break;
        }
    }

    if (shotFile_.empty()) forfeitIfRated();
    cancelBot();
    cancelRetry();
    if (egFuture_.valid()) egFuture_.wait();
    analyzer_.cancel();
    {
        std::lock_guard<std::mutex> lock(helperMutex_);
        helperEngine_.stop();
    }
    pieces_.unloadAll();
    ui::shutdown();
    importProgress_.cancel = true;
    updateProgress_.cancel = true;
    sound::shutdown();
    CloseWindow();
    return 0;
}

void App::applySettings() {
    setTheme(withBoardPalette(themeById(settings_.theme), settings_.boardPalette));
    sound::setEnabled(settings_.soundOn);
    sound::setUiEnabled(settings_.uiSounds);
    sound::setPack(settings_.soundPack);
    sound::setVolume(settings_.volume);
    const Theme& t = theme();
    auto toRgb = [](::Color c) { return (unsigned(c.r) << 16) | (unsigned(c.g) << 8) | c.b; };
    setTitleBarColors(GetWindowHandle(), toRgb(t.bg), toRgb(t.sub), toRgb(t.bg), t.bg.r + t.bg.g + t.bg.b < 384);
    if (shotFile_.empty()) settings_.save();
}

bool App::boardFlipped() const {
    if (settings_.autoFlip && !bot_) return session_.position().sideToMove() == chess::Black;
    return flipped_;
}

void App::frame() {
    updateBot();
    pollPuzzleImport();
    if (autoPlay_ && screen_ == Screen::Puzzles && puzzleState_ == PuzzleState::Play) puzzleMove(run_.expected());
    if (autoPlay_ && bot_ && !session_.over() && humanToMove()) {
        const auto legal = session_.position().legalMoves();
        playUserMove(legal[rng_() % legal.size()]);
    }
    if (screen_ == Screen::Game) {
        session_.tick(GetFrameTime());
        for (chess::Color c : {chess::White, chess::Black}) {
            if (session_.clocksRunning() && session_.clock(c) < 20 && !lowTimeWarned_[c] &&
                session_.timeControl().baseSec >= 60) {
                lowTimeWarned_[c] = true;
                sound::play(sound::Id::LowTime);
            }
        }
        if (session_.over() && !wasOver_) {
            sound::play(session_.result() == "1/2-1/2" ? sound::Id::Draw : sound::Id::Checkmate);
            recordFinishedGame();
        }
        wasOver_ = session_.over();
    }

    BeginDrawing();
    beginScaled();
    ClearBackground(theme().bg);
    beginFrame();

    setInputEnabled(!newGameOpen_ && !ratedOpen_);
    if (screen_ != Screen::Menu) menuPuzzle_ = false;
    switch (screen_) {
    case Screen::Menu: drawMenu(); break;
    case Screen::Game: drawGame(); break;
    case Screen::Settings: drawSettings(); break;
    case Screen::Profile: drawProfile(); break;
    case Screen::Analysis: drawAnalysis(); break;
    case Screen::Puzzles: drawPuzzles(); break;
    case Screen::Lessons: drawLessons(); break;
    case Screen::Endgames: drawEndgames(); break;
    case Screen::Plan: drawPlan(); break;
    }
    setInputEnabled(true);
    if (newGameOpen_) drawNewGameModal();
    if (ratedOpen_) drawRatedModal();

    drawToast();
    endFrame();
    endScaled();
    EndDrawing();
}

void App::drawMenu() {
    const Theme& t = theme();
    const float W = (float)screenW(), H = (float)screenH();
    const float left = 56, top = 56;
    const float colW = std::min(600.0f, W * 0.5f - left);

    text("Шахматный тренер", {left, top}, 40, t.text, true);
    if (!drawUpdateBanner({left, top + 48, colW, 44})) text("Играй, учись и поднимай рейтинг", {left, top + 54}, 19, t.sub);

    struct Item {
        const char* title;
        const char* desc;
        bool ready;
    };
    const Item items[] = {
        {"Играть", "Тренировочная партия без рейтинга", true},  {"Уроки", "Курс от основ до эндшпиля", true},
        {"Задачи", "Тактика и твои ошибки", true},     {"Эндшпили", "Доиграй позицию против Stockfish", true},
        {"Анализ", "Разбор партий Stockfish'ом", true},     {"Дебюты", "Репертуар с повторением", false},
        {"План дня", "Тренировка на сегодня", true},
    };
    const float gap = 14, cardW = (colW - gap) / 2, cardH = 104;
    float y = top + 120;
    for (int i = 0; i < 7; ++i) {
        const Rectangle r{left + (i % 2) * (cardW + gap), y + (i / 2) * (cardH + gap), i == 6 ? colW : cardW, cardH};
        const bool hot = items[i].ready && hovered(r);
        roundRect(r, 12, hot ? mix(t.panel, t.text, 0.06f) : t.panel);
        if (i == 0) roundRectLines(r, 12, 2, t.accent);
        text(items[i].title, {r.x + 20, r.y + 22}, 23, items[i].ready ? t.text : t.sub, true);
        if (i == 6) {
            const auto& plan = todayPlan();
            int done = 0;
            for (const auto& it : plan) done += it.complete();
            const int streak = storage::liveStreak(profile_.streak(), storage::Profile::today());
            const bool all = storage::planComplete(plan);
            text(all ? "Всё выполнено — так держать!"
                     : "Выполнено " + std::to_string(done) + " из " + std::to_string(plan.size()) + " · около " +
                           std::to_string(storage::planMinutes(plan)) + " минут",
                 {r.x + 20, r.y + 58}, 16, all ? t.good : t.sub);
            drawProgressBar({r.x + 20, r.y + 86, r.width - 40, 6}, plan.empty() ? 0 : float(done) / plan.size(),
                            all ? t.good : t.accent);
            streakFlame({r.x + r.width - 20, r.y + 22}, streak, 20);
        } else {
            text(items[i].desc, {r.x + 20, r.y + 58}, 16, t.sub);
        }
        if (!items[i].ready) badge({r.x + r.width - 70, r.y + 18}, "скоро", t.panel2, t.sub, 13);
        if (hot) setHandCursor();
        if (items[i].ready && clicked(r) && i == 0) newGameOpen_ = true;
        if (items[i].ready && clicked(r) && i == 1) {
            lessonIdx_ = -1;
            screen_ = Screen::Lessons;
        }
        if (items[i].ready && clicked(r) && i == 2) openPuzzles(PuzzleMode::Rated);
        if (items[i].ready && clicked(r) && i == 3) {
            egIdx_ = -1;
            screen_ = Screen::Endgames;
        }
        if (items[i].ready && clicked(r) && i == 4) openLastReview(Screen::Menu);
        if (items[i].ready && clicked(r) && i == 6) screen_ = Screen::Plan;
    }
    y += 4 * (cardH + gap) + 10;
    if (button({left, y, 240, 46}, "Рейтинговая игра", Style::Primary, true, 18)) openRated();
    if (button({left + 252, y, 160, 46}, "Настройки")) {
        settingsReturn_ = Screen::Menu;
        screen_ = Screen::Settings;
    }
    if (button({left + 424, y, 110, 46}, "Выход", Style::Ghost)) quit_ = true;
    text("Соперника подбирает система, партия идёт в рейтинг", {left, y + 56}, 14, t.sub);

    const float rx = left + colW + 56;
    const float rw = W - rx - 56;
    const float boardSize = std::floor(std::min(rw, H - top - 56 - 110 - 56) / 8) * 8;
    if (boardSize >= 200) {
        if (!menuPuzzle_ && puzzles_.empty() && puzzlesLoading_.valid() &&
            puzzlesLoading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            puzzles_ = puzzlesLoading_.get();
        if (!menuPuzzle_ && !puzzles_.empty()) {
            pmode_ = PuzzleMode::Rated;
            nextPuzzle();
            menuPuzzle_ = true;
        }
        const Rectangle br{rx + (rw - boardSize) / 2, top, boardSize, boardSize};
        roundRect({br.x - 8, br.y - 8, br.width + 16, br.height + 16}, 12, t.panel);
        const bool live = menuPuzzle_ && puzzleState_ != PuzzleState::Empty;
        if (live) {
            puzzleBoard(br);
            const float cy = br.y + boardSize + 20;
            std::string label = std::string(run_.playerColor() == chess::White ? "Задача: ход белых" : "Задача: ход чёрных") +
                                " · " + std::to_string(run_.puzzle().rating);
            ::Color lc = t.text;
            if (puzzleState_ == PuzzleState::Solved) label = puzzleFailed_ ? "Решение показано" : "Решено!", lc = t.good;
            if (puzzleState_ == PuzzleState::Wrong) label = "Не тот ход — попробуй ещё", lc = t.danger;
            if (puzzleState_ == PuzzleState::Reply) label = "Верно! Продолжай", lc = t.good;
            if (puzzleDelta_) label += puzzleDelta_ > 0 ? "  +" + std::to_string(puzzleDelta_) : "  " + std::to_string(puzzleDelta_);
            text(label, {br.x, cy + 10}, 18, lc, true);
            const Rectangle nb{br.x + boardSize - 130, cy, 130, 40};
            if (puzzleState_ == PuzzleState::Solved) {
                if (button(nb, "Следующая", Style::Primary, true, 15)) nextPuzzle();
            } else if (button(nb, "Решение", Style::Ghost, puzzleState_ == PuzzleState::Play || puzzleState_ == PuzzleState::Wrong, 15)) {
                showPuzzleSolution();
            }
        } else {
            static const Position demo = previewPosition();
            drawMiniBoard(br, demo, pieces_.get(settings_.pieceSet), t);
        }

        const Rectangle pr{br.x - 8, br.y + boardSize + 76, boardSize + 16, 84};
        const bool hot = hovered(pr);
        roundRect(pr, 12, hot ? mix(t.panel, t.text, 0.05f) : t.panel);
        if (hot) setHandCursor();
        const int elo = profile_.rating();
        const bool cal = !profile_.calibrated();
        drawLevelBadge({pr.x + 44, pr.y + 42}, 28, elo, cal);
        if (cal) {
            text("Калибровка", {pr.x + 86, pr.y + 14}, 19, t.text, true);
            text("Калибровка " + std::to_string(profile_.calibrationPlayed()) + " из " +
                     std::to_string(rating::CalibrationGames) + " — играй рейтинговые партии",
                 {pr.x + 86, pr.y + 44}, 15, t.sub);
        } else {
            const rating::Level& L = rating::levelFor(elo);
            text(std::to_string(elo) + " Elo", {pr.x + 86, pr.y + 12}, 21, t.text, true);
            text("Уровень " + std::to_string(L.number), {pr.x + 86 + measure(std::to_string(elo) + " Elo", 21, true).x + 12,
                                                        pr.y + 16}, 16, hex(L.color), true);
            drawProgressBar({pr.x + 86, pr.y + 50, pr.width - 86 - 110, 8}, rating::levelProgress(elo), hex(L.color));
        }
        text("Профиль ›", {pr.x + pr.width - 96, pr.y + 32}, 16, t.sub);
        if (clicked(pr)) screen_ = Screen::Profile;
    }
}

void App::drawNewGameModal() {
    const Theme& t = theme();
    const float W = (float)screenW(), H = (float)screenH();
    DrawRectangle(0, 0, (int)W, (int)H, Color{0, 0, 0, 140});
    const bool vsBot = settings_.vsBot;
    const float mw = 900, mh = vsBot ? 640.0f : 360.0f;
    const Rectangle r{std::floor((W - mw) / 2), std::floor((H - mh) / 2), mw, mh};
    const float x0 = r.x + 24, inner = mw - 48;
    card(r, 16);
    float y = r.y + 24;
    text("Новая партия", {x0, y}, 28, t.text, true);
    y += 54;

    text("Соперник", {x0, y}, 15, t.sub);
    y += 24;
    if (chip({x0, y, 200, 44}, "Бот", vsBot)) settings_.vsBot = true;
    if (chip({x0 + 210, y, 200, 44}, "Друг рядом", !vsBot)) settings_.vsBot = false;
    toggle({x0 + 430, y, 210, 44}, "Тренер", settings_.coachMode);
    y += 60;

    if (vsBot) {
        text("Выбери бота", {x0, y}, 15, t.sub);
        y += 24;
        const auto& list = engine::bots();
        const float gap = 10, cw = (inner - 3 * gap) / 4, ch = 80;
        for (size_t i = 0; i < list.size(); ++i) {
            const engine::BotProfile& b = list[i];
            const Rectangle c{x0 + (i % 4) * (cw + gap), y + (i / 4) * (ch + gap), cw, ch};
            const bool sel = settings_.botId == b.id;
            const bool hot = hovered(c);
            roundRect(c, 10, sel ? fade(t.accent, 0.16f) : (hot ? mix(t.panel2, t.text, 0.06f) : t.panel2));
            if (sel) roundRectLines(c, 10, 2, t.accent);
            if (hot) setHandCursor();
            const ::Color av = hex(b.color);
            DrawCircleV({c.x + 30, c.y + 30}, 18, av);
            textCentered(b.name.substr(0, 2), {c.x + 12, c.y + 12, 36, 36}, 18, WHITE, true);
            text(b.name, {c.x + 58, c.y + 12}, 17, t.text, true);
            text(std::to_string(b.rating), {c.x + 58, c.y + 34}, 14, t.sub);
            textWrapped(b.style, {c.x + 14, c.y + 56, c.width - 24, 30}, 13, t.sub);
            if (b.stockfishElo > 0 && stockfishExe_.empty())
                text("нужен Stockfish", {c.x + c.width - 112, c.y + 12}, 12, t.danger);
            if (clicked(c)) settings_.botId = b.id;
        }
        y += float((list.size() + 3) / 4) * (ch + gap) + 6;
    }

    const float colorW = vsBot ? 3 * 92.0f + 2 * 8.0f : 0.0f;
    if (vsBot) {
        text("Играю", {x0, y}, 15, t.sub);
        const char* names[3] = {"Белыми", "Случайно", "Чёрными"};
        for (int i = 0; i < 3; ++i)
            if (chip({x0 + i * 100.0f, y + 24, 92, 44}, names[i], settings_.colorChoice == i, true, 15))
                settings_.colorChoice = i;
    }
    const float tx = x0 + (vsBot ? colorW + 24 : 0);
    text("Контроль времени", {tx, y}, 15, t.sub);
    const auto& tcs = timeControls();
    settings_.tcIndex = std::clamp(settings_.tcIndex, 0, int(tcs.size()) - 1);
    const float tw = (x0 + inner - tx - 6 * 8) / 7;
    for (size_t i = 0; i < tcs.size(); ++i) {
        const Rectangle cr{tx + i * (tw + 8), y + 24, tw, 44};
        if (chip(cr, tcs[i].label, int(i) == settings_.tcIndex, true, i == 0 ? 13.0f : 15.0f))
            settings_.tcIndex = int(i);
    }

    const float by = r.y + r.height - 24 - 48;
    if (screen_ == Screen::Game && rated_ && !session_.over() && session_.moves().size() >= 2)
        text("Текущая рейтинговая партия будет засчитана как поражение", {x0, by + 14}, 15, t.danger);
    else if (vsBot)
        text("Тренировочная партия — в рейтинг не идёт", {x0, by + 14}, 15, t.sub);
    if (button({r.x + r.width - 24 - 200, by, 200, 48}, "Начать", Style::Primary, true, 19) ||
        IsKeyPressed(KEY_ENTER))
        startGame(tcs[settings_.tcIndex]);
    else if (button({r.x + r.width - 24 - 200 - 12 - 130, by, 130, 48}, "Отмена", Style::Ghost) ||
             IsKeyPressed(KEY_ESCAPE))
        newGameOpen_ = false;
}

void App::startGame(const TimeControl& tc, const std::string& fen, const engine::BotProfile* practiceBot,
                    chess::Color practiceColor, bool rated) {
    forfeitIfRated();
    cancelBot();
    bot_ = practiceBot ? practiceBot : settings_.vsBot ? engine::botById(settings_.botId) : nullptr;
    if (bot_) {
        humanColor_ = practiceBot                  ? practiceColor
                      : settings_.colorChoice == 0 ? chess::White
                      : settings_.colorChoice == 2 ? chess::Black
                                                   : (rng_() % 2 ? chess::White : chess::Black);
        session_.start(tc, humanColor_ == chess::White ? "Ты" : bot_->name,
                       humanColor_ == chess::White ? bot_->name : "Ты", fen);
    } else {
        humanColor_ = chess::White;
        session_.start(tc, "Белые", "Чёрные", fen);
    }
    searcher_.clear();
    lastBotScore_ = 0;
    coachOn_ = !practiceBot && settings_.coachMode;
    rated_ = rated;
    pendingMove_ = Move::none();
    recorded_ = false;
    hasChange_ = false;
    board_.reset();
    flipped_ = bot_ && humanColor_ == chess::Black;
    gameOverDismissed_ = false;
    wasOver_ = false;
    lowTimeWarned_[0] = lowTimeWarned_[1] = false;
    lastMoveCount_ = 0;
    moveScroll_ = 0;
    newGameOpen_ = false;
    screen_ = Screen::Game;
    if (shotFile_.empty()) settings_.save();
    sound::play(sound::Id::GameStart);
}

void App::cancelBot() {
    if (!botFuture_.valid()) return;
    botStop_ = true;
    botFuture_.wait();
    botFuture_ = {};
    botStop_ = false;
}

void App::updateBot() {
    if (!bot_) return;
    if (session_.over()) {
        cancelBot();
        return;
    }
    const chess::Color botColor = ~humanColor_;
    if (botFuture_.valid()) {
        if (botFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready || GetTime() < botReadyAt_)
            return;
        const engine::BotMove bm = botFuture_.get();
        if (session_.moves().size() == botForMoves_ && !bm.move.isNone() &&
            session_.position().sideToMove() == botColor) {
            if (!bm.fromBook) lastBotScore_ = bm.score;
            const Position before = session_.position();
            if (settings_.animate) board_.animate(before, bm.move);
            if (session_.play(bm.move)) moveSound(bm.move);
        }
        return;
    }
    if (session_.position().sideToMove() != botColor) return;

    int budget = 0;
    const TimeControl& tc = session_.timeControl();
    if (tc.baseSec > 0) budget = std::max(100, int(session_.clock(botColor) * 1000 / 30 + tc.incSec * 800));

    const Position pos = session_.position();
    const std::vector<Move> moves = session_.moves();
    engine::BotProfile prof = *bot_;
    if (!session_.startFen().empty()) prof.useBook = false;
    const unsigned seed = rng_();
    botForMoves_ = moves.size();
    double minThink = prof.fullSearch ? 0.25 : 0.5 + (rng_() % 900) / 1000.0;
    if (tc.baseSec > 0 && session_.clock(botColor) < 15) minThink = 0.05;
    if (moves.size() < 2) minThink = std::min(minThink, 0.6);
    botReadyAt_ = GetTime() + minThink;
    botStop_ = false;
    const std::filesystem::path sf = stockfishExe_;
    botFuture_ = std::async(std::launch::async, [this, pos, moves, prof, budget, seed, sf, fen = session_.startFen()]() {
        if (prof.stockfishElo > 0 && !sf.empty()) {
            std::lock_guard<std::mutex> lock(helperMutex_);
            bool ok = helperEngine_.running();
            if (!ok) {
                ok = helperEngine_.start(sf);
                if (ok) helperEngine_.setOption("Hash", "32");
            }
            if (ok) {
                helperEngine_.setOption("UCI_LimitStrength", "true");
                helperEngine_.setOption("UCI_Elo", std::to_string(prof.stockfishElo));
                std::vector<std::string> uciMoves;
                for (const Move& m : moves) uciMoves.push_back(chess::toUCI(m));
                const int ms = budget > 0 ? std::clamp(budget, 100, 1500) : 1200;
                const uci::Analysis a = helperEngine_.analyse(fen.empty() ? "startpos" : fen, uciMoves, 0, ms, 1, &botStop_);
                engine::BotMove bm;
                Position p = pos;
                bm.move = chess::parseUCI(p, a.bestmove);
                if (!a.lines.empty()) bm.score = analysis::cpFromScore(a.lines.front().score);
                bm.depth = a.lines.empty() ? 0 : a.lines.front().depth;
                if (!bm.move.isNone() || botStop_) return bm;
            }
        }
        std::mt19937 r(seed);
        return engine::chooseMove(pos, moves, prof, searcher_, r, budget, &botStop_);
    });
}

void App::undoMove() {
    cancelBot();
    if (!session_.canUndo()) return;
    if (rated_ && !session_.over()) return;
    session_.undo();
    if (bot_ && session_.canUndo() && session_.position().sideToMove() != humanColor_) session_.undo();
    liveVersion_ = -1;
    board_.reset();
    gameOverDismissed_ = false;
}

void App::offerDraw() {
    if (!bot_) {
        session_.agreeDraw();
        return;
    }
    const bool accept = lastBotScore_ <= -150 || (std::abs(lastBotScore_) <= 40 && session_.moves().size() >= 60) ||
                        session_.position().isInsufficientMaterial();
    if (accept) session_.agreeDraw();
    toast(accept ? bot_->name + " согласен на ничью" : bot_->name + " отказывается от ничьей");
}

void App::recordFinishedGame() {
    if (!bot_ || recorded_ || !session_.over()) return;
    recorded_ = true;
    storage::GameRecord g;
    g.opponent = bot_->name;
    g.botId = bot_->id;
    g.opponentRating = bot_->rating;
    g.color = humanColor_ == chess::White ? "w" : "b";
    const std::string r = session_.result();
    g.score = r == "1/2-1/2" ? 0.5 : ((r == "1-0") == (humanColor_ == chess::White) ? 1.0 : 0.0);
    g.reason = session_.resultDetail();
    g.timeControl = session_.timeControl().label;
    const bool aborted = session_.moves().size() < 2;
    g.rated = rated_ && !aborted;
    chess::Game pgn = session_.toGame();
    pgn.setTag(humanColor_ == chess::White ? "BlackElo" : "WhiteElo", std::to_string(bot_->rating));
    pgn.setTag(humanColor_ == chess::White ? "WhiteElo" : "BlackElo", std::to_string(profile_.rating()));
    g.pgn = chess::toPGN(pgn);
    if (!profile_.isOpen()) return;
    lastChange_ = profile_.recordGame(g);
    hasChange_ = true;
    progress(g.rated ? "rated" : "game");
    if (rated_ && aborted) toast("Партия прервана в самом начале — в рейтинг не идёт");
}

void App::forfeitIfRated() {
    if (!bot_ || !rated_ || recorded_ || session_.over() || session_.moves().size() < 2) return;
    session_.resign(humanColor_);
    recordFinishedGame();
}

void App::findOpponent() {
    const bool cal = !profile_.calibrated();
    const int target = cal ? rating::calibrationTarget(profile_.calibrationGames()) : profile_.rating();
    foundBot_ = engine::pickOpponent(target, cal, !stockfishExe_.empty(), rng_);
    foundColor_ = rng_() % 2 ? chess::White : chess::Black;
}

void App::openRated() {
    ratedOpen_ = true;
    ratedPhase_ = 0;
}

void App::startRated() {
    forfeitIfRated();
    ratedBot_ = foundBot_;
    ratedOpen_ = false;
    startGame(timeControls()[std::clamp(settings_.tcIndex, 0, int(timeControls().size()) - 1)], {}, &ratedBot_,
              foundColor_, true);
}

void App::drawRatedModal() {
    const Theme& t = theme();
    const float W = (float)screenW(), H = (float)screenH();
    DrawRectangle(0, 0, (int)W, (int)H, Color{0, 0, 0, 140});
    const float mw = 640, mh = 380;
    const Rectangle r{std::floor((W - mw) / 2), std::floor((H - mh) / 2), mw, mh};
    const float x0 = r.x + 24, inner = mw - 48;
    card(r, 16);
    text("Рейтинговая игра", {x0, r.y + 24}, 28, t.text, true);
    const std::string status = profile_.calibrated()
                                   ? std::to_string(profile_.rating()) + " Elo · уровень " +
                                         std::to_string(rating::levelFor(profile_.rating()).number)
                                   : "Калибровка: партия " + std::to_string(profile_.calibrationPlayed() + 1) +
                                         " из " + std::to_string(rating::CalibrationGames);
    text(status, {x0, r.y + 64}, 16, t.sub);
    const float by = r.y + r.height - 24 - 48;
    const auto& tcs = timeControls();
    settings_.tcIndex = std::clamp(settings_.tcIndex, 0, int(tcs.size()) - 1);

    if (ratedPhase_ == 0) {
        const float y = r.y + 110;
        text("Контроль времени", {x0, y}, 15, t.sub);
        const float n = float(tcs.size()), tw = (inner - (n - 1) * 8) / n;
        for (size_t i = 0; i < tcs.size(); ++i)
            if (chip({x0 + i * (tw + 8), y + 24, tw, 44}, tcs[i].label, int(i) == settings_.tcIndex, true,
                     i == 0 ? 13.0f : 15.0f))
                settings_.tcIndex = int(i);
        textWrapped("Соперника и цвет выбирает система. Подсказок и отмены хода нет.", {x0, y + 90, inner, 50}, 15,
                    t.sub);
        if (button({r.x + r.width - 24 - 220, by, 220, 48}, "Найти соперника", Style::Primary, true, 18) ||
            IsKeyPressed(KEY_ENTER)) {
            ratedPhase_ = 1;
            ratedSearchUntil_ = GetTime() + 1.5;
        }
    } else if (ratedPhase_ == 1) {
        const std::string dots(size_t(int(GetTime() * 3) % 4), '.');
        textCentered("Поиск соперника" + dots, {r.x, r.y + 150, r.width, 60}, 24, t.text, true);
        if (GetTime() >= ratedSearchUntil_) {
            findOpponent();
            ratedPhase_ = 2;
        }
    } else {
        const engine::BotProfile& b = foundBot_;
        const Rectangle c{x0, r.y + 110, inner, 130};
        roundRect(c, 12, t.panel2);
        DrawCircleV({c.x + 60, c.y + 65}, 38, hex(b.color));
        textCentered(b.name.substr(0, 2), {c.x + 22, c.y + 27, 76, 76}, 30, WHITE, true);
        text(b.name, {c.x + 120, c.y + 18}, 26, t.text, true);
        text(std::to_string(b.rating) + " Elo · " + b.style, {c.x + 120, c.y + 56}, 16, t.sub);
        text(foundColor_ == chess::White ? "Ты играешь белыми" : "Ты играешь чёрными", {c.x + 120, c.y + 88}, 16,
             t.accent, true);
        if (button({r.x + r.width - 24 - 200, by, 200, 48}, "Играть", Style::Primary, true, 19) ||
            IsKeyPressed(KEY_ENTER))
            return startRated();
    }
    if (button({r.x + 24, by, 130, 48}, "Отмена", Style::Ghost) || IsKeyPressed(KEY_ESCAPE)) ratedOpen_ = false;
}

std::string App::resultTitle() const {
    if (!bot_) return session_.resultTitle();
    const std::string r = session_.result();
    if (r == "1/2-1/2") return "Ничья";
    const bool whiteWon = r == "1-0";
    return whiteWon == (humanColor_ == chess::White) ? "Победа!" : "Поражение";
}

void App::moveSound(Move m) {
    if (session_.over()) return;
    if (session_.position().inCheck()) sound::play(sound::Id::Check);
    else sound::play(m.isCapture() ? sound::Id::Capture : sound::Id::Move);
}

void App::playUserMove(Move m) {
    if (coachIntercept(m)) return;
    if (session_.play(m)) moveSound(m);
}

void App::drawGame() {
    const float W = (float)screenW(), H = (float)screenH();
    const float gap = 8;
    const float availW = W - 3 * M - PanelW;
    const float availH = H - 2 * M - 2 * (BarH + gap);
    const float boardSize = std::floor(std::min(availW, availH) / 8) * 8;
    const float bx = M + std::floor((availW - boardSize) / 2);
    const float by = M + BarH + gap;
    const Rectangle boardRect{bx, by, boardSize, boardSize};

    handleGameKeys();
    const bool flipped = boardFlipped();
    const chess::Color topSide = flipped ? chess::White : chess::Black;

    BoardOptions opt;
    opt.flipped = flipped;
    opt.coords = settings_.showCoords;
    opt.legalMoves = settings_.showLegalMoves;
    opt.lastMove = settings_.highlightLastMove;
    opt.animate = settings_.animate;

    const bool overlay = session_.over() && !gameOverDismissed_;
    PieceSet& set = pieces_.get(settings_.pieceSet);

    std::vector<BoardView::Arrow> hints;
    if (!session_.viewingLatest() && settings_.showMoveArrow && session_.viewPly() > 0) {
        const Move played = session_.moves()[session_.viewPly() - 1];
        hints.push_back({played.from, played.to, ::Color{255, 170, 0, 190}});
    }
    coachUpdate();
    for (const auto& a : coachArrows()) hints.push_back(a);
    board_.setHintArrows(std::move(hints));
    updateLiveGrades();
    const int gradeIdx = session_.viewPly() - 1;
    const analysis::Grade shownGrade = liveGrade(gradeIdx);
    if (shownGrade != analysis::Grade::None) opt.lastMoveTint = fade(gradeColor(shownGrade), 0.5f);
    auto drawGrade = [&] {
        if (gradeIdx >= 0 &&
            drawSquareGrade(board_.squareRect(session_.moves()[gradeIdx].to), gradeIdx, shownGrade, liveAnim_) &&
            shownGrade == analysis::Grade::Brilliant)
            sound::play(sound::Id::Brilliant);
    };

    if (session_.viewingLatest()) {
        Position& pos = session_.position();
        const bool interactive = !session_.over() && !overlay && humanToMove() && pendingMove_.isNone();
        const Move m = board_.update(boardRect, pos, interactive, set, opt);
        if (!m.isNone()) playUserMove(m);
        else drawGrade();
        drawPlayerBar({bx, M, boardSize, BarH}, topSide, pos);
        drawPlayerBar({bx, by + boardSize + gap, boardSize, BarH}, ~topSide, pos);
    } else {
        Position shown = session_.viewPosition();
        board_.update(boardRect, shown, false, set, opt);
        drawGrade();
        if (inputEnabled() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mouse(), boardRect))
            session_.setViewPly(int(session_.moves().size()));
        drawPlayerBar({bx, M, boardSize, BarH}, topSide, shown);
        drawPlayerBar({bx, by + boardSize + gap, boardSize, BarH}, ~topSide, shown);
    }
    if (overlay) drawGameOver(boardRect);
    if (!pendingMove_.isNone()) drawBlunderWarning(boardRect);

    const float px = W - M - PanelW;
    float py = M;
    drawStatusCard({px, py, PanelW, 84});
    py += 84 + 12;
    if (coachOn_) {
        drawCoachCard({px, py, PanelW, 172});
        py += 172 + 12;
    }

    const float actionsH = 4 * 46 + 3 * 8;
    const Rectangle movesRect{px, py, PanelW, H - M - py - actionsH - 12};
    drawMoveList(movesRect);
    py = movesRect.y + movesRect.height + 12;

    const float bw = (PanelW - 8) / 2;
    auto cell = [&](int i) { return Rectangle{px + (i % 2) * (bw + 8), py + (i / 2) * 54.0f, bw, 46}; };
    if (button(cell(0), "Отменить ход", Style::Secondary, session_.canUndo() && !(rated_ && !session_.over())))
        undoMove();
    if (button(cell(1), "Перевернуть")) flipped_ = !flipped_;
    if (button(cell(2), "Ничья", Style::Secondary, !session_.over() && !session_.moves().empty())) {
        offerDraw();
    }
    const bool confirming = GetTime() < resignConfirmUntil_;
    if (button(cell(3), confirming ? "Точно сдаться?" : "Сдаться", Style::Secondary, !session_.over())) {
        if (confirming) {
            session_.resign(bot_ ? humanColor_ : session_.position().sideToMove());
            resignConfirmUntil_ = 0;
        } else {
            resignConfirmUntil_ = GetTime() + 3;
        }
    }
    const bool canReview = !session_.moves().empty() && (session_.over() || !bot_);
    if (button(cell(4), "Разбор партии", Style::Secondary, canReview))
        openReview(session_.toGame(), bot_ ? (humanColor_ == chess::White ? 0 : 1) : -1, Screen::Game);
    if (button(cell(5), "Новая партия")) newGameOpen_ = true;
    if (button(cell(6), "Сохранить PGN", Style::Ghost, !session_.moves().empty())) savePGN();
    if (button(cell(7), "В меню", Style::Ghost)) screen_ = Screen::Menu;

    animateNavigation();
}

void App::animateNavigation() {
    const int ply = session_.viewPly();
    const size_t count = session_.moves().size();
    if (count == shownMoves_ && settings_.animate) {
        if (ply == shownPly_ + 1) board_.animate(session_.positionAt(shownPly_), session_.moves()[shownPly_]);
        else if (ply == shownPly_ - 1) board_.animateBack(session_.positionAt(ply), session_.moves()[ply]);
    }
    shownPly_ = ply;
    shownMoves_ = count;
}

void App::handleGameKeys() {
    if (!inputEnabled()) return;
    const int last = int(session_.moves().size());
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) session_.setViewPly(session_.viewPly() - 1);
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) session_.setViewPly(session_.viewPly() + 1);
    if (IsKeyPressed(KEY_HOME) || IsKeyPressed(KEY_UP)) session_.setViewPly(0);
    if (IsKeyPressed(KEY_END) || IsKeyPressed(KEY_DOWN)) session_.setViewPly(last);
    if (IsKeyPressed(KEY_F)) flipped_ = !flipped_;
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_Z) && session_.canUndo())
        undoMove();
}

void App::drawPlayerBar(Rectangle r, chess::Color side, const Position& shown) {
    const Theme& t = theme();
    const Vector2 c{r.x + 22, r.y + r.height / 2};
    const bool isBot = bot_ && side != humanColor_;
    if (isBot) {
        const ::Color av = hex(bot_->color);
        DrawCircleV(c, 18, av);
        textCentered(bot_->name.substr(0, 2), {c.x - 18, c.y - 18, 36, 36}, 18, WHITE, true);
    } else if (bot_) {
        drawLevelBadge(c, 19, profile_.rating(), !profile_.calibrated());
    } else {
        DrawCircleV(c, 17, side == chess::White ? Color{240, 240, 240, 255} : Color{30, 30, 34, 255});
        DrawCircleLinesV(c, 17, t.border);
    }
    const std::string& name = session_.playerName(side);
    text(name, {r.x + 50, r.y + 4}, 19, t.text, true);
    if (bot_ && !isBot)
        text(profile_.calibrated() ? std::to_string(profile_.rating()) : "калибровка",
             {r.x + 50 + measure(name, 19, true).x + 10, r.y + 7}, 15, t.sub);
    if (isBot) {
        float nx = r.x + 50 + measure(name, 19, true).x + 10;
        text(std::to_string(bot_->rating), {nx, r.y + 7}, 15, t.sub);
        nx += measure(std::to_string(bot_->rating), 15).x + 12;
        if (botThinking()) {
            const int dots = int(GetTime() * 3) % 4;
            text("думает" + std::string(size_t(dots), '.'), {nx, r.y + 7}, 15, t.accent);
        }
    }

    static const int initial[7] = {0, 8, 2, 2, 2, 1, 0};
    int count[2][7] = {};
    int material[2] = {0, 0};
    for (int s = 0; s < 64; ++s) {
        const chess::Piece p = shown.at(s);
        if (p == chess::NoPiece) continue;
        ++count[chess::colorOf(p)][chess::typeOf(p)];
        material[chess::colorOf(p)] += pieceValue(chess::typeOf(p));
    }
    PieceSet& set = pieces_.get(settings_.pieceSet);
    float x = r.x + 50;
    const chess::Color opp = ~side;
    for (chess::PieceType pt : {chess::Pawn, chess::Knight, chess::Bishop, chess::Rook, chess::Queen}) {
        const int taken = std::max(0, initial[pt] - count[opp][pt]);
        for (int k = 0; k < taken; ++k) {
            set.draw(chess::makePiece(opp, pt), {x, r.y + 27, 20, 20});
            x += 11;
        }
        if (taken) x += 8;
    }
    const int diff = material[side] - material[opp];
    if (diff > 0) text("+" + std::to_string(diff), {x + 4, r.y + 28}, 15, t.sub, true);

    const Rectangle cr{r.x + r.width - 132, r.y + 4, 132, r.height - 8};
    const bool hasClock = session_.timeControl().baseSec > 0;
    const bool active = hasClock && session_.clocksRunning() && session_.position().sideToMove() == side;
    const double secs = session_.clock(side);
    Color bg = active ? t.clockActive : t.clock, fg = active ? t.clockActiveText : t.text;
    if (hasClock && secs < 20 && session_.clocksRunning() && active) bg = t.danger, fg = WHITE;
    if (!hasClock) fg = t.sub;
    roundRect(cr, 8, bg);
    textCentered(hasClock ? formatClock(secs) : "—", cr, 26, fg, true);
}

void App::drawStatusCard(Rectangle r) {
    const Theme& t = theme();
    card(r);
    std::string title, detail;
    Color titleColor = t.text;
    if (session_.over()) {
        title = resultTitle();
        detail = session_.resultDetail();
        titleColor = t.accent;
    } else {
        Position& p = session_.position();
        if (bot_) title = humanToMove() ? "Твой ход" : bot_->name + " думает…";
        else title = p.sideToMove() == chess::White ? "Ход белых" : "Ход чёрных";
        if (p.inCheck()) {
            title = "Шах! " + title;
            titleColor = t.danger;
        }
        detail = session_.timeControl().label;
        if (bot_) detail = std::string(rated_ ? "Рейтинговая" : "Тренировочная") + " · " + detail;
    }
    if (!session_.viewingLatest())
        detail = "Просмотр: ход " + std::to_string((session_.viewPly() + 1) / 2) + ". Нажми → или End";
    text(title, {r.x + 20, r.y + 14}, 23, titleColor, true);
    text(detail, {r.x + 20, r.y + 50}, 16, t.sub);
}

void App::updateLiveGrades() {
    const bool same = analyzer_.moves() == session_.moves() && analyzer_.fen() == session_.startFen();
    if (settings_.liveGrades && !same && !stockfishExe_.empty())
        analyzer_.start(stockfishExe_, session_.startFen(), session_.moves(), 250, 1);
    if (liveVersion_ == analyzer_.version() && liveGrades_.size() == session_.moves().size()) return;
    liveVersion_ = analyzer_.version();
    const bool ready = analyzer_.moves() == session_.moves() && analyzer_.fen() == session_.startFen();
    liveGrades_ = analysis::reviewMoves(ready ? analyzer_.snapshot() : std::vector<analysis::Eval>{},
                                        session_.moves(), session_.startFen());
}

analysis::Grade App::liveGrade(int index) const {
    if (!settings_.liveGrades || index < 0 || index >= int(liveGrades_.size())) return analysis::Grade::None;
    return liveGrades_[index].grade;
}

void App::drawMoveList(Rectangle r) {
    const Theme& t = theme();
    card(r);
    text("Ходы", {r.x + 20, r.y + 14}, 16, t.sub);
    const analysis::Grade hg = liveGrade(session_.viewPly() - 1);
    if (hg != analysis::Grade::None) {
        const std::string name = analysis::gradeName(hg);
        const float nx = r.x + r.width - 20 - measure(name, 16, true).x;
        drawGradeIcon({nx - 14, r.y + 24}, 9, hg);
        text(name, {nx, r.y + 14}, 16, gradeColor(hg), true);
    }

    const auto& sans = settings_.russianNotation ? session_.sanRu() : session_.sanEn();
    const float rowH = 32, top = r.y + 44, navH = 52;
    const Rectangle area{r.x + 8, top, r.width - 16, r.height - (top - r.y) - navH};
    const int rows = int((sans.size() + 1) / 2);
    const float contentH = rows * rowH;
    const float maxScroll = std::max(0.0f, contentH - area.height);

    if (sans.size() != lastMoveCount_) {
        lastMoveCount_ = sans.size();
        moveScroll_ = maxScroll;
    }
    if (session_.viewPly() != listViewPly_) {
        listViewPly_ = session_.viewPly();
        if (listViewPly_ > 0) {
            const float rowTop = ((listViewPly_ - 1) / 2) * rowH;
            if (rowTop < moveScroll_) moveScroll_ = rowTop;
            if (rowTop + rowH > moveScroll_ + area.height) moveScroll_ = rowTop + rowH - area.height;
        }
    }
    if (hovered(area)) moveScroll_ -= GetMouseWheelMove() * rowH * 2;
    moveScroll_ = std::clamp(moveScroll_, 0.0f, maxScroll);

    beginClip(area);
    const float colW = (area.width - 52) / 2;
    for (int row = 0; row < rows; ++row) {
        const float y = area.y + row * rowH - moveScroll_;
        if (y + rowH < area.y || y > area.y + area.height) continue;
        if (row % 2) DrawRectangleRec({area.x, y, area.width, rowH}, fade(t.panel2, 0.35f));
        text(std::to_string(row + 1) + ".", {area.x + 12, y + 6}, 16, t.sub);
        for (int k = 0; k < 2; ++k) {
            const size_t i = size_t(row * 2 + k);
            if (i >= sans.size()) break;
            const Rectangle cell{area.x + 52 + k * colW, y + 2, colW - 6, rowH - 4};
            const bool current = session_.viewPly() == int(i + 1);
            const bool hot = hovered(cell) && hovered(area);
            if (current) roundRect(cell, 6, t.hl);
            else if (hot) roundRect(cell, 6, t.panel2);
            const analysis::Grade g = liveGrade(int(i));
            const bool bad = isBadGrade(g);
            text(sans[i] + analysis::gradeSymbol(g), {cell.x + 10, cell.y + 4}, 17,
                 bad ? gradeColor(g) : (current ? t.hlText : t.text), current || bad);
            if (g != analysis::Grade::None) drawGradeIcon({cell.x + cell.width - 14, cell.y + cell.height / 2}, 8, g);
            if (hot) setHandCursor();
            if (clicked(cell) && hovered(area)) session_.setViewPly(int(i + 1));
        }
    }
    endClip();
    if (sans.empty()) text("Сделай первый ход", {area.x + 12, area.y + 6}, 16, t.sub);

    const float nw = (r.width - 40 - 3 * 6) / 4, ny = r.y + r.height - navH + 6;
    const int last = int(session_.moves().size()), v = session_.viewPly();
    const char* labels[4] = {"«", "‹", "›", "»"};
    const int targets[4] = {0, v - 1, v + 1, last};
    for (int i = 0; i < 4; ++i) {
        const bool en = (i < 2) ? v > 0 : v < last;
        if (button({r.x + 20 + i * (nw + 6), ny, nw, 38}, labels[i], Style::Secondary, en, 22))
            session_.setViewPly(targets[i]);
    }
}

void App::drawGameOver(Rectangle b) {
    const Theme& t = theme();
    DrawRectangleRec(b, Color{0, 0, 0, 110});
    const bool ratingBlock = bot_ && hasChange_;
    const float h = ratingBlock ? 420.0f : 270.0f;
    const Rectangle r{b.x + (b.width - 440) / 2, b.y + (b.height - h) / 2, 440, h};
    card(r, 16);
    roundRectLines(r, 16, 2, t.accent);
    textCentered(resultTitle(), {r.x, r.y + 22, r.width, 40}, 30, t.text, true);
    textCentered(session_.resultDetail(), {r.x, r.y + 64, r.width, 26}, 18, t.sub);
    float y = r.y + 104;
    if (ratingBlock) {
        drawRatingBlock({r.x + 24, y, r.width - 48, 150});
        y += 166;
    } else {
        y += 18;
    }
    const float bw = (r.width - 48 - 20) / 3;
    if (bot_) {
        if (button({r.x + 24, y + 60, r.width - 48, 46}, "Разбор партии со Stockfish", Style::Secondary))
            openReview(session_.toGame(), humanColor_ == chess::White ? 0 : 1, Screen::Game);
        if (rated_) {
            if (button({r.x + 24, y, bw, 50}, "Следующая", Style::Primary)) openRated();
        } else if (button({r.x + 24, y, bw, 50}, "Реванш", Style::Primary)) {
            if (!session_.startFen().empty()) {
                startGame(session_.timeControl(), session_.startFen(), bot_, humanColor_);
            } else {
                settings_.colorChoice = humanColor_ == chess::White ? 2 : 0;
                startGame(session_.timeControl());
            }
        }
        if (button({r.x + 24 + bw + 10, y, bw, 50}, "Новая партия")) newGameOpen_ = true;
        if (button({r.x + 24 + 2 * (bw + 10), y, bw, 50}, "К доске")) gameOverDismissed_ = true;
    } else {
        if (button({r.x + 24, y + 60, r.width - 48, 46}, "Разбор партии со Stockfish", Style::Secondary))
            openReview(session_.toGame(), -1, Screen::Game);
        if (button({r.x + 24, y, 200, 50}, "Новая партия", Style::Primary)) newGameOpen_ = true;
        if (button({r.x + 236, y, 180, 50}, "К доске", Style::Secondary)) gameOverDismissed_ = true;
    }
}

void App::drawRatingBlock(Rectangle r) {
    const Theme& t = theme();
    const storage::RatingChange& ch = lastChange_;
    roundRect(r, 12, t.panel2);
    if (!ch.rated) {
        textCentered("Тренировочная партия — рейтинг не меняется", r, 16, t.sub);
        return;
    }
    const bool unknown = ch.calibrating;
    drawLevelBadge({r.x + 58, r.y + r.height / 2}, 42, ch.after, unknown);
    const float x = r.x + 118;
    if (ch.calibrating) {
        text("Калибровка " + std::to_string(ch.calibrationPlayed) + " из " + std::to_string(rating::CalibrationGames),
             {x, r.y + 18}, 22, t.text, true);
        text("Предварительный рейтинг: " + std::to_string(ch.after), {x, r.y + 54}, 16, t.sub);
        drawProgressBar({x, r.y + 90, r.width - (x - r.x) - 18, 10},
                        float(ch.calibrationPlayed) / rating::CalibrationGames, t.accent);
        text("Уровень откроется после " + std::to_string(rating::CalibrationGames) + " партий", {x, r.y + 112}, 14,
             t.sub);
        return;
    }
    const std::string elo = std::to_string(ch.after);
    text(elo, {x, r.y + 14}, 34, t.text, true);
    if (!ch.calibrationDone) {
        const std::string d = (ch.delta >= 0 ? "+" : "−") + std::to_string(std::abs(ch.delta));
        text(d, {x + measure(elo, 34, true).x + 12, r.y + 24}, 22, ch.delta >= 0 ? t.good : t.danger, true);
    }
    const rating::Level& L = rating::levelFor(ch.after);
    drawProgressBar({x, r.y + 66, r.width - (x - r.x) - 18, 10}, rating::levelProgress(ch.after), hex(L.color));
    std::string line;
    ::Color lc = t.sub;
    if (ch.calibrationDone) {
        line = "Калибровка завершена! Твой уровень " + std::to_string(L.number);
        lc = hex(L.color);
    } else if (ch.levelAfter > ch.levelBefore) {
        line = "Новый уровень: " + std::to_string(ch.levelAfter) + "!";
        lc = hex(L.color);
    } else if (ch.levelAfter < ch.levelBefore) {
        line = "Уровень понизился до " + std::to_string(ch.levelAfter);
        lc = t.danger;
    } else if (L.number < 10) {
        line = "До " + std::to_string(L.number + 1) + " уровня: " + std::to_string(L.to - ch.after) + " очков";
    } else {
        line = "Максимальный уровень";
    }
    text(line, {x, r.y + 88}, 16, lc, true);
    text("Уровень " + std::to_string(L.number) + " · " + std::to_string(L.from) +
             (L.number < 10 ? "–" + std::to_string(L.to - 1) : "+"),
         {x, r.y + 114}, 14, t.sub);
}

void App::savePGN() {
    const fs::path dir = documentsDir() / "games";
    std::error_code ec;
    fs::create_directories(dir, ec);
    char name[64];
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    std::strftime(name, sizeof name, "%Y-%m-%d_%H-%M-%S.pgn", &tm);
    std::ofstream out(dir / name, std::ios::binary);
    chess::Game g = session_.toGame();
    if (bot_) g.setTag(humanColor_ == chess::White ? "BlackElo" : "WhiteElo", std::to_string(bot_->rating));
    out << chess::toPGN(g);
    toast(out ? "Сохранено в Документы\\ChessTrainer\\games" : "Не получилось сохранить партию");
}

void App::drawProfile() {
    const Theme& t = theme();
    if (button({M + 8, M + 4, 120, 42}, "‹ Назад", Style::Ghost) || (inputEnabled() && IsKeyPressed(KEY_ESCAPE)))
        screen_ = Screen::Menu;
    text("Профиль", {M + 16, M + 60}, 30, t.text, true);
    const char* tabs[] = {"Обзор", "Партии", "Достижения"};
    float tx = M + 16 + measure("Профиль", 30, true).x + 32;
    for (int i = 0; i < 3; ++i) {
        const float w = measure(tabs[i], 16).x + 36;
        if (chip({tx, M + 62, w, 38}, tabs[i], profileTab_ == i)) profileTab_ = i;
        tx += w + 8;
    }
    const float top = M + 116;
    if (profileTab_ == 0) drawProfileOverview(top);
    if (profileTab_ == 1) drawProfileGames(top);
    if (profileTab_ == 2) drawProfileAchievements(top);
}

void App::drawProfileOverview(float top) {
    const float W = (float)screenW(), H = (float)screenH();
    const Theme& t = theme();
    const int elo = profile_.rating();
    const bool cal = !profile_.calibrated();
    const rating::Level& L = rating::levelFor(elo);
    const float x0 = M + 16, inner = W - 2 * x0;
    const Rectangle lc{x0, top, 470, 210};
    card(lc);
    drawLevelBadge({lc.x + 92, lc.y + 105}, 70, elo, cal);
    const float tx = lc.x + 186;
    if (cal) {
        text("Калибровка", {tx, lc.y + 30}, 28, t.text, true);
        text("Калибровка " + std::to_string(profile_.calibrationPlayed()) + " из " +
                 std::to_string(rating::CalibrationGames),
             {tx, lc.y + 74}, 17, t.sub);
        drawProgressBar({tx, lc.y + 112, lc.width - (tx - lc.x) - 24, 10},
                        float(rating::CalibrationGames - profile_.calibrationLeft()) / rating::CalibrationGames,
                        t.accent);
        textWrapped("Сыграй рейтинговые партии — система подберёт соперников и определит твой уровень.",
                    {tx, lc.y + 138, lc.width - (tx - lc.x) - 24, 60}, 14, t.sub);
    } else {
        text(std::to_string(elo), {tx, lc.y + 22}, 46, t.text, true);
        text("Elo", {tx + measure(std::to_string(elo), 46, true).x + 10, lc.y + 44}, 18, t.sub);
        text("Уровень " + std::to_string(L.number), {tx, lc.y + 84}, 19, hex(L.color), true);
        drawProgressBar({tx, lc.y + 118, lc.width - (tx - lc.x) - 24, 10}, rating::levelProgress(elo), hex(L.color));
        text(L.number < 10 ? "До " + std::to_string(L.number + 1) + " уровня: " + std::to_string(L.to - elo) + " очков"
                           : "Максимальный уровень",
             {tx, lc.y + 138}, 15, t.sub);
        text("Лучший рейтинг: " + std::to_string(profile_.peak()), {tx, lc.y + 164}, 15, t.sub);
    }


    const float sw = (inner - lc.width - 32) / 2;
    const Rectangle pc{lc.x + lc.width + 16, top, sw, 210};
    card(pc);
    text("Рейтинг задач", {pc.x + 20, pc.y + 16}, 16, t.sub);
    text(std::to_string(profile_.puzzleRating()), {pc.x + 20, pc.y + 44}, 46, t.text, true);
    text("Решено задач: " + std::to_string(profile_.counter("puzzles_solved")), {pc.x + 20, pc.y + 120}, 15, t.sub);
    text("Рекорд «Штурма»: " + std::to_string(profile_.stormBest()), {pc.x + 20, pc.y + 146}, 15, t.sub);

    const storage::Streak sk = profile_.streak();
    const int streak = storage::liveStreak(sk, storage::Profile::today());
    const Rectangle kc{pc.x + sw + 16, top, sw, 210};
    card(kc);
    text("Серия дней", {kc.x + 20, kc.y + 16}, 16, t.sub);
    text(std::to_string(streak), {kc.x + 20, kc.y + 44}, 46, streak ? t.accent : t.text, true);
    text(sk.lastDay == storage::Profile::today() ? "Сегодня уже занимался" : "Позанимайся сегодня, чтобы не прервать",
         {kc.x + 20, kc.y + 120}, 15, t.sub);
    text("Лучшая серия: " + std::to_string(sk.best), {kc.x + 20, kc.y + 146}, 15, t.sub);

    const float row2 = top + 226, hw = (inner - 16) / 2;
    const Rectangle prc{x0, row2, hw, 170};
    card(prc);
    text("Прогресс", {prc.x + 20, prc.y + 16}, 16, t.sub);
    const std::pair<std::string, std::pair<size_t, size_t>> bars[] = {
        {"Уроки", {lessonsDone_.size(), lessonCount()}}, {"Эндшпили", {egDone_.size(), training::endgames().size()}}};
    for (int i = 0; i < 2; ++i) {
        const float y = prc.y + 50 + i * 50;
        const auto [done, total] = bars[i].second;
        const std::string lbl = bars[i].first + ": " + std::to_string(done) + " из " + std::to_string(total);
        const Rectangle hit{prc.x + 20, y, prc.width - 40, 40};
        text(lbl + " ›", {hit.x, y}, 16, hovered(hit) ? t.accent : t.text);
        drawProgressBar({hit.x, y + 26, hit.width, 8}, total ? float(done) / total : 0, done >= total ? t.good : t.accent);
        if (hovered(hit)) setHandCursor();
        if (clicked(hit) && i == 0) lessonIdx_ = -1, screen_ = Screen::Lessons;
        if (clicked(hit) && i == 1) egIdx_ = -1, screen_ = Screen::Endgames;
    }
    text("Планов дня выполнено: " + std::to_string(profile_.counter("plans_done")), {prc.x + 20, prc.y + 142}, 14, t.sub);

    const Rectangle wc{x0 + hw + 16, row2, hw, 170};
    card(wc);
    text("Слабые места", {wc.x + 20, wc.y + 16}, 16, t.sub);
    const auto srs = profile_.srsItems(storage::Profile::today(), false, "");
    const auto weak = storage::weakThemes(srs, training::pickerThemes());
    if (weak.empty()) text("Пока нет — ошибки в задачах появятся здесь", {wc.x + 20, wc.y + 50}, 15, t.sub);
    for (size_t i = 0; i < weak.size() && i < 3; ++i) {
        const float y = wc.y + 46 + i * 26;
        text(training::themeName(weak[i].first), {wc.x + 20, y}, 16, t.text);
        text(std::to_string(weak[i].second), {wc.x + 230, y}, 16, t.danger, true);
    }
    int phases[3] = {};
    for (const auto& it : srs)
        if (it.source == "mine") ++phases[storage::fenPhase(it.fen)];
    const std::string ph = "Ошибки в партиях: дебют " + std::to_string(phases[0]) + " · миттельшпиль " +
                           std::to_string(phases[1]) + " · эндшпиль " + std::to_string(phases[2]);
    text(ph, {wc.x + 20, wc.y + 136}, 14, t.sub);

    top = row2 + 186;
    const auto hist = profile_.ratingHistory();
    const Rectangle gc{x0, top, inner, H - M - top};
    if (gc.height < 140) return;
    card(gc);
    text("Рейтинг", {gc.x + 20, gc.y + 14}, 16, t.sub);
    const Rectangle plot{gc.x + 64, gc.y + 46, gc.width - 92, gc.height - 76};
    if (hist.size() < 2) {
        textCentered("Сыграй пару рейтинговых партий — здесь появится график", plot, 16, t.sub);
    } else {
        int lo = hist[0].second, hi = lo;
        for (const auto& h : hist) lo = std::min(lo, h.second), hi = std::max(hi, h.second);
        lo = (lo - 50) / 50 * 50;
        hi = (hi + 99) / 50 * 50;
        auto yOf = [&](int v) { return plot.y + plot.height - (v - lo) * plot.height / float(hi - lo); };
        for (const rating::Level& lv : rating::levels()) {
            const int a = std::max(lv.from, lo), b2 = std::min(lv.to, hi);
            if (a >= b2) continue;
            DrawRectangleRec({plot.x, yOf(b2), plot.width, yOf(a) - yOf(b2)}, fade(hex(lv.color), 0.07f));
        }
        const int step = (hi - lo) > 400 ? 200 : 100;
        for (int v = (lo + step - 1) / step * step; v <= hi; v += step) {
            DrawLineEx({plot.x, yOf(v)}, {plot.x + plot.width, yOf(v)}, 1, fade(t.border, 0.8f));
            const std::string lbl = std::to_string(v);
            text(lbl, {plot.x - 12 - measure(lbl, 13).x, yOf(v) - 9}, 13, t.sub);
        }
        const float dx = plot.width / float(hist.size() - 1);
        for (size_t i = 1; i < hist.size(); ++i)
            DrawLineEx({plot.x + (i - 1) * dx, yOf(hist[i - 1].second)}, {plot.x + i * dx, yOf(hist[i].second)}, 3,
                       t.accent);
        for (size_t i = 0; i < hist.size(); ++i) {
            const Vector2 pt{plot.x + i * dx, yOf(hist[i].second)};
            DrawCircleV(pt, 4.5f, t.accent);
            if (CheckCollisionPointCircle(mouse(), pt, 10)) {
                const std::string tip = std::to_string(hist[i].second) + " · " + hist[i].first;
                const Vector2 m = measure(tip, 14);
                roundRect({pt.x - m.x / 2 - 8, pt.y - 38, m.x + 16, 26}, 6, t.panel2);
                text(tip, {pt.x - m.x / 2, pt.y - 34}, 14, t.text);
            }
        }
        text("партии →", {plot.x + plot.width - 60, plot.y + plot.height + 8}, 13, t.sub);
    }

}

void App::drawProfileGames(float top) {
    const float W = (float)screenW(), H = (float)screenH();
    const Theme& t = theme();
    const int elo = profile_.rating();
    const bool cal = !profile_.calibrated();
    const rating::Level& L = rating::levelFor(elo);
    const float x0 = M + 16, inner = W - 2 * x0;
    const storage::Stats st = profile_.stats();
    const Rectangle sc{x0, top, inner, 210};
    card(sc);
    text("Статистика партий с ботами", {sc.x + 20, sc.y + 16}, 16, t.sub);
    const std::string streak = st.streak > 0   ? std::to_string(st.streak) + " W"
                               : st.streak < 0 ? std::to_string(-st.streak) + " L"
                                               : "—";
    const std::pair<std::string, std::string> tiles[] = {
        {"Партий", std::to_string(st.games)}, {"Побед", std::to_string(st.wins)},
        {"Ничьих", std::to_string(st.draws)}, {"Поражений", std::to_string(st.losses)},
        {"Серия", streak},                    {"Лучшая серия", std::to_string(st.bestStreak)}};
    const float tw = (sc.width - 40 - 5 * 10) / 6;
    for (int i = 0; i < 6; ++i) {
        const Rectangle tr{sc.x + 20 + i * (tw + 10), sc.y + 50, tw, 84};
        roundRect(tr, 10, t.panel2);
        text(tiles[i].first, {tr.x + 12, tr.y + 12}, 13, t.sub);
        ::Color vc = t.text;
        if (i == 1) vc = t.good;
        if (i == 3) vc = t.danger;
        if (i == 4) vc = st.streak > 0 ? t.good : (st.streak < 0 ? t.danger : t.sub);
        text(tiles[i].second, {tr.x + 12, tr.y + 38}, 28, vc, true);
    }
    const float lw = (sc.width - 40 - 9 * 4) / 10;
    for (int i = 0; i < 10; ++i) {
        const rating::Level& lv = rating::levels()[i];
        const Rectangle seg{sc.x + 20 + i * (lw + 4), sc.y + 152, lw, 10};
        const bool here = !cal && lv.number == L.number;
        roundRect(seg, 5, here ? hex(lv.color) : fade(hex(lv.color), 0.3f));
        const std::string lbl = std::to_string(lv.number);
        text(lbl, {seg.x + (lw - measure(lbl, 13, true).x) / 2, seg.y + 16}, 13, here ? t.text : t.sub, true);
    }

    const Rectangle hc{x0, top + 226, inner, H - M - top - 226};
    if (hc.height < 80) return;
    card(hc);
    text("Последние партии — нажми, чтобы разобрать", {hc.x + 20, hc.y + 14}, 16, t.sub);
    const int rows = int((hc.height - 50) / 34);
    const auto games = profile_.recentGames(std::max(0, rows));
    if (games.empty()) text("Пока пусто — сыграй с ботом!", {hc.x + 20, hc.y + 48}, 16, t.sub);
    float y = hc.y + 44;
    for (const storage::GameRecord& g : games) {
        const Rectangle row{hc.x + 8, y, hc.width - 16, 34};
        if (hovered(row)) {
            DrawRectangleRec(row, fade(t.accent, 0.12f));
            setHandCursor();
        }
        if (clicked(row)) {
            std::string err;
            const auto parsed = chess::parsePGN(g.pgn, &err);
            if (!parsed.empty() && !parsed[0].moves.empty())
                openReview(parsed[0], g.color == "w" ? 0 : 1, Screen::Profile);
            else toast("У этой партии нет записи ходов");
        }
        if (int((y - hc.y) / 34) % 2 == 0) DrawRectangleRec({hc.x + 8, y, hc.width - 16, 34}, fade(t.panel2, 0.35f));
        text(g.date, {hc.x + 20, y + 8}, 14, t.sub);
        DrawCircleV({hc.x + 178, y + 17}, 7, g.color == "w" ? Color{240, 240, 240, 255} : Color{30, 30, 34, 255});
        text(g.opponent + " (" + std::to_string(g.opponentRating) + ")", {hc.x + 196, y + 7}, 16, t.text);
        const bool win = g.score > 0.75, loss = g.score < 0.25;
        text(win ? "Победа" : loss ? "Поражение" : "Ничья", {hc.x + 420, y + 7}, 16,
             win ? t.good : loss ? t.danger : t.sub, true);
        text(g.reason, {hc.x + 540, y + 8}, 14, t.sub);
        text(g.timeControl, {hc.x + 760, y + 8}, 14, t.sub);
        if (g.rated) {
            const int d = g.ratingAfter - g.ratingBefore;
            const std::string ds = (d >= 0 ? "+" : "−") + std::to_string(std::abs(d));
            text(std::to_string(g.ratingAfter), {hc.x + hc.width - 150, y + 7}, 16, t.text, true);
            text(ds, {hc.x + hc.width - 90, y + 7}, 16, d >= 0 ? t.good : t.danger, true);
        } else {
            text("без рейтинга", {hc.x + hc.width - 150, y + 8}, 14, t.sub);
        }
        y += 34;
    }
}

void App::drawSettings() {
    const Theme& t = theme();
    const float W = (float)screenW(), H = (float)screenH();
    const float navW = 230;

    if (button({M + 8, M + 4, 120, 42}, "‹ Назад", Style::Ghost) || (inputEnabled() && IsKeyPressed(KEY_ESCAPE)))
        screen_ = settingsReturn_;
    text("Настройки", {M + 16, M + 64}, 30, t.text, true);

    struct TabItem {
        SettingsTab tab;
        const char* name;
    };
    const TabItem tabs[] = {{SettingsTab::Look, "Оформление"},
                            {SettingsTab::Pieces, "Фигуры"},
                            {SettingsTab::Game, "Игра"},
                            {SettingsTab::Sound, "Звук"},
                            {SettingsTab::About, "О программе"}};
    float y = M + 120;
    for (const TabItem& ti : tabs) {
        const Rectangle r{M + 8, y, navW - 16, 46};
        const bool sel = tab_ == ti.tab;
        if (sel) roundRect(r, 8, t.panel2);
        else if (hovered(r)) roundRect(r, 8, fade(t.panel2, 0.5f)), setHandCursor();
        if (sel) DrawRectangleRec({r.x, r.y + 10, 3, r.height - 20}, t.accent);
        text(ti.name, {r.x + 18, r.y + 12}, 18, sel ? t.text : t.sub, sel);
        if (clicked(r)) tab_ = ti.tab;
        y += 52;
    }

    const Rectangle content{M + navW + 16, M + 4, W - (M + navW + 16) - M - 16, H - 2 * M - 8};
    switch (tab_) {
    case SettingsTab::Look: drawSettingsLook(content); break;
    case SettingsTab::Pieces: drawSettingsPieces(content); break;
    case SettingsTab::Game: drawSettingsGame(content); break;
    case SettingsTab::Sound: drawSettingsSound(content); break;
    case SettingsTab::About: drawSettingsAbout(content); break;
    }
}

void App::drawSettingsLook(Rectangle r) {
    const Theme& cur = theme();
    text("Тема", {r.x, r.y + 8}, 24, cur.text, true);
    const float gap = 16;
    const float cw = std::min(300.0f, (r.width - 2 * gap) / 3);
    const float ch = 236;
    float y = r.y + 52;
    for (size_t i = 0; i < themes().size(); ++i) {
        const Theme& th = themes()[i];
        const Rectangle c{r.x + i * (cw + gap), y, cw, ch};
        const bool sel = settings_.theme == th.id;
        const bool hot = hovered(c);
        card(c);
        if (sel) roundRectLines(c, 12, 3, cur.accent);
        else if (hot) roundRectLines(c, 12, 1, cur.sub);
        if (hot) setHandCursor();

        const Rectangle pv{c.x + 14, c.y + 14, c.width - 28, 128};
        roundRect(pv, 8, th.bg);
        const float sq = 26;
        for (int k = 0; k < 16; ++k)
            DrawRectangleRec({pv.x + 12 + (k % 4) * sq, pv.y + 12 + (k / 4) * sq, sq, sq},
                             (k % 4 + k / 4) % 2 ? th.dark : th.light);
        const Rectangle side{pv.x + 12 + 4 * sq + 12, pv.y + 12, pv.width - (4 * sq + 36), 4 * sq};
        roundRect(side, 6, th.panel);
        roundRect({side.x + 8, side.y + 10, side.width - 16, 10}, 4, th.panel2);
        roundRect({side.x + 8, side.y + 28, (side.width - 16) * 0.6f, 10}, 4, th.panel2);
        roundRect({side.x + 8, side.y + side.height - 30, side.width - 16, 22}, 5, th.accent);

        text(th.name, {c.x + 16, c.y + 154}, 20, cur.text, true);
        textWrapped(th.desc, {c.x + 16, c.y + 184, c.width - 32, 50}, 15, cur.sub);
        if (clicked(c) && !sel) {
            settings_.theme = th.id;
            applySettings();
        }
    }

    y += ch + 36;
    text("Цвет доски", {r.x, y}, 24, cur.text, true);
    y += 44;
    const auto& pals = boardPalettes();
    const float pw = 170, ph = 56;
    const int perRow = std::max(1, int((r.width + 12) / (pw + 12)));
    for (size_t i = 0; i < pals.size(); ++i) {
        const Rectangle c{r.x + (i % perRow) * (pw + 12), y + (i / perRow) * (ph + 12), pw, ph};
        const bool sel = settings_.boardPalette == pals[i].id;
        const Theme& base = themeById(settings_.theme);
        const Color l = pals[i].id == "theme" ? base.light : pals[i].light;
        const Color d = pals[i].id == "theme" ? base.dark : pals[i].dark;
        roundRect(c, 10, cur.panel);
        if (sel) roundRectLines(c, 10, 2, cur.accent);
        if (hovered(c)) setHandCursor();
        DrawRectangleRec({c.x + 12, c.y + 12, 16, 16}, l);
        DrawRectangleRec({c.x + 28, c.y + 12, 16, 16}, d);
        DrawRectangleRec({c.x + 12, c.y + 28, 16, 16}, d);
        DrawRectangleRec({c.x + 28, c.y + 28, 16, 16}, l);
        text(pals[i].name, {c.x + 56, c.y + 17}, 17, cur.text);
        if (clicked(c) && !sel) {
            settings_.boardPalette = pals[i].id;
            applySettings();
        }
    }
}

void App::drawSettingsPieces(Rectangle r) {
    const Theme& t = theme();
    text("Фигуры", {r.x, r.y + 8}, 24, t.text, true);
    text("Нажми на набор — он сразу встанет на доску", {r.x, r.y + 42}, 16, t.sub);

    const float top = r.y + 80;
    const float boardSize = std::floor(std::min(r.height - 80 - 70, r.width * 0.48f) / 8) * 8;
    static const Position demo = previewPosition();
    PieceSet& current = pieces_.get(settings_.pieceSet);
    const Rectangle br{r.x, top, boardSize, boardSize};
    roundRect({br.x - 6, br.y - 6, br.width + 12, br.height + 12}, 10, t.panel);
    drawMiniBoard(br, demo, current, t);
    text(current.id(), {br.x, br.y + boardSize + 18}, 21, t.text, true);
    text(current.desc(), {br.x, br.y + boardSize + 46}, 16, t.sub);

    const float gx = r.x + boardSize + 32, gw = r.x + r.width - gx;
    const float cw = 178, chh = 84, gap = 10;
    const int cols = std::max(1, int((gw + gap) / (cw + gap)));
    const auto& sets = pieces_.sets();
    for (size_t i = 0; i < sets.size(); ++i) {
        PieceSet& s = *sets[i];
        const Rectangle c{gx + (i % cols) * (cw + gap), top + (i / cols) * (chh + gap), cw, chh};
        const bool sel = s.id() == current.id();
        const bool hot = hovered(c);
        roundRect(c, 10, hot && !sel ? mix(t.panel, t.text, 0.05f) : t.panel);
        if (sel) roundRectLines(c, 10, 2, t.accent);
        if (hot) setHandCursor();
        const chess::Piece row[6] = {chess::WKing, chess::WQueen, chess::WKnight,
                                     chess::BKing, chess::BQueen, chess::BKnight};
        for (int k = 0; k < 6; ++k) s.draw(row[k], {c.x + 8 + k * 27.0f, c.y + 8, 28, 28});
        text(s.id(), {c.x + 12, c.y + 50}, 16, sel ? t.text : t.sub, sel);
        if (clicked(c) && !sel) {
            settings_.pieceSet = s.id();
            applySettings();
        }
    }
}

void App::drawSettingsGame(Rectangle r) {
    const Theme& t = theme();
    text("Игра", {r.x, r.y + 8}, 24, t.text, true);
    const float w = std::min(620.0f, r.width);
    float y = r.y + 56;
    struct Opt {
        const char* label;
        bool* value;
    };
    const Opt opts[] = {
        {"Русская нотация (Кf3 вместо Nf3)", &settings_.russianNotation},
        {"Координаты на доске", &settings_.showCoords},
        {"Показывать возможные ходы", &settings_.showLegalMoves},
        {"Подсвечивать последний ход", &settings_.highlightLastMove},
        {"Анимация ходов", &settings_.animate},
        {"Стрелка хода при просмотре партии", &settings_.showMoveArrow},
        {"Игра вдвоём: поворачивать доску к тому, чей ход", &settings_.autoFlip},
        {"Оценки ходов во время партии", &settings_.liveGrades},
        {"Проверять обновления", &settings_.checkUpdates},
    };
    for (const Opt& o : opts) {
        if (toggle({r.x, y, w, 50}, o.label, *o.value)) applySettings();
        y += 56;
    }
    y += 20;
    text("Подсказка: правой кнопкой мыши рисуются стрелки и кружки (Shift — красные, Alt — синие).",
         {r.x, y}, 15, t.sub);
    text("Стрелки ← → листают ходы, F — перевернуть доску, Ctrl+Z — отменить ход.", {r.x, y + 26}, 15, t.sub);
}

void App::drawSettingsSound(Rectangle r) {
    const Theme& t = theme();
    text("Звук", {r.x, r.y + 8}, 24, t.text, true);
    const float w = std::min(620.0f, r.width);
    float y = r.y + 56;
    if (toggle({r.x, y, w, 50}, "Звуки партии", settings_.soundOn)) applySettings();
    y += 56;
    if (toggle({r.x, y, w, 50}, "Щелчки кнопок", settings_.uiSounds)) applySettings();
    y += 72;
    text("Громкость", {r.x + 12, y}, 17, t.text);
    if (slider({r.x + 140, y + 2, w - 300, 20}, settings_.volume)) sound::setVolume(settings_.volume);
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) applySettings();
    text(std::to_string(int(std::lround(settings_.volume * 100))) + "%", {r.x + w - 140, y}, 17, t.sub);
    y += 60;

    text("Набор звуков", {r.x, y}, 24, t.text, true);
    y += 44;
    const auto& packs = sound::packs();
    const float cw = 210, ch = 78, gap = 12;
    const int cols = std::max(1, int((r.width + gap) / (cw + gap)));
    for (size_t i = 0; i < packs.size(); ++i) {
        const Rectangle c{r.x + (i % cols) * (cw + gap), y + (i / cols) * (ch + gap), cw, ch};
        const bool sel = settings_.soundPack == packs[i].id;
        roundRect(c, 10, hovered(c) && !sel ? mix(t.panel, t.text, 0.05f) : t.panel);
        if (sel) roundRectLines(c, 10, 2, t.accent);
        if (hovered(c)) setHandCursor();
        text(packs[i].name, {c.x + 16, c.y + 14}, 19, t.text, true);
        text(packs[i].desc, {c.x + 16, c.y + 44}, 14, t.sub);
        if (clicked(c)) {
            settings_.soundPack = packs[i].id;
            applySettings();
            sound::play(sound::Id::Move);
        }
    }
    y += ((packs.size() + cols - 1) / cols) * (ch + gap) + 16;

    text("Послушать:", {r.x + 12, y + 11}, 16, t.sub);
    float x = r.x + 120;
    const std::pair<const char*, sound::Id> tests[] = {
        {"Ход", sound::Id::Move},         {"Взятие", sound::Id::Capture}, {"Шах", sound::Id::Check},
        {"Мат", sound::Id::Checkmate},    {"Ничья", sound::Id::Draw},     {"Мало времени", sound::Id::LowTime}};
    for (const auto& [name, id] : tests) {
        const float bw = measure(name, 16).x + 32;
        if (button({x, y, bw, 42}, name, Style::Secondary, settings_.soundOn, 16)) sound::play(id);
        x += bw + 8;
    }
}

void App::drawSettingsAbout(Rectangle r) {
    const Theme& t = theme();
    text("Шахматный тренер", {r.x, r.y + 8}, 24, t.text, true);
    text("Версия " CHESS_VERSION, {r.x, r.y + 46}, 16, t.sub);
    text("Программа бесплатная. Фигуры и звуки принадлежат их авторам:", {r.x, r.y + 90}, 17, t.text);
    float y = r.y + 126;
    for (const std::string& c : credits_) {
        text("•  " + c, {r.x + 8, y}, 15, t.sub);
        y += 26;
    }
    text("Файл лицензии каждого набора: assets/pieces/<набор>/LICENSE.txt", {r.x, y + 14}, 15, t.sub);
}

}
