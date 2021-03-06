#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "san.h"

#define BB_SQUARE(sq) (1ULL << (sq))

#define BB_ALL    0xffffffffffffffffULL
#define BB_FILE_A 0x0101010101010101ULL
#define BB_FILE_H 0x8080808080808080ULL

static const char PCHR[] = "\0PNBRQK";

static inline int square_file(uint8_t square) {
    return square & 7;
}

static inline int square_rank(uint8_t square) {
    return square >> 3;
}

static int square_distance(uint8_t a, uint8_t b) {
    int rd = abs(square_rank(a) - square_rank(b));
    int fd = abs(square_file(a) - square_file(b));
    return (rd > fd) ? rd : fd;
}

static const int ROOK_DELTAS[] = { 8, 1, -8, -1, 0 };
static const int BISHOP_DELTAS[] = { 9, -9, 7, -7, 0 };
static const int KING_DELTAS[] = { 8, 1, -8, -1, 9, -9, 7, -7, 0 };
static const int KNIGHT_DELTAS[] = { 17, 15, 10, 6, -6, -10, -15, -17, 0 };

static uint64_t attacks_sliding(const int deltas[], uint8_t square, uint64_t occupied) {
    uint64_t attack = 0;

    for (int i = 0; deltas[i]; i++) {
        for (int s = square + deltas[i];
             s >= 0 && s < 64 && square_distance(s, s - deltas[i]) <= 2;
             s += deltas[i])
        {
            attack |= BB_SQUARE(s);
            if (occupied & BB_SQUARE(s)) break;
        }
    }

    return attack;
}

static inline uint8_t move_from(move_t move) {
    return (move >> 6) & 077;
}

static inline uint8_t move_to(move_t move) {
    return move & 077;
}

static piece_type_t move_promotion(move_t move) {
    static const piece_type_t promotions[] = { kNone, kPawn, kKnight, kBishop, kRook, kQueen, kKing, kNone };
    return promotions[move >> 12];
}

void move_uci(move_t move, char *uci) {
    if (!move) {
        sprintf(uci, "(none)");
        return;
    }

    int from = move_from(move);
    int to = move_to(move);

    const char promotions[] = "\0pnbrqk";

    sprintf(uci, "%c%c%c%c%c", 'a' + square_file(from), '1' + square_rank(from),
                               'a' + square_file(to),   '1' + square_rank(to),
                               promotions[move_promotion(move)]);
}

move_t move_parse(const char *uci) {
    const char promotions[] = "\0pnbrqk";

    if (strlen(uci) > 5 || strlen(uci) < 4) return 0;

    move_t move = (uci[2] - 'a') + ((uci[3] - '1') << 3) + ((uci[0] - 'a') << 6) + ((uci[1] - '1') << 9);

    if (uci[4]) {
        for (int k = 2; k <= 6; k++) {
            if (uci[4] == promotions[k]) {
                move |= k << 12;
                return move;
            }
        }

        return 0;
    }

    return move;
}

void board_reset(board_t *board) {
    board->occupied_co[kWhite] = 0xffffULL;
    board->occupied_co[kBlack] = 0xffff000000000000ULL;

    board->occupied[kAll] = 0xffff00000000ffffULL;
    board->occupied[kPawn] = 0xff00000000ff00ULL;
    board->occupied[kKnight] = 0x4200000000000042ULL;
    board->occupied[kBishop] = 0x2400000000000024ULL;
    board->occupied[kRook] = 0x8100000000000081ULL;
    board->occupied[kQueen] = 0x800000000000008ULL;
    board->occupied[kKing] = 0x1000000000000010ULL;

    board->turn = kWhite;
    board->ep_square = 0;
}

static piece_type_t board_piece_type_at(const board_t *board, uint8_t square) {
    uint64_t bb = BB_SQUARE(square);
    for (piece_type_t pt = kPawn; pt <= kKing; pt++) {
        if (board->occupied[pt] & bb) return pt;
    }

    return kNone;
}

void bb_debug(uint64_t bb) {
    for (int rank = 7; rank >= 0; rank--) {
        for (int file = 0; file < 8; file++) {
            uint8_t square = (rank << 3) | file;

            if (bb & BB_SQUARE(square)) printf("1");
            else printf(".");

            if (file < 7) printf(" ");
            else printf("\n");
        }
    }
}

void board_debug(const board_t *board) {
    for (int rank = 7; rank >= 0; rank--) {
        for (int file = 0; file < 8; file++) {
            uint8_t square = (rank << 3) | file;
            piece_type_t pt = board_piece_type_at(board, square);

            if (!pt) printf(".");
            else if (board->occupied_co[kWhite] & BB_SQUARE(square)) printf("%c", PCHR[pt]);
            else printf("%c", PCHR[pt] - 'A' + 'a');

            if (file < 7) printf(" ");
            else printf("\n");
        }
    }
}

void board_move(board_t *board, move_t move) {
    if (!move) return;

    board->ep_square = 0;

    uint8_t from = move_from(move);
    uint8_t to = move_to(move);
    piece_type_t pt = board_piece_type_at(board, from);
    if (!pt) return;

    board->occupied_co[board->turn] &= ~BB_SQUARE(from);
    board->occupied[kAll] &= ~BB_SQUARE(from);
    board->occupied[pt] &= ~BB_SQUARE(from);

    const piece_type_t capture = board_piece_type_at(board, to);

    if (capture) {
        board->occupied_co[!board->turn] &= ~BB_SQUARE(to);
        board->occupied[capture] &= ~BB_SQUARE(to);
    }

    if (pt == kPawn) {
        if (square_file(from) != square_file(to) && !capture) {
            uint64_t ep_mask = BB_SQUARE(to + (board->turn ? -8 : 8));
            board->occupied_co[!board->turn] &= ~ep_mask;
            board->occupied[kAll] &= ~ep_mask;
            board->occupied[kPawn] &= ~ep_mask;
        } else if (square_distance(from, to) == 2) {
            board->ep_square = from + (board->turn ? 8 : -8);
        }
    }

    if (move_promotion(move)) pt = move_promotion(move);
    board->occupied_co[board->turn] |= BB_SQUARE(to);
    board->occupied[kAll] |= BB_SQUARE(to);
    board->occupied[pt] |= BB_SQUARE(to);

    board->turn = !board->turn;
}

bool board_is_game_over(const board_t *board) {
    if (!board->occupied_co[kBlack] || !board->occupied_co[kWhite]) return true;

    uint64_t us = board->occupied_co[board->turn];

    uint64_t pawn_attacks = 0, pawn_moves;
    if (board->turn == kWhite) {
        pawn_attacks |= ((board->occupied[kPawn] & us) << 7) & ~BB_FILE_H;
        pawn_attacks |= ((board->occupied[kPawn] & us) << 9) & ~BB_FILE_A;
        pawn_moves = (board->occupied[kPawn] & us) << 8;
    } else {
        pawn_attacks |= ((board->occupied[kPawn] & us) >> 9) & ~BB_FILE_H;
        pawn_attacks |= ((board->occupied[kPawn] & us) >> 7) & ~BB_FILE_A;
        pawn_moves = (board->occupied[kPawn] & us) >> 8;
    }
    if (pawn_attacks & board->occupied_co[!board->turn]) return false;
    if (board->ep_square && pawn_attacks & BB_SQUARE(board->ep_square)) return false;
    if (pawn_moves & ~board->occupied[kAll]) return false;

    uint64_t knights = board->occupied[kKnight] & us;
    while (knights) {
        if (attacks_sliding(KNIGHT_DELTAS, bb_poplsb(&knights), BB_ALL) & ~us) return false;
    }

    uint64_t diagonal = (board->occupied[kBishop] | board->occupied[kQueen] | board->occupied[kKing]) & us;
    while (diagonal) {
        if (attacks_sliding(BISHOP_DELTAS, bb_poplsb(&diagonal), board->occupied[kAll]) & ~us) return false;
    }

    uint64_t straight = (board->occupied[kRook] | board->occupied[kQueen] | board->occupied[kKing]) & us;
    while (straight) {
        if (attacks_sliding(ROOK_DELTAS, bb_poplsb(&straight), board->occupied[kAll]) & ~us) return false;
    }

    return true;
}

void board_san(board_t *board, move_t move, char *san) {
    uint8_t from = move_from(move);
    uint8_t to = move_to(move);
    piece_type_t pt = board_piece_type_at(board, from);

    if (!move || !pt) {
        sprintf(san, "--");
        return;
    }

    if (pt == kPawn) {
        if (square_file(from) != square_file(to)) {
            *san++ = 'a' + square_file(from);
            *san++ = 'x';
        }
    } else {
        *san++ = PCHR[pt];

        uint64_t candidates = 0;
        if (pt == kKing) candidates = attacks_sliding(KING_DELTAS, to, board->occupied[kAll]);
        if (pt == kKnight) candidates = attacks_sliding(KNIGHT_DELTAS, to, BB_ALL);
        if (pt == kRook || pt == kQueen) candidates |= attacks_sliding(ROOK_DELTAS, to, board->occupied[kAll]);
        if (pt == kBishop || pt == kQueen) candidates |= attacks_sliding(BISHOP_DELTAS, to, board->occupied[kAll]);
        candidates &= board->occupied[pt] & board->occupied_co[board->turn];

        bool rank = false, file = false;
        while (candidates) {
            uint8_t square = bb_poplsb(&candidates);
            if (square == from) continue;
            if (square_rank(from) == square_rank(square)) file = true;
            if (square_file(from) == square_file(square)) rank = true;
            else file = true;
        }

        if (file) *san++ = 'a' + square_file(from);
        if (rank) *san++ = '1' + square_rank(from);

        if (board->occupied[kAll] & BB_SQUARE(to)) *san++ = 'x';
    }

    *san++ = 'a' + square_file(to);
    *san++ = '1' + square_rank(to);

    if (move_promotion(move)) {
        *san++ = '=';
        *san++ = PCHR[move_promotion(move)];
    }

    board_t board_after = *board;
    board_move(&board_after, move);
    if (board_is_game_over(&board_after)) *san++ = '#';

    *san++ = 0;
}
