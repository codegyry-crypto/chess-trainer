#include "core/notation.h"

#include <vector>

namespace chess {
const PieceLetters EnglishLetters = {"", "", "N", "B", "R", "Q", "K"};
const PieceLetters RussianLetters = {"", "", "К", "С", "Л", "Ф", "Кр"};

std::string toUCI(Move m) {
    if (m.isNone()) return "0000";
    std::string s = squareName(m.from) + squareName(m.to);
    if (m.isPromotion()) s += " pnbrqk"[m.promo];
    return s;
}

Move parseUCI(Position& pos, std::string_view text) {
    if (text.size() < 4 || text.size() > 5) return Move::none();
    const Square from = parseSquare(text.substr(0, 2));
    const Square to = parseSquare(text.substr(2, 2));
    if (from == NoSquare || to == NoSquare) return Move::none();
    PieceType promo = NoPieceType;
    if (text.size() == 5) {
        switch (text[4]) {
        case 'n': case 'N': promo = Knight; break;
        case 'b': case 'B': promo = Bishop; break;
        case 'r': case 'R': promo = Rook; break;
        case 'q': case 'Q': promo = Queen; break;
        default: return Move::none();
        }
    }
    return pos.findLegal(from, to, promo);
}

namespace {
std::string sanBody(const Move& m, const std::vector<Move>& legal, Position& pos, const PieceLetters& L) {
    if (m.isCastle()) return fileOf(m.to) == 6 ? "O-O" : "O-O-O";

    const PieceType pt = typeOf(pos.at(m.from));
    std::string s;
    if (pt == Pawn) {
        if (m.isCapture()) s += char('a' + fileOf(m.from)), s += 'x';
        s += squareName(m.to);
        if (m.isPromotion()) s += "=" + L[m.promo];
        return s;
    }

    s += L[pt];
    bool ambiguous = false, sameFile = false, sameRank = false;
    for (const Move& o : legal) {
        if (o.to != m.to || o.from == m.from || typeOf(pos.at(o.from)) != pt) continue;
        ambiguous = true;
        if (fileOf(o.from) == fileOf(m.from)) sameFile = true;
        if (rankOf(o.from) == rankOf(m.from)) sameRank = true;
    }
    if (ambiguous) {
        if (!sameFile) s += char('a' + fileOf(m.from));
        else if (!sameRank) s += char('1' + rankOf(m.from));
        else s += squareName(m.from);
    }
    if (m.isCapture()) s += 'x';
    s += squareName(m.to);
    return s;
}

std::string checkSuffix(Position& pos, const Move& m) {
    pos.makeMove(m);
    std::string suffix;
    if (pos.inCheck()) suffix = pos.legalMoves().empty() ? "#" : "+";
    pos.unmakeMove();
    return suffix;
}

std::string normalize(std::string_view text) {
    std::string s;
    for (char c : text) {
        if (c == '+' || c == '#' || c == '!' || c == '?' || c == '=' || c == ' ') continue;
        s += c == '0' ? 'O' : c;
    }
    if (s.size() > 4 && s.compare(s.size() - 4, 4, "e.p.") == 0) s.resize(s.size() - 4);
    return s;
}

}

std::string toSAN(Position& pos, Move m, const PieceLetters& letters) {
    const std::vector<Move> legal = pos.legalMoves();
    for (const Move& l : legal) {
        if (l == m) {
            return sanBody(l, legal, pos, letters) + checkSuffix(pos, l);
        }
    }
    return "";
}

Move parseSAN(Position& pos, std::string_view text) {
    const std::string want = normalize(text);
    if (want.empty()) return Move::none();
    const std::vector<Move> legal = pos.legalMoves();
    for (const PieceLetters* L : {&EnglishLetters, &RussianLetters}) {
        for (const Move& m : legal) {
            if (normalize(sanBody(m, legal, pos, *L)) == want) return m;
        }
    }
    return Move::none();
}

}
