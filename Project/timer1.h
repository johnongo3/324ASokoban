/*
 * timer1.h
 *
 * Author: Peter Sutton
 *
 * Timer 1 skeleton.
 */

#ifndef TIMER1_H_
#define TIMER1_H_

#include <stdint.h>

/// <summary>
/// Skeletal timer 1 initialisation function.
/// </summary>
void init_timer1(void);

uint16_t freq_to_clock_period(uint16_t freq);

uint16_t duty_cycle_to_pulse_width(float dutycycle, uint16_t clockperiod);

void play_tone(uint16_t frequency, uint16_t duration_ms);

#endif /* TIMER1_H_ */
