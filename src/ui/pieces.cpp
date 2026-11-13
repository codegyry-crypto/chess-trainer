#include "ui/pieces.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace ui {
namespace fs = std::filesystem;

namespace {
const char* const FileNames[16] = {nullptr, "wP", "wN", "wB", "wR", "wQ", "wK", nullptr,
                                   nullptr, "bP", "bN", "bB", "bR", "bQ", "bK", nullptr};

const std::pair<const char*, const char*> KnownSets[] = {
    {"cburnett", "Классика, как на Lichess по умолчанию"},
    {"merida", "Турнирная, как в книгах"},
    {"alpha", "Строгая, контрастная"},
    {"california", "Мягкая, современная"},
    {"cardinal", "Изящная, с тонкими линиями"},
    {"maestro", "Гладкая, плотная"},
    {"staunty", "Объёмная, похожа на Стаунтон"},
    {"tatiana", "Рисованная, художественная"},
    {"fantasy", "Сказочная"},
    {"pixel", "Пиксельная, как в ретро-играх"},
    {"letter", "Буквы вместо фигур, для тренировки вслепую"},
    {"chessnut", "Мультяшная, округлая"},
};

bool isPieceDir(const fs::path& dir) {
    for (const char* n : FileNames)
        if (n && !fs::exists(dir / (std::string(n) + ".svg"))) return false;
    return true;
}

}

PieceSet::PieceSet(std::string id, std::string desc, fs::path dir)
    : id_(std::move(id)), desc_(std::move(desc)), dir_(std::move(dir)) {}

PieceSet::~PieceSet() {
    for (auto& [px, r] : rasters_)
        for (Texture2D& t : r.tex)
            if (t.id) UnloadTexture(t);
    for (NSVGimage* img : svg_)
        if (img) nsvgDelete(img);
}

bool PieceSet::parse() {
    if (parsed_) return parseOk_;
    parsed_ = true;
    parseOk_ = true;
    for (int p = 0; p < 16; ++p) {
        if (!FileNames[p]) continue;
        std::ifstream in(dir_ / (std::string(FileNames[p]) + ".svg"), std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string data = ss.str();
        if (data.empty()) {
            parseOk_ = false;
            continue;
        }
        svg_[p] = nsvgParse(data.data(), "px", 96.0f);
        if (!svg_[p]) parseOk_ = false;
    }
    return parseOk_;
}

PieceSet::Raster& PieceSet::rasterFor(int px) {
    auto it = rasters_.find(px);
    if (it != rasters_.end()) return it->second;

    parse();
    Raster r;
    for (int p = 0; p < 16; ++p) {
        if (!svg_[p]) continue;
        Image im = renderImage(chess::Piece(p), px);
        r.tex[p] = LoadTextureFromImage(im);
        UnloadImage(im);
        GenTextureMipmaps(&r.tex[p]);
        SetTextureFilter(r.tex[p], TEXTURE_FILTER_TRILINEAR);
    }

    auto victim = rasters_.begin();
    while (victim != rasters_.end() && (victim->first == 32 || victim->first == 64)) ++victim;
    if (rasters_.size() >= 6 && victim != rasters_.end()) {
        for (Texture2D& t : victim->second.tex)
            if (t.id) UnloadTexture(t);
        rasters_.erase(victim);
    }
    return rasters_.emplace(px, r).first->second;
}

void PieceSet::draw(chess::Piece p, Rectangle dst, Color tint) {
    if (p == chess::NoPiece) return;
    const float dpi = GetWindowScaleDPI().x > 0.5f ? GetWindowScaleDPI().x : 1.0f;
    int px = (int)std::lround(std::min(dst.width, dst.height) * dpi);
    if (px < 64) px = px < 32 ? 32 : 64;
    px = std::min(px, 512);
    Raster& r = rasterFor(px);
    const Texture2D& t = r.tex[p];
    if (!t.id) {
        const char* letters = " PNBRQK  pnbrqk";
        char s[2] = {letters[p], 0};
        DrawText(s, int(dst.x + dst.width * 0.3f), int(dst.y + dst.height * 0.2f), int(dst.height * 0.6f), tint);
        return;
    }
    DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, dst, {0, 0}, 0, tint);
}

Image PieceSet::renderImage(chess::Piece p, int px) {
    parse();
    Image im = GenImageColor(px, px, BLANK);
    NSVGimage* img = svg_[p];
    if (!img) return im;
    const float w = img->width > 0 ? img->width : 45.0f;
    const float h = img->height > 0 ? img->height : 45.0f;
    const float scale = px / std::max(w, h);
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    nsvgRasterize(rast, img, (px - w * scale) / 2, (px - h * scale) / 2, scale, (unsigned char*)im.data, px, px,
                  px * 4);
    nsvgDeleteRasterizer(rast);
    return im;
}

void PieceLibrary::scan(const fs::path& piecesDir) {
    unloadAll();
    std::error_code ec;
    for (const auto& [id, desc] : KnownSets) {
        const fs::path dir = piecesDir / id;
        if (isPieceDir(dir)) sets_.push_back(std::make_unique<PieceSet>(id, desc, dir));
    }
    std::vector<fs::path> extra;
    for (const auto& e : fs::directory_iterator(piecesDir, ec)) {
        if (!e.is_directory()) continue;
        const std::string name = e.path().filename().string();
        bool known = false;
        for (const auto& [id, desc] : KnownSets) known |= name == id;
        if (!known && isPieceDir(e.path())) extra.push_back(e.path());
    }
    std::sort(extra.begin(), extra.end());
    for (const fs::path& p : extra)
        sets_.push_back(std::make_unique<PieceSet>(p.filename().string(), "Свой набор", p));
}

void PieceLibrary::unloadAll() { sets_.clear(); }

PieceSet& PieceLibrary::get(const std::string& id) {
    for (auto& s : sets_)
        if (s->id() == id) return *s;
    if (sets_.empty()) {
        sets_.push_back(std::make_unique<PieceSet>("none", "", fs::path()));
    }
    return *sets_.front();
}

}
