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

// Full-game Stockfish analysis + chess.com-style move classification.
//
// Must be included AFTER gameboard.cpp (needs ChessFacade / Color). One engine
// evaluation per position (N+1 calls for an N-ply game): the eval after ply i
// is reused as the eval before ply i+1, so no double evaluation. Each ply is
// classified by centipawn loss in the mover's perspective, with a "Book" label
// for opening moves matched against a small embedded list.

#ifndef GAME_ANALYZER_H
#define GAME_ANALYZER_H

#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

// ncurses color pair ids for quality badges (initialised by the TUI).
const int COLOR_Q_BOOK = 5;
const int COLOR_Q_BEST = 6;
const int COLOR_Q_GOOD = 7;
const int COLOR_Q_INACC = 8;
const int COLOR_Q_MISTAKE = 9;
const int COLOR_Q_BLUNDER = 10;

enum class MoveQuality {
  Book,
  Best,
  Excellent,
  Good,
  Inaccuracy,
  Mistake,
  Blunder
};

struct PlyAnalysis {
  std::string uci;     // move played
  std::string bestUci; // engine best move in the position before this ply
  int evalBeforeWhiteCp = 0; // white-perspective eval before the move
  int evalAfterWhiteCp = 0;  // white-perspective eval after the move
  int lossCp = 0;            // mover-perspective centipawn loss (>= 0)
  MoveQuality quality = MoveQuality::Good;
};

struct GameAnalysis {
  std::vector<PlyAnalysis> plies; // aligned: plies[i] describes move i
  bool complete = false;
};

// Mate scores are encoded as a large bounded centipawn value so arithmetic and
// ordering stay sane (mate in fewer moves = larger magnitude).
const int ANALYZER_MATE_BASE = 100000;

inline std::string formatWhiteCp(int absCp) {
  if (std::abs(absCp) >= ANALYZER_MATE_BASE - 5000) {
    int n = (ANALYZER_MATE_BASE - std::abs(absCp)) / 100;
    return std::string(absCp > 0 ? "M" : "-M") + std::to_string(n);
  }
  char buf[16];
  double v = absCp / 100.0;
  snprintf(buf, sizeof(buf), "%+.2f", v);
  return std::string(buf);
}

// Parse the StockfishAdapter::getEvaluation string
// ("Evaluation: <score> | Best move: <uci>") into a white-perspective
// centipawn score and the best move.
inline void parseEvalString(const std::string &s, int &absCp,
                            std::string &best) {
  absCp = 0;
  best.clear();

  size_t e = s.find("Evaluation: ");
  if (e != std::string::npos) {
    size_t start = e + 12;
    size_t bar = s.find(" |", start);
    std::string score =
        s.substr(start, bar == std::string::npos ? std::string::npos
                                                  : bar - start);
    while (!score.empty() && (score.back() == ' ' || score.back() == '\t'))
      score.pop_back();

    bool neg = false;
    size_t idx = 0;
    if (!score.empty() && (score[0] == '+' || score[0] == '-')) {
      neg = (score[0] == '-');
      idx = 1;
    }
    if (idx < score.size() && (score[idx] == 'M' || score[idx] == 'm')) {
      int n = std::atoi(score.c_str() + idx + 1);
      absCp = neg ? (-ANALYZER_MATE_BASE + n * 100)
                  : (ANALYZER_MATE_BASE - n * 100);
    } else {
      double v = std::atof(score.c_str());
      absCp = static_cast<int>(std::lround(v * 100.0));
    }
  }

  size_t b = s.find("Best move: ");
  if (b != std::string::npos) {
    best = s.substr(b + 11);
    while (!best.empty() &&
           (best.back() == '\n' || best.back() == '\r' || best.back() == ' '))
      best.pop_back();
  }
}

// A small embedded opening list (UCI move sequences). A ply is "Book" while the
// game's UCI prefix is still a prefix of one of these lines.
inline const std::vector<std::string> &openingBook() {
  static const std::vector<std::string> book = {
      // 1.e4 e5
      "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6",        // Ruy Lopez
      "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6",        // Italian / Two Knights
      "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5",        // Giuoco Piano
      "e2e4 e7e5 g1f3 b8c6 d2d4",             // Scotch
      "e2e4 e7e5 g1f3 g8f6",                  // Petrov
      "e2e4 e7e5 b1c3",                       // Vienna
      // 1.e4 c5 Sicilian
      "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3", // Najdorf-ish
      "e2e4 c7c5 g1f3 b8c6",                  // Sicilian ...Nc6
      "e2e4 c7c5 g1f3 e7e6",                  // Taimanov/Kan
      "e2e4 c7c5 b1c3",                       // Closed Sicilian
      // 1.e4 other
      "e2e4 e7e6 d2d4 d7d5",                  // French
      "e2e4 c7c6 d2d4 d7d5",                  // Caro-Kann
      "e2e4 d7d5",                            // Scandinavian
      "e2e4 g8f6",                            // Alekhine
      "e2e4 g7g6 d2d4 f8g7",                  // Modern
      "e2e4 d7d6 d2d4 g8f6",                  // Pirc
      // 1.d4
      "d2d4 d7d5 c2c4 e7e6",                  // QGD
      "d2d4 d7d5 c2c4 c7c6",                  // Slav
      "d2d4 d7d5 c2c4 d5c4",                  // QGA
      "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4",        // Nimzo-Indian
      "d2d4 g8f6 c2c4 e7e6 g1f3 b7b6",        // Queen's Indian
      "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7",        // King's Indian / Gruenfeld
      "d2d4 g8f6 c2c4 c7c5",                  // Benoni
      "d2d4 f7f5",                            // Dutch
      // Flank
      "c2c4 e7e5",                            // English
      "c2c4 g8f6",                            // English
      "g1f3 d7d5",                            // Reti
      "g1f3 g8f6",                            // Reti
  };
  return book;
}

inline bool isBookMove(const std::vector<std::string> &uci, int upToPly) {
  std::string prefix;
  for (int k = 0; k <= upToPly; k++) {
    if (k)
      prefix += " ";
    prefix += uci[k];
  }
  for (const auto &line : openingBook()) {
    if (line.size() >= prefix.size() &&
        line.compare(0, prefix.size(), prefix) == 0 &&
        (line.size() == prefix.size() || line[prefix.size()] == ' ')) {
      return true;
    }
  }
  return false;
}

inline MoveQuality classify(int lossCp, bool book) {
  if (book)
    return MoveQuality::Book;
  if (lossCp <= 10)
    return MoveQuality::Best;
  if (lossCp <= 25)
    return MoveQuality::Excellent;
  if (lossCp <= 50)
    return MoveQuality::Good;
  if (lossCp <= 100)
    return MoveQuality::Inaccuracy;
  if (lossCp <= 250)
    return MoveQuality::Mistake;
  return MoveQuality::Blunder;
}

inline const char *qualityLabel(MoveQuality q) {
  switch (q) {
  case MoveQuality::Book:
    return "Book";
  case MoveQuality::Best:
    return "Best";
  case MoveQuality::Excellent:
    return "Excellent";
  case MoveQuality::Good:
    return "Good";
  case MoveQuality::Inaccuracy:
    return "Inaccuracy ?!";
  case MoveQuality::Mistake:
    return "Mistake ?";
  case MoveQuality::Blunder:
    return "Blunder ??";
  }
  return "";
}

inline int qualityColorPair(MoveQuality q) {
  switch (q) {
  case MoveQuality::Book:
    return COLOR_Q_BOOK;
  case MoveQuality::Best:
  case MoveQuality::Excellent:
    return COLOR_Q_BEST;
  case MoveQuality::Good:
    return COLOR_Q_GOOD;
  case MoveQuality::Inaccuracy:
    return COLOR_Q_INACC;
  case MoveQuality::Mistake:
    return COLOR_Q_MISTAKE;
  case MoveQuality::Blunder:
    return COLOR_Q_BLUNDER;
  }
  return COLOR_Q_GOOD;
}

// Analyse the whole game on `game` (which must already hold a working engine).
// Mutates the board; the caller is responsible for restoring the position
// afterwards. progressCb(done, total) is invoked once per evaluated position.
inline GameAnalysis analyzeGame(ChessFacade &game,
                                const std::vector<std::string> &uci,
                                int movetimeMs,
                                std::function<void(int, int)> progressCb) {
  GameAnalysis ga;
  int N = static_cast<int>(uci.size());
  game.fillBoard();
  game.setMoveTimeMs(movetimeMs);

  std::vector<int> posEval(N + 1, 0);
  std::vector<std::string> posBest(N + 1);

  for (int i = 0; i <= N; i++) {
    std::string s = game.getPositionEvaluation();
    parseEvalString(s, posEval[i], posBest[i]);
    if (progressCb)
      progressCb(i, N);
    if (i < N) {
      if (!game.makeMoveByUCI(uci[i])) {
        ga.complete = false;
        return ga;
      }
    }
  }

  for (int i = 0; i < N; i++) {
    Color turn = (i % 2 == 0) ? WHITE : BLACK;
    int bestMover = (turn == WHITE) ? posEval[i] : -posEval[i];
    int playedMover = (turn == WHITE) ? posEval[i + 1] : -posEval[i + 1];
    int loss = bestMover - playedMover;
    if (loss < 0)
      loss = 0;

    PlyAnalysis p;
    p.uci = uci[i];
    p.bestUci = posBest[i];
    p.evalBeforeWhiteCp = posEval[i];
    p.evalAfterWhiteCp = posEval[i + 1];
    p.lossCp = loss;
    p.quality = classify(loss, isBookMove(uci, i));
    ga.plies.push_back(p);
  }

  ga.complete = true;
  return ga;
}

#endif // GAME_ANALYZER_H
