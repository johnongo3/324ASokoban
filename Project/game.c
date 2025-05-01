/*
 * game.c
 *
 * Authors: Jarrod Bennett, Cody Burnett, Bradley Stone, Yufeng Gao
 * Modified by: Johnathan Ngo
 *
 * Game logic and state handler.
 */ 

#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include "ledmatrix.h"
#include "timer0.h"
#include "timer1.h"
#include "terminalio.h"


// ========================== NOTE ABOUT MODULARITY ==========================

// The functions and global variables defined with the static keyword can
// only be accessed by this source file. If you wish to access them in
// another C file, you can remove the static keyword, and define them with
// the extern keyword in the other C file (or a header file included by the
// other C file). While not assessed, it is suggested that you develop the
// project with modularity in mind. Exposing internal variables and functions
// to other .C files reduces modularity.


// ============================ GLOBAL VARIABLES =============================

// The game board, which is dynamically constructed by initialise_game() and
// updated throughout the game. The 0th element of this array represents the
// bottom row, and the 7th element of this array represents the top row.
static uint8_t board[MATRIX_NUM_ROWS][MATRIX_NUM_COLUMNS];

// The location of the player.
static uint8_t player_row;
static uint8_t player_col;

// A flag for keeping track of whether the player is currently visible.
static bool player_visible;

// Global variable to track steps
int step_count = 0; // Initialize step_count

// Digits for seven segment display
uint8_t sevSeg[10] = {63,6,91,79,102,109,125,7,127,111};

// Tens or Ones on SSD
static uint8_t digit = 0;

// A flag for the visibility of the target.
static bool targets_visible;

bool box_on_target_flash;
uint32_t box_flash_start_time;
uint8_t box_flash_row, box_flash_col;
bool flash_on;

// joystick values
uint16_t joystick_value;
uint8_t x_or_y = 0;

// Move Undo Arrays
int8_t move_delta_row[6] = {0};
int8_t move_delta_col[6] = {0};
bool move_moved_box[6] = {0};
int8_t move_box_row[6] = {0};
int8_t move_box_col[6] = {0};

int undo_capacity = 0;
int redo_capacity = 0;
int move_index = 0;
int redo_index = 0;

extern volatile uint8_t tone_playing;
extern volatile uint16_t tone_duration_ms;

// ========================== GAME LOGIC FUNCTIONS ===========================

// This function paints a square based on the object(s) currently on it.
static void paint_square(uint8_t row, uint8_t col)
{
	switch (board[row][col] & OBJECT_MASK)
	{
		case ROOM:
			ledmatrix_update_pixel(row, col, COLOUR_BLACK);
			break;
		case WALL:
			ledmatrix_update_pixel(row, col, COLOUR_WALL);
			break;
		case BOX:
			ledmatrix_update_pixel(row, col, COLOUR_BOX);
			break;
		case TARGET:
			ledmatrix_update_pixel(row, col, COLOUR_TARGET);
			break;
		case BOX | TARGET:
			ledmatrix_update_pixel(row, col, COLOUR_DONE);
			break;
		default:
			break;
	}
}

void display_digit(uint8_t digit, uint8_t number) {
	// Clear PORTC before setting new value
	PORTC = 0;

	// Set the segments for the given number
	PORTC = sevSeg[number]; // Assume number is in range 0 to 9

	// Activate the correct digit
	if (digit == 0) {
		// Activate the ones digit
		PORTD &= ~(1 << 2); // Clear bit to select ones digit
		} else {
		// Activate the tens digit
		PORTD |= (1 << 2); // Set bit to select tens digit
	}
}

void display_step_count(void) {
	DDRC |= 0xFF;
	DDRD |= (1 << 2);
	
	// Output the current digit
	if (digit == 0) {
		// Extract the ones place from the step count
		display_digit(digit, step_count % 10);
		} else {
		// Extract the tens place from the step count
		display_digit(digit, (step_count / 10) % 10);
	}
	// Change the digit flag for next time. if 0 becomes 1, if 1 becomes 0.
	digit = 1 - digit;
}

void update_undo_leds(void) {
	// Clear the LED bits (PA2 to PA7)
	PORTA &= ~0xFC; // Clear bits 2 to 7

	// Set the appropriate bits based on the undo capacity
	for (int i = 0; i < undo_capacity; i++) {
		PORTA |= (1 << (i + 2)); // Set bits PA2 to PA7 based on undo capacity
	}
}

// This function initialises the global variables used to store the game
// state, and renders the initial game display.
void initialise_game(void)
{
	// Short definitions of game objects used temporarily for constructing
	// an easier-to-visualise game layout.
	#define _	(ROOM)
	#define W	(WALL)
	#define T	(TARGET)
	#define B	(BOX)

	// The starting layout of level 1. In this array, the top row is the
	// 0th row, and the bottom row is the 7th row. This makes it visually
	// identical to how the pixels are oriented on the LED matrix, however
	// the LED matrix treats row 0 as the bottom row and row 7 as the top
	// row.
	static const uint8_t lv1_layout[MATRIX_NUM_ROWS][MATRIX_NUM_COLUMNS] =
	{
		{ _, W, _, W, W, W, _, W, W, W, _, _, W, W, W, W },
		{ _, W, T, W, _, _, W, T, _, B, _, _, _, _, T, W },
		{ _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _ },
		{ W, _, B, _, _, _, _, W, _, _, B, _, _, B, _, W },
		{ W, _, _, _, W, _, B, _, _, _, _, _, _, _, _, _ },
		{ _, _, _, _, _, _, T, _, _, _, _, _, _, _, _, _ },
		{ _, _, _, W, W, W, W, W, W, T, _, _, _, _, _, W },
		{ W, W, _, _, _, _, _, _, W, W, _, _, W, W, W, W }
	};


	// Undefine the short game object names defined above, so that you
	// cannot use use them in your own code. Use of single-letter names/
	// constants is never a good idea.
	#undef _
	#undef W
	#undef T
	#undef B

	// Set the initial player location (for level 1).
	player_row = 5;
	player_col = 2;

	// Make the player icon initially invisible.
	player_visible = false;

	// Copy the starting layout (level 1 map) to the board array, and flip
	// all the rows.
	for (uint8_t row = 0; row < MATRIX_NUM_ROWS; row++)
	{
		for (uint8_t col = 0; col < MATRIX_NUM_COLUMNS; col++)
		{
			board[MATRIX_NUM_ROWS - 1 - row][col] =
				lv1_layout[row][col];
		}
	}

	// Draw the game board (map).
	for (uint8_t row = 0; row < MATRIX_NUM_ROWS; row++)
	{
		for (uint8_t col = 0; col < MATRIX_NUM_COLUMNS; col++)
		{
			paint_square(row, col);
		}
	}
		
	for (int8_t row = MATRIX_NUM_ROWS - 1; row >= 0; row--) {
		move_terminal_cursor(MATRIX_NUM_ROWS - row, 20); 

		for (uint8_t col = 0; col < MATRIX_NUM_COLUMNS; col++) {
			if (board[row][col] == WALL) {
				set_display_attribute(BG_YELLOW);  // Wall
				} else if (board[row][col] == BOX) {
				set_display_attribute(BG_MAGENTA);  // Box
				} else if (board[row][col] == TARGET) {
				set_display_attribute(BG_RED);  // Target
				} else if (board[row][col] == (BOX | TARGET)) {
				set_display_attribute(BG_GREEN);  // Box on target
				} else if (row == player_row && col == player_col) {
				set_display_attribute(BG_CYAN);  // Player position
			}

			// Print a space with the current background color to represent the object
			printf_P(PSTR("  "));
			set_display_attribute(BG_BLACK);  // Reset to black background for next loop
		}

		// Print newline after each row
		printf_P(PSTR("\n"));
	}
	
	// Initialize step count
	step_count = 0;
	display_step_count();
	
	move_terminal_cursor(5, 5);
	printf_P(PSTR("Level 1"));
	
	memset(move_delta_row, 0, sizeof(move_delta_row));
	memset(move_delta_col, 0, sizeof(move_delta_col));
	memset(move_moved_box, 0, sizeof(move_moved_box));
	memset(move_box_row, 0, sizeof(move_box_row));
	memset(move_box_col, 0, sizeof(move_box_col));
	move_index = 0;
	undo_capacity = 0;
	redo_capacity = 0;
	redo_index = 0;
		
	update_undo_leds();
}

void initialise_levelTwo(void) {
	// Short definitions of game objects used temporarily for constructing
	// an easier-to-visualise game layout.
	#define _	(ROOM)
	#define W	(WALL)
	#define T	(TARGET)
	#define B	(BOX)

	// The starting layout of level 1. In this array, the top row is the
	// 0th row, and the bottom row is the 7th row. This makes it visually
	// identical to how the pixels are oriented on the LED matrix, however
	// the LED matrix treats row 0 as the bottom row and row 7 as the top
	// row.
	static const uint8_t lv2_layout[MATRIX_NUM_ROWS][MATRIX_NUM_COLUMNS] =
	{
		{ _, _, W, W, W, W, _, _, W, W, _, _, _, _, _, W },
		{ _, _, W, _, _, W, _, W, W, _, _, _, _, B, _, _ },
		{ _, _, W, _, B, W, W, W, _, _, T, W, _, T, W, W },
		{ _, _, W, _, _, _, _, T, _, _, B, W, W, W, _, _ },
		{ W, W, W, W, _, W, _, _, _, _, _, W, _, W, W, _ },
		{ W, T, B, _, _, _, _, B, _, _, _, W, W, _, W, W },
		{ W, _, _, _, T, _, _, _, _, _, _, B, T, _, _, _ },
		{ W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W }
	};


	// Undefine the short game object names defined above, so that you
	// cannot use use them in your own code. Use of single-letter names/
	// constants is never a good idea.
	#undef _
	#undef W
	#undef T
	#undef B
	
	// starting player location
	player_col = 15;
	player_row = 6;
	
	// Make the player icon initially invisible.
	player_visible = false;

	// Copy the starting layout (level 1 map) to the board array, and flip
	// all the rows.
	for (uint8_t row = 0; row < MATRIX_NUM_ROWS; row++)
	{
		for (uint8_t col = 0; col < MATRIX_NUM_COLUMNS; col++)
		{
			board[MATRIX_NUM_ROWS - 1 - row][col] =
			lv2_layout[row][col];
		}
	}

	// Draw the game board (map).
	for (uint8_t row = 0; row < MATRIX_NUM_ROWS; row++)
	{
		for (uint8_t col = 0; col < MATRIX_NUM_COLUMNS; col++)
		{
			paint_square(row, col);
		}
	}
	
	for (int8_t row = MATRIX_NUM_ROWS - 1; row >= 0; row--) {
		move_terminal_cursor(MATRIX_NUM_ROWS - row, 20);

		for (uint8_t col = 0; col < MATRIX_NUM_COLUMNS; col++) {
			if (board[row][col] == WALL) {
				set_display_attribute(BG_YELLOW);  // Wall
				} else if (board[row][col] == BOX) {
				set_display_attribute(BG_MAGENTA);  // Box
				} else if (board[row][col] == TARGET) {
				set_display_attribute(BG_RED);  // Target
				} else if (board[row][col] == (BOX | TARGET)) {
				set_display_attribute(BG_GREEN);  // Box on target
				} else if (row == player_row && col == player_col) {
				set_display_attribute(BG_CYAN);  // Player position
			}

			// Print a space with the current background color to represent the object
			printf_P(PSTR("  "));
			set_display_attribute(BG_BLACK);  // Reset to black background for next loop
		}

		// Print newline after each row
		printf_P(PSTR("\n"));
	}
	
	// Initialize step count
	step_count = 0;
	display_step_count();
	
	move_terminal_cursor(5, 5);
	printf_P(PSTR("Level 2"));
	
	memset(move_delta_row, 0, sizeof(move_delta_row));
	memset(move_delta_col, 0, sizeof(move_delta_col));
	memset(move_moved_box, 0, sizeof(move_moved_box));
	memset(move_box_row, 0, sizeof(move_box_row));
	memset(move_box_col, 0, sizeof(move_box_col));
	move_index = 0;
	undo_capacity = 0;
	redo_capacity = 0;
	redo_index = 0;
		
	update_undo_leds();
}

void update_terminal_screen(int8_t row, int8_t col, PixelColour colour) {
	move_terminal_cursor(MATRIX_NUM_ROWS - row, col * 2 + 20);
	set_display_attribute(colour);
	printf_P(PSTR("  "));
	set_display_attribute(BG_BLACK);
}


void player_move_sound(void) {
	play_tone(NOTE_C4, 50);
	play_tone(NOTE_D4, 50);
}

void box_on_target_sound(void) {
	play_tone(NOTE_B5, 100);
	_delay_ms(50);
	play_tone(NOTE_E6, 150);
}

void game_over_sound(void) {
	play_tone(NOTE_G3, 250);
	_delay_ms(150);
	play_tone(NOTE_G4, 250);
	_delay_ms(150);
	play_tone(NOTE_G3, 250);
	_delay_ms(150);
	play_tone(NOTE_G5, 400);
	_delay_ms(200);
}

// This function flashes the player icon. If the icon is currently visible, it
// is set to not visible and removed from the display. If the player icon is
// currently not visible, it is set to visible and rendered on the display.
// The static global variable "player_visible" indicates whether the player
// icon is currently visible.
void flash_player(void)
{
	player_visible = !player_visible;
	if (player_visible)
	{
		// The player is visible, paint it with COLOUR_PLAYER.
		ledmatrix_update_pixel(player_row, player_col, COLOUR_PLAYER);
	}
	else
	{
		// The player is not visible, paint the underlying square
		paint_square(player_row, player_col);
	}
}

void flash_target(void) {
	targets_visible = !targets_visible;
	
	for (uint8_t row = 0; row < MATRIX_NUM_ROWS; row++) {
		for (uint8_t col = 0; col < MATRIX_NUM_COLUMNS; col++) {
			if ((board[row][col] & TARGET) && !(board[row][col] & BOX)) {
				if (targets_visible) {
					ledmatrix_update_pixel(row, col, COLOUR_TARGET);
				} else {
					ledmatrix_update_pixel(row, col, COLOUR_BLACK);
				}
			}
		}
	}

}

void repaint_squares_around_target(uint8_t target_row, uint8_t target_col) {
	for (uint8_t row = target_row - 1; row <= target_row + 1; row++) {
		for (uint8_t col = target_col - 1; col <= target_col + 1; col++) {
			paint_square(row, col);
		}
	}
}

void flash_box_on_target(uint8_t target_row, uint8_t target_col) {
    flash_on = !flash_on;

    // Check if the box is still on the target
    if (!(board[target_row][target_col] & BOX)) {
        box_on_target_flash = false; // Stop the animation
        repaint_squares_around_target(target_row, target_col);
        return;
    }

    // Flash an "X" pattern
    for (int8_t offset = -1; offset <= 1; offset++) {
        if (flash_on) {
            // Top-left to bottom-right diagonal
            ledmatrix_update_pixel(target_row + offset, target_col + offset, COLOUR_LIGHT_GREEN);
            // Top-right to bottom-left diagonal
            ledmatrix_update_pixel(target_row + offset, target_col - offset, COLOUR_LIGHT_GREEN);
        } else {
            // Repaint the original squares
            paint_square(target_row + offset, target_col + offset);
            paint_square(target_row + offset, target_col - offset);
        }
    }
}

void init_adc(void) {
	// Set up ADC - AVCC reference, right adjust
	// Input selection doesn't matter yet - we'll swap this around in the while
	// loop below.
	ADMUX = (1<<REFS0);
	// Turn on the ADC (but don't start a conversion yet). Choose a clock
	// divider of 64. (The ADC clock must be somewhere
	// between 50kHz and 200kHz. We will divide our 8MHz clock by 64
	// to give us 125kHz.)
	ADCSRA = (1<<ADEN)|(1<<ADPS2)|(1<<ADPS1);
}

uint16_t read_adc(uint8_t x_or_y) {
	ADCSRA |= (1<<ADSC);
	while(ADCSRA & (1<<ADSC)) {
		; /* Wait until conversion finished */
	}
	// Set the ADC mux to choose ADC0 if x_or_y is 0, ADC1 if x_or_y is 1
	if(x_or_y == 0) {
		ADMUX &= ~1;
		} else {
		ADMUX |= 1;
	}
	// Start the ADC conversion
	ADCSRA |= (1<<ADSC);
	
	while(ADCSRA & (1<<ADSC)) {
		; /* Wait until conversion finished */
	}
	return ADC; // read the value
}

void read_joystick_direction(int8_t *delta_row, int8_t *delta_col) {
	uint16_t x_value = read_adc(0);
	uint16_t y_value = read_adc(1);
	
	if (x_value < 550 && x_value > 500 && y_value < 500 && y_value > 500) {
		*delta_row = 0;
		*delta_col = 0;
	}
	
	// NORTH
	if (x_value > 1000 && y_value > 500 && y_value < 600) {
		*delta_row = 1;
	} else if (x_value > 500 && x_value < 600 && y_value < 100) {
		// EAST
		*delta_col = 1;
	} else if (x_value > 500 && x_value < 600 && y_value > 900) {
		// WEST
		*delta_col = -1;
	} else if (x_value < 200 && y_value > 450 && y_value < 600) {
		// SOUTHn
		*delta_row = -1;
	}
	
	// NORTH EAST
	if (x_value > 900 && y_value < 450) {
		*delta_row = 1;
		*delta_col = 1;
	} else if (x_value > 900 && y_value > 750) {
		// NORTH WEST
		*delta_row = 1;
		*delta_col = -1;
		
	} else if (x_value < 350 && y_value < 450) {
		// SOUTH EAST
		*delta_row = -1;
		*delta_col = 1;
		
	} else if (x_value < 350 && y_value > 750) {
		// SOUTH WEST
		*delta_row = -1;
		*delta_col = -1;
	}
	
}

bool can_move_diagonally(uint8_t delta_row, uint8_t delta_col) {
	int8_t new_row = player_row + delta_row;
	int8_t new_col = player_col + delta_col;

	// Check for wrapping
	if (new_row < 0 || new_row >= MATRIX_NUM_ROWS || new_col < 0 || new_col >= MATRIX_NUM_COLUMNS) {
		return false;
	}

	// Check for obstacles
	int obstacles = 0;
	if (board[new_row][player_col] & (WALL | BOX)) {
		obstacles++;
	}
	if (board[player_row][new_col] & (WALL | BOX)) {
		obstacles++;
	}

	// If there are two obstacles, the move cannot be made
	if (obstacles >= 2) {
		move_terminal_cursor(13, 1);
		printf_P(PSTR("Cannot move diagonally."));
		return false;
	}

	// Ensure the diagonal position itself is not blocked
	if (board[new_row][new_col] & (WALL | BOX)) {
		move_terminal_cursor(13, 1);
		printf_P(PSTR("Cannot move diagonally."));
		return false;
	}

	return true;
}


void calculate_position(int8_t delta_row, int8_t delta_col, int8_t *new_row, int8_t *new_col) {
	*new_row = player_row + delta_row;
	*new_col = player_col + delta_col;
	
	if (*new_row < 0) {
		// MOVE DOWN OUT OF BOARD
		*new_row = 7;
		} else if (*new_col < 0) {
		// MOVE LEFT OUT OF BOARD
		*new_col = 15;
		} else if (*new_row > 7) {
		// MOVE UP OUT OF BOARD
		*new_row = 0;
		} else if (*new_col > 15) {
		// MOVE RIGHT OUT OF BOARD
		*new_col = 0;
	}
}

void undo_move(void) {
	if (undo_capacity > 0) {
		move_index = (move_index - 1 + MAX_UNDO_CAPACITY) % MAX_UNDO_CAPACITY;
		undo_capacity--;
		redo_capacity++;
		
		int8_t delta_row = move_delta_row[move_index];
		int8_t delta_col = move_delta_col[move_index];
		bool moved_box = move_moved_box[move_index];
		int8_t box_row = move_box_row[move_index];
		int8_t box_col = move_box_col[move_index];
		
		// undo player movement
		player_visible = 1;
		flash_player();
		
		update_terminal_screen(player_row, player_col, BG_BLACK);
		player_row -= delta_row;
		player_col -= delta_col;
		
		// Check for wrap-around or out-of-bounds
		if (player_row < 0) player_row = 7;
		if (player_row > 7) player_row = 0;
		if (player_col < 0) player_col = 15;
		if (player_col > 15) player_col = 0;
				
		player_move_sound();
		update_terminal_screen(player_row, player_col, BG_CYAN);
		
		flash_player();
		
		if (moved_box) {
            board[box_row][box_col] &= ~BOX;
            board[box_row - delta_row][box_col - delta_col] |= BOX;
			
			// Check for wrap-around or out-of-bounds for the box
			if (box_row - delta_row < 0) box_row = 7;
			if (box_row - delta_row > 7) box_row = 0;
			if (box_col - delta_col < 0) box_col = 15;
			if (box_col - delta_col > 15) box_col = 0;
					
			update_terminal_screen(box_row, box_col, BG_BLACK);
			paint_square(box_row, box_col);
			paint_square(player_row + delta_row, player_col + delta_col);
			update_terminal_screen(player_row + delta_row, player_col + delta_col, BG_MAGENTA);
		}
		
		// update step count
		step_count--;
		display_step_count();
		
		// update LEDs
		update_undo_leds();
	}
}

void redo_move(void) {
    if (redo_capacity > 0) {
        // The move to redo is at the current move_index
        int8_t delta_row = move_delta_row[move_index];
        int8_t delta_col = move_delta_col[move_index];
        bool moved_box = move_moved_box[move_index];
        int8_t box_new_row = move_box_row[move_index];
        int8_t box_new_col = move_box_col[move_index];

        // Remove player from current position on display
        if (board[player_row][player_col] & TARGET) {
            update_terminal_screen(player_row, player_col, BG_RED);
        } else {
            update_terminal_screen(player_row, player_col, BG_BLACK);
        }

        // Update player position with delta
		update_terminal_screen(player_row, player_col, BG_BLACK);
		paint_square(player_row, player_col);
        player_row += delta_row;
        player_col += delta_col;

        // Handle wrap-around for player
        if (player_row < 0) {
            player_row = MATRIX_NUM_ROWS - 1;
        }
        if (player_row >= MATRIX_NUM_ROWS) {
            player_row = 0;
        }
        if (player_col < 0) {
            player_col = MATRIX_NUM_COLUMNS - 1;
        }
        if (player_col >= MATRIX_NUM_COLUMNS) {
            player_col = 0;
        }

        // Update player position on display
        update_terminal_screen(player_row, player_col, BG_CYAN);
        player_move_sound();
        flash_player();

        if (moved_box) {
            // Calculate the old box position
            int8_t old_box_row = box_new_row - delta_row;
            int8_t old_box_col = box_new_col - delta_col;

            // Handle wrap-around for old box position
            if (old_box_row < 0) {
                old_box_row = MATRIX_NUM_ROWS - 1;
            }
            if (old_box_row >= MATRIX_NUM_ROWS) {
                old_box_row = 0;
            }
            if (old_box_col < 0) {
                old_box_col = MATRIX_NUM_COLUMNS - 1;
            }
            if (old_box_col >= MATRIX_NUM_COLUMNS) {
                old_box_col = 0;
            }

            // Remove box from old position
            board[old_box_row][old_box_col] &= ~BOX;
            // Update display for old box position
            update_terminal_screen(old_box_row, old_box_col, BG_BLACK);
			paint_square(old_box_row, old_box_col); // Repaint new box position


            // Place box at new position
            board[box_new_row][box_new_col] |= BOX;
            // Update display for new box position
            update_terminal_screen(box_new_row, box_new_col, BG_MAGENTA);
            paint_square(box_new_row, box_new_col); // Repaint new box position

        }

        // Increment step count
        step_count++;
        display_step_count();

        // Update capacities
        move_index = (move_index + 1) % MAX_UNDO_CAPACITY;
        undo_capacity++;
        redo_capacity--;

        // Update LED indicators
        update_undo_leds();

    }
}

// This function handles player movements.
void move_player(int8_t delta_row, int8_t delta_col)
{
	// REMOVE PLAYER
	player_visible = 1;
	flash_player();
	bool hit_wall = false;
	bool moved_box = false;
	
	// CALCULATE POSITION
	int8_t new_row, new_col;
	
	calculate_position(delta_row, delta_col, &new_row, &new_col);
	
	int8_t box_new_row = new_row + delta_row;
	int8_t box_new_col = new_col + delta_col;
	
	// CHECK FOR WALL
	if (board[new_row][new_col] & WALL) {
		move_terminal_cursor(13, 1);
		printf("I've hit a wall.");
		hit_wall = true;
		return;
	}
	
	// CHECK FOR BOXES
	if (board[new_row][new_col] & BOX) {
		// CHECK IF BOX CAN BE PUSHED
		
		// Handle wrapping for rows
		if (box_new_row < 0) {
				box_new_row = MATRIX_NUM_ROWS - 1;
				} else if (box_new_row >= MATRIX_NUM_ROWS) {
				box_new_row = 0;
		}
		
		// Handle wrapping for columns
		if (box_new_col < 0) {
				box_new_col = MATRIX_NUM_COLUMNS - 1;
				} else if (box_new_col >= MATRIX_NUM_COLUMNS) {
				box_new_col = 0;
		}
		
		// Check if the new position is free for the box to move
		if (board[box_new_row][box_new_col] & WALL) {
				move_terminal_cursor(13, 1);
				printf("Cannot push box onto wall.");
				return;
		}
		
		if (board[box_new_row][box_new_col] & BOX) {
				move_terminal_cursor(13, 1);
				printf("Cannot push box onto another box.");
				return;
		}
		
		// Move the box
		board[new_row][new_col] &= ~BOX; // Remove box from current position
		board[box_new_row][box_new_col] |= BOX; // Add box to new position
		moved_box = true; // set flag to true.
		paint_square(new_row, new_col); // Repaint the old position
		paint_square(box_new_row, box_new_col); // Paint the new position
		update_terminal_screen(box_new_row, box_new_col, BG_MAGENTA);
		update_terminal_screen(new_row, new_col, BG_BLACK);
					
		if (board[box_new_row][box_new_col] & TARGET) {
				// If the box moves onto a target
				board[box_new_row][box_new_col] |= BOX;
				paint_square(box_new_row, box_new_col);
				update_terminal_screen(box_new_row, box_new_col, BG_GREEN); // Box on target
				box_on_target_sound();
				box_on_target_flash = true;
				flash_on = false;
				box_flash_row = box_new_row;
				box_flash_col = box_new_col;
				box_flash_start_time = get_current_time();
		}
		
	}
	
	if (hit_wall == false) {
		if (board[player_row][player_col] & TARGET) {
				update_terminal_screen(player_row, player_col, BG_RED); // Re-display target
		} else {
				update_terminal_screen(player_row, player_col, BG_BLACK);
		}
		
		player_row = new_row;
		player_col = new_col;
		
        // Store the move in history
		move_delta_row[move_index] = delta_row;
		move_delta_col[move_index] = delta_col;
		move_moved_box[move_index] = moved_box;
		move_box_row[move_index] = box_new_row;
		move_box_col[move_index] = box_new_col;

        move_index = (move_index + 1) % MAX_UNDO_CAPACITY;
        if (undo_capacity < MAX_UNDO_CAPACITY) {
	        undo_capacity++;
        }	
		
		redo_capacity = 0;
		
		move_terminal_cursor(13, 1);
		clear_to_end_of_line();
		
		update_terminal_screen(player_row, player_col, BG_CYAN);
		player_move_sound();
		
		step_count++;
		display_step_count();
		
		update_undo_leds();
	}
	

	
	flash_player();
	player_visible = 1;
}

// This function checks if the game is over (i.e., the level is solved), and
// returns true iff (if and only if) the game is over.
bool is_game_over(void)
{
	for (uint8_t row = 0; row < MATRIX_NUM_ROWS; row++) {
		for (uint8_t col = 0; col < MATRIX_NUM_COLUMNS; col++) {
			if ((board[row][col] & BOX) && !(board[row][col] & TARGET)) {
				return false;
			} else {
				paint_square(row, col);
			}
		}
	}
	
	if (player_visible) {
		flash_player();
		update_terminal_screen(player_row, player_col, BG_BLACK);
	}
	
	game_over_sound();
	return true;
}







