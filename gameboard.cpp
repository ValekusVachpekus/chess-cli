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

#include "chess_engine.h"
#include "stockfish_adapter.h"
#include <cctype>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

using namespace std;

class Coordinates {
private:
  int x;
  int y;

public:
  Coordinates(int x, int y) : x(x), y(y) {}

  int getX() const { return x; }
  int getY() const { return y; }

  bool canMove() const {
    if (this->x > 7 || this->x < 0 || this->y > 7 || this->y < 0) {
      return false;
    } else {
      return true;
    }
  }

  void move(const Coordinates &coordinates) {
    this->x = coordinates.getX();
    this->y = coordinates.getY();
  }

  bool equals(const Coordinates &other) const {
    if (this->getX() == other.getX() && this->getY() == other.getY()) {
      return true;
    } else {
      return false;
    }
  }
};

enum Color { BLACK, WHITE, NONE };

enum Type { PAWN, HORSE, ELEPHANT, KING, QUEEN, LADYA };

string typeToString(Type type) {
  switch (type) {
  case PAWN: {
    return "";
  }
  case HORSE: {
    return "";
  }
  case ELEPHANT: {
    return "";
  }
  case KING: {
    return "";
  }
  case QUEEN: {
    return "";
  }
  case LADYA: {
    return "";
  }
  }
  return "";
}

typedef Type (*PromotionSelector)(Color);
static PromotionSelector gPromotionSelector = nullptr;
static bool gPromotionOverrideActive = false;
static Type gPromotionOverride = QUEEN;

void setPromotionSelector(PromotionSelector selector) {
  gPromotionSelector = selector;
}

void setPromotionOverride(Type type) {
  gPromotionOverride = type;
  gPromotionOverrideActive = true;
}

Type consumePromotionOverride() {
  if (!gPromotionOverrideActive) {
    return QUEEN;
  }
  gPromotionOverrideActive = false;
  return gPromotionOverride;
}

class IFigure;

const string ANSI_RESET = "\033[0m";
const string ANSI_RED = "\033[31m";
const string ANSI_BLUE = "\033[34m";
const string ANSI_YELLOW = "\033[33m";

string colorize(const string &text, const string &color) {
  return color + text + ANSI_RESET;
}

class IFigure {
public:
  virtual ~IFigure() = default;
  virtual bool isValidMove(const Coordinates &coordinates) = 0;
  virtual bool move(const Coordinates &coordinates) = 0;
  virtual void setCoordinates(const Coordinates &coordinates) = 0;
  virtual Color getColor() const = 0;
  virtual Coordinates getCoordinates() const = 0;
  virtual int getCost() const = 0;
  virtual int getId() const = 0;
  virtual Type getType() const = 0;
  virtual vector<Coordinates> getValidMoves() = 0;
};

string figureToString(const IFigure *figure) {
  if (figure == nullptr) {
    return ".";
  }
  string symbol = typeToString(figure->getType());
  if (figure->getColor() == WHITE) {
    return colorize(symbol, ANSI_BLUE);
  }
  if (figure->getColor() == BLACK) {
    return colorize(symbol, ANSI_YELLOW);
  }
  return symbol;
}

#include "fen_converter.h"

class Figure;

class IBoard {
public:
  virtual ~IBoard() = default;
  virtual bool isEmpty(const Coordinates &coordinates) const = 0;
  virtual void moveFigure(const Coordinates &oldc, const Coordinates &newc) = 0;
  virtual IFigure *getFigure(const Coordinates &coordinates) const = 0;
  virtual void setFigure(Figure *figure) = 0;
  virtual Coordinates getEnPassantTarget() const = 0;
  virtual void setEnPassantTarget(const Coordinates &coordinates) = 0;
};

class Figure : public IFigure {
protected:
  int id;
  IBoard *gameboard;
  int cost;
  Coordinates coordinates;
  Color color;
  Type type;

public:
  Figure(int id, Color color, IBoard *gameboard, int cost,
         Coordinates coordinates, Type type)
      : id(id), color(color), gameboard(gameboard), cost(cost),
        coordinates(coordinates), type(type) {
    if (coordinates.getX() >= 0 && coordinates.getX() <= 7 &&
        coordinates.getY() >= 0 && coordinates.getY() <= 7) {
    } else {
      cerr << "Invalid coordinates: " << coordinates.getX()
           << coordinates.getY() << endl;
    }
  }

  virtual vector<Coordinates> getValidMoves() = 0;
  bool isValidMove(const Coordinates &coordinates) override {
    vector<Coordinates> validMoves = getValidMoves();
    for (auto coordinate : validMoves) {
      if (coordinates.equals(coordinate)) {
        return true;
      }
    }
    return false;
  }

  bool move(const Coordinates &coordinates) override {
    if (isValidMove(coordinates)) {
#ifndef CHESSGAME_SILENT
      cout << "Moving id " << id << " to " << coordinates.getX()
           << coordinates.getY() << endl;
#endif
      gameboard->moveFigure(this->coordinates, coordinates);
      this->coordinates.move(coordinates);
      return true;
    } else {
#ifndef CHESSGAME_SILENT
      cerr << "Invalid move" << endl;
#endif
      return false;
    }
  }

  void setCoordinates(const Coordinates &coordinates) override {
    this->coordinates.move(coordinates);
  }

  Color getColor() const override { return this->color; }
  Coordinates getCoordinates() const override { return this->coordinates; }
  int getCost() const override { return this->cost; }
  int getId() const override { return this->id; }
  Type getType() const override { return this->type; }
  IBoard *getGameboard() { return this->gameboard; }
  const IBoard *getGameboard() const { return this->gameboard; }
};

class Board : public IBoard {
private:
  vector<vector<Figure *>> figures;
  Coordinates enPassantTarget = Coordinates(-1, -1);

public:
  Board() : figures(8, vector<Figure *>(8, nullptr)) {}
  ~Board() override {
    for (int x = 0; x < 8; x++) {
      for (int y = 0; y < 8; y++) {
        if (figures[x][y] != nullptr) {
          delete figures[x][y];
          figures[x][y] = nullptr;
        }
      }
    }
  }
  void clearBoard() {
    for (int x = 0; x < 8; x++) {
      for (int y = 0; y < 8; y++) {
        if (figures[x][y] != nullptr) {
          delete figures[x][y];
          figures[x][y] = nullptr;
        }
      }
    }
    enPassantTarget = Coordinates(-1, -1);
  }

  bool isEmpty(const Coordinates &coordinates) const override {
    if (coordinates.canMove()) {
      return figures[coordinates.getX()][coordinates.getY()] == nullptr;
    } else {
      return false;
    }
  }
  Coordinates getEnPassantTarget() const override { return enPassantTarget; }
  void setEnPassantTarget(const Coordinates &coordinates) override {
    enPassantTarget = coordinates;
  }

  void setFigure(Figure *figure) override {
    if (!figure->getCoordinates().canMove()) {
      cerr << "Invalid coordinates for setFigure: "
           << figure->getCoordinates().getX() << figure->getCoordinates().getY()
           << endl;
      return;
    }
    int x = figure->getCoordinates().getX();
    int y = figure->getCoordinates().getY();
    if (figures[x][y] != nullptr && figures[x][y] != figure) {
      delete figures[x][y];
    }
    figures[x][y] = figure;
  }

  IFigure *getFigure(const Coordinates &coordinates) const override {
    if (!coordinates.canMove()) {
      return nullptr;
    }
    return figures[coordinates.getX()][coordinates.getY()];
  }

  void moveFigure(const Coordinates &oldc, const Coordinates &newc) override {
    Figure *moving = figures[oldc.getX()][oldc.getY()];
    if (moving != nullptr && moving->getType() == PAWN &&
        newc.equals(enPassantTarget) &&
        figures[newc.getX()][newc.getY()] == nullptr) {
      int dir = (moving->getColor() == WHITE) ? 1 : -1;
      int capY = newc.getY() - dir;
      if (capY >= 0 && capY <= 7) {
        Figure *captured = figures[newc.getX()][capY];
        if (captured != nullptr && captured->getType() == PAWN &&
            captured->getColor() != moving->getColor()) {
          delete captured;
          figures[newc.getX()][capY] = nullptr;
        }
      }
    }
    if (figures[newc.getX()][newc.getY()] != nullptr) {
      delete figures[newc.getX()][newc.getY()];
    }
    figures[newc.getX()][newc.getY()] = figures[oldc.getX()][oldc.getY()];
    figures[oldc.getX()][oldc.getY()] = nullptr;
  }

  void applyMove(Figure *figure, const Coordinates &to, Figure *&captured,
                 Coordinates &from, Coordinates &capturedPos,
                 Figure *&rookMoved, Coordinates &rookFrom, Coordinates &rookTo,
                 Coordinates &prevEnPassantTarget) {
    from = figure->getCoordinates();
    prevEnPassantTarget = enPassantTarget;
    int fromX = from.getX();
    int fromY = from.getY();
    int toX = to.getX();
    int toY = to.getY();
    capturedPos = to;
    captured = figures[toX][toY];
    rookMoved = nullptr;
    rookFrom = Coordinates(-1, -1);
    rookTo = Coordinates(-1, -1);
    if (figure->getType() == PAWN && to.equals(enPassantTarget) &&
        captured == nullptr) {
      int dir = (figure->getColor() == WHITE) ? 1 : -1;
      int capY = toY - dir;
      if (capY >= 0 && capY <= 7) {
        capturedPos = Coordinates(toX, capY);
        captured = figures[toX][capY];
        figures[toX][capY] = nullptr;
      }
    }
    if (figure->getType() == KING && abs(toX - fromX) == 2) {
      int rookStartX = (toX > fromX) ? 7 : 0;
      int rookEndX = (toX > fromX) ? 5 : 3;
      Figure *rook = figures[rookStartX][fromY];
      if (rook != nullptr && rook->getType() == LADYA) {
        rookMoved = rook;
        rookFrom = Coordinates(rookStartX, fromY);
        rookTo = Coordinates(rookEndX, fromY);
        figures[rookEndX][fromY] = rook;
        figures[rookStartX][fromY] = nullptr;
        rook->setCoordinates(rookTo);
      }
    }
    figures[toX][toY] = figure;
    figures[fromX][fromY] = nullptr;
    figure->setCoordinates(to);
  }

  void undoMove(Figure *figure, const Coordinates &to, const Coordinates &from,
                Figure *captured, const Coordinates &capturedPos,
                Figure *rookMoved, const Coordinates &rookFrom,
                const Coordinates &rookTo,
                const Coordinates &prevEnPassantTarget) {
    int fromX = from.getX();
    int fromY = from.getY();
    int toX = to.getX();
    int toY = to.getY();
    figures[fromX][fromY] = figure;
    if (captured != nullptr) {
      if (capturedPos.equals(to)) {
        figures[toX][toY] = captured;
      } else {
        figures[capturedPos.getX()][capturedPos.getY()] = captured;
        figures[toX][toY] = nullptr;
      }
    } else {
      figures[toX][toY] = nullptr;
    }
    if (rookMoved != nullptr && rookFrom.canMove() && rookTo.canMove()) {
      figures[rookFrom.getX()][rookFrom.getY()] = rookMoved;
      figures[rookTo.getX()][rookTo.getY()] = nullptr;
      rookMoved->setCoordinates(rookFrom);
    }
    enPassantTarget = prevEnPassantTarget;
    figure->setCoordinates(from);
  }

  string toFEN(bool castling_white_kingside, bool castling_white_queenside,
               bool castling_black_kingside, bool castling_black_queenside,
               const Coordinates &en_passant_target, char active_color,
               int halfmove_clock, int fullmove_number) {
    stringstream fen;

    // Piece placement (from rank 8 down to rank 1)
    for (int y = 7; y >= 0; y--) {
      int empty_count = 0;
      for (int x = 0; x < 8; x++) {
        IFigure *fig = figures[x][y];
        if (fig == nullptr) {
          empty_count++;
        } else {
          if (empty_count > 0) {
            fen << empty_count;
            empty_count = 0;
          }

          char piece_char = ' ';
          Type type = fig->getType();
          switch (type) {
          case PAWN:
            piece_char = 'p';
            break;
          case HORSE:
            piece_char = 'n';
            break;
          case ELEPHANT:
            piece_char = 'b';
            break;
          case LADYA:
            piece_char = 'r';
            break;
          case QUEEN:
            piece_char = 'q';
            break;
          case KING:
            piece_char = 'k';
            break;
          }

          if (fig->getColor() == WHITE) {
            piece_char = toupper(piece_char);
          }
          fen << piece_char;
        }
      }
      if (empty_count > 0) {
        fen << empty_count;
      }
      if (y > 0) {
        fen << '/';
      }
    }

    // Active color
    fen << ' ' << active_color;

    // Castling rights
    fen << ' ';
    bool has_castling = castling_white_kingside || castling_white_queenside ||
                        castling_black_kingside || castling_black_queenside;
    if (!has_castling) {
      fen << '-';
    } else {
      if (castling_white_kingside)
        fen << 'K';
      if (castling_white_queenside)
        fen << 'Q';
      if (castling_black_kingside)
        fen << 'k';
      if (castling_black_queenside)
        fen << 'q';
    }

    // En passant target square
    if (en_passant_target.canMove()) {
      char file = static_cast<char>('a' + en_passant_target.getX());
      char rank = static_cast<char>('1' + en_passant_target.getY());
      fen << ' ' << file << rank;
    } else {
      fen << " -";
    }

    // Halfmove clock
    fen << ' ' << halfmove_clock;

    // Fullmove number
    fen << ' ' << fullmove_number;

    return fen.str();
  }

  void display() {
    int row = 8;
    for (int y = 7; y >= 0; y--) {
      cout << row << "| ";
      row--;
      for (int x = 0; x < 8; x++) {
        IFigure *fig = figures[x][y];
        cout << figureToString(fig) << ' ';
      }
      cout << endl;
    }
    cout << "| A B C D E F G H" << endl;
  }
};

class Queen;
class Elephant;
class Ladya;
class Horse;

class Pawn : public Figure {
private:
  bool canDoubleMove;

public:
  Pawn(int id, Color color, IBoard *gameboard, Coordinates coordinates)
      : Figure(id, color, gameboard, 1, coordinates, PAWN) {
    canDoubleMove = true;
  }

  vector<Coordinates> getValidMoves() override {
    vector<Coordinates> validMoves;
    int dir = (this->getColor() == WHITE) ? 1 : -1;
    int startRank = (this->getColor() == WHITE) ? 1 : 6;
    int x = this->getCoordinates().getX();
    int y = this->getCoordinates().getY();

    Coordinates forward1(x, y + dir);
    if (forward1.canMove() && this->gameboard->isEmpty(forward1)) {
      validMoves.push_back(forward1);

      Coordinates forward2(x, y + 2 * dir);
      if (canDoubleMove && y == startRank && forward2.canMove() &&
          this->gameboard->isEmpty(forward2)) {
        validMoves.push_back(forward2);
      }
    }

    Coordinates captureRight(x + 1, y + dir);
    IFigure *rightTarget = this->gameboard->getFigure(captureRight);
    if (rightTarget != nullptr && rightTarget->getColor() != this->getColor()) {
      validMoves.push_back(captureRight);
    }

    Coordinates captureLeft(x - 1, y + dir);
    IFigure *leftTarget = this->gameboard->getFigure(captureLeft);
    if (leftTarget != nullptr && leftTarget->getColor() != this->getColor()) {
      validMoves.push_back(captureLeft);
    }

    Coordinates enPassant = this->gameboard->getEnPassantTarget();
    if (enPassant.canMove() && enPassant.getY() == y + dir &&
        abs(enPassant.getX() - x) == 1) {
      IFigure *pawn =
          this->gameboard->getFigure(Coordinates(enPassant.getX(), y));
      if (pawn != nullptr && pawn->getType() == PAWN &&
          pawn->getColor() != this->getColor()) {
        validMoves.push_back(enPassant);
      }
    }

    return validMoves;
  }

  bool move(const Coordinates &coordinates) override {
    bool moved = Figure::move(coordinates);
    if (moved) {
      canDoubleMove = false;
    }
    if (coordinates.getY() == 0 || coordinates.getY() == 7) {
      prevrashenie(typeSelector());
    }
    return moved;
  }

  Type typeSelector() {
    if (gPromotionOverrideActive) {
      return consumePromotionOverride();
    }
    if (gPromotionSelector != nullptr) {
      return gPromotionSelector(this->getColor());
    }
    return QUEEN;
  }
  void prevrashenie(Type type);
};

class OrthogonalMoving {
public:
  static vector<Coordinates> getOrthogonalMoves(Figure *figure) {
    vector<Coordinates> validMoves;
    int x = figure->getCoordinates().getX();
    int y = figure->getCoordinates().getY();

    for (int x1 = x + 1; x1 < 8; x1++) {
      IFigure *target = figure->getGameboard()->getFigure(Coordinates(x1, y));
      if (target == nullptr) {
        validMoves.push_back(Coordinates(x1, y));
        continue;
      }
      if (target->getColor() != figure->getColor()) {
        validMoves.push_back(Coordinates(x1, y));
      }
      break;
    }

    for (int x1 = x - 1; x1 >= 0; x1--) {
      IFigure *target = figure->getGameboard()->getFigure(Coordinates(x1, y));
      if (target == nullptr) {
        validMoves.push_back(Coordinates(x1, y));
        continue;
      }
      if (target->getColor() != figure->getColor()) {
        validMoves.push_back(Coordinates(x1, y));
      }
      break;
    }

    for (int y1 = y + 1; y1 < 8; y1++) {
      IFigure *target = figure->getGameboard()->getFigure(Coordinates(x, y1));
      if (target == nullptr) {
        validMoves.push_back(Coordinates(x, y1));
        continue;
      }
      if (target->getColor() != figure->getColor()) {
        validMoves.push_back(Coordinates(x, y1));
      }
      break;
    }

    for (int y1 = y - 1; y1 >= 0; y1--) {
      IFigure *target = figure->getGameboard()->getFigure(Coordinates(x, y1));
      if (target == nullptr) {
        validMoves.push_back(Coordinates(x, y1));
        continue;
      }
      if (target->getColor() != figure->getColor()) {
        validMoves.push_back(Coordinates(x, y1));
      }
      break;
    }
    return validMoves;
  }
};

class Ladya : public Figure {
private:
  bool hadMoved = false;

public:
  Ladya(int id, Color color, IBoard *gameboard, Coordinates coordinates)
      : Figure(id, color, gameboard, 4, coordinates, LADYA) {}

  vector<Coordinates> getValidMoves() override {
    vector<Coordinates> validMoves = OrthogonalMoving::getOrthogonalMoves(this);
    return validMoves;
  }

  bool move(const Coordinates &coordinates) override {
    if (isValidMove(coordinates)) {
#ifndef CHESSGAME_SILENT
      cout << "Moving id " << id << " to " << coordinates.getX()
           << coordinates.getY() << endl;
#endif
      gameboard->moveFigure(this->coordinates, coordinates);
      this->coordinates.move(coordinates);
      this->hadMoved = true;
      return true;
    } else {
#ifndef CHESSGAME_SILENT
      cerr << "Invalid move" << endl;
#endif
      return false;
    }
  }

  bool getHasMoved() const { return this->hadMoved; }
  void setHasMoved(bool moved) { this->hadMoved = moved; }
};

class DiagonalMoving {
public:
  static vector<Coordinates> getDiagonalMoves(Figure *figure) {
    vector<Coordinates> validMoves;
    int x = figure->getCoordinates().getX();
    int y = figure->getCoordinates().getY();

    for (int x1 = x + 1, y1 = y + 1; x1 < 8 && y1 < 8; x1++, y1++) {
      IFigure *target = figure->getGameboard()->getFigure(Coordinates(x1, y1));
      if (target == nullptr) {
        validMoves.push_back(Coordinates(x1, y1));
        continue;
      }
      if (target->getColor() != figure->getColor()) {
        validMoves.push_back(Coordinates(x1, y1));
      }
      break;
    }

    for (int x1 = x - 1, y1 = y + 1; x1 >= 0 && y1 < 8; x1--, y1++) {
      IFigure *target = figure->getGameboard()->getFigure(Coordinates(x1, y1));
      if (target == nullptr) {
        validMoves.push_back(Coordinates(x1, y1));
        continue;
      }
      if (target->getColor() != figure->getColor()) {
        validMoves.push_back(Coordinates(x1, y1));
      }
      break;
    }

    for (int x1 = x + 1, y1 = y - 1; x1 < 8 && y1 >= 0; x1++, y1--) {
      IFigure *target = figure->getGameboard()->getFigure(Coordinates(x1, y1));
      if (target == nullptr) {
        validMoves.push_back(Coordinates(x1, y1));
        continue;
      }
      if (target->getColor() != figure->getColor()) {
        validMoves.push_back(Coordinates(x1, y1));
      }
      break;
    }

    for (int x1 = x - 1, y1 = y - 1; x1 >= 0 && y1 >= 0; x1--, y1--) {
      IFigure *target = figure->getGameboard()->getFigure(Coordinates(x1, y1));
      if (target == nullptr) {
        validMoves.push_back(Coordinates(x1, y1));
        continue;
      }
      if (target->getColor() != figure->getColor()) {
        validMoves.push_back(Coordinates(x1, y1));
      }
      break;
    }

    return validMoves;
  }
};

class Elephant : public Figure {
public:
  Elephant(int id, Color color, IBoard *gameboard, Coordinates coordinates)
      : Figure(id, color, gameboard, 3, coordinates, ELEPHANT) {}

  vector<Coordinates> getValidMoves() override {
    vector<Coordinates> validMoves = DiagonalMoving::getDiagonalMoves(this);
    return validMoves;
  }
};

class Queen : public Figure {
public:
  Queen(int id, Color color, IBoard *gameboard, Coordinates coordinates)
      : Figure(id, color, gameboard, 5, coordinates, QUEEN) {}

  vector<Coordinates> getValidMoves() override {
    vector<Coordinates> validMoves = DiagonalMoving::getDiagonalMoves(this);
    vector<Coordinates> validMoves1 =
        OrthogonalMoving::getOrthogonalMoves(this);
    for (auto move : validMoves1) {
      validMoves.push_back(move);
    }
    return validMoves;
  }
};

class Horse : public Figure {
public:
  Horse(int id, Color color, IBoard *gameboard, Coordinates coordinates)
      : Figure(id, color, gameboard, 3, coordinates, HORSE) {}

  vector<Coordinates> getValidMoves() override {
    vector<Coordinates> validMoves;
    int x = this->getCoordinates().getX();
    int y = this->getCoordinates().getY();

    vector<Coordinates> moves = {
        Coordinates(x + 2, y + 1), Coordinates(x + 2, y - 1),
        Coordinates(x - 2, y + 1), Coordinates(x - 2, y - 1),
        Coordinates(x + 1, y + 2), Coordinates(x + 1, y - 2),
        Coordinates(x - 1, y + 2), Coordinates(x - 1, y - 2)};

    for (auto move : moves) {
      if (move.getX() >= 0 && move.getX() <= 7 && move.getY() >= 0 &&
          move.getY() <= 7) {
        IFigure *target = this->getGameboard()->getFigure(move);
        if (target == nullptr || target->getColor() != this->getColor()) {
          validMoves.push_back(move);
        }
      }
    }

    return validMoves;
  }
};

void Pawn::prevrashenie(Type type) {
  Coordinates pos = this->getCoordinates();
  Figure *newFigure = nullptr;
  switch (type) {
  case QUEEN:
    newFigure =
        new Queen(this->getId(), this->getColor(), this->getGameboard(), pos);
    break;
  case ELEPHANT:
    newFigure = new Elephant(this->getId(), this->getColor(),
                             this->getGameboard(), pos);
    break;
  case LADYA:
    newFigure =
        new Ladya(this->getId(), this->getColor(), this->getGameboard(), pos);
    break;
  case HORSE:
    newFigure =
        new Horse(this->getId(), this->getColor(), this->getGameboard(), pos);
    break;
  default:
    newFigure =
        new Queen(this->getId(), this->getColor(), this->getGameboard(), pos);
    break;
  }

  this->getGameboard()->setFigure(newFigure);
  // Commented out because of seg error when online mode.
  // delete this;
}

class King : public Figure {
private:
  bool hadMoved = false;
  bool isSquareUnderAttack(const Coordinates &square) const {
    for (int x = 0; x < 8; x++) {
      for (int y = 0; y < 8; y++) {
        IFigure *fig = this->getGameboard()->getFigure(Coordinates(x, y));
        if (fig == nullptr || fig->getColor() == this->getColor()) {
          continue;
        }
        if (fig->getType() == KING) {
          int dx = abs(fig->getCoordinates().getX() - square.getX());
          int dy = abs(fig->getCoordinates().getY() - square.getY());
          if (dx <= 1 && dy <= 1) {
            return true;
          }
          continue;
        }
        vector<Coordinates> moves = fig->getValidMoves();
        for (const auto &move : moves) {
          if (move.equals(square)) {
            return true;
          }
        }
      }
    }
    return false;
  }

public:
  King(int id, Color color, IBoard *gameboard, Coordinates coordinates)
      : Figure(id, color, gameboard, 1000, coordinates, KING) {}

  bool isInCheck() const { return isSquareUnderAttack(this->getCoordinates()); }

  bool isCheckmate() {
    if (!isInCheck()) {
      return false;
    }
    Board *board = dynamic_cast<Board *>(this->getGameboard());
    if (board == nullptr) {
      return false;
    }
    for (int x = 0; x < 8; x++) {
      for (int y = 0; y < 8; y++) {
        IFigure *fig = board->getFigure(Coordinates(x, y));
        if (fig == nullptr || fig->getColor() != this->getColor()) {
          continue;
        }
        Figure *concrete = dynamic_cast<Figure *>(fig);
        if (concrete == nullptr) {
          continue;
        }
        vector<Coordinates> moves = fig->getValidMoves();
        for (const auto &move : moves) {
          Figure *captured = nullptr;
          Coordinates from(0, 0);
          Coordinates capturedPos(0, 0);
          Figure *rookMoved = nullptr;
          Coordinates rookFrom(0, 0);
          Coordinates rookTo(0, 0);
          Coordinates prevEnPassantTarget(0, 0);
          board->applyMove(concrete, move, captured, from, capturedPos,
                           rookMoved, rookFrom, rookTo, prevEnPassantTarget);
          bool stillInCheck = this->isInCheck();
          board->undoMove(concrete, move, from, captured, capturedPos,
                          rookMoved, rookFrom, rookTo, prevEnPassantTarget);
          if (!stillInCheck) {
            return false;
          }
        }
      }
    }
    return true;
  }

  vector<Coordinates> getValidMoves() override {
    vector<Coordinates> validMoves;
    int x = this->getCoordinates().getX();
    int y = this->getCoordinates().getY();

    vector<Coordinates> moves = {
        Coordinates(x + 1, y),     Coordinates(x + 1, y + 1),
        Coordinates(x + 1, y - 1), Coordinates(x, y + 1),
        Coordinates(x, y - 1),     Coordinates(x - 1, y),
        Coordinates(x - 1, y - 1), Coordinates(x - 1, y + 1)};

    for (auto move : moves) {
      if (move.getX() >= 0 && move.getX() <= 7 && move.getY() >= 0 &&
          move.getY() <= 7 && !isSquareUnderAttack(move)) {
        IFigure *target = this->getGameboard()->getFigure(move);
        if (target == nullptr || target->getColor() != this->getColor()) {
          validMoves.push_back(move);
        }
      }
    }

    if (!hadMoved && x == 4 && (y == 0 || y == 7)) {
      IFigure *kingRookFigure =
          this->getGameboard()->getFigure(Coordinates(7, y));
      Ladya *kingRook =
          (kingRookFigure != nullptr && kingRookFigure->getType() == LADYA &&
           kingRookFigure->getColor() == this->getColor())
              ? dynamic_cast<Ladya *>(kingRookFigure)
              : nullptr;
      if (kingRook != nullptr && !kingRook->getHasMoved() &&
          this->getGameboard()->isEmpty(Coordinates(5, y)) &&
          this->getGameboard()->isEmpty(Coordinates(6, y)) &&
          !isSquareUnderAttack(Coordinates(4, y)) &&
          !isSquareUnderAttack(Coordinates(5, y)) &&
          !isSquareUnderAttack(Coordinates(6, y))) {
        validMoves.push_back(Coordinates(6, y));
      }

      IFigure *queenRookFigure =
          this->getGameboard()->getFigure(Coordinates(0, y));
      Ladya *queenRook =
          (queenRookFigure != nullptr && queenRookFigure->getType() == LADYA &&
           queenRookFigure->getColor() == this->getColor())
              ? dynamic_cast<Ladya *>(queenRookFigure)
              : nullptr;
      if (queenRook != nullptr && !queenRook->getHasMoved() &&
          this->getGameboard()->isEmpty(Coordinates(3, y)) &&
          this->getGameboard()->isEmpty(Coordinates(2, y)) &&
          this->getGameboard()->isEmpty(Coordinates(1, y)) &&
          !isSquareUnderAttack(Coordinates(4, y)) &&
          !isSquareUnderAttack(Coordinates(3, y)) &&
          !isSquareUnderAttack(Coordinates(2, y))) {
        validMoves.push_back(Coordinates(2, y));
      }
    }

    return validMoves;
  }

  bool move(const Coordinates &coordinates) override {
    if (isValidMove(coordinates)) {
#ifndef CHESSGAME_SILENT
      cout << "Moving id " << id << " to " << coordinates.getX()
           << coordinates.getY() << endl;
#endif
      int curX = this->coordinates.getX();
      int curY = this->coordinates.getY();
      int dx = coordinates.getX() - curX;

      if (!hadMoved && curY == coordinates.getY() && (dx == 2 || dx == -2)) {
        int rookX = (dx == 2) ? 7 : 0;
        int rookTargetX = (dx == 2) ? 5 : 3;
        IFigure *rookFigure =
            this->getGameboard()->getFigure(Coordinates(rookX, curY));
        Ladya *rook = (rookFigure != nullptr && rookFigure->getType() == LADYA)
                          ? dynamic_cast<Ladya *>(rookFigure)
                          : nullptr;
        if (rook != nullptr) {
          Coordinates rookTarget(rookTargetX, curY);
          this->getGameboard()->moveFigure(rook->getCoordinates(), rookTarget);
          rook->setCoordinates(rookTarget);
          rook->setHasMoved(true);
        }
      }

      gameboard->moveFigure(this->coordinates, coordinates);
      this->coordinates.move(coordinates);
      this->hadMoved = true;
      return true;
    } else {
#ifndef CHESSGAME_SILENT
      cerr << "Invalid move" << endl;
#endif
      return false;
    }
  }

  bool getHasMoved() { return this->hadMoved; }
};

class ChessFacade {
private:
  Board *gameboard;
  IFigure *selected;
  bool gameOver = false;
  string lastMessage;
  string lastExecutedUCIMove = ""; // For multiplayer.
  vector<string> moveHistory;      // История ходов в формате UCI
  map<string, int> positionCounts; // Количество повторений каждой позиции

  // Engine support
  unique_ptr<ChessEngine> engine;
  bool botWhite = false;
  bool botBlack = false;
  Color currentTurn = WHITE;
  int halfmoveClockCount = 0;
  int fullmoveNumber = 1;
  int moveTimeMs = 200;
  bool canCastleWK = true;
  bool canCastleWQ = true;
  bool canCastleBK = true;
  bool canCastleBQ = true;

  // Формирование строки истории для Stockfish
  string getStockfishPositionCmd() const {
    string cmd = "position startpos";
    if (!moveHistory.empty()) {
      cmd += " moves";
      for (const auto &m : moveHistory) {
        cmd += " " + m;
      }
    }
    return cmd;
  }

  // Извлечение идентификатора позиции (первые 4 поля FEN)
  string getPositionKey() const {
    string fen = getFEN();
    stringstream ss(fen);
    string pieces, turnField, castling, enPassant;
    ss >> pieces >> turnField >> castling >> enPassant;
    return pieces + " " + turnField + " " + castling + " " + enPassant;
  }

  // Проверка, есть ли у цвета хотя бы один легальный ход
  bool hasLegalMoves(Color color) {
    for (int x = 0; x < 8; x++) {
      for (int y = 0; y < 8; y++) {
        IFigure *fig = gameboard->getFigure(Coordinates(x, y));
        if (fig != nullptr && fig->getColor() == color) {
          if (!getLegalMovesFor(fig).empty()) {
            return true;
          }
        }
      }
    }
    return false;
  }

  Coordinates uciToCoordinates(const string &uci) {
    if (uci.length() < 4) {
      return Coordinates(-1, -1);
    }
    int from_x = uci[0] - 'a';
    int from_y = uci[1] - '1';
    return Coordinates(from_x, from_y);
  }

  Coordinates uciToMoveTarget(const string &uci) {
    if (uci.length() < 4) {
      return Coordinates(-1, -1);
    }
    int to_x = uci[2] - 'a';
    int to_y = uci[3] - '1';
    return Coordinates(to_x, to_y);
  }

  Type promotionCharToType(char c) {
    switch (tolower(static_cast<unsigned char>(c))) {
    case 'q':
      return QUEEN;
    case 'r':
      return LADYA;
    case 'b':
      return ELEPHANT;
    case 'n':
      return HORSE;
    default:
      return QUEEN;
    }
  }

  King *findKing(Color color) const {
    for (int x = 0; x < 8; x++) {
      for (int y = 0; y < 8; y++) {
        IFigure *fig = gameboard->getFigure(Coordinates(x, y));
        if (fig != nullptr && fig->getType() == KING &&
            fig->getColor() == color) {
          return dynamic_cast<King *>(fig);
        }
      }
    }
    return nullptr;
  }

  bool reportCheck() {
    lastMessage.clear();
    King *whiteKing = findKing(WHITE);
    if (whiteKing != nullptr && whiteKing->isInCheck()) {
      if (whiteKing->isCheckmate()) {
        lastMessage = "Checkmate to WHITE";
        gameOver = true;
        return true;
      }
      lastMessage = "Check to WHITE";
      return false;
    }
    King *blackKing = findKing(BLACK);
    if (blackKing != nullptr && blackKing->isInCheck()) {
      if (blackKing->isCheckmate()) {
        lastMessage = "Checkmate to BLACK";
        gameOver = true;
        return true;
      }
      lastMessage = "Check to BLACK";
      return false;
    }

    if (!hasLegalMoves(currentTurn)) {
      lastMessage = "Draw by Stalemate";
      gameOver = true;
      return true;
    }

    return false;
  }

  vector<Coordinates> getLegalMovesFor(IFigure *figure) {
    if (figure == nullptr) {
      return {};
    }
    Color color = figure->getColor();
    King *king = findKing(color);
    if (king == nullptr) {
      return {};
    }
    Figure *concrete = dynamic_cast<Figure *>(figure);
    if (concrete == nullptr) {
      return {};
    }
    vector<Coordinates> legalMoves;
    for (const auto &move : figure->getValidMoves()) {
      IFigure *target = gameboard->getFigure(move);
      if (target != nullptr && target->getType() == KING) {
        continue;
      }
      Figure *captured = nullptr;
      Coordinates from(0, 0);
      Coordinates capturedPos(0, 0);
      Figure *rookMoved = nullptr;
      Coordinates rookFrom(0, 0);
      Coordinates rookTo(0, 0);
      Coordinates prevEnPassantTarget(0, 0);
      gameboard->applyMove(concrete, move, captured, from, capturedPos,
                           rookMoved, rookFrom, rookTo, prevEnPassantTarget);
      bool inCheck = king->isInCheck();
      gameboard->undoMove(concrete, move, from, captured, capturedPos,
                          rookMoved, rookFrom, rookTo, prevEnPassantTarget);
      if (!inCheck) {
        legalMoves.push_back(move);
      }
    }
    return legalMoves;
  }
  vector<Coordinates> getLegalMovesForSelected() {
    return getLegalMovesFor(selected);
  }

public:
  ChessFacade(Board *gameboard)
      : gameboard(gameboard), selected(nullptr), gameOver(false) {}
  bool isGameOver() const { return gameOver; }
  string getLastMessage() const { return lastMessage; }
  void fillBoard() {
    moveHistory.clear();
    positionCounts.clear();
    // Pawns
    for (int i = 0; i < 8; i++) {
      gameboard->setFigure(new Pawn(i, WHITE, gameboard, Coordinates(i, 1)));
    }

    for (int i = 0; i < 8; i++) {
      gameboard->setFigure(
          new Pawn(i + 8, BLACK, gameboard, Coordinates(i, 6)));
    }

    // Ladya
    gameboard->setFigure(new Ladya(16, WHITE, gameboard, Coordinates(0, 0)));
    gameboard->setFigure(new Ladya(17, WHITE, gameboard, Coordinates(7, 0)));

    gameboard->setFigure(new Ladya(18, BLACK, gameboard, Coordinates(0, 7)));
    gameboard->setFigure(new Ladya(19, BLACK, gameboard, Coordinates(7, 7)));

    // Elephants
    gameboard->setFigure(new Elephant(20, WHITE, gameboard, Coordinates(2, 0)));
    gameboard->setFigure(new Elephant(21, WHITE, gameboard, Coordinates(5, 0)));

    gameboard->setFigure(new Elephant(22, BLACK, gameboard, Coordinates(2, 7)));
    gameboard->setFigure(new Elephant(23, BLACK, gameboard, Coordinates(5, 7)));

    // Queens
    gameboard->setFigure(new Queen(24, WHITE, gameboard, Coordinates(3, 0)));
    gameboard->setFigure(new Queen(25, BLACK, gameboard, Coordinates(3, 7)));

    // Horses
    gameboard->setFigure(new Horse(26, WHITE, gameboard, Coordinates(1, 0)));
    gameboard->setFigure(new Horse(27, WHITE, gameboard, Coordinates(6, 0)));

    gameboard->setFigure(new Horse(28, BLACK, gameboard, Coordinates(1, 7)));
    gameboard->setFigure(new Horse(29, BLACK, gameboard, Coordinates(6, 7)));

    // Kings
    gameboard->setFigure(new King(30, WHITE, gameboard, Coordinates(4, 0)));
    gameboard->setFigure(new King(31, BLACK, gameboard, Coordinates(4, 7)));

    canCastleWK = true;
    canCastleWQ = true;
    canCastleBK = true;
    canCastleBQ = true;
    positionCounts[getPositionKey()] = 1;
  }

  Coordinates stringToCoordinates(string cord) {
    int x = -1, y = -1;

    if (cord.empty()) {
      return Coordinates(-1, -1);
    }

    if (cord[0] == 'a' || cord[0] == 'A') {
      x = 0;
    } else if (cord[0] == 'b' || cord[0] == 'B') {
      x = 1;
    } else if (cord[0] == 'c' || cord[0] == 'C') {
      x = 2;
    } else if (cord[0] == 'd' || cord[0] == 'D') {
      x = 3;
    } else if (cord[0] == 'e' || cord[0] == 'E') {
      x = 4;
    } else if (cord[0] == 'f' || cord[0] == 'F') {
      x = 5;
    } else if (cord[0] == 'g' || cord[0] == 'G') {
      x = 6;
    } else if (cord[0] == 'h' || cord[0] == 'H') {
      x = 7;
    }

    if (cord.length() < 2) {
      return Coordinates(-1, -1);
    }

    if (cord[1] == '1') {
      y = 0;
    } else if (cord[1] == '2') {
      y = 1;
    } else if (cord[1] == '3') {
      y = 2;
    } else if (cord[1] == '4') {
      y = 3;
    } else if (cord[1] == '5') {
      y = 4;
    } else if (cord[1] == '6') {
      y = 5;
    } else if (cord[1] == '7') {
      y = 6;
    } else if (cord[1] == '8') {
      y = 7;
    }
    return Coordinates(x, y);
  }

  struct PieceView {
    bool present = false;
    Type type = PAWN;
    Color color = NONE;
  };

  PieceView getPieceAt(const Coordinates &coordinates) const {
    IFigure *fig = gameboard->getFigure(coordinates);
    if (fig == nullptr) {
      return {};
    }
    return {true, fig->getType(), fig->getColor()};
  }

  bool hasSelection() const { return selected != nullptr; }
  void clearSelection() { selected = nullptr; }
  Coordinates getSelectedCoordinates() const {
    if (selected == nullptr) {
      return Coordinates(-1, -1);
    }
    return selected->getCoordinates();
  }
  vector<Coordinates> getSelectedValidMoves() {
    return getLegalMovesForSelected();
  }

  bool selectFigure(Color color, Coordinates coordinates) {
    if (!coordinates.canMove()) {
      return false;
    }
    IFigure *fig = gameboard->getFigure(coordinates);
    if (fig == nullptr || fig->getColor() != color) {
      return false;
    }
    if (getLegalMovesFor(fig).empty()) {
      return false;
    }
    selected = fig;
    return true;
  }

  void showValidMoves(Coordinates coordinates) {
    selected = gameboard->getFigure(coordinates);
    vector<Coordinates> validMoves = getLegalMovesForSelected();
    auto isValidMove = [&](int x, int y) {
      for (const auto &move : validMoves) {
        if (move.getX() == x && move.getY() == y) {
          return true;
        }
      }
      return false;
    };
    int row = 8;
    for (int y = 7; y >= 0; y--) {
      cout << row << "| ";
      row--;
      for (int x = 0; x < 8; x++) {
        if (isValidMove(x, y)) {
          IFigure *fig = gameboard->getFigure(Coordinates(x, y));
          if (fig == nullptr) {
            cout << "* ";
          } else {
            cout << colorize(typeToString(fig->getType()), ANSI_RED) << ' ';
          }
          continue;
        }
        IFigure *fig = gameboard->getFigure(Coordinates(x, y));
        cout << figureToString(fig) << ' ';
      }
      cout << endl;
    }
    cout << "| A B C D E F G H" << endl;
  }
  string getLastExecutedUCIMove() const { return lastExecutedUCIMove; }
  bool moveFigure(Coordinates coordinates) {
    if (selected == nullptr) {
      return false;
    }
    Coordinates from = selected->getCoordinates();
    Type movingType = selected->getType();
    Color movingColor = selected->getColor();
    IFigure *targetBefore = gameboard->getFigure(coordinates);
    bool capturedRook =
        (targetBefore != nullptr && targetBefore->getType() == LADYA);
    Color capturedColor =
        (targetBefore != nullptr) ? targetBefore->getColor() : NONE;
    for (auto var : getLegalMovesForSelected()) {
      if (var.equals(coordinates)) {
        bool moved = selected->move(coordinates);
        if (moved) {

          char fX = 'a' + from.getX();
          char fY = '1' + from.getY();
          char tX = 'a' + coordinates.getX();
          char tY = '1' + coordinates.getY();
          lastExecutedUCIMove = std::string() + fX + fY + tX + tY;

          IFigure *finalPiece = gameboard->getFigure(coordinates);
          if (movingType == PAWN && finalPiece != nullptr &&
              finalPiece->getType() != PAWN) {
            Type promotedType = finalPiece->getType();
            if (promotedType == QUEEN)
              lastExecutedUCIMove += "q";
            else if (promotedType == LADYA)
              lastExecutedUCIMove += "r";
            else if (promotedType == ELEPHANT)
              lastExecutedUCIMove += "b";
            else if (promotedType == HORSE)
              lastExecutedUCIMove += "n";
          }

          moveHistory.push_back(lastExecutedUCIMove);

          if (movingType == PAWN) {
            int dy = coordinates.getY() - from.getY();
            int dir = (movingColor == WHITE) ? 1 : -1;
            if (dy == 2 * dir) {
              gameboard->setEnPassantTarget(
                  Coordinates(from.getX(), from.getY() + dir));
            } else {
              gameboard->setEnPassantTarget(Coordinates(-1, -1));
            }
          } else {
            gameboard->setEnPassantTarget(Coordinates(-1, -1));
          }
          if (movingType == KING) {
            if (movingColor == WHITE) {
              canCastleWK = false;
              canCastleWQ = false;
            } else {
              canCastleBK = false;
              canCastleBQ = false;
            }
          } else if (movingType == LADYA) {
            if (movingColor == WHITE && from.getY() == 0) {
              if (from.getX() == 0) {
                canCastleWQ = false;
              } else if (from.getX() == 7) {
                canCastleWK = false;
              }
            } else if (movingColor == BLACK && from.getY() == 7) {
              if (from.getX() == 0) {
                canCastleBQ = false;
              } else if (from.getX() == 7) {
                canCastleBK = false;
              }
            }
          }
          if (capturedRook) {
            if (capturedColor == WHITE && coordinates.getY() == 0) {
              if (coordinates.getX() == 0) {
                canCastleWQ = false;
              } else if (coordinates.getX() == 7) {
                canCastleWK = false;
              }
            } else if (capturedColor == BLACK && coordinates.getY() == 7) {
              if (coordinates.getX() == 0) {
                canCastleBQ = false;
              } else if (coordinates.getX() == 7) {
                canCastleBK = false;
              }
            }
          }
          reportCheck();

          if (!gameOver) {
            string key = getPositionKey();
            positionCounts[key]++;
            if (positionCounts[key] >= 3) {
              lastMessage = "Draw by Threefold Repetition";
              gameOver = true;
            }
          }

          clearSelection();
          // Switch turn
          currentTurn = (currentTurn == WHITE) ? BLACK : WHITE;
          if (currentTurn == WHITE) {
            fullmoveNumber++;
          }
        }
        return moved;
      }
    }
#ifndef CHESSGAME_SILENT
    cout << "Invalid move!" << endl;
#endif
    return false;
  }

  // Engine and bot support methods
  void setEngine(unique_ptr<ChessEngine> engine_ptr) {
    engine = move(engine_ptr);
  }

  void setBotColor(Color color, bool enabled) {
    if (color == WHITE) {
      botWhite = enabled;
    } else {
      botBlack = enabled;
    }
  }

  bool isBotTurn() const {
    if (currentTurn == WHITE) {
      return botWhite;
    } else {
      return botBlack;
    }
  }

  void setMoveTimeMs(int ms) { moveTimeMs = ms; }

  string getFEN() const {
    char active = (currentTurn == WHITE) ? 'w' : 'b';

    return gameboard->toFEN(canCastleWK, canCastleWQ, canCastleBK, canCastleBQ,
                            gameboard->getEnPassantTarget(), active,
                            halfmoveClockCount, fullmoveNumber);
  }

  Color getCurrentTurn() const { return currentTurn; }

  bool makeMoveByUCI(const string &uciMove) {
    Coordinates from = uciToCoordinates(uciMove);
    Coordinates to = uciToMoveTarget(uciMove);

    if (!from.canMove() || !to.canMove()) {
      return false;
    }

    IFigure *fig = gameboard->getFigure(from);
    if (fig == nullptr || fig->getColor() != currentTurn) {
      return false;
    }

    if (uciMove.length() >= 5) {
      setPromotionOverride(promotionCharToType(uciMove[4]));
    }

    if (!selectFigure(currentTurn, from)) {
      return false;
    }

    if (!moveFigure(to)) {
      clearSelection();
      return false;
    }

    // Turn already switched in moveFigure()
    return true;
  }

  Coordinates getBotMove() {
    if (!engine || !engine->isAvailable()) {
      return Coordinates(-1, -1);
    }

    // string fen = getFEN();
    // string uci_move = engine->getBestMove(fen, moveTimeMs);
    string pos_cmd = getStockfishPositionCmd();
    string uci_move = engine->getBestMove(pos_cmd, moveTimeMs);

    if (uci_move.empty() || uci_move.length() < 4) {
      return Coordinates(-1, -1);
    }

    // Return the target coordinates
    return uciToMoveTarget(uci_move);
  }

  bool playBotMove() {
    if (!isBotTurn() || !engine || !engine->isAvailable()) {
      return false;
    }

    // string fen = getFEN();
    // string uci_move = engine->getBestMove(fen, moveTimeMs);

    string pos_cmd = getStockfishPositionCmd();
    string uci_move = engine->getBestMove(pos_cmd, moveTimeMs);

    if (uci_move.empty() || uci_move.length() < 4) {
      lastMessage = "ERROR: Bot move failed!";
      return false;
    }

    return makeMoveByUCI(uci_move);
  }
};

#ifndef CHESSGAME_NO_MAIN
int main() {
  Board *gameboard = new Board();
  ChessFacade chessGame = ChessFacade(gameboard);
  chessGame.fillBoard();
  gameboard->display();

  // Bot configuration
  string bot_choice;
  cout << "\nEnable bot play? (h=human vs human, w=human vs white bot, "
          "b=human vs black bot, a=bot vs bot): ";
  cin >> bot_choice;

  if (bot_choice != "h") {
    auto engine = make_unique<StockfishAdapter>();
    if (engine->initialize()) {
      chessGame.setEngine(move(engine));

      if (bot_choice == "w" || bot_choice == "a") {
        chessGame.setBotColor(WHITE, true);
      }
      if (bot_choice == "b" || bot_choice == "a") {
        chessGame.setBotColor(BLACK, true);
      }
      cout << "Stockfish engine initialized. Movetime: 200ms" << endl;
    } else {
      cout << "Warning: Could not initialize Stockfish. Playing human vs human."
           << endl;
    }
  }

  string fig;
  while (true) {
    Color turn = chessGame.getCurrentTurn();

    // Check if it's a bot's turn
    if (chessGame.isBotTurn()) {
      string color_str = (turn == WHITE) ? "White" : "Black";
      cout << color_str << " (bot) is thinking..." << endl;
      if (!chessGame.playBotMove()) {
        cout << "ERROR: Bot move failed!" << endl;
        break;
      }
      cout << color_str << " bot made a move." << endl;
    } else {
      // Human player's turn
      string player_prompt = (turn == WHITE) ? "White chose figure to move: "
                                             : "Black chose figure to move: ";
      while (true) {
        cout << player_prompt;
        cin >> fig;
        Coordinates picked = chessGame.stringToCoordinates(fig);
        if (chessGame.selectFigure(turn, picked)) {
          chessGame.showValidMoves(picked);
          break;
        }
        cout << "Invalid figure. Choose again." << endl;
      }

      string move_prompt =
          (turn == WHITE) ? "White move figure: " : "Black move figure: ";
      while (true) {
        cout << move_prompt;
        cin >> fig;
        if (chessGame.moveFigure(chessGame.stringToCoordinates(fig))) {
          break;
        }
      }
    }

    gameboard->display();
    if (chessGame.isGameOver()) {
      cout << chessGame.getLastMessage() << endl;
      break;
    }
  }
}
#endif
