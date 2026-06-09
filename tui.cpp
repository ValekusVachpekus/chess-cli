/*
 * Copyright (C) 2026 Ilia Shchetkov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free-Software Foundation, either version 3 of the License, or
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

#define CHESSGAME_NO_MAIN
#define CHESSGAME_SILENT
#include "gameboard.cpp"
#include "network_adapter.h"
// Analysis + chess.com import (included after gameboard.cpp so they see
// ChessFacade / Color / Type).
#include "chesscom_adapter.h"
#include "game_analyzer.h"
#include "lichess_adapter.h"
#include "pgn_parser.h"
#include <fstream>
#include <getopt.h>
#include <locale.h>
#include <ncurses.h>
#include <unistd.h>

const int COLOR_WHITE_PIECE = 1;
const int COLOR_BLACK_PIECE = 2;
const int COLOR_HIGHLIGHT = 3;
const int COLOR_CAPTURE = 4;

// Board square color pairs (ids 5-10 are used by game_analyzer.h). Each square
// background combines with a white-side or black-side piece foreground, so we
// pre-init one pair per (background, side) combination. See initBoardColors().
const int PAIR_W_LIGHT = 20;
const int PAIR_B_LIGHT = 21;
const int PAIR_W_DARK = 22;
const int PAIR_B_DARK = 23;
const int PAIR_W_TARGET = 24;
const int PAIR_B_TARGET = 25;
const int PAIR_W_CAPTURE = 26;
const int PAIR_B_CAPTURE = 27;
const int PAIR_W_LAST = 28;
const int PAIR_B_LAST = 29;
const int PAIR_W_CURSOR = 30;
const int PAIR_B_CURSOR = 31;

// Square background kinds, listed high-priority first (see drawBoard).
enum SquareBg {
  SQ_CURSOR,
  SQ_CHECK,
  SQ_CAPTURE,
  SQ_TARGET,
  SQ_LAST,
  SQ_LIGHT,
  SQ_DARK
};

// ncurses color-pair id for a glyph belonging to `side` (WHITE/BLACK) drawn on a
// square whose background is `bg`. Empty cells pass WHITE; only the bg shows.
int squarePair(SquareBg bg, Color side) {
  bool w = (side != BLACK);
  switch (bg) {
  case SQ_CURSOR:
    return w ? PAIR_W_CURSOR : PAIR_B_CURSOR;
  case SQ_LIGHT:
    return w ? PAIR_W_LIGHT : PAIR_B_LIGHT;
  case SQ_DARK:
    return w ? PAIR_W_DARK : PAIR_B_DARK;
  case SQ_TARGET:
    return w ? PAIR_W_TARGET : PAIR_B_TARGET;
  case SQ_CAPTURE:
  case SQ_CHECK: // check reuses the red capture background
    return w ? PAIR_W_CAPTURE : PAIR_B_CAPTURE;
  case SQ_LAST:
    return w ? PAIR_W_LAST : PAIR_B_LAST;
  }
  return w ? PAIR_W_LIGHT : PAIR_B_LIGHT;
}

// Board themes the user can cycle through with 't'.
//  - THEME_GRAY:    gray checkerboard with colored highlight backgrounds.
//  - THEME_WOOD:    warm tan/brown checkerboard, same highlights.
//  - THEME_CLASSIC: the original look — no square fill, pieces colored by side
//                   (blue/yellow), highlights via foreground markers, compact
//                   one-row cells.
enum BoardTheme { THEME_GRAY = 0, THEME_WOOD = 1, THEME_CLASSIC = 2 };
const int THEME_COUNT = 3;
static int gTheme = THEME_GRAY;

const char *themeName(int t) {
  switch (t) {
  case THEME_GRAY:
    return "Gray";
  case THEME_WOOD:
    return "Wood";
  case THEME_CLASSIC:
    return "Classic";
  }
  return "?";
}

// Initialise the board color pairs for `theme`. Uses a 256-color palette when
// the terminal supports it, otherwise the basic 8 colors. (THEME_CLASSIC draws
// with the COLOR_*_PIECE foreground pairs instead and ignores these.)
void initBoardColors(int theme) {
  int whiteFg, blackFg, lightBg, darkBg, targetBg, captureBg, lastBg, cursorBg;
  if (COLORS >= 256) {
    whiteFg = 231; // bright white (white side)
    blackFg = 16;  // black        (black side)
    if (theme == THEME_WOOD) {
      lightBg = 180; // tan   (#d7af87)
      darkBg = 94;   // brown (#875f00)
    } else {
      lightBg = 246; // mid-gray  (#949494)
      darkBg = 239;  // dark-gray (#4e4e4e)
    }
    targetBg = 65;   // muted green  (legal empty target)
    captureBg = 131; // muted red    (capture)
    lastBg = 136;    // amber        (last move)
    cursorBg = 33;   // bright blue cursor (keeps both piece colors visible)
  } else {
    whiteFg = COLOR_WHITE;
    blackFg = COLOR_BLACK;
    lightBg = COLOR_WHITE;
    darkBg = COLOR_BLUE;
    targetBg = COLOR_GREEN;
    captureBg = COLOR_RED;
    lastBg = COLOR_YELLOW;
    cursorBg = COLOR_CYAN;
  }
  init_pair(PAIR_W_LIGHT, whiteFg, lightBg);
  init_pair(PAIR_B_LIGHT, blackFg, lightBg);
  init_pair(PAIR_W_DARK, whiteFg, darkBg);
  init_pair(PAIR_B_DARK, blackFg, darkBg);
  init_pair(PAIR_W_TARGET, whiteFg, targetBg);
  init_pair(PAIR_B_TARGET, blackFg, targetBg);
  init_pair(PAIR_W_CAPTURE, whiteFg, captureBg);
  init_pair(PAIR_B_CAPTURE, blackFg, captureBg);
  init_pair(PAIR_W_LAST, whiteFg, lastBg);
  init_pair(PAIR_B_LAST, blackFg, lastBg);
  init_pair(PAIR_W_CURSOR, whiteFg, cursorBg);
  init_pair(PAIR_B_CURSOR, blackFg, cursorBg);
}

bool isMoveInList(const vector<Coordinates> &moves, int x, int y) {
  for (const auto &move : moves) {
    if (move.getX() == x && move.getY() == y) {
      return true;
    }
  }
  return false;
}

// Added network_adapter
void drawBoard(ChessFacade &game, int cursorX, int cursorY,
               const vector<Coordinates> &validMoves, const string &status,
               const string &botInfo, Color turn, bool flipped, bool flipOnTurn,
               bool centerBoard, bool hideUI,
               [[maybe_unused]] string gameMode = "white",
               [[maybe_unused]] NetworkAdapter *netAdapter = nullptr) {
  clear();
  int rows = 0, cols = 0;
  getmaxyx(stdscr, rows, cols);
  const int boardWidth = 19; // "8| " gutter + 8 squares * 2 columns
  const int boardHeight = 10;
  int offsetX =
      centerBoard ? ((cols > boardWidth) ? (cols - boardWidth) / 2 : 0) : 0;
  int offsetY =
      centerBoard ? ((rows > boardHeight) ? (rows - boardHeight) / 2 : 0) : 0;

  // Highlight inputs derived from game state (no extra parameters needed).
  // Last move: the from/to squares of the most recent move (works in replay too,
  // since the history is rebuilt as you step through it).
  Coordinates lastFrom(-1, -1), lastTo(-1, -1);
  const vector<string> &hist = game.getMoveHistory();
  if (!hist.empty() && hist.back().size() >= 4) {
    const string &m = hist.back();
    lastFrom = Coordinates(m[0] - 'a', m[1] - '1');
    lastTo = Coordinates(m[2] - 'a', m[3] - '1');
  }
  // King square of any side currently in check.
  Coordinates checkW =
      game.isInCheck(WHITE) ? game.getKingSquare(WHITE) : Coordinates(-1, -1);
  Coordinates checkB =
      game.isInCheck(BLACK) ? game.getKingSquare(BLACK) : Coordinates(-1, -1);

  bool classic = (gTheme == THEME_CLASSIC);
  int boardRows = 8; // compact: one text row per rank, cells 2 columns wide

  for (int sy = 7; sy >= 0; sy--) {
    int displayRow = flipped ? (8 - sy) : (sy + 1);
    int r0 = offsetY + (7 - sy);
    mvprintw(r0, offsetX, "%d|", displayRow);
    for (int sx = 0; sx < 8; sx++) {
      int bx = flipped ? (7 - sx) : sx;
      int by = flipped ? (7 - sy) : sy;
      bool isCursor = (sx == cursorX && sy == cursorY);
      bool isValid = isMoveInList(validMoves, bx, by);
      ChessFacade::PieceView piece = game.getPieceAt(Coordinates(bx, by));
      bool isLast = (lastFrom.getX() == bx && lastFrom.getY() == by) ||
                    (lastTo.getX() == bx && lastTo.getY() == by);
      bool isCheck = (checkW.getX() == bx && checkW.getY() == by) ||
                     (checkB.getX() == bx && checkB.getY() == by);
      int cellCol = offsetX + 3 + sx * 2; // 2 columns per cell

      if (classic) {
        // Original look: foreground colors only, no square fill.
        string cell = piece.present ? typeToString(piece.type) : ".";
        int cp = 0;
        if (isCheck || (isValid && piece.present)) {
          cp = COLOR_CAPTURE; // check / capturable square in red
        } else if (isValid) {
          cell = "*";
          cp = COLOR_HIGHLIGHT;
        } else if (piece.present) {
          cp = (piece.color == WHITE) ? COLOR_WHITE_PIECE : COLOR_BLACK_PIECE;
        }
        if (cp != 0) {
          attron(COLOR_PAIR(cp));
        }
        if (isLast) {
          attron(A_UNDERLINE);
        }
        if (isCursor) {
          attron(A_REVERSE);
        }
        mvprintw(r0, cellCol, "%s", cell.c_str());
        if (isCursor) {
          attroff(A_REVERSE);
        }
        if (isLast) {
          attroff(A_UNDERLINE);
        }
        if (cp != 0) {
          attroff(COLOR_PAIR(cp));
        }
        continue;
      }

      // Background themes (gray / wood): a filled 2x1 square per cell.
      // Priority: cursor > check > capture-target > empty-target > last-move
      // > checkerboard base.
      SquareBg bg;
      if (isCursor) {
        bg = SQ_CURSOR;
      } else if (isCheck) {
        bg = SQ_CHECK;
      } else if (isValid && piece.present) {
        bg = SQ_CAPTURE;
      } else if (isValid) {
        bg = SQ_TARGET;
      } else if (isLast) {
        bg = SQ_LAST;
      } else {
        bg = ((bx + by) % 2 == 0) ? SQ_DARK : SQ_LIGHT;
      }

      string cell;
      Color fgSide;
      if (piece.present) {
        cell = typeToString(piece.type);
        fgSide = piece.color;
      } else if (bg == SQ_TARGET) {
        cell = "*"; // marker on an empty legal-move square
        fgSide = BLACK;
      } else {
        cell = " ";
        fgSide = WHITE;
      }

      int pair = squarePair(bg, fgSide);
      attron(COLOR_PAIR(pair));
      if (piece.present) {
        attron(A_BOLD); // crisp glyphs against the colored squares
      }
      // Two-column block (glyph + trailing space) so squares are contiguous.
      mvprintw(r0, cellCol, "%s ", cell.c_str());
      if (piece.present) {
        attroff(A_BOLD);
      }
      attroff(COLOR_PAIR(pair));
    }
  }

  int labelRow = offsetY + boardRows;
  // File labels, one per column, aligned with each square's glyph.
  mvprintw(labelRow, offsetX, "%s", getCornerIcon().c_str());
  mvprintw(labelRow, offsetX + 1, "|");
  for (int sx = 0; sx < 8; sx++) {
    char f = static_cast<char>(flipped ? ('H' - sx) : ('A' + sx));
    mvprintw(labelRow, offsetX + 3 + sx * 2, "%c", f);
  }

  // Captured pieces + material score, lichess-style, just below the board.
  // White's row shows the black pieces White has taken (getCapturedFromBlack),
  // and vice-versa. The leading side gets a "+N" material advantage.
  {
    const auto &whiteTook = game.getCapturedFromBlack(); // black pieces taken
    const auto &blackTook = game.getCapturedFromWhite(); // white pieces taken
    int matWhite = 0, matBlack = 0;
    for (const auto &c : whiteTook) {
      matWhite += c.cost;
    }
    for (const auto &c : blackTook) {
      matBlack += c.cost;
    }
    int adv = matWhite - matBlack;

    auto drawTaken = [&](int rowAbs, const char *label,
                         const vector<ChessFacade::Captured> &taken,
                         int piecePair, int plus) {
      mvprintw(rowAbs, offsetX, "%s", label);
      int col = offsetX + 7; // both labels ("White: "/"Black: ") are 7 chars
      for (size_t i = 0; i < taken.size(); i++) {
        attron(COLOR_PAIR(piecePair));
        mvprintw(rowAbs, col, "%s", typeToString(taken[i].type).c_str());
        attroff(COLOR_PAIR(piecePair));
        col += 2;
      }
      if (plus > 0) {
        mvprintw(rowAbs, col + 1, "+%d", plus);
      }
    };
    drawTaken(labelRow + 1, "White: ", whiteTook, COLOR_BLACK_PIECE,
              adv > 0 ? adv : 0);
    drawTaken(labelRow + 2, "Black: ", blackTook, COLOR_WHITE_PIECE,
              adv < 0 ? -adv : 0);
  }

  if (!hideUI) {
    int t = labelRow + 4;
    mvprintw(t, offsetX, "Turn: %s", (turn == WHITE) ? "WHITE" : "BLACK");
    mvprintw(t + 1, offsetX, "Status: %s", status.c_str());
    mvprintw(t + 2, offsetX, "%s", botInfo.c_str());
    mvprintw(t + 3, offsetX, "Flip on turn: %s  Theme: %s",
             flipOnTurn ? "ON" : "OFF", themeName(gTheme));
    mvprintw(t + 4, offsetX,
             "Controls: arrows/jkl, Enter select/deselect, Esc deselect, f "
             "flip, t theme, C center, i hide UI, s save, o load, q quit");
  }
  refresh();
}

// Board offsets, computed the same way drawBoard does, so the analysis panel
// lines up with the board.
void computeOffsets(bool centerBoard, int &offsetX, int &offsetY) {
  int rows = 0, cols = 0;
  getmaxyx(stdscr, rows, cols);
  const int boardWidth = 19; // "8| " gutter + 8 squares * 2 columns
  const int boardHeight = 10;
  offsetX = centerBoard ? ((cols > boardWidth) ? (cols - boardWidth) / 2 : 0) : 0;
  offsetY =
      centerBoard ? ((rows > boardHeight) ? (rows - boardHeight) / 2 : 0) : 0;
}

// Analysis side panel, drawn to the right of the board (column offsetX + 22).
// Call after drawBoard each frame in the replay loop.
void drawAnalysisPanel(int offsetX, int offsetY, bool active, size_t replayIndex,
                       size_t total, const GameAnalysis &analysis,
                       bool analyzing, int progDone, int progTotal) {
  int px = offsetX + 22;
  int py = offsetY;

  if (analyzing) {
    const int barW = 20;
    int filled = (progTotal > 0) ? (progDone * barW) / progTotal : 0;
    string bar(static_cast<size_t>(filled), '#');
    bar.resize(barW, ' ');
    mvprintw(py, px, "Analyzing game...");
    mvprintw(py + 1, px, "[%s] %d/%d", bar.c_str(), progDone, progTotal);
    refresh();
    return;
  }

  if (!active) {
    return; // nothing to show until the game has been analysed
  }

  mvprintw(py, px, "== Analysis ==");
  if (replayIndex == 0) {
    mvprintw(py + 1, px, "Initial position");
    return;
  }
  size_t idx = replayIndex - 1;
  if (idx >= analysis.plies.size()) {
    return;
  }
  const PlyAnalysis &p = analysis.plies[idx];
  Color mover = (idx % 2 == 0) ? WHITE : BLACK;
  mvprintw(py + 1, px, "Move %zu/%zu (%s)", replayIndex, total,
           mover == WHITE ? "White" : "Black");
  mvprintw(py + 2, px, "Played: %s   ", p.uci.c_str());
  mvprintw(py + 3, px, "Best  : %s   ",
           p.bestUci.empty() ? "-" : p.bestUci.c_str());
  mvprintw(py + 4, px, "Eval  : %s -> %s   ",
           formatWhiteCp(p.evalBeforeWhiteCp).c_str(),
           formatWhiteCp(p.evalAfterWhiteCp).c_str());
  mvprintw(py + 5, px, "Loss  : %d cp   ", p.lossCp);

  int cp = qualityColorPair(p.quality);
  bool bold =
      (p.quality == MoveQuality::Best || p.quality == MoveQuality::Excellent);
  attron(COLOR_PAIR(cp));
  if (bold) {
    attron(A_BOLD);
  }
  mvprintw(py + 6, px, "[%s]            ", qualityLabel(p.quality));
  if (bold) {
    attroff(A_BOLD);
  }
  attroff(COLOR_PAIR(cp));
  refresh();
}

string promptInput(int row, const string &label, const string &defaultValue) {
  char buffer[64];
  buffer[0] = '\0';
  echo();
  curs_set(1);
  mvprintw(row, 0, "%s", label.c_str());
  clrtoeol();
  getnstr(buffer, 63);
  noecho();
  curs_set(0);
  string input(buffer);
  if (input.empty()) {
    return defaultValue;
  }
  return input;
}

// Like promptInput but keeps echo off, so a secret (e.g. a Lichess token) is
// not shown on screen as it is typed.
string promptSecret(int row, const string &label) {
  char buffer[256];
  buffer[0] = '\0';
  noecho();
  curs_set(1);
  mvprintw(row, 0, "%s", label.c_str());
  clrtoeol();
  getnstr(buffer, 255);
  curs_set(0);
  return string(buffer);
}

// Milliseconds -> "M:SS" for clock display.
string formatClock(long ms) {
  if (ms < 0) {
    ms = 0;
  }
  long totalSec = ms / 1000;
  long minutes = totalSec / 60;
  long seconds = totalSec % 60;
  char buf[32];
  snprintf(buf, sizeof(buf), "%ld:%02ld", minutes, seconds);
  return string(buf);
}

// Trim surrounding whitespace and a single pair of matching quotes.
static string trimEnvValue(string s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  size_t b = s.find_last_not_of(" \t\r\n");
  if (a == string::npos) {
    return "";
  }
  s = s.substr(a, b - a + 1);
  if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') &&
      s.back() == s.front()) {
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

// Reads the value of `key` from a .env file in the current directory. Lines look
// like `KEY=value` (an optional leading `export ` is ignored). Returns "" if the
// file or key is absent.
string readDotenv(const string &path, const string &key) {
  ifstream f(path);
  if (!f.is_open()) {
    return "";
  }
  string line;
  while (getline(f, line)) {
    string l = line;
    size_t s = l.find_first_not_of(" \t");
    if (s != string::npos) {
      l = l.substr(s);
    }
    if (l.rfind("export ", 0) == 0) {
      l = l.substr(7);
    }
    size_t eq = l.find('=');
    if (eq == string::npos) {
      continue;
    }
    if (trimEnvValue(l.substr(0, eq)) == key) {
      return trimEnvValue(l.substr(eq + 1));
    }
  }
  return "";
}

// Stores `key=value` in the .env file, replacing any existing entry for `key`
// and leaving other lines intact. Returns true on success.
bool writeDotenv(const string &path, const string &key, const string &value) {
  vector<string> lines;
  {
    ifstream f(path);
    string line;
    while (getline(f, line)) {
      string l = line;
      size_t s = l.find_first_not_of(" \t");
      string body = (s == string::npos) ? l : l.substr(s);
      if (body.rfind("export ", 0) == 0) {
        body = body.substr(7);
      }
      size_t eq = body.find('=');
      if (eq != string::npos && trimEnvValue(body.substr(0, eq)) == key) {
        continue; // drop the old entry; we re-append it below
      }
      lines.push_back(line);
    }
  }
  ofstream out(path, ios::trunc);
  if (!out.is_open()) {
    return false;
  }
  for (const string &l : lines) {
    out << l << "\n";
  }
  out << key << "=" << value << "\n";
  return out.good();
}

int promptMenu(int row, const string &title, const vector<string> &options) {
  int current = 0;
  while (true) {
    clear();
    mvprintw(row, 0, "%s", title.c_str());
    for (size_t i = 0; i < options.size(); i++) {
      if (static_cast<int>(i) == current) {
        attron(A_REVERSE);
      }
      mvprintw(row + 2 + static_cast<int>(i), 2, "%s", options[i].c_str());
      if (static_cast<int>(i) == current) {
        attroff(A_REVERSE);
      }
    }
    mvprintw(row + 3 + static_cast<int>(options.size()), 0,
             "Use ↑/↓ and Enter");
    refresh();

    int ch = getch();
    if (ch == KEY_UP || ch == 'k') {
      if (current > 0) {
        current--;
      }
    } else if (ch == KEY_DOWN || ch == 'j') {
      if (current < static_cast<int>(options.size()) - 1) {
        current++;
      }
    } else if (ch == '\n' || ch == KEY_ENTER) {
      return current;
    }
  }
}

Type promptPromotion(Color color) {
  vector<Type> types = {QUEEN, LADYA, ELEPHANT, HORSE};
  vector<string> options = {
      string(typeToString(QUEEN)) + " Queen",
      string(typeToString(LADYA)) + " Rook",
      string(typeToString(ELEPHANT)) + " Bishop",
      string(typeToString(HORSE)) + " Knight",
  };
  int current = 0;
  while (true) {
    clear();
    string title =
        (color == WHITE) ? "Promote pawn (WHITE):" : "Promote pawn (BLACK):";
    mvprintw(2, 0, "%s", title.c_str());
    for (size_t i = 0; i < options.size(); i++) {
      if (static_cast<int>(i) == current) {
        attron(A_REVERSE);
      }
      mvprintw(4 + static_cast<int>(i), 2, "%s", options[i].c_str());
      if (static_cast<int>(i) == current) {
        attroff(A_REVERSE);
      }
    }
    mvprintw(9, 0, "Use ↑/↓ or j/k, Enter to select");
    refresh();

    int ch = getch();
    if (ch == KEY_UP || ch == 'k') {
      if (current > 0) {
        current--;
      }
    } else if (ch == KEY_DOWN || ch == 'j') {
      if (current < static_cast<int>(options.size()) - 1) {
        current++;
      }
    } else if (ch == '\n' || ch == KEY_ENTER) {
      return types[current];
    }
  }
}

void printHelp() {
  cout
      << "Usage: chess-tui [OPTIONS]\n\n"
      << "Options:\n"
      << "  -m, --mode <mode>    Game mode: human, white, black, auto, replay, "
         "chesscom, lichess\n"
      << "                       (replay: press 'a' to analyse the game)\n"
      << "                       (lichess: play live on lichess.org; set "
         "$LICHESS_TOKEN or .env)\n"
      << "  -t, --time <ms>      Bot movetime in milliseconds (default: 200)\n"
      << "  -i, --icons <1-5>    Icon set (1: Nerd, 2: Markdown, 3: ASCII, "
         "4: Fae, 5: Unicode chess)\n"
      << "  -f, --flip           Flip board at start (black on bottom)\n"
      << "  -c, --center         Center board in terminal\n"
      << "  -H, --hide-ui        Hide UI text (show only board)\n"
      << "  -S, --server         Start as server (plays White)\n"
      << "  -C, --connect <IP>   Connect to a server (plays Black)\n"
      << "  -P, --port <port>    Network port (default: 14888)\n"
      << "  -h, --help           Show this help\n"
      << "  -v, --version        Show version\n";
}

void printVersion() {
  cout << "chess-tui version 1.4.0\n"
       << "Copyright (C) 2026 Ilia Shchetkov\n"
       << "License GPLv3+: GNU GPL version 3 or later "
          "<https://gnu.org/licenses/gpl.html>.\n"
       << "This is free software: you are free to change and redistribute it.\n"
       << "There is NO WARRANTY, to the extent permitted by law.\n";
}

int main(int argc, char **argv) {
  string modeArg;
  bool modeProvided = false;
  int moveTimeMs = 200;
  bool startFlipped = false;
  bool startCentered = false;
  bool startHiddenUI = false;
  bool autoPlay = false;
  int iconArg = 1;
  bool iconProvided = false;

  // For network_adapter
  bool isServer = false;
  bool isClient = false;
  string connectIp = "";
  int port = 14888;

  static struct option long_options[] = {
      {"mode", required_argument, nullptr, 'm'},
      {"time", required_argument, nullptr, 't'},
      {"icons", required_argument, nullptr, 'i'},   // <-- ДОБАВИТЬ ЭТО
      {"server", no_argument, nullptr, 'S'},        // <-- ДОБАВИТЬ
      {"connect", required_argument, nullptr, 'C'}, // <-- ДОБАВИТЬ
      {"port", required_argument, nullptr, 'P'},    // <-- ДОБАВИТЬ
      {"flip", no_argument, nullptr, 'f'},
      {"center", no_argument, nullptr, 'c'},
      {"hide-ui", no_argument, nullptr, 'H'},
      {"help", no_argument, nullptr, 'h'},
      {"version", no_argument, nullptr, 'v'},
      {nullptr, 0, nullptr, 0}};

  int opt;
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "m:t:i:SC:P:fcHhv", long_options,
                            &option_index)) != -1) {
    switch (opt) {
    case 'm':
      modeArg = optarg;
      modeProvided = true;
      if (modeArg != "human" && modeArg != "white" && modeArg != "black" &&
          modeArg != "auto" && modeArg != "replay" && modeArg != "chesscom" &&
          modeArg != "lichess") {
        cerr << "Error: invalid mode '" << modeArg
             << "'. Use human, white, black, auto, replay, chesscom, "
                "lichess.\n";
        return 1;
      }
      break;
    case 't':
      try {
        moveTimeMs = stoi(optarg);
      } catch (...) {
        cerr << "Error: time must be a number of milliseconds.\n";
        return 1;
      }
      if (moveTimeMs <= 0) {
        cerr << "Error: time must be > 0.\n";
        return 1;
      }
      break;
    case 'i':
      try {
        iconArg = stoi(optarg);
        if (iconArg >= 1 && iconArg <= 5) {
          iconProvided = true;
        } else {
          cerr << "Error: icons must be 1, 2, 3, 4 or 5.\n";
          return 1;
        }
      } catch (...) {
        cerr << "Error: invalid icons argument.\n";
        return 1;
      }
      break;
    case 'f':
      startFlipped = true;
      break;
    case 'c':
      startCentered = true;
      break;
    case 'H':
      startHiddenUI = true;
      break;
    case 'h':
      printHelp();
      return 0;
    case 'v':
      printVersion();
      return 0;
    case 'S': // Network start
      isServer = true;
      break;
    case 'C':
      isClient = true;
      connectIp = optarg;
      break;
    case 'P':
      port = stoi(optarg);
      break; // net end
    default:
      return 1;
    }
  }

  curl_global_init(CURL_GLOBAL_DEFAULT);

  NetworkAdapter netAdapter;
  bool networkEnabled = false;
  char mode = 'h';
  bool importFromChessCom = false;
  bool lichessEnabled = false;
  Color lichessColor = WHITE;
  unique_ptr<LichessAdapter> lichess;

  // 1. ТОЛЬКО ОДИН РАЗ ЗАПУСКАЕМ ИНТЕРФЕЙС
  setlocale(LC_ALL, "");
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  start_color();
  use_default_colors();
  init_pair(COLOR_WHITE_PIECE, COLOR_BLUE, -1);
  init_pair(COLOR_BLACK_PIECE, COLOR_YELLOW, -1);
  init_pair(COLOR_HIGHLIGHT, COLOR_GREEN, -1);
  init_pair(COLOR_CAPTURE, COLOR_RED, -1);
  // Move-quality badges for analysis mode.
  init_pair(COLOR_Q_BOOK, COLOR_CYAN, -1);
  init_pair(COLOR_Q_BEST, COLOR_GREEN, -1);
  init_pair(COLOR_Q_GOOD, COLOR_GREEN, -1);
  init_pair(COLOR_Q_INACC, COLOR_YELLOW, -1);
  init_pair(COLOR_Q_MISTAKE, COLOR_MAGENTA, -1);
  init_pair(COLOR_Q_BLUNDER, COLOR_RED, -1);
  // Checkerboard squares + move/check highlights.
  initBoardColors(gTheme);

  // 2. ВЫБОР ИКОНОК
  if (!iconProvided) {
    vector<string> iconOptions = {
        "1. Nerd Font (     )",
        "2. Classic Markdown (󰡙 󰡘 󰡜 󰡛 󰡚 󰡗)",
        "3. ASCII Minimal (P N B R Q K)",
        "4. FAE Font (     )",
        "5. Unicode Chess (♟ ♞ ♝ ♜ ♛ ♚)"};
    int iconIndex = promptMenu(2, "Select piece icon set:", iconOptions);
    setIconStyle(static_cast<IconStyle>(iconIndex + 1));
    iconProvided = true; // ВАЖНО: блокируем повторный вызов
  } else {
    setIconStyle(static_cast<IconStyle>(iconArg));
  }

  // 3. ГЛАВНОЕ МЕНЮ (ВЫБОР РЕЖИМА)
  if (!modeProvided && !isServer && !isClient) {
    vector<string> modeOptions = {
        "1. Local: Human vs Human", "2. Local: Play vs Bot",
        "3. Network: Create Game (Host)", "4. Network: Find Local Games (Join)",
        "5. Replay Mode", "6. Online: Import from chess.com",
        "7. Online: Play on Lichess"};
    int modeIndex = promptMenu(2, "Select game mode:", modeOptions);

    if (modeIndex == 0) {
      mode = 'h';
    } else if (modeIndex == 1) {
      // Подменю для игры с ботом
      vector<string> botOptions = {"1. Bot plays Black (You play White)",
                                   "2. Bot plays White (You play Black)",
                                   "3. Bot vs Bot"};
      int botIndex = promptMenu(2, "Select bot configuration:", botOptions);
      if (botIndex == 0)
        mode = 'w';
      else if (botIndex == 1)
        mode = 'b';
      else if (botIndex == 2)
        mode = 'a';
    } else if (modeIndex == 2) {
      isServer = true;
    } else if (modeIndex == 3) {
      isClient = true;
    } else if (modeIndex == 4) {
      mode = 'r';
    } else if (modeIndex == 5) {
      mode = 'r';
      importFromChessCom = true;
    } else if (modeIndex == 6) {
      mode = 'L';
      lichessEnabled = true;
    }
    modeProvided = true; // ВАЖНО: блокируем повторный вызов
  } else if (modeProvided) {
    // Обработка флага -m из консоли
    if (modeArg == "white")
      mode = 'w';
    else if (modeArg == "black")
      mode = 'b';
    else if (modeArg == "auto")
      mode = 'a';
    else if (modeArg == "replay")
      mode = 'r';
    else if (modeArg == "chesscom") {
      mode = 'r';
      importFromChessCom = true;
    } else if (modeArg == "lichess") {
      mode = 'L';
      lichessEnabled = true;
    } else
      mode = 'h';
  }

  // --- ОБРАБОТКА УЛУЧШЕННОЙ СЕТИ ВНУТРИ TUI ---
  if (isServer) {
    if (netAdapter.startServer(port)) {
      networkEnabled = true;
      mode = 'n';
      // Заранее собираем локальные IP, чтобы показать сопернику.
      vector<string> localIPs = NetworkAdapter::getLocalIPv4();
      // Отрисовываем ожидание клиента в ncurses!
      timeout(100); // Опрос каждые 100мс
      while (!netAdapter.acceptClient()) {
        clear();
        int row = LINES / 2 - 2 - static_cast<int>(localIPs.size());
        mvprintw(row, COLS / 2 - 15, "Waiting for opponent on port %d...",
                 port);
        row += 2;
        if (localIPs.empty()) {
          mvprintw(row++, COLS / 2 - 15, "Your IP: (not detected)");
        } else {
          mvprintw(row++, COLS / 2 - 15, "Tell the other player to connect to:");
          for (const auto &ip : localIPs)
            mvprintw(row++, COLS / 2 - 15, "   %s -P %d", ip.c_str(), port);
        }
        row += 1;
        mvprintw(row, COLS / 2 - 15, "(Press 'q' to cancel)");
        refresh();
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
          endwin();
          return 0;
        }
      }
      timeout(-1); // Возвращаем блокирующий режим после подключения
    }
  } else if (isClient) {
    if (connectIp.empty()) {
      clear();
      mvprintw(LINES / 2, COLS / 2 - 15, "Scanning for local games...");
      refresh();
      // Вызываем наш статический сканер сети!
      auto servers = NetworkAdapter::discoverLocalServers(2000);

      vector<string> lanOptions;
      vector<string> ips;
      for (auto &s : servers) {
        lanOptions.push_back("Join " + s.first + ":" + to_string(s.second));
        ips.push_back(s.first);
      }
      lanOptions.push_back("Enter IP manually...");

      int choice = promptMenu(2, "Found local games:", lanOptions);
      if (static_cast<size_t>(choice) < servers.size()) {
        connectIp = ips[choice];
        port = servers[connectIp];
      } else {
        // Запасной план: ручной ввод
        connectIp = promptInput(10, "Enter IP: ", "127.0.0.1");
      }
    }

    if (netAdapter.connectToServer(connectIp, port)) {
      networkEnabled = true;
      mode = 'n';
    } else {
      endwin();
      cout << "Connection failed!" << endl;
      return 1;
    }
  }

  // --- LICHESS LOBBY (token, pairing, wait for game) ---
  if (lichessEnabled) {
    const string dotenvPath = ".env";
    string token;
    const char *envTok = getenv("LICHESS_TOKEN");
    if (envTok != nullptr && envTok[0] != '\0') {
      token = envTok; // $LICHESS_TOKEN wins
    } else {
      token = readDotenv(dotenvPath, "LICHESS_TOKEN"); // then the .env file
    }
    if (token.empty()) {
      clear();
      mvprintw(2, 0, "Lichess personal API token needed (scope board:play).");
      mvprintw(3, 0, "Create one at https://lichess.org/account/oauth/token");
      mvprintw(4, 0, "Tip: set $LICHESS_TOKEN or store it in a .env file.");
      refresh();
      token = promptSecret(6, "Token: ");
      if (!token.empty()) {
        string save = promptInput(8, "Save token to .env? [y/N]: ", "n");
        if (!save.empty() && (save[0] == 'y' || save[0] == 'Y')) {
          if (writeDotenv(dotenvPath, "LICHESS_TOKEN", token)) {
            mvprintw(9, 0, "Saved to %s (keep it private; .gitignore'd).",
                     dotenvPath.c_str());
          } else {
            mvprintw(9, 0, "Could not write %s.", dotenvPath.c_str());
          }
          refresh();
        }
      }
    }
    if (token.empty()) {
      endwin();
      cerr << "Lichess: no token provided.\n";
      return 1;
    }

    lichess = make_unique<LichessAdapter>(token);
    string accountId;
    string verr = lichess->verifyToken(accountId);
    if (!verr.empty()) {
      endwin();
      cerr << "Lichess: " << verr << "\n";
      return 1;
    }

    vector<string> lobbyOptions = {"1. Quick seek (auto-pair)",
                                   "2. Challenge a player",
                                   "3. Wait for an incoming challenge"};
    int lobbyChoice =
        promptMenu(2, "Lichess (" + accountId + "):", lobbyOptions);

    if (lobbyChoice == 0) {
      string mins = promptInput(8, "Minutes per side [5]: ", "5");
      string inc = promptInput(9, "Increment seconds [0]: ", "0");
      int m = 5, ic = 0;
      try {
        m = stoi(mins);
      } catch (...) {
        m = 5;
      }
      try {
        ic = stoi(inc);
      } catch (...) {
        ic = 0;
      }
      lichess->startEventStream();
      lichess->seek(m, ic, false);
    } else if (lobbyChoice == 1) {
      string user = promptInput(8, "Opponent username: ", "");
      string mins = promptInput(9, "Minutes per side [5]: ", "5");
      string inc = promptInput(10, "Increment seconds [0]: ", "0");
      int m = 5, ic = 0;
      try {
        m = stoi(mins);
      } catch (...) {
        m = 5;
      }
      try {
        ic = stoi(inc);
      } catch (...) {
        ic = 0;
      }
      lichess->startEventStream();
      string chErr = lichess->challengeUser(user, m, ic, false);
      if (!chErr.empty()) {
        endwin();
        cerr << "Lichess: " << chErr << "\n";
        return 1;
      }
    } else {
      lichess->startEventStream();
      timeout(200);
      int sel = 0;
      bool accepted = false;
      while (!accepted) {
        auto ch = lichess->pollIncomingChallenges();
        clear();
        mvprintw(2, 0, "Waiting for incoming challenges (q to cancel)...");
        if (ch.empty()) {
          mvprintw(4, 0, "  (none yet)");
        } else {
          if (sel >= static_cast<int>(ch.size())) {
            sel = static_cast<int>(ch.size()) - 1;
          }
          for (size_t i = 0; i < ch.size(); i++) {
            if (static_cast<int>(i) == sel) {
              attron(A_REVERSE);
            }
            mvprintw(4 + static_cast<int>(i), 2, "%s", ch[i].label.c_str());
            if (static_cast<int>(i) == sel) {
              attroff(A_REVERSE);
            }
          }
          mvprintw(5 + static_cast<int>(ch.size()), 0,
                   "Up/Down + Enter to accept");
        }
        refresh();
        int k = getch();
        if (k == 'q' || k == 'Q') {
          endwin();
          return 0;
        }
        if (ch.empty()) {
          continue;
        }
        if (k == KEY_UP || k == 'k') {
          if (sel > 0) {
            sel--;
          }
        } else if (k == KEY_DOWN || k == 'j') {
          if (sel < static_cast<int>(ch.size()) - 1) {
            sel++;
          }
        } else if (k == '\n' || k == KEY_ENTER) {
          if (lichess->acceptChallenge(ch[static_cast<size_t>(sel)].id)) {
            accepted = true;
          }
        }
      }
      timeout(-1);
    }

    // Wait for the paired game, then for its first full state.
    string gid;
    timeout(150);
    while (!lichess->gameStarted(gid)) {
      clear();
      mvprintw(LINES / 2, COLS / 2 - 22,
               "Waiting for a game to start... (q to cancel)");
      refresh();
      int k = getch();
      if (k == 'q' || k == 'Q') {
        endwin();
        return 0;
      }
    }
    lichess->startGameStream(gid);
    while (!lichess->ready(lichessColor)) {
      clear();
      mvprintw(LINES / 2, COLS / 2 - 11, "Game found, loading...");
      refresh();
      int k = getch();
      if (k == 'q' || k == 'Q') {
        endwin();
        return 0;
      }
    }
    timeout(-1);
    mode = 'L';
  }

  Board *gameboard = new Board();
  ChessFacade game(gameboard);
  game.fillBoard();
  setPromotionSelector(promptPromotion);

  bool botEnabled = false;
  string botInfo = "Bot: OFF";
  string status = "";

  if (!modeProvided && mode != 'h' && mode != 'n' && mode != 'L') {
    string mt = promptInput(11, "Bot movetime in ms [200]: ", "200");
    try {
      moveTimeMs = stoi(mt);
    } catch (...) {
      moveTimeMs = 200;
    }
    if (moveTimeMs <= 0) {
      moveTimeMs = 200;
    }
  }

  if (mode != 'h' && mode != 'n' && mode != 'L') {
    auto engine = make_unique<StockfishAdapter>();
    if (engine->initialize()) {
      game.setEngine(move(engine));
      game.setMoveTimeMs(moveTimeMs);
      botEnabled = true;
      if (mode == 'w' || mode == 'a') {
        game.setBotColor(WHITE, true);
      }
      if (mode == 'b' || mode == 'a') {
        game.setBotColor(BLACK, true);
      }
    } else {
      status = "Bot init failed. Playing human vs human.";
    }
  }

  int cursorX = 4;
  int cursorY = 0;
  bool flipped = false;
  bool flipOnTurn = false;
  bool centerBoard = false;
  bool hideUI = false;
  if (startFlipped || isClient || mode == 'w') {
    flipped = true;
  }
  if (lichessEnabled) {
    flipped = (lichessColor == BLACK);
  }
  if (startCentered) {
    centerBoard = true;
  }
  if (startHiddenUI) {
    hideUI = true;
  }

  if (botEnabled) {
    string side = "Bot: ";
    if (mode == 'a') {
      side += "BOTH";
    } else if (mode == 'w') {
      side += "WHITE";
    } else if (mode == 'b') {
      side += "BLACK";
    } else {
      side += "OFF";
    }
    botInfo = side + ", " + to_string(moveTimeMs) + "ms";
  }

  // Replay history
  vector<string> replayHistory;
  size_t replayIndex = 0;
  GameAnalysis analysis;     // filled on demand by pressing 'a'
  bool analysisActive = false;

  if (mode == 'r') {
    int r = 0, c = 0;
    getmaxyx(stdscr, r, c);
    int current_offsetY = startCentered ? ((r > 10) ? (r - 10) / 2 : 0) : 0;

    if (importFromChessCom) {
      clear(); // wipe menu/engine-init leftovers before prompting
      refresh();
      string user =
          promptInput(current_offsetY + 11, "chess.com username: ", "");
      clear();
      mvprintw(current_offsetY + 11, 0, "Fetching games for '%s'...",
               user.c_str());
      refresh();

      ChessComAdapter cc;
      vector<ChessComGame> games;
      string err = cc.fetchUserGames(user, games);
      if (!err.empty()) {
        endwin();
        cerr << "chess.com error: " << err << "\n";
        return 1;
      }

      const size_t maxList = 30; // keep the selection menu on one screen
      if (games.size() > maxList) {
        games.resize(maxList);
      }
      vector<string> labels;
      for (const auto &g : games) {
        labels.push_back(g.whiteUser + " (W) vs " + g.blackUser + " (B) | " +
                         g.whiteResult + "-" + g.blackResult + " | " +
                         g.timeClass + " | " + g.endDate);
      }
      int choice = promptMenu(
          2, "Select a game (newest first):", labels);

      string convErr;
      if (!sanGameToUci(games[static_cast<size_t>(choice)].pgn, replayHistory,
                        convErr)) {
        endwin();
        cerr << "Failed to parse game PGN: " << convErr << "\n";
        return 1;
      }
      game.fillBoard();
      // Orient the board from the imported player's side.
      const ChessComGame &chosen = games[static_cast<size_t>(choice)];
      auto lower = [](string s) {
        for (char &ch : s)
          ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        return s;
      };
      if (lower(user) == lower(chosen.blackUser)) {
        flipped = true;
      }
      status = "Imported chess.com game: " +
               to_string(replayHistory.size()) + " moves. Press 'a' to analyse.";
    } else {
      string filename = promptInput(current_offsetY + 11,
                                    "Enter replay file path: ", "save.txt");
      ifstream inFile(filename);
      if (inFile.is_open()) {
        string mv;
        while (inFile >> mv) {
          replayHistory.push_back(mv);
        }
        inFile.close();
        game.fillBoard();
        status = "Replay loaded. Total moves: " +
                 to_string(replayHistory.size()) + ". Press 'a' to analyse.";
      } else {
        endwin();
        cerr << "Error: Replay file '" << filename << "' not found!\n";
        return 1;
      }
    }
  }

  while (mode == 'r' || !game.isGameOver()) {
    Color turn = game.getCurrentTurn();

    // Replay mode.
    if (mode == 'r') {
      drawBoard(game, cursorX, cursorY, {}, status, botInfo, turn, flipped,
                flipOnTurn, centerBoard, hideUI);
      {
        int ox = 0, oy = 0;
        computeOffsets(centerBoard, ox, oy);
        drawAnalysisPanel(ox, oy, analysisActive, replayIndex,
                          replayHistory.size(), analysis, false, 0, 0);
      }

      if (autoPlay) {
        timeout(moveTimeMs); // Задержка берется из -t (по умолчанию 200мс)
      } else {
        timeout(-1); // Обычное блокирующее ожидание
      }

      int ch = getch();

      if (autoPlay && ch == ERR) {
        ch = KEY_RIGHT;
      }

      if (ch == 'q' || ch == 'Q') {
        break;
      } else if (ch == 'i') {
        hideUI = !hideUI;
      } else if (ch == 't') {
        gTheme = (gTheme + 1) % THEME_COUNT;
        initBoardColors(gTheme);
        status = "Theme: " + string(themeName(gTheme));
      } else if (ch == 'c' || ch == 'C') {
        centerBoard = !centerBoard;
      } else if (ch == ' ') {
        autoPlay = !autoPlay; // Старт / Стоп автоплея по нажатию на пробел
        status = autoPlay ? "Autoplay ON" : "Autoplay OFF";
      } else if (ch == 'f' || ch == 'F') {
        flipOnTurn = !flipOnTurn;
        if (flipOnTurn) {
          flipped = (game.getCurrentTurn() == BLACK);
        }
      } else if (ch == 'e' || ch == 'E') {
        status = "Move " + to_string(replayIndex) + "/" +
                 to_string(replayHistory.size()) + " | Analyzing position...";
        drawBoard(game, cursorX, cursorY, {}, status, botInfo, turn, flipped,
                  flipOnTurn, centerBoard, hideUI);
        refresh();
        status = "Move " + to_string(replayIndex) + "/" +
                 to_string(replayHistory.size()) + " | " +
                 game.getPositionEvaluation();
      } else if (ch == 'a' || ch == 'A') {
        if (!botEnabled) {
          status = "Analysis needs Stockfish (engine offline)";
        } else if (replayHistory.empty()) {
          status = "Nothing to analyse";
        } else {
          int ox = 0, oy = 0;
          computeOffsets(centerBoard, ox, oy);
          string mt =
              promptInput(oy + 11, "Analysis movetime ms [" +
                                       to_string(moveTimeMs) + "]: ",
                          to_string(moveTimeMs));
          int amt = moveTimeMs;
          try {
            amt = stoi(mt);
          } catch (...) {
            amt = moveTimeMs;
          }
          if (amt < 10) {
            amt = 10;
          }
          size_t savedIndex = replayIndex;
          analysis = analyzeGame(
              game, replayHistory, amt, [&](int done, int total) {
                int pox = 0, poy = 0;
                computeOffsets(centerBoard, pox, poy);
                drawBoard(game, cursorX, cursorY, {}, "Analysing game...",
                          botInfo, game.getCurrentTurn(), flipped, flipOnTurn,
                          centerBoard, hideUI);
                drawAnalysisPanel(pox, poy, true, 0, replayHistory.size(),
                                  analysis, true, done, total);
              });
          // Restore the board to where the user was (mirrors KEY_LEFT rebuild).
          game.fillBoard();
          for (size_t i = 0; i < savedIndex; i++) {
            game.makeMoveByUCI(replayHistory[i]);
          }
          replayIndex = savedIndex;
          game.setMoveTimeMs(moveTimeMs); // restore eval movetime
          if (flipOnTurn) {
            flipped = (game.getCurrentTurn() == BLACK);
          }
          analysisActive = analysis.complete;
          status = analysis.complete
                       ? "Analysis complete (movetime " + to_string(amt) +
                             "ms). Use arrows to browse."
                       : "Analysis failed";
        }
      } else if (ch == KEY_RIGHT || ch == 'l') {
        if (replayIndex < replayHistory.size()) {
          game.makeMoveByUCI(replayHistory[replayIndex]);
          replayIndex++;
          status = "Move " + to_string(replayIndex) + "/" +
                   to_string(replayHistory.size());
          if (flipOnTurn) {
            flipped = (game.getCurrentTurn() == BLACK);
          }
        } else {
          autoPlay = false; // Выключаем автоплей на конце файла
          status =
              "End of replay. Total moves: " + to_string(replayHistory.size());
        }
      } else if (ch == KEY_LEFT || ch == 'j') {
        if (replayIndex > 0) {
          autoPlay = false; // Клик назад останавливает автоплей
          replayIndex--;
          game.fillBoard();
          for (size_t i = 0; i < replayIndex; i++) {
            game.makeMoveByUCI(replayHistory[i]);
          }
          status = "Move " + to_string(replayIndex) + "/" +
                   to_string(replayHistory.size());
          if (flipOnTurn) {
            flipped = (game.getCurrentTurn() == BLACK);
          }
        } else {
          status = "Start of replay. Initial position.";
        }
      }
      continue;
    }
    // network_adapter
    if (networkEnabled) {
      bool isMyTurn =
          (isServer && turn == WHITE) || (isClient && turn == BLACK);

      if (!isMyTurn) {
        status = "Waiting for opponent...";
        // Твоя функция drawBoard (добавил параметры)
        drawBoard(game, cursorX, cursorY, {}, status, botInfo, turn, flipped,
                  flipOnTurn, centerBoard, hideUI, "network", &netAdapter);

        timeout(20); // Неблокирующий опрос
        int ch = getch();
        if (ch == 'q' || ch == 'Q')
          break;
        if (ch == 'i') {
          hideUI = !hideUI;
        } else if (ch == 'c' || ch == 'C') {
          centerBoard = !centerBoard;
        } else if (ch == 'f' || ch == 'F') {
          flipOnTurn = !flipOnTurn;
          if (flipOnTurn) {
            flipped = (game.getCurrentTurn() == BLACK);
          }
        } else if (ch == 't') {
          gTheme = (gTheme + 1) % THEME_COUNT;
          initBoardColors(gTheme);
        }

        string enemyMove;
        if (netAdapter.tryReadMove(enemyMove)) {
          if (enemyMove == "DISCONNECT") {
            status = "Opponent disconnected!";
            timeout(-1);
            drawBoard(game, cursorX, cursorY, {}, status, botInfo, turn,
                      flipped, flipOnTurn, centerBoard, hideUI, "network",
                      &netAdapter);
            getch();
            break;
          }
          // Применяем ход
          if (!game.makeMoveByUCI(enemyMove)) {
            status = "Network Error: Opponent sent invalid move!";
            timeout(-1);
            drawBoard(game, cursorX, cursorY, {}, status, botInfo, turn,
                      flipped, flipOnTurn, centerBoard, hideUI, "network",
                      &netAdapter);
            getch();
            break;
          }
          if (flipOnTurn)
            flipped = (game.getCurrentTurn() == BLACK);
        }
        continue; // Ждем дальше
      } else {
        timeout(-1); // Наш ход — обычный getch
      }
    }
    // End of network_adapter

    // Lichess live game: the server is the source of truth.
    if (lichessEnabled) {
      // Server-side endings (resign/abort/timeout/draw) are not detectable from
      // the local board, so report them straight from the stream.
      string lst, lwin;
      if (lichess->isGameOver(lst, lwin) && !game.isGameOver()) {
        string res = "Game over (" + lst + ")";
        if (!lwin.empty()) {
          res += " - " + lwin + " wins";
        }
        timeout(-1);
        drawBoard(game, cursorX, cursorY, {}, res, botInfo, turn, flipped,
                  flipOnTurn, centerBoard, hideUI);
        getch();
        break;
      }

      // Reconcile the local board with the server's full move list.
      {
        vector<string> sm = lichess->snapshotMoves();
        const vector<string> &local = game.getMoveHistory();
        bool localPrefix = local.size() <= sm.size() &&
                           equal(local.begin(), local.end(), sm.begin());
        bool serverPrefix = sm.size() <= local.size() &&
                            equal(sm.begin(), sm.end(), local.begin());
        if (localPrefix) {
          for (size_t i = local.size(); i < sm.size(); i++) {
            game.makeMoveByUCI(sm[i]);
          }
          if (flipOnTurn) {
            flipped = (game.getCurrentTurn() == BLACK);
          }
        } else if (serverPrefix) {
          // Local is ahead: our own move is not echoed yet — wait for it.
        } else {
          // Divergence (e.g. our move was rejected): rebuild from the server.
          game.fillBoard();
          for (const string &m : sm) {
            game.makeMoveByUCI(m);
          }
          if (flipOnTurn) {
            flipped = (game.getCurrentTurn() == BLACK);
          }
        }
      }

      turn = game.getCurrentTurn();
      long wt = 0, bt = 0, wi = 0, bi = 0;
      lichess->clocks(wt, bt, wi, bi);
      string clockStr = "W " + formatClock(wt) + "  B " + formatClock(bt);
      bool isMyTurn = (turn == lichessColor);

      if (!isMyTurn) {
        status = "Waiting for opponent... (R resign) | " + clockStr;
        drawBoard(game, cursorX, cursorY, {}, status, botInfo, turn, flipped,
                  flipOnTurn, centerBoard, hideUI);
        timeout(150); // poll the stream
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
          break;
        }
        if (ch == 'i') {
          hideUI = !hideUI;
        } else if (ch == 'c' || ch == 'C') {
          centerBoard = !centerBoard;
        } else if (ch == 'f' || ch == 'F') {
          flipOnTurn = !flipOnTurn;
          if (flipOnTurn) {
            flipped = (game.getCurrentTurn() == BLACK);
          }
        } else if (ch == 'R') {
          lichess->resign();
          status = "Resigning...";
        } else if (ch == 't') {
          gTheme = (gTheme + 1) % THEME_COUNT;
          initBoardColors(gTheme);
        }
        continue;
      } else {
        status = "Your move (R resign) | " + clockStr;
        // Wake periodically to refresh clocks and notice a server-side ending.
        timeout(250);
      }
    }
    // End of Lichess

    // Auto-play bot moves
    if (botEnabled && game.isBotTurn()) {
      status = "Bot is thinking...";
      drawBoard(game, cursorX, cursorY, {}, status, botInfo, turn, flipped,
                flipOnTurn, centerBoard, hideUI);
      refresh();
      usleep(100000); // Brief display of thinking message

      if (game.playBotMove()) {
        status = "Bot moved";
        if (flipOnTurn) {
          flipped = (game.getCurrentTurn() == BLACK);
        }
      } else {
        status = "Bot move failed!";
      }
      usleep(10000); // Avoid busy loop in bot-vs-bot
      continue;
    }

    vector<Coordinates> validMoves = game.getSelectedValidMoves();
    drawBoard(game, cursorX, cursorY, validMoves, status, botInfo, turn,
              flipped, flipOnTurn, centerBoard, hideUI);
    int ch = getch();
    if (ch == 'q' || ch == 'Q') {
      break;
    }
    if (ch == 'i') {
      hideUI = !hideUI;
    } else if (ch == 't') {
      gTheme = (gTheme + 1) % THEME_COUNT;
      initBoardColors(gTheme);
      status = "Theme: " + string(themeName(gTheme));
    } else if (ch == 'c' || ch == 'C') {
      centerBoard = !centerBoard;
    } else if ((ch == KEY_UP || ch == 'k') && cursorY < 7) {
      cursorY++;
    } else if ((ch == KEY_DOWN || ch == 'j') && cursorY > 0) {
      cursorY--;
    } else if ((ch == KEY_LEFT || ch == 'h') && cursorX > 0) {
      cursorX--;
    } else if ((ch == KEY_RIGHT || ch == 'l') && cursorX < 7) {
      cursorX++;
    } else if (ch == 'f' || ch == 'F') {
      flipOnTurn = !flipOnTurn;
      if (flipOnTurn) {
        flipped = (game.getCurrentTurn() == BLACK);
      }
    } else if (ch == 's' || ch == 'S') {
      // Вычисление текущего смещения по вертикали
      int r = 0, c = 0;
      getmaxyx(stdscr, r, c);
      int current_offsetY = centerBoard ? ((r > 10) ? (r - 10) / 2 : 0) : 0;

      // Экспорт истории в файл
      string filename =
          promptInput(current_offsetY + 17, "Save to file: ", "save.txt");
      ofstream outFile(filename);
      if (outFile.is_open()) {
        for (const string &m : game.getMoveHistory()) {
          outFile << m << " "; // Запись ходов через пробел
        }
        outFile.close();
        status = "Game saved to " + filename;
      } else {
        status = "Failed to save file!";
      }
    } else if (ch == 'o' || ch == 'O') {
      if (networkEnabled || lichessEnabled) {
        status = "Cannot load game in multiplayer!";
      } else {
        // Вычисление текущего смещения по вертикали
        int r = 0, c = 0;
        getmaxyx(stdscr, r, c);
        int current_offsetY = centerBoard ? ((r > 10) ? (r - 10) / 2 : 0) : 0;

        string filename =
            promptInput(current_offsetY + 17, "Load from file: ", "save.txt");
        ifstream inFile(filename);
        if (inFile.is_open()) {
          vector<string> history;
          string move;
          while (inFile >> move) {
            history.push_back(move);
          }
          inFile.close();

          if (game.loadFromHistory(history)) {
            status = "Game loaded from " + filename;
            if (flipOnTurn) {
              flipped = (game.getCurrentTurn() == BLACK);
            }
          } else {
            status = "Error loading game!";
          }
        } else {
          status = "File not found!";
        }
      }
    } else if (ch == 'R' && lichessEnabled) {
      lichess->resign();
      status = "Resigning...";
    } else if (ch == 27) { // Esc cancels the current selection
      if (game.hasSelection()) {
        game.clearSelection();
        status = "Selection cancelled";
      }
    } else if (ch == '\n' || ch == KEY_ENTER) {
      int bx = flipped ? (7 - cursorX) : cursorX;
      int by = flipped ? (7 - cursorY) : cursorY;
      Coordinates cursor(bx, by);
      if (!game.hasSelection()) {
        if (game.selectFigure(turn, cursor)) {
          status = "Selected piece";
        } else {
          status = "Select your piece with available moves";
        }
      } else if (game.getSelectedCoordinates().equals(cursor)) {
        // Enter on the already-selected square deselects it.
        game.clearSelection();
        status = "Selection cancelled";
      } else {
        if (game.moveFigure(cursor)) {
          bool sendFailed = false;
          if (networkEnabled) {
            netAdapter.sendMove(game.getLastExecutedUCIMove());
          }
          if (lichessEnabled) {
            sendFailed = !lichess->sendMove(game.getLastExecutedUCIMove());
          }
          status = game.getLastMessage();
          if (status.empty()) {
            status = "Move done";
          }
          if (sendFailed) {
            status = "Lichess rejected move (will resync)";
          }
          if (flipOnTurn) {
            flipped = (game.getCurrentTurn() == BLACK);
          }
        } else {
          status = "Invalid move";
        }
      }
    }
  }

  if (game.isGameOver()) {
    string finalStatus =
        game.getLastMessage() + " | Final. You can save game to see replay";
    while (true) {
      drawBoard(game, cursorX, cursorY, {}, finalStatus, botInfo,
                game.getCurrentTurn(), flipped, flipOnTurn, centerBoard,
                hideUI);

      int ch = getch();
      if (ch == 's' || ch == 'S') {
        int r = 0, c = 0;
        getmaxyx(stdscr, r, c);
        int current_offsetY = centerBoard ? ((r > 10) ? (r - 10) / 2 : 0) : 0;

        string filename =
            promptInput(current_offsetY + 17, "Save to file: ", "save.txt");
        ofstream outFile(filename);
        if (outFile.is_open()) {
          for (const string &m : game.getMoveHistory()) {
            outFile << m << " ";
          }
          outFile.close();
          finalStatus =
              "Game saved to " + filename + " | Press any key to exit";
        } else {
          finalStatus =
              "Failed to save file! | 's' retry, any other key to exit";
        }
      } else if (ch == 'f' || ch == 'F') {
        flipped = !flipped;
      } else if (ch == 'c' || ch == 'C') {
        centerBoard = !centerBoard;
      } else if (ch == 'i') {
        hideUI = !hideUI;
      } else if (ch == 't') {
        gTheme = (gTheme + 1) % THEME_COUNT;
        initBoardColors(gTheme);
      } else {
        break;
      }
    }
  }

  endwin();
  return 0;
}
