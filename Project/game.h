/*
 * game.h
 *
 * Authors: Jarrod Bennett, Cody Burnett, Bradley Stone, Yufeng Gao
 * Modified by: Johnathan Ngo
 *
 * Function prototypes for game functions available externally. You may wish
 * to add extra function prototypes here to make other functions available to
 * other files.
 */

#ifndef GAME_H_
#define GAME_H_
#define F_CPU 8000000UL
#define MAX_UNDO_CAPACITY 6

#include <util/delay.h>
#include <stdint.h>
#include <stdbool.h>

// Object definitions.
#define ROOM       	(0U << 0)
#define WALL       	(1U << 0)
#define BOX        	(1U << 1)
#define TARGET     	(1U << 2)
#define OBJECT_MASK	(ROOM | WALL | BOX | TARGET)

// Colour definitions.
#define COLOUR_PLAYER	(COLOUR_DARK_GREEN)
#define COLOUR_WALL  	(COLOUR_YELLOW)
#define COLOUR_BOX   	(COLOUR_ORANGE)
#define COLOUR_TARGET	(COLOUR_RED)
#define COLOUR_DONE  	(COLOUR_GREEN)

// MUSIC NOTES
#define NOTE_C2  33
#define NOTE_C4  262
#define NOTE_C6  1046
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_E6  1319
#define NOTE_F4  349
#define NOTE_G2  98
#define NOTE_G3  185
#define NOTE_G4  392
#define NOTE_G5  784
#define NOTE_G6  1568
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_B5  988
#define NOTE_C5  523


/// <summary>
/// Initialises the game.
/// </summary>
void initialise_game(void);

/// <summary>
/// Moves the player based on row and column deltas.
/// </summary>
/// <param name="delta_row">The row delta.</param>
/// <param name="delta_col">The column delta.</param>
void move_player(int8_t delta_row, int8_t delta_col);

void initialise_levelTwo(void);

void display_step_count(void);

void flash_target(void);

void flash_box_on_target(uint8_t target_row, uint8_t target_col);

void repaint_squares_around_target(uint8_t target_row, uint8_t target_col);

void read_joystick_direction(int8_t *delta_row, int8_t *delta_col);

void init_adc(void);

bool can_move_diagonally(uint8_t row, uint8_t col);

void redo_move(void);

void undo_move(void);

void init_pwm();

/// <summary>
/// Detects whether the game is over (i.e., current level solved).
/// </summary>
/// <returns>Whether the game is over.</returns>
bool is_game_over(void);

/// <summary>
/// Flashes the player icon.
/// </summary>
void flash_player(void);

// Declare step_count as extern to be accessible in other files
extern int step_count;

#endif /* GAME_H_ */
