#pragma once
#include "core/types.h"

#include <raylib.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct NSVGimage;

namespace ui {
class PieceSet {
public:
    PieceSet(std::string id, std::string desc, std::filesystem::path dir);
    ~PieceSet();
    PieceSet(const PieceSet&) = delete;
    PieceSet& operator=(const PieceSet&) = delete;

    const std::string& id() const { return id_; }
    const std::string& desc() const { return desc_; }

    void draw(chess::Piece p, Rectangle dst, Color tint = WHITE);
    Image renderImage(chess::Piece p, int px);

private:
    struct Raster {
        Texture2D tex[16]{};
    };
    bool parse();
    Raster& rasterFor(int px);

    std::string id_, desc_;
    std::filesystem::path dir_;
    bool parsed_ = false, parseOk_ = false;
    NSVGimage* svg_[16]{};
    std::map<int, Raster> rasters_;
};

class PieceLibrary {
public:
    void scan(const std::filesystem::path& piecesDir);
    void unloadAll();
    const std::vector<std::unique_ptr<PieceSet>>& sets() const { return sets_; }
    PieceSet& get(const std::string& id);

private:
    std::vector<std::unique_ptr<PieceSet>> sets_;
};

}
