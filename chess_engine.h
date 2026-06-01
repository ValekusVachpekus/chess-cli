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

#ifndef CHESS_ENGINE_H
#define CHESS_ENGINE_H

#include <string>

using namespace std;

class ChessEngine {
public:
  virtual ~ChessEngine() = default;

  /**
   * Get the best move for the given FEN position.
   * @param fen FEN notation of current board position
   * @param movetime_ms Maximum thinking time in milliseconds
   * @return Best move in UCI format (e.g., "e2e4")
   */
  virtual string getBestMove(const string &fen, int movetime_ms) = 0;

  /**
   * Check if the engine is available/ready to play
   * @return true if engine is ready, false if unavailable or crashed
   */
  virtual bool isAvailable() = 0;

  /**
   * Initialize/reconnect to the engine
   * @return true if successful, false otherwise
   */
  virtual bool initialize() = 0;

  /**
   * Gracefully shutdown the engine
   */
  virtual void shutdown() = 0;
};

#endif // CHESS_ENGINE_H
