#pragma once
#include <filesystem>
#include <string>

namespace ui {
struct Settings {
    std::string theme = "night";
    std::string boardPalette = "theme";
    std::string pieceSet = "cburnett";
    bool russianNotation = true;
    bool squareNotation = false;
    bool showCoords = true;
    bool showLegalMoves = true;
    bool highlightLastMove = true;
    bool animate = true;
    bool showMoveArrow = true;
    bool autoFlip = false;
    bool soundOn = true;
    bool vsBot = true;
    std::string botId = "amateur";
    int colorChoice = 1;
    int tcIndex = 4;
    bool coachMode = false;
    bool liveGrades = true;
    bool checkUpdates = true;
    bool uiSounds = true;
    std::string soundPack = "marble";
    float volume = 0.7f;

    void load();
    void save() const;
};

std::filesystem::path appDataDir();
std::filesystem::path exeDir();
std::filesystem::path documentsDir();

}
