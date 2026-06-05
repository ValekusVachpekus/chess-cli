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

#ifndef STOCKFISH_ADAPTER_H
#define STOCKFISH_ADAPTER_H

#include "chess_engine.h"
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

class StockfishAdapter : public ChessEngine {
private:
  pid_t process_pid = -1;
  int stdin_fd = -1;
  int stdout_fd = -1;
  bool available = false;
  std::string read_buffer; // Leftover bytes between readLine() calls

  /**
   * Write a command to Stockfish stdin
   */
  bool writeCommand(const std::string &command) {
    if (stdin_fd < 0) {
      return false;
    }
    std::string cmd = command + "\n";
    ssize_t bytes_written = write(stdin_fd, cmd.c_str(), cmd.length());
    if (bytes_written < 0) {
      std::cerr << "Failed to write to Stockfish stdin" << std::endl;
      return false;
    }
    return true;
  }

  /**
   * Read a line from Stockfish stdout. Reads in chunks and keeps any leftover
   * bytes in read_buffer for the next call.
   */
  std::string readLine(int timeout_ms = 60000) {
    if (stdout_fd < 0) {
      return "";
    }

    auto start = std::chrono::high_resolution_clock::now();

    while (true) {
      size_t nl = read_buffer.find('\n');
      if (nl != std::string::npos) {
        std::string line = read_buffer.substr(0, nl);
        read_buffer.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        return line;
      }

      auto now = std::chrono::high_resolution_clock::now();
      auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
              .count();
      if (elapsed > timeout_ms) {
        std::cerr << "Timeout reading from Stockfish" << std::endl;
        return "";
      }

      char buffer[256];
      ssize_t bytes_read = read(stdout_fd, buffer, sizeof(buffer));
      if (bytes_read < 0) {
        if (errno == EINTR) {
          continue;
        }
        std::cerr << "Error reading from Stockfish stdout" << std::endl;
        return "";
      }
      if (bytes_read == 0) {
        std::cerr << "Stockfish closed the connection (EOF)" << std::endl;
        return "";
      }
      read_buffer.append(buffer, static_cast<size_t>(bytes_read));
    }
  }

  /**
   * Wait for a specific response from Stockfish
   */
  bool waitForResponse(const std::string &expected, int timeout_ms = 60000) {
    auto start = std::chrono::high_resolution_clock::now();

    while (true) {
      if (process_pid > 0) {
        int status = 0;
        pid_t res = waitpid(process_pid, &status, WNOHANG);
        if (res == process_pid) {
          std::cerr << "Stockfish process exited before responding" << std::endl;
          return false;
        }
      }
      auto now = std::chrono::high_resolution_clock::now();
      auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
              .count();
      if (elapsed > timeout_ms) {
        std::cerr << "Timeout waiting for: " << expected << std::endl;
        return false;
      }

      std::string line = readLine(timeout_ms - elapsed);
      if (line.empty()) {
        continue;
      }
      if (line.find(expected) != std::string::npos) {
        return true;
      }
    }
  }

public:
  virtual ~StockfishAdapter() { shutdown(); }

  bool initialize() override {
    if (available) {
      return true;
    }

    int stdin_pipe[2], stdout_pipe[2];

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
      std::cerr << "Failed to create pipes" << std::endl;
      return false;
    }

    process_pid = fork();
    if (process_pid < 0) {
      std::cerr << "Failed to fork Stockfish process" << std::endl;
      return false;
    }

    if (process_pid == 0) {
      close(stdin_pipe[1]);
      close(stdout_pipe[0]);

      dup2(stdin_pipe[0], STDIN_FILENO);
      dup2(stdout_pipe[1], STDOUT_FILENO);

      close(stdin_pipe[0]);
      close(stdout_pipe[1]);

      const char *stockfish_path = getenv("STOCKFISH_PATH");
      if (stockfish_path != nullptr && stockfish_path[0] != '\0') {
        execl(stockfish_path, "stockfish", nullptr);
      } else {
        execlp("stockfish", "stockfish", nullptr);
      }
      std::cerr << "Failed to exec stockfish (set STOCKFISH_PATH or install "
                   "stockfish in PATH)"
                << std::endl;
      exit(1);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    stdin_fd = stdin_pipe[1];
    stdout_fd = stdout_pipe[0];

    if (!writeCommand("uci")) {
      std::cerr << "Failed to send uci command" << std::endl;
      shutdown();
      return false;
    }

    if (!waitForResponse("uciok")) {
      std::cerr << "Stockfish did not respond to uci command" << std::endl;
      shutdown();
      return false;
    }

    if (!writeCommand("isready")) {
      std::cerr << "Failed to send isready command" << std::endl;
      shutdown();
      return false;
    }

    if (!waitForResponse("readyok")) {
      std::cerr << "Stockfish did not respond to isready command" << std::endl;
      shutdown();
      return false;
    }

    available = true;
    std::cout << "Stockfish initialized successfully" << std::endl;
    return true;
  }

  std::string getEvaluation(const std::string &cmd, int movetime_ms,
                            bool isWhiteTurn) override {
    if (!available)
      return "Stockfish unavailable";
    if (!writeCommand(cmd))
      return "Failed to send position command";
    if (!writeCommand("go movetime " + std::to_string(movetime_ms)))
      return "Failed to send go command";

    std::string last_score = "0.00";
    std::string best_move = "";

    while (true) {
      std::string line = readLine(movetime_ms + 1000);
      if (line.empty())
        break;

      size_t score_pos = line.find("score ");
      if (score_pos != std::string::npos) {
        // Парсинг оценки в пешках (centipawns)
        size_t cp_pos = line.find("cp ", score_pos);
        if (cp_pos != std::string::npos) {
          try {
            int cp = std::stoi(line.substr(cp_pos + 3));
            if (!isWhiteTurn)
              cp = -cp; // Перевод в абсолютную оценку (+ у белых, - у черных)
            double val = cp / 100.0;
            std::stringstream ss;
            if (val > 0)
              ss << "+";
            ss << std::fixed << std::setprecision(2) << val;
            last_score = ss.str();
          } catch (...) {
          }
        }
        // Парсинг матовых угроз
        size_t mate_pos = line.find("mate ", score_pos);
        if (mate_pos != std::string::npos) {
          try {
            int moves = std::stoi(line.substr(mate_pos + 5));
            if (!isWhiteTurn)
              moves = -moves;
            if (moves > 0)
              last_score = "M" + std::to_string(moves);
            else
              last_score = "-M" + std::to_string(std::abs(moves));
          } catch (...) {
          }
        }
      }

      // Ожидание финального лучшего хода для завершения цикла
      if (line.find("bestmove") != std::string::npos) {
        size_t move_pos = line.find("bestmove") + 9;
        size_t move_end = line.find(" ", move_pos);
        if (move_end == std::string::npos)
          move_end = line.length();
        best_move = line.substr(move_pos, move_end - move_pos);
        break;
      }
    }
    return "Evaluation: " + last_score + " | Best move: " + best_move;
  }

  std::string getBestMove(const std::string &cmd, int movetime_ms) override {
    if (!available) {
      std::cerr << "Stockfish not available" << std::endl;
      return "";
    }

    std::string pos_cmd = cmd;
    if (!writeCommand(pos_cmd)) {
      std::cerr << "Failed to send position command" << std::endl;
      available = false;
      return "";
    }

    std::string go_cmd = "go movetime " + std::to_string(movetime_ms);
    if (!writeCommand(go_cmd)) {
      std::cerr << "Failed to send go command" << std::endl;
      available = false;
      return "";
    }

    while (true) {
      std::string line = readLine(movetime_ms + 1000);
      if (line.empty()) {
        std::cerr << "Failed to get move from Stockfish" << std::endl;
        available = false;
        return "";
      }

      if (line.find("bestmove") != std::string::npos) {
        size_t move_pos = line.find("bestmove") + 9;
        size_t move_end = line.find(" ", move_pos);
        if (move_end == std::string::npos) {
          move_end = line.length();
        }
        std::string move = line.substr(move_pos, move_end - move_pos);

        return move;
      }
    }
  }

  bool isAvailable() override { return available; }

  void shutdown() override {
    if (stdin_fd >= 0) {
      close(stdin_fd);
      stdin_fd = -1;
    }
    if (stdout_fd >= 0) {
      close(stdout_fd);
      stdout_fd = -1;
    }
    if (process_pid > 0) {
      kill(process_pid, SIGTERM);
      int status;
      waitpid(process_pid, &status, 0);
      process_pid = -1;
    }
    available = false;
  }
};

#endif // STOCKFISH_ADAPTER_H
