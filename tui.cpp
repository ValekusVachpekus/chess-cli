#define CHESSGAME_NO_MAIN
#define CHESSGAME_SILENT
#include "gameboard.cpp"

#include <locale.h>
#include <ncurses.h>

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
               Color turn, bool flipped, bool flipOnTurn) {
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
  mvprintw(13, 0, "Flip on turn: %s", flipOnTurn ? "ON" : "OFF");
  mvprintw(14, 0,
           "Controls: arrows/hjkl move, Enter select/move, f toggle flip, q "
           "quit");
  refresh();
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

  int cursorX = 4;
  int cursorY = 0;
  Color turn = WHITE;
  string status = "";
  bool flipped = false;
  bool flipOnTurn = false;

  while (!game.isGameOver()) {
    vector<Coordinates> validMoves = game.getSelectedValidMoves();
    drawBoard(game, cursorX, cursorY, validMoves, status, turn, flipped,
              flipOnTurn);
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
        flipped = (turn == BLACK);
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
          turn = (turn == WHITE) ? BLACK : WHITE;
          if (flipOnTurn) {
            flipped = (turn == BLACK);
          }
        } else {
          status = "Invalid move";
        }
      }
    }
  }

  if (game.isGameOver()) {
    drawBoard(game, cursorX, cursorY, {}, game.getLastMessage(), turn, flipped,
              flipOnTurn);
    getch();
  }

  endwin();
  return 0;
}
