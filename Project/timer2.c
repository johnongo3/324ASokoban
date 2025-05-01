/*
 * timer2.c
 *
 * Author: Peter Sutton
 */

#include "timer2.h"
#include <avr/io.h>
#include <avr/interrupt.h>

extern volatile uint8_t step_count; // Declare step_count as extern

volatile uint8_t digits_displayed = 1; // Enable digit display
volatile uint16_t count = 0; // Step count
volatile uint8_t seven_seg_cc = 0;
uint8_t seven_seg_data[10] = {63,6,91,79,102,109,125,7,127,111};

void init_timer2(void)
{  
	// Set PORTC and PORTD as output for the seven-segment display
	TCCR2A = 0; // CTC mode
	TCCR2B = (1 << CS22) | (1 << CS21) | (1 << CS20);
	OCR2A = 999; // Set compare value for desired interrupt frequency
	TIMSK2 = (1 << OCIE2A); // Enable Timer 2 compare interrupt
	TCNT2 = 0; // Initialize counter
}

// Ensure ISR is declared outside of any function
ISR(TIMER2_COMPA_vect) {
	// Toggle between displaying the ones and tens digit
	seven_seg_cc = 1 ^ seven_seg_cc;
	
	// Clear PORTC before setting new value
	PORTC = 0;

	if (digits_displayed) {
		if (seven_seg_cc == 0) {
			// Display rightmost digit - ones place
			PORTC = seven_seg_data[step_count % 10];
			// Activate the ones digit (assuming PORTD bit 2 controls digit selection)
			PORTD &= ~(1 << 2); // Clear bit to select ones digit
		} else {
			// Display leftmost digit - tens place
			PORTC = seven_seg_data[(step_count / 10) % 10];
			// Activate the tens digit
			PORTD |= (1 << 2); // Set bit to select tens digit
		}
	}
}
