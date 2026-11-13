#include "training/puzzles.h"

#include "core/notation.h"

#include <zstd.h>

#include <cstdlib>
#include <fstream>
#include <memory>

namespace training {

namespace {
bool valid(const Puzzle& p) {
    chess::Position pos;
    if (p.moves.size() < 2 || !pos.setFEN(p.fen)) return false;
    for (const std::string& m : p.moves) {
        const chess::Move mv = chess::parseUCI(pos, m);
        if (mv.isNone()) return false;
        pos.makeMove(mv);
    }
    return true;
}

bool take(const std::string& line, const std::unordered_set<std::string>& have, Puzzle& out) {
    std::string f[8];
    size_t start = 0;
    for (std::string& x : f) {
        const size_t end = line.find(',', start);
        if (end == std::string::npos && &x != &f[7]) return false;
        x = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end + 1;
    }
    const int rating = std::atoi(f[3].c_str()), dev = std::atoi(f[4].c_str()), pop = std::atoi(f[5].c_str()),
              plays = std::atoi(f[6].c_str());
    if (dev > 90 || pop < 85 || plays < 300 || rating < 400 || rating > 2900 || have.count(f[0])) return false;
    out = parsePuzzle(f[0], f[1], f[2], rating, f[7]);
    return valid(out);
}
}

bool importPuzzles(const std::filesystem::path& zst, const std::filesystem::path& csv,
                   const std::unordered_set<std::string>& have, size_t limit, std::vector<Puzzle>& out,
                   ImportProgress& p) {
    std::ifstream in(zst, std::ios::binary);
    std::ofstream file(csv, std::ios::app);
    if (!in || !file) return false;
    std::error_code ec;
    p.total = int64_t(std::filesystem::file_size(zst, ec));
    p.done = 0;
    std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> ctx(ZSTD_createDCtx(), ZSTD_freeDCtx);
    std::vector<char> inBuf(ZSTD_DStreamInSize()), outBuf(ZSTD_DStreamOutSize());
    std::string text;
    while (!p.cancel && out.size() < limit && (in.read(inBuf.data(), inBuf.size()) || in.gcount() > 0)) {
        p.done += in.gcount();
        ZSTD_inBuffer input{inBuf.data(), size_t(in.gcount()), 0};
        while (input.pos < input.size && out.size() < limit) {
            ZSTD_outBuffer output{outBuf.data(), outBuf.size(), 0};
            if (ZSTD_isError(ZSTD_decompressStream(ctx.get(), &output, &input))) return false;
            text.append(outBuf.data(), output.pos);
            size_t start = 0;
            for (size_t nl; out.size() < limit && (nl = text.find('\n', start)) != std::string::npos; start = nl + 1) {
                Puzzle pz;
                if (!take(text.substr(start, nl - start), have, pz)) continue;
                file << pz.id << ',' << pz.fen << ',' << joinMoves(pz.moves) << ',' << pz.rating << ','
                     << joinMoves(pz.themes) << '\n';
                out.push_back(std::move(pz));
            }
            text.erase(0, start);
        }
    }
    return bool(file);
}

}
