#define CHESSGAME_NO_MAIN
#define CHESSGAME_SILENT
#include "gameboard.cpp"

#include <locale.h>
#include <ncurses.h>
#include <unistd.h>

const int COLOR_WHITE_PIECE = 1;
const int COLOR_BLACK_PIECE = 2;
const int COLOR_HIGHLIGHT = 3;
const int COLOR_CAPTURE = 4;

bool isMoveInList(const vector<Coordinates> &moves, int x, int y) {
  for (const auto &move : moves) {
    if (move.getX() == x && move.getY() == y) {
      return true;
    }
  }
  return false;
}

void drawBoard(ChessFacade &game, int cursorX, int cursorY,
               const vector<Coordinates> &validMoves, const string &status,
               const string &botInfo, Color turn, bool flipped,
               bool flipOnTurn) {
  clear();
  int row = 8;
  for (int sy = 7; sy >= 0; sy--) {
    int displayRow = flipped ? (8 - sy) : row--;
    mvprintw(8 - sy, 0, "%d| ", displayRow);
    for (int sx = 0; sx < 8; sx++) {
      int bx = flipped ? (7 - sx) : sx;
      int by = flipped ? (7 - sy) : sy;
      bool isCursor = (sx == cursorX && sy == cursorY);
      bool isValid = isMoveInList(validMoves, bx, by);
      ChessFacade::PieceView piece = game.getPieceAt(Coordinates(bx, by));
      string cell = ".";
      int colorPair = 0;
      if (isValid) {
        if (piece.present) {
          cell = typeToString(piece.type);
          colorPair = COLOR_CAPTURE;
        } else {
          cell = "*";
          colorPair = COLOR_HIGHLIGHT;
        }
      } else if (piece.present) {
        cell = typeToString(piece.type);
        colorPair =
            (piece.color == WHITE) ? COLOR_WHITE_PIECE : COLOR_BLACK_PIECE;
      }

      if (colorPair != 0) {
        attron(COLOR_PAIR(colorPair));
      }
      if (isCursor) {
        attron(A_REVERSE);
      }
      mvprintw(8 - sy, 3 + sx * 2, "%s", cell.c_str());
      if (isCursor) {
        attroff(A_REVERSE);
      }
      if (colorPair != 0) {
        attroff(COLOR_PAIR(colorPair));
      }
    }
  }
  if (flipped) {
    mvprintw(9, 0, "| H G F E D C B A");
  } else {
    mvprintw(9, 0, "| A B C D E F G H");
  }
  mvprintw(11, 0, "Turn: %s", (turn == WHITE) ? "WHITE" : "BLACK");
  mvprintw(12, 0, "Status: %s", status.c_str());
  mvprintw(13, 0, "%s", botInfo.c_str());
  mvprintw(14, 0, "Flip on turn: %s", flipOnTurn ? "ON" : "OFF");
  mvprintw(15, 0,
           "Controls: arrows/hjkl move, Enter select/move, f toggle flip, q "
           "quit");
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

int promptMenu(int row, const string &title,
               const vector<string> &options) {
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

int main() {
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

  Board *gameboard = new Board();
  ChessFacade game(gameboard);
  game.fillBoard();

  bool botEnabled = false;
  string botInfo = "Bot: OFF";
  string status = "";

  vector<string> modeOptions = {
      "Human vs Human",
      "Bot plays White",
      "Bot plays Black",
      "Bot vs Bot",
  };
  int modeIndex = promptMenu(2, "Select game mode:", modeOptions);
  char mode = 'h';
  if (modeIndex == 1) {
    mode = 'w';
  } else if (modeIndex == 2) {
    mode = 'b';
  } else if (modeIndex == 3) {
    mode = 'a';
  }

  int moveTimeMs = 200;
  if (mode != 'h') {
    string mt = promptInput(11, "Bot movetime in ms [200]: ", "200");
    try {
      moveTimeMs = stoi(mt);
    } catch (...) {
      moveTimeMs = 200;
    }
    if (moveTimeMs <= 0) {
      moveTimeMs = 200;
    }

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

  while (!game.isGameOver()) {
    Color turn = game.getCurrentTurn();
    
    // Auto-play bot moves
    if (botEnabled && game.isBotTurn()) {
      status = "Bot is thinking...";
      drawBoard(game, cursorX, cursorY, {}, status, botInfo, turn, flipped,
                flipOnTurn);
      refresh();
      usleep(100000);  // Brief display of thinking message
      
      if (game.playBotMove()) {
        status = "Bot moved";
        if (flipOnTurn) {
          flipped = (game.getCurrentTurn() == BLACK);
        }
      } else {
        status = "Bot move failed!";
      }
      continue;
    }

    vector<Coordinates> validMoves = game.getSelectedValidMoves();
    drawBoard(game, cursorX, cursorY, validMoves, status, botInfo, turn,
              flipped, flipOnTurn);
    int ch = getch();
    if (ch == 'q' || ch == 'Q') {
      break;
    }
    if ((ch == KEY_UP || ch == 'k') && cursorY < 7) {
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
      } else {
        if (game.moveFigure(cursor)) {
          status = game.getLastMessage();
          if (status.empty()) {
            status = "Move done";
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
    drawBoard(game, cursorX, cursorY, {}, game.getLastMessage(), botInfo,
              game.getCurrentTurn(), flipped, flipOnTurn);
    getch();
  }

  endwin();
  return 0;
}
