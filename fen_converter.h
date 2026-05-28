#ifndef FEN_CONVERTER_H
#define FEN_CONVERTER_H

#include <string>
#include <sstream>

using namespace std;

/**
 * Utility for converting between Board state and FEN notation
 * NOTE: This header must be used AFTER the Board, Figure, and King/Ladya classes
 * are defined in gameboard.cpp
 */

/**
 * Generate FEN string from current board state
 * Assumes board is 8x8 with figures indexed [x][y] where:
 *   - x: 0-7 (files a-h)
 *   - y: 0-7 (ranks 1-8, where 0=rank1, 7=rank8)
 *
 * @param figures 8x8 array of Figure* (nullptr for empty squares)
 * @param castling_white_kingside whether white king-side castling is available
 * @param castling_white_queenside whether white queen-side castling is available
 * @param castling_black_kingside whether black king-side castling is available
 * @param castling_black_queenside whether black queen-side castling is available
 * @param active_color 'w' for white, 'b' for black
 * @param halfmove_clock number of halfmoves since last capture or pawn move
 * @param fullmove_number starting from 1, incremented after black move
 * @return FEN string
 */
inline string boardToFEN(const vector<vector<IFigure *>> &figures,
                          bool castling_white_kingside,
                          bool castling_white_queenside,
                          bool castling_black_kingside,
                          bool castling_black_queenside, char active_color,
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

    // En passant target square (not implemented, use -)
    fen << " -";

    // Halfmove clock
    fen << ' ' << halfmove_clock;

    // Fullmove number
    fen << ' ' << fullmove_number;

    return fen.str();
}

#endif // FEN_CONVERTER_H
