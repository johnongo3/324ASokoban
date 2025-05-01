/*
 * project.c
 *
 * Authors: Peter Sutton, Luke Kamols, Jarrod Bennett, Cody Burnett,
 *          Bradley Stone, Yufeng Gao
 * Modified by: Johnathan Ngo
 *
 * Main project event loop and entry point.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#include "game.h"
#include "startscrn.h"
#include "ledmatrix.h"
#include "buttons.h"
#include "serialio.h"
#include "terminalio.h"
#include "timer0.h"
#include "timer1.h"
#include "timer2.h"


// Function prototypes - these are defined below (after main()) in the order
// given here.
void initialise_hardware(void);
void start_screen(void);
void new_game(void);
void play_game(void);
void handle_game_over(void);

uint8_t currentLevel;
extern volatile uint8_t tone_playing;
extern volatile uint16_t tone_duration_ms;
bool isMuted = false;


/////////////////////////////// main //////////////////////////////////
int main(void)
{
	// Setup hardware and callbacks. This will turn on interrupts.
	initialise_hardware();

	// Show the start screen. Returns when the player starts the game.
	start_screen();

	// Loop forever and continuously play the game.
	while (1)
	{
		new_game();
		play_game();
		handle_game_over();
	}
}

void initialise_hardware(void)
{
	init_ledmatrix();
	init_buttons();
	init_serial_stdio(19200, false);
	init_timer0();
	init_timer1();
	init_timer2();
	init_adc(); // ADC input

	DDRD |= (1 << 4); // Piezo Buzzer
	
	DDRA |= 0xFC; // A2 - A7

	// Turn on global interrupts.
	sei();
}



void start_screen(void)
{
	// Set current level to the first level,
	currentLevel = 1;
	
	// Hide terminal cursor and set display mode to default.
	hide_cursor();
	normal_display_mode();

	// Clear terminal screen and output the title ASCII art.
	clear_terminal();
	display_terminal_title(3, 5);
	move_terminal_cursor(11, 5);
	// Change this to your name and student number. Remember to remove the
	// chevrons - "<" and ">"!
	printf_P(PSTR("CSSE2010/7201 Project by Johnathan Ngo - 48897668"));

	// Setup the start screen on the LED matrix.
	setup_start_screen();

	// Clear button presses registered as the result of powering on the
	// I/O board. This is just to work around a minor limitation of the
	// hardware, and is only done here to ensure that the start screen is
	// not skipped when you power cycle the I/O board.
	clear_button_presses();

	// Wait until a button is pushed, or 's'/'S' is entered.
	while (1)
	{
		// Check for button presses. If any button is pressed, exit
		// the start screen by breaking out of this infinite loop.
		if (button_pushed() != NO_BUTTON_PUSHED)
		{
			break;
		}

		// No button was pressed, check if we have terminal inputs.
		if (serial_input_available())
		{
			// Terminal input is available, get the character.
			int serial_input = fgetc(stdin);

			// If the input is 's'/'S', exit the start screen by
			// breaking out of this loop.
			if (serial_input == 's' || serial_input == 'S')
			{
				break;
			}
		}

		// No button presses and no 's'/'S' typed into the terminal,
		// we will loop back and do the checks again. We also update
		// the start screen animation on the LED matrix here.
		update_start_screen();
	}
}

void new_game(void)
{
	// Clear the serial terminal.
	hide_cursor();
	clear_terminal();

	// Initialise the game and display based on level.
	if (currentLevel == 1) {
		initialise_game();
	} else if (currentLevel == 2) {
		initialise_levelTwo();
	}
	
	
	// Reset tone variables and stop PWM
	tone_playing = 0;
	tone_duration_ms = 0;
	TCCR1A &= ~(1 << COM1B1); // Ensure PWM is stopped
	
	seconds_elapsed = 0;
	
	// Clear all button presses and serial inputs, so that potentially
	// buffered inputs aren't going to make it to the new game.
	clear_button_presses();
	clear_serial_input_buffer();
		
	start_printing_time();
}


void play_game(void)
{
	uint32_t last_flash_time = get_current_time();
	uint32_t last_target_flash_time = get_current_time();
	uint32_t pause_start_time = 0;
	uint32_t last_box_flash_time = 0;
	uint32_t last_joystick_check_time = 0;
	uint32_t joystick_check_delay = 0;
	
	extern bool box_on_target_flash;
	extern uint32_t box_flash_start_time;
	extern uint8_t box_flash_row, box_flash_col;
	
	
	bool isPaused = false;
	// We play the game until it's over.
	while (!is_game_over())
	{
		uint32_t joystick_start_time = get_current_time();

		// Check if it's time to read the joystick
		if (joystick_start_time >= last_joystick_check_time + joystick_check_delay) {
			int8_t delta_row = 0, delta_col = 0;
			read_joystick_direction(&delta_row, &delta_col);

			if (delta_row != 0 || delta_col != 0) {
				if (abs(delta_row) == 1 && abs(delta_col) == 1) {
					// Diagonal movement
					if (can_move_diagonally(delta_row, delta_col)) {
						move_player(delta_row, delta_col);
						step_count += 1; // Count as two steps
						display_step_count();
					}
				} else {
					// Normal movement
					move_player(delta_row, delta_col);
				}
				last_flash_time = joystick_start_time;
				joystick_check_delay = 200; // Set delay to 200ms if tilted
			} else {
				joystick_check_delay = 0; // Set delay to 0ms if not tilted
			}

			last_joystick_check_time = joystick_start_time;
		}

		// We need to check if any buttons have been pushed, this will
		// be NO_BUTTON_PUSHED if no button has been pushed. If button
		// 0 has been pushed, we get BUTTON0_PUSHED, and likewise, if
		// button 1 has been pushed, we get BUTTON1_PUSHED, and so on.
		ButtonState btn = button_pushed();
		
		// Now, repeat for the other buttons, and combine with serial
		// inputs.
		
		// CHECK PAUSE
		if (serial_input_available()) {
			int serial_input = fgetc(stdin);
			
			if (serial_input == 'p' || serial_input == 'P') {
				isPaused = !isPaused;
				
				if (isPaused == true) {
					pause_start_time = get_current_time();
					move_terminal_cursor(9, 0);
					printf_P(PSTR("Game Paused."));
					TCCR0B = 0;
					while (isPaused) {
						if (serial_input_available()) {
							int serial_input = fgetc(stdin);
							
							
							
							if (serial_input == 'p' || serial_input == 'P') {
								isPaused = !isPaused;
								uint8_t previous_seconds = seconds_elapsed;
								init_timer0();
								move_terminal_cursor(9, 0);
								clear_to_end_of_line();
								uint32_t pause_duration = get_current_time() - pause_start_time;
								seconds_elapsed = previous_seconds;
								last_flash_time += pause_duration;
								clear_button_presses();
								clear_serial_input_buffer();
								continue;
								break;
							}
						}
					}
				} 
				
			} else if (serial_input == 'd' || serial_input == 'D') {
				move_player(0, 1);
				last_flash_time = get_current_time();
			} else if (serial_input == 's' || serial_input == 'S') {
				move_player(-1, 0);
				last_flash_time = get_current_time();
			} else if (serial_input == 'w' || serial_input == 'W') {
				move_player(1, 0);
				last_flash_time = get_current_time();
			} else if (serial_input == 'a' || serial_input == 'A') {
				move_player(0, -1);
				last_flash_time = get_current_time();
			} else if (serial_input == 'z' || serial_input == 'Z') {
				undo_move();
				last_flash_time = get_current_time();
			} else if (serial_input == 'y' || serial_input == 'Y') {
				redo_move();
				last_flash_time = get_current_time();
			} else if (serial_input == 'q' || serial_input == 'Q') {
				isMuted = !isMuted;
			}
		}
		
		
		if (btn == BUTTON0_PUSHED) {
			move_player(0, 1);
			last_flash_time = get_current_time();
		} else if (btn == BUTTON1_PUSHED) {
			move_player(-1, 0);
			last_flash_time = get_current_time();
		} else if (btn == BUTTON2_PUSHED) {
			move_player(1, 0);
			last_flash_time = get_current_time();
		} else if (btn == BUTTON3_PUSHED) {
			move_player(0, -1);
			last_flash_time = get_current_time();
		}
		
	
				
		uint32_t current_time = get_current_time();
		if (current_time >= last_flash_time + 200)
		{
			// 200ms (0.2 seconds) has passed since the last time
			// we flashed the player icon, flash it now.
			flash_player();

			// Update the most recent icon flash time.
			last_flash_time = current_time;
		}
		if (current_time >= last_target_flash_time + 500)
		{
			// 500ms (0.5 seconds) has passed since the last time
			// we flashed the target icon, flash it now.
			flash_target();

			// Update the most recent icon flash time.
			last_target_flash_time = current_time;
		}

		// Handle box on target flashing
		if (box_on_target_flash && current_time >= last_box_flash_time + 100)
		{
			flash_box_on_target(box_flash_row, box_flash_col);
			last_box_flash_time = current_time;
			
			// Stop flashing after 500ms
			if (current_time >= box_flash_start_time + 500)
			{
				repaint_squares_around_target(box_flash_row, box_flash_col);
				box_on_target_flash = false;
			}
		}
	}
	// We get here if the game is over.
}

void handle_game_over(void)
{
	
	stop_printing_time();
			
	int stepScore = ((200 - step_count) > 0) ? (200 - step_count) : 0;
	int timeScore = ((1200 - seconds_elapsed) > 0) ? (1200 - seconds_elapsed) : 0;
	
	int score = stepScore * 20 + timeScore;
	
	// get rid of time elapsed
	move_terminal_cursor(12, 0);
	clear_to_end_of_line();
	
	// display game over screen
	move_terminal_cursor(12, 10);
	printf("Time Taken: %lu seconds  ||  Steps: %d  ||  Score: %d", seconds_elapsed, step_count, score);

	move_terminal_cursor(14, 28);
	printf_P(PSTR("*** GAME OVER ***"));
	move_terminal_cursor(15, 2);
	printf_P(PSTR("Press 'n'/'N' for the next level, 'r'/'R' to restart, or 'e'/'E' to exit"));

	// Do nothing until a valid input is made.
	while (1)
	{
		// Get serial input. If no serial input is ready, serial_input
		// would be -1 (not a valid character).
		int serial_input = -1;
		if (serial_input_available())
		{
			serial_input = fgetc(stdin);
		}

		// Check serial input.
		if (toupper(serial_input) == 'R' || toupper(serial_input) == 'r')
		{
			new_game();
			break;
		}
		// Now check for other possible inputs.
		else if (toupper(serial_input) == 'E' || toupper(serial_input) == 'e')
		{
			start_screen();
			break;
		} else if (toupper(serial_input) == 'N' || toupper(serial_input) == 'n' || toupper(serial_input) == '2') {
			if ((currentLevel < 2) == true) {
				currentLevel++;
				new_game();
				break;
			}
			
			move_terminal_cursor(16, 8);
			printf_P(PSTR("No more levels!"));
		}
	}
}

