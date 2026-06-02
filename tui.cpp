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
#include <fstream>
#include <getopt.h>
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

// Added network_adapter
void drawBoard(ChessFacade &game, int cursorX, int cursorY,
               const vector<Coordinates> &validMoves, const string &status,
               const string &botInfo, Color turn, bool flipped, bool flipOnTurn,
               bool centerBoard, bool hideUI, string gameMode = "white",
               NetworkAdapter *netAdapter = nullptr) {
  clear();
  int rows = 0, cols = 0;
  getmaxyx(stdscr, rows, cols);
  const int boardWidth = 19;
  const int boardHeight = 10;
  int offsetX =
      centerBoard ? ((cols > boardWidth) ? (cols - boardWidth) / 2 : 0) : 0;
  int offsetY =
      centerBoard ? ((rows > boardHeight) ? (rows - boardHeight) / 2 : 0) : 0;

  int row = 8;
  for (int sy = 7; sy >= 0; sy--) {
    int displayRow = flipped ? (8 - sy) : row--;
    mvprintw(offsetY + (8 - sy), offsetX, "%d| ", displayRow);
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
      mvprintw(offsetY + (8 - sy), offsetX + 3 + sx * 2, "%s", cell.c_str());
      if (isCursor) {
        attroff(A_REVERSE);
      }
      if (colorPair != 0) {
        attroff(COLOR_PAIR(colorPair));
      }
    }
  }
  if (flipped) {
    mvprintw(offsetY + 9, offsetX, "| H G F E D C B A");
  } else {
    mvprintw(offsetY + 9, offsetX, "| A B C D E F G H");
  }
  if (!hideUI) {
    mvprintw(offsetY + 11, offsetX, "Turn: %s",
             (turn == WHITE) ? "WHITE" : "BLACK");
    mvprintw(offsetY + 12, offsetX, "Status: %s", status.c_str());
    mvprintw(offsetY + 13, offsetX, "%s", botInfo.c_str());
    mvprintw(offsetY + 14, offsetX, "Flip on turn: %s",
             flipOnTurn ? "ON" : "OFF");
    mvprintw(offsetY + 15, offsetX,
             "Controls: arrows/jkl, Enter select, f flip, C center, i hide "
             "interface, s "
             "save, o load, q quit");
  }
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
      << "  -m, --mode <mode>    Game mode: human, white, black, auto, replay\n"
      << "  -t, --time <ms>      Bot movetime in milliseconds (default: 200)\n"
      << "  -f, --flip           Flip board at start (black on bottom)\n"
      << "  -c, --center         Center board in terminal\n"
      << "  -H, --hide-ui        Hide UI text (show only board)\n"
      << "  -S, --server         Start as server (plays White)\n"
      << "  -C, --connect <IP>   Connect to a server (plays Black)\n"
      << "  -P, --port <port>    Network port (default: 8888)\n"
      << "  -h, --help           Show this help\n"
      << "  -v, --version        Show version\n";
}

void printVersion() {
  cout << "chess-tui version 1.1.0\n"
       << "Copyright (C) 2026 Ilia Shchetkov\n"
       << "License GPLv3+: GNU GPL version 3 or later "
          "<https://gnu.org/licenses/gpl.html>.\n"
       << "This is free software: you are free to change and redistribute it.\n"
       << "There is NO WARRANTY, to the extent permitted by law.\n";
}

int main(int argc, char **argv) {
  string modeArg;
  bool modeProvided = false;
  bool timeProvided = false;
  int moveTimeMs = 200;
  bool startFlipped = false;
  bool startCentered = false;
  bool startHiddenUI = false;
  bool autoPlay = false;

  // For network_adapter
  bool isServer = false;
  bool isClient = false;
  string connectIp = "";
  int port = 8888;

  static struct option long_options[] = {
      {"mode", required_argument, nullptr, 'm'},
      {"time", required_argument, nullptr, 't'},
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
  while ((opt = getopt_long(argc, argv, "m:t:SC:P:fcHhv", long_options,
                            &option_index)) != -1) {
    switch (opt) {
    case 'm':
      modeArg = optarg;
      modeProvided = true;
      if (modeArg != "human" && modeArg != "white" && modeArg != "black" &&
          modeArg != "auto" && modeArg != "replay") {
        cerr << "Error: invalid mode '" << modeArg
             << "'. Use human, white, black, auto, replay.\n";
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
      timeProvided = true;
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

  NetworkAdapter netAdapter;
  bool networkEnabled = false;
  char mode = 'h';
  if (isServer) {
    if (netAdapter.startServer(port)) {
      networkEnabled = true;
      mode = 'n'; // n - marker for network
    }
  } else if (isClient) {
    if (netAdapter.connectToServer(connectIp, port)) {
      networkEnabled = true;
      mode = 'n'; // n - marker for network
    }
  }

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
  setPromotionSelector(promptPromotion);

  bool botEnabled = false;
  string botInfo = "Bot: OFF";
  string status = "";

  if (!networkEnabled) {
    if (modeProvided) {
      if (modeArg == "white") {
        mode = 'w';
      } else if (modeArg == "black") {
        mode = 'b';
      } else if (modeArg == "auto") {
        mode = 'a';
      } else if (modeArg == "replay") {
        mode = 'r';
      } else {
        mode = 'h';
      }
    } else {
      vector<string> modeOptions = {
          "Human vs Human", "Bot plays White", "Bot plays Black",
          "Bot vs Bot",     "Replay",
      };
      int modeIndex = promptMenu(2, "Select game mode:", modeOptions);
      if (modeIndex == 1) {
        mode = 'w';
      } else if (modeIndex == 2) {
        mode = 'b';
      } else if (modeIndex == 3) {
        mode = 'a';
      } else if (modeIndex == 4) {
        mode = 'r';
      }
    }
  }

  if (!modeProvided && mode != 'h' && mode != 'n') {
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

  if (mode != 'h' && mode != 'n') {
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

  if (mode == 'r') {
    int r = 0, c = 0;
    getmaxyx(stdscr, r, c);
    int current_offsetY = startCentered ? ((r > 10) ? (r - 10) / 2 : 0) : 0;

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
      status = "Replay loaded. Total moves: " + to_string(replayHistory.size());
    } else {
      endwin();
      cerr << "Error: Replay file '" << filename << "' not found!\n";
      return 1;
    }
  }

  while (mode == 'r' || !game.isGameOver()) {
    Color turn = game.getCurrentTurn();

    // Replay mode.
    if (mode == 'r') {
      drawBoard(game, cursorX, cursorY, {}, status, botInfo, turn, flipped,
                flipOnTurn, centerBoard, hideUI);

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
      if (networkEnabled) {
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
          if (networkEnabled) {
            netAdapter.sendMove(game.getLastExecutedUCIMove());
          }
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
      } else {
        break;
      }
    }
  }

  endwin();
  return 0;
}
