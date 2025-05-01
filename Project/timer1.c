/*
 * timer1.c
 *
 * Author: Peter Sutton
 */

#include "timer1.h"
#include "timer0.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <stdbool.h>

// For a given frequency (Hz), return the clock period (in terms of the
// number of clock cycles of a 1MHz clock)
uint16_t freq_to_clock_period(uint16_t freq) {
	return (1000000UL / freq);	// UL makes the constant an unsigned long (32 bits)
	// and ensures we do 32 bit arithmetic, not 16
}

// Return the width of a pulse (in clock cycles) given a duty cycle (%) and
// the period of the clock (measured in clock cycles)
uint16_t duty_cycle_to_pulse_width(float dutycycle, uint16_t clockperiod) {
	return (dutycycle * clockperiod) / 100;
}

extern volatile uint8_t tone_playing;
extern volatile uint16_t tone_duration_ms;
extern bool isMuted;

void init_timer1(void)
{
    // Set up timer/counter 1 for Fast PWM, counting from 0 to the value in OCR1A
    // Configure output OC1B to be clear on compare match and set on timer/counter overflow (non-inverting mode)
	TCCR1A = (1 << COM1B1) | (1 <<WGM11) | (1 << WGM10);
	TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11);
}

void play_tone(uint16_t frequency, uint16_t duration_ms) {
	
	if (isMuted) {
		return;
	}
	
	// Calculate the clock period and pulse width for the given frequency and duty cycle
	uint16_t clockperiod = freq_to_clock_period(frequency);
	uint16_t pulsewidth = duty_cycle_to_pulse_width(50.0, clockperiod); // 50% duty cycle

	// Set the maximum count value for timer/counter 1 to be one less than the clockperiod
	OCR1A = clockperiod - 1;

	// Set the count compare value based on the pulse width
	OCR1B = (pulsewidth > 0) ? (pulsewidth - 1) : 0;

	// Make pin OC1B (PD4) an output
	DDRD |= (1 << 4);
	
	// Update the PWM registers
	if(pulsewidth > 0) {
		// The compare value is one less than the number of clock cycles in the pulse width
		OCR1B = pulsewidth - 1;
		} else {
		OCR1B = 0;
	}
	// Note that a compare value of 0 results in special behaviour - see page 130 of the
	// datasheet (2018 version)
		
	// Set the maximum count value for timer/counter 1 to be one less than the clockperiod
	OCR1A = clockperiod - 1;
	
	init_timer1();
	tone_duration_ms = duration_ms;
	tone_playing = 1;
}

ISR(TIMER1_COMPA_vect)
{
	
}
