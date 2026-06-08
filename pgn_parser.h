/*
 * Copyright (C) 2026 Ilia Shchetkov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// PGN movetext (SAN) -> list of UCI moves.
//
// Must be included AFTER gameboard.cpp so that ChessFacade, Board, Coordinates,
// Color and Type are visible. Conversion is driven by replaying the game on a
// scratch ChessFacade and, for each SAN token, picking the single legal UCI
// move (from ChessFacade::getAllLegalUCIForCurrentTurn) whose structure
// matches the token.

#ifndef PGN_PARSER_H
#define PGN_PARSER_H

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

// Strip headers ([..]), comments ({..}), variations ((..)), line comments (;),
// NAG glyphs ($n), move numbers and the result token; return ordered SAN tokens.
inline std::vector<std::string> extractSanTokens(const std::string &pgn) {
  std::string flat;
  flat.reserve(pgn.size());
  for (size_t i = 0; i < pgn.size();) {
    char c = pgn[i];
    if (c == '{') { // comment until '}'
      size_t end = pgn.find('}', i);
      i = (end == std::string::npos) ? pgn.size() : end + 1;
    } else if (c == '[') { // header tag until ']'
      size_t end = pgn.find(']', i);
      i = (end == std::string::npos) ? pgn.size() : end + 1;
    } else if (c == ';') { // line comment until newline
      size_t end = pgn.find('\n', i);
      i = (end == std::string::npos) ? pgn.size() : end + 1;
    } else if (c == '(') { // (possibly nested) variation
      int depth = 1;
      i++;
      while (i < pgn.size() && depth > 0) {
        if (pgn[i] == '(')
          depth++;
        else if (pgn[i] == ')')
          depth--;
        i++;
      }
    } else if (c == '$') { // NAG: '$' followed by digits
      i++;
      while (i < pgn.size() && std::isdigit(static_cast<unsigned char>(pgn[i])))
        i++;
    } else {
      flat += c;
      i++;
    }
  }

  std::vector<std::string> tokens;
  std::string tok;
  auto flush = [&]() {
    if (tok.empty())
      return;
    // Strip a leading move number "12." / "12..." if present.
    size_t p = 0;
    while (p < tok.size() && std::isdigit(static_cast<unsigned char>(tok[p])))
      p++;
    if (p > 0 && p < tok.size() && tok[p] == '.') {
      while (p < tok.size() && tok[p] == '.')
        p++;
      tok = tok.substr(p);
    }
    if (!tok.empty() && tok != "1-0" && tok != "0-1" && tok != "1/2-1/2" &&
        tok != "*") {
      tokens.push_back(tok);
    }
    tok.clear();
  };
  for (char c : flat) {
    if (std::isspace(static_cast<unsigned char>(c)))
      flush();
    else
      tok += c;
  }
  flush();
  return tokens;
}

namespace pgn_detail {
inline Type sanLetterToType(char c) {
  switch (c) {
  case 'N':
    return HORSE;
  case 'B':
    return ELEPHANT;
  case 'R':
    return LADYA;
  case 'Q':
    return QUEEN;
  case 'K':
    return KING;
  default:
    return PAWN;
  }
}
} // namespace pgn_detail

// Match one (already game-contextual) SAN token to a legal UCI move on `game`.
inline bool matchSanToUci(ChessFacade &game, const std::string &sanRaw,
                          std::string &out) {
  // Normalize: drop check/mate markers and annotation glyphs.
  std::string san;
  for (char c : sanRaw) {
    if (c == '+' || c == '#' || c == '!' || c == '?')
      continue;
    san += c;
  }
  if (san.empty())
    return false;

  Color turn = game.getCurrentTurn();
  std::vector<std::string> cands = game.getAllLegalUCIForCurrentTurn();

  // Castling.
  if (san == "O-O" || san == "0-0") {
    std::string want = (turn == WHITE) ? "e1g1" : "e8g8";
    for (const auto &m : cands)
      if (m == want) {
        out = m;
        return true;
      }
    return false;
  }
  if (san == "O-O-O" || san == "0-0-0") {
    std::string want = (turn == WHITE) ? "e1c1" : "e8c8";
    for (const auto &m : cands)
      if (m == want) {
        out = m;
        return true;
      }
    return false;
  }

  // Promotion suffix "=X".
  char promo = 0;
  size_t eq = san.find('=');
  if (eq != std::string::npos) {
    if (eq + 1 < san.size())
      promo = static_cast<char>(
          std::tolower(static_cast<unsigned char>(san[eq + 1])));
    san = san.substr(0, eq);
  }

  // Drop capture marker (capture is derived from the board, not trusted here).
  std::string core;
  for (char c : san)
    if (c != 'x')
      core += c;
  if (core.size() < 2)
    return false;

  char pieceLetter = 0;
  if (std::strchr("NBRQK", core[0]) != nullptr)
    pieceLetter = core[0];
  Type wantType =
      pieceLetter ? pgn_detail::sanLetterToType(pieceLetter) : PAWN;

  std::string destStr = core.substr(core.size() - 2);
  int destX = destStr[0] - 'a';
  int destY = destStr[1] - '1';
  if (destX < 0 || destX > 7 || destY < 0 || destY > 7)
    return false;

  // Disambiguation lives between the piece letter (if any) and the destination.
  std::string disamb;
  if (pieceLetter)
    disamb = core.substr(1, core.size() - 1 - 2);
  else
    disamb = core.substr(0, core.size() - 2);
  int srcFile = -1, srcRank = -1;
  for (char c : disamb) {
    if (c >= 'a' && c <= 'h')
      srcFile = c - 'a';
    else if (c >= '1' && c <= '8')
      srcRank = c - '1';
  }

  std::string found;
  int matches = 0;
  for (const auto &m : cands) {
    int fx = m[0] - 'a', fy = m[1] - '1';
    int tx = m[2] - 'a', ty = m[3] - '1';
    if (tx != destX || ty != destY)
      continue;
    ChessFacade::PieceView pv = game.getPieceAt(Coordinates(fx, fy));
    if (!pv.present || pv.type != wantType)
      continue;
    if (srcFile >= 0 && fx != srcFile)
      continue;
    if (srcRank >= 0 && fy != srcRank)
      continue;
    if (promo) {
      if (m.size() < 5 ||
          std::tolower(static_cast<unsigned char>(m[4])) != promo)
        continue;
    } else if (m.size() >= 5) {
      continue; // promotion candidate but SAN gave no '=' piece
    }
    found = m;
    matches++;
  }
  if (matches == 1) {
    out = found;
    return true;
  }
  return false;
}

// Convert a full game's PGN into a UCI move list. Returns false (with errMsg
// set and uciOut holding the successfully parsed prefix) on the first token
// that cannot be matched or applied.
inline bool sanGameToUci(const std::string &pgn, std::vector<std::string> &uciOut,
                         std::string &errMsg) {
  uciOut.clear();
  errMsg.clear();
  std::vector<std::string> tokens = extractSanTokens(pgn);

  Board board;
  ChessFacade game(&board);
  game.fillBoard();

  int moveNo = 0;
  for (const std::string &san : tokens) {
    moveNo++;
    std::string uci;
    if (!matchSanToUci(game, san, uci)) {
      errMsg = "Could not parse move " + std::to_string(moveNo) + ": " + san;
      return false;
    }
    if (!game.makeMoveByUCI(uci)) {
      errMsg = "Illegal move " + std::to_string(moveNo) + ": " + san + " (" +
               uci + ")";
      return false;
    }
    uciOut.push_back(uci);
  }
  return true;
}

#endif // PGN_PARSER_H
