/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdarg.h>
#include <string.h>

#include "muxbus.h"
#include "fifo.h"
#include "ts_zpu.h"
#include "ts8820.h"

/* Bit defines for IO used throughout the application */
#define RED_LED			0x10000000
#define GREEN_LED		0x08000000
#define RELAY1_REG1		0x00002000
#define RELAY_BTN_DIN		0x0400
#define ESTOP_BTN_DIN		0x2000
#define ESTOP_LED_DOUT		0x0002
#define MOTOR_MANUAL_SW_DIN	0x0800
#define MOTOR_FWD_SW_DIN	0x0001
#define MOTOR_REV_SW_DIN	0x0002

/* NOTE! These states are carefully balanced and the motor control code block
 * takes advantage of the ordering in order to reduce the number of conditional
 * code segments. Do not change any of these states without careful consideration!
 */
enum motor_states {
	MOTOR_MANUAL,
	MOTOR_FWD_RAMP_UP,
	MOTOR_FWD,
	MOTOR_FWD_RAMP_DOWN,
	MOTOR_REV_RAMP_UP,
	MOTOR_REV,
	MOTOR_REV_RAMP_DOWN,
	MOTOR_BRAKE,
};

/*********************************************************
* Look up table for a 10 kohm NTC B25/50=3950 thermistor
* Based on https://cdn-shop.adafruit.com/product-files/372/103_3950_lookuptable.txt
**********************************************************/
struct temp_lut {
	const unsigned int ohms;
	const unsigned short dac;
};

const struct temp_lut lut[] = {
	{ 116600,	0 }, // -25 C
	{ 110000,	27 },
	{ 103700,	55 },
	{ 97900,	82 },
	{ 92500,	109 },
	{ 87430,	137 },
	{ 82790,	164 },
	{ 78440,	191 },
	{ 74360,	218 },
	{ 70530,	246 },
	{ 66920,	273 },
	{ 63540,	300 },
	{ 60340,	328 },
	{ 57330,	355 },
	{ 54500,	382 },
	{ 51820,	410 },
	{ 49280,	437 },
	{ 46890,	464 },
	{ 44620,	491 },
	{ 42480,	519 },
	{ 40450,	546 },
	{ 38530,	573 },
	{ 36700,	601 },
	{ 34970,	628 },
	{ 33330,	655 },
	{ 31770,	683 },
	{ 30250,	710 },
	{ 28820,	737 },
	{ 27450,	764 },
	{ 26160,	792 },
	{ 24940,	819 },
	{ 23770,	846 },
	{ 22670,	874 },
	{ 21620,	901 },
	{ 20630,	928 },
	{ 19680,	956 },
	{ 18780,	983 },
	{ 17930,	1010 },
	{ 17120,	1037 },
	{ 16350,	1065 },
	{ 15620,	1092 },
	{ 14930,	1119 },
	{ 14260,	1147 },
	{ 13630,	1174 },
	{ 13040,	1201 },
	{ 12470,	1229 },
	{ 11920,	1256 },
	{ 11410,	1283 },
	{ 10910,	1310 },
	{ 10450,	1338 },
	{ 10000,	1365 },
	{ 9575,		1392 },
	{ 9170,		1420 },
	{ 8784,		1447 },
	{ 8416,		1474 },
	{ 8064,		1502 },
	{ 7730,		1529 },
	{ 7410,		1556 },
	{ 7106,		1583 },
	{ 6815,		1611 },
	{ 6538,		1638 },
	{ 6273,		1665 },
	{ 6020,		1693 },
	{ 5778,		1720 },
	{ 5548,		1747 },
	{ 5327,		1775 },
	{ 5117,		1802 },
	{ 4915,		1829 },
	{ 4723,		1856 },
	{ 4539,		1884 },
	{ 4363,		1911 },
	{ 4195,		1938 },
	{ 4034,		1966 },
	{ 3880,		1993 },
	{ 3733,		2020 },
	{ 3592,		2048 },
	{ 3457,		2075 },
	{ 3328,		2102 },
	{ 3204,		2129 },
	{ 3086,		2157 },
	{ 2972,		2184 },
	{ 2863,		2211 },
	{ 2759,		2239 },
	{ 2659,		2266 },
	{ 2564,		2293 },
	{ 2472,		2321 },
	{ 2384,		2348 },
	{ 2299,		2375 },
	{ 2218,		2402 },
	{ 2141,		2430 },
	{ 2066,		2457 },
	{ 1994,		2484 },
	{ 1926,		2513 },
	{ 1860,		2539 },
	{ 1796,		2566 },
	{ 1735,		2594 },
	{ 1677,		2621 },
	{ 1621,		2648 },
	{ 1567,		2675 },
	{ 1515,		2703 },
	{ 1465,		2730 },
	{ 1417,		2758 },
	{ 1371,		2785 },
	{ 1326,		2812 },
	{ 1284,		2839 },
	{ 1243,		2867 },
	{ 1203,		2894 },
	{ 1165,		2921 },
	{ 1128,		2948 },
	{ 1093,		2976 },
	{ 1059,		3003 },
	{ 1027,		3030 },
	{ 996,		3058 },
	{ 965,		3085 },
	{ 936,		3112 },
	{ 908,		3140 },
	{ 881,		3167 },
	{ 855,		3194 },
	{ 830,		3221 },
	{ 805,		3249 },
	{ 782,		3276 },
	{ 759,		3303 },
	{ 737,		3331 },
	{ 715,		3358 },
	{ 695,		3385 },
	{ 674,		3413 },
	{ 656,		3440 },
	{ 638,		3467 },
	{ 620,		3494 },
	{ 603,		3522 },
	{ 586,		3549 },
	{ 569,		3576 },
	{ 554,		3604 },
	{ 538,		3631 },
	{ 523,		3658 },
	{ 508,		3686 },
	{ 494,		3713 },
	{ 480,		3740 },
	{ 467,		3767 },
	{ 454,		3795 },
	{ 441,		3822 },
	{ 429,		3850 },
	{ 417,		3877 },
	{ 406,		3904 },
	{ 394,		3931 },
	{ 384,		3959 },
	{ 373,		3986 },
	{ 363,		4013 },
	{ 353,		4040 },
	{ 343,		4068 },
	{ 334,		4095 }, // +125 C
	{ 0, 		4095 },
	{ },
};

#define LUT_LEN (sizeof(lut)/sizeof(lut[0]))

/* Helper functions for setting bits in registers */
static void bit_clear(volatile unsigned long *adr, int bit)
{
	*adr &= ~(bit);
}

static void bit_set(volatile unsigned long *adr, int bit)
{
	*adr |= bit;
}

static void bit_toggle(volatile unsigned long *adr, int bit)
{
	*adr ^= bit;
}

#define dir_out(adr, bit) bit_set(adr, bit);
#define dir_in(adr, bit) bit_clear(adr, bit);
#define led_on(bit) bit_clear(O_REG0_ADR, bit);
#define led_off(bit) bit_set(O_REG0_ADR, bit);
#define led_toggle(bit) bit_toggle(O_REG0_ADR, bit);

/* Variables touched by the demo_init and main funcs are global for ease */
unsigned short hbridge1;
int estopped;
enum motor_states motor_state;
enum motor_states motor_state_next;
unsigned short DIN, DOUT;
unsigned short adc_ctrl;
unsigned short adc_dac;
unsigned int cnt;

static void demo_init(void)
{
	/* Reset E-Stop status */
	estopped = 0;

	/* Put motor back in BRAKE state */
	motor_state = MOTOR_BRAKE;
	motor_state_next = MOTOR_BRAKE;

	/* Set up FPGA IO */
	bit_clear(O_REG1_ADR, RELAY1_REG1);
	dir_out(OE_REG1_ADR, RELAY1_REG1);

	/* Set up H Bridge settings */
	// Set Hz, 0% duty
	hbridge1 = 0;
	muxbus_write_16(REG_PWM7, 0);
	// Enable HB1 output, direction doesn't matter at this point
	muxbus_write_16(REG_PU_HB, muxbus_read_16(REG_PU_HB) | HB_1_EN);

	/* Init TS-8820 ADC settings */
	/* Note! The CPU DIO pin 3 26 needs to be set to a low output for ADC
	 * reads to function correclty. This is part of the oversample value.
	 */
	adc_ctrl = 0;
	/* Select channels 0 and 1 of both chips */
	adc_ctrl = (3 << ADC_CHAN_offs);
	adc_ctrl |= (1 << ADC_CHIP_offs);
	adc_ctrl |= (ADC_RESET);
	muxbus_write_16(REG_ADC, adc_ctrl);

	/* Set up pullup on ADCs 1-2 */
	muxbus_write_16(REG_PU_HB, (muxbus_read_16(REG_PU_HB) | PU_12_EN));

	/* Set all DOUT low */
	DOUT = 0;

	/* Set green LED on, red off */
	led_on(GREEN_LED);
	led_off(RED_LED);

	/* Counter for heartbeat */
	cnt = 0;

	/* DAC1 output max for ADC1 pot reference */
	muxbus_write_16(REG_DAC1, 0x8FFF);
	delay_clks(1);

	/* Set up DOUT1 in PWM mode with no output */
	muxbus_write_16(REG_PWM1, 0);
	DOUT |= (1 << DOUT_PWM_EN_offs);
	muxbus_write_16(REG_DOUT, DOUT);
}

/* Rudimentary binary search to match nearest calculated resistance to
 * what its DAC output should be on the analog meter.
 */
static int res_to_dac_lookup(int low, int high, signed int ohms) {
	int middle;
	while (low <= high) {
		middle = low + ((high - low) / 2);

		if (lut[middle].ohms > ohms) low = middle + 1;
		else high = middle - 1;
	}

	/* Use the smaller of the values to not have overshoot */
	return high;
}

/*
 * This application is intended to be simple, just a large loop that runs
 * through it's IO every loop.
 *
 * Each loop:
 * Checks the E-Stop switch, if pressed all output goes to a known state and no
 *   further actions are performed until the E-Stop is released.
 * Reads a button, and sets the relay state based on that button.
 * Reads a thermistor, calculates its resistance, and outputs a PWM pattern
 *   that can directly drive an analog voltmeter modified to show degrees C
 * Reads a potentiometer and outputs a voltage on a DAC channel to mirror the input
 * Using a combination of switches and potentiometers, drive a DC motor using the
 *   H-Bridge driven by a PWM signal. This includes ramping the motor, and manual
 *   control using the potentiometer.
 */
int main(int argc, char **argv)
{
	unsigned int relay_last = 0;
	unsigned int relay_btn_now;
	unsigned long o_reg;
	unsigned short adc_sam;
	signed int vout;
	signed int ohms;
	int temperature;
	unsigned int tmp;

	fifo_init();
	initmuxbusio();
	demo_init();


	while(1) {
		/* Heartbeat/loop counter */
		cnt++;

		/**********************************************************
		 *
		 * Output on FIFO if request to do so
		 *
		 *********************************************************/
		/* To reduce overall memory accesses when unneeded, only send
		 * current states upon request.
		 */
		if (getc() != -1) {
			putc_noirq(estopped);
			putc_noirq(relay_btn_now);
			putc_noirq((adc_dac * 100)/0xFFF);
			putc_noirq(temperature);
			putc_noirq(((lut[temperature].dac) * 100)/0xFFF);
			putc_noirq(motor_state);
			putc_noirq(!!(muxbus_read_16(REG_PU_HB) & HB_1_DIR));
			putc((hbridge1 * 100)/0x1100);
		}

		/* Get state of inputs */
		DIN = muxbus_read_16(REG_DIN);

		/* Check E-Stop button */
		if (DIN & ESTOP_BTN_DIN) {
			estopped = 1;

			/* Set all systems to a safe mode-set */
			/* Disable almost all DOUT pins, this also disables PWM */
			DOUT = 0;
			/* Blink the E-Stop LED still, however */
			if (cnt & 0x100) DOUT |= ESTOP_LED_DOUT;
			muxbus_write_16(REG_DOUT, DOUT);

			/* Brake HBridge 1 */
			hbridge1 &= ~(0x1FFF);
			muxbus_write_16(REG_PWM7, hbridge1);

			/* Disable DAC 1&2 output */
			muxbus_write_16(REG_DAC2, 0x0000);
			muxbus_write_16(REG_DAC1, 0x8000);
			delay_clks(1);

			/* Disable relay 1 */
			bit_clear(O_REG1_ADR, RELAY1_REG1);

			/* Clear the ADC input value */
			adc_dac = 0;

			/* There is nothing really worth doing with the ADC,
			 * just let it time-out and reset itself */

			/* Turn off green LED, assert red LED */
			led_off(GREEN_LED);
			led_on(RED_LED);

			/* Do not process any further events until E-Stop is
			 * released */
			continue;
		}
		/* If E-Stop was released, do a clean restart of the TS-8820
		 * IO */
		if (estopped) demo_init();

		/* Start ADC sample */
		/* Take out of reset, and then start sampling */
		adc_ctrl &= ~(ADC_RESET);
		muxbus_write_16(REG_ADC, adc_ctrl);
		adc_ctrl |= ADC_RUN;
		muxbus_write_16(REG_ADC, adc_ctrl);

		/**********************************************************
		 *
		 * Button -> Relay 1 mirror
		 *
		 *********************************************************/
		relay_btn_now = !!(DIN & RELAY_BTN_DIN);
		/* Only do a write of the relay IO if we need to */
		if (relay_btn_now != relay_last) {
			o_reg = O_REG1;
			o_reg &= ~(0x00002000); // Clear FPGA output
			o_reg |= ((relay_btn_now) & 0x1) << 13; // Set new relay state
			O_REG1 = o_reg;
			relay_last = relay_btn_now;
		}

		/**********************************************************
		 *
		 * Thermistor -> PWM
		 *
		 *********************************************************/
		/* Thermistor is read first */
		adc_sam = muxbus_read_16(REG_ADC_RD);

		/* Current probe used is a 10K NTC. ADC setup enables 6.04k
		 * ohm resistor pullup to 12.5 V. Temp range is -25~+125 C.
		 *
		 * The analog dial is driven with a 5V PWM to vary from 0 to
		 * 100 % duty cycle. This spans the full range of the dial.
		 */
		vout = (((signed int)adc_sam * 10000) / 32768);
		ohms = (6050*((vout*1000)/(12500-vout)))/1000;
		temperature = res_to_dac_lookup(0, LUT_LEN, ohms);
		muxbus_write_16(REG_PWM1, lut[temperature].dac | 0xe000);

		/**********************************************************
		 *
		 * ADC -> DAC voltage mirror
		 *
		 *********************************************************/
		/* 2nd ADC read, channel 9 (channel 0 of chip 2), mirrored
		 * voltage on DAC output 2. DAC output 1 is our reference
		 * voltage.
		 */
		adc_sam = muxbus_read_16(REG_ADC_RD);

		/* If sign bit is set, then that is likely a DAC->ADC error,
		 * we will never go below 0 V with this setup. Set the output
		 * to zero in this case.
		 */
		if (adc_sam & 0x8000) adc_sam = 0;

		/* Write DAC2 output from ADC 1 */
		/* ADC is 16 bit signed (effectivly 15 bit when sign bit is
		 * ignored) so shift that to the 12 bits of DAC output. */
		adc_sam >>= 3;
		/* Set up control register to write output */
		adc_sam &= ~(0xF000);
		adc_dac = adc_sam;
		muxbus_write_16(REG_DAC2, adc_dac | 0x8000);
		delay_clks(1);


		/**********************************************************
		 *
		 * Motor control
		 *
		 *********************************************************/

		/* Get current input states to determine if we need to start
		 * moving to a new motor state.
		 *
		 * The motor related input switches are such that FWD and REV
		 * of automatic modes are mutually exclusive, and the MANUAL
		 * SW would override automatic modes.
		 */
		if (DIN & MOTOR_FWD_SW_DIN) motor_state_next = MOTOR_FWD;
		else if (DIN & MOTOR_REV_SW_DIN) motor_state_next = MOTOR_REV;
		else motor_state_next = MOTOR_BRAKE;
		if (DIN & MOTOR_MANUAL_SW_DIN) motor_state_next = MOTOR_MANUAL;

		/* Despite what the next state is, there is a very specific
		 * state transition that needs to happen.
		 *
		 * At a high level, we want the motor to only change directions
		 * once completely stopped. Transitions to/from manual also need
		 * to happen when the motor is stopped. This could still lead to
		 * a bit of a jerk if the manual control has the motor pegged one
		 * direction or the other, but that is just how manual is intended
		 * to function.
		 *
		 * MOTOR_FWD and MOTOR_REV can only be reached from MOTOR_FWD_RAMP
		 * and MOTOR_REV_RAMP. MOTOR_*_RAMP states can only be reached from
		 * MOTOR_BRAKE.
		 * A transition to/from MANUAL to MOTOR_FWD or MOTOR_REV needs to
		 * go through RAMP down states, BRAKE, and then RAMP up.
		 */

		/* Operational notes purely for the demo:
		 *
		 * In automatic mode, the PWM output is started at a value of 0x200.
		 * This is to get the selected motor visually spinning sooner as
		 * values from zero to ~0x200 do not generate enough movement
		 * to visibly see it on the planetary gear output. In turn, it looks
		 * as though the motor takes some number of moments to turn once
		 * the switch was flipped. The same is done on the ramp down, any
		 * value less than 0x200 simply stops the motor.
		 *
		 * There is a similar phenomena when the PWM value is above ~0x1100,
		 * changes in rotation speed on the high end are indiscernable from
		 * that point until the max PWM value 0x1fff. This has an effect
		 * of the motor still appearing to spin at max speed when it should
		 * be starting its ramp down.
		 */

		switch (motor_state) {
		/* From this state, it is safe to move to most other states */
		case MOTOR_BRAKE:
			hbridge1 = 0;
			switch (motor_state_next) {
			case MOTOR_FWD:
				motor_state = MOTOR_FWD_RAMP_UP;
				muxbus_write_16(REG_PU_HB, muxbus_read_16(REG_PU_HB) & ~HB_1_DIR);
				hbridge1 = 0x200;
				break;
			case MOTOR_REV:
				motor_state = MOTOR_REV_RAMP_UP;
				muxbus_write_16(REG_PU_HB, muxbus_read_16(REG_PU_HB) | HB_1_DIR);
				hbridge1 = 0x200;
				break;
			/* This should only ever be BRAKE or MANUAL */
			default:
				motor_state = motor_state_next;
				break;
			}
			break;
		case MOTOR_FWD_RAMP_UP:
		case MOTOR_REV_RAMP_UP:
			/* This is a bit clever, here is an explanation:
			 *
			 * state_next will only ever be whole states, that is
			 * MANUAL, BRAKE, FWD, REV. The RAMP states are "half"
			 * states with UP and DOWN preceding and following each
			 * FWD and REV state,
			 *
			 * Because of that stepping, and the fact that we are now
			 * in a half state that precedes its full state, if the
			 * next state is not the following whole state, then we
			 * need to move to the ramp down state, which is another
			 * half state that is +2 from the current state.
			 */
			if (motor_state_next != (motor_state+1)) {
				motor_state += 2;
				break;
			}

			/* Move to the next state once at full speed */
			if (hbridge1 >= 0x1100) {
				motor_state++;
				break;
			}

			/* Continue to ramp up */
			hbridge1 += 2;
			break;
		/* These states just spin forever until a new next state */
		case MOTOR_FWD:
		case MOTOR_REV:
			if (motor_state != motor_state_next) {
				/* Move to the RAMP_DOWN half state */
				motor_state++;
			}
			break;
		case MOTOR_FWD_RAMP_DOWN:
		case MOTOR_REV_RAMP_DOWN:
			/* Similar to the RAMP_UP states but a bit different,
			 * if motor_next_state equals the previous whole step
			 * we need to move to that state's ramp up. Any other
			 * state differences and we need to loop here until
			 * BRAKE before moving to those states.
			 *
			 * For example, if a user switches from FWD to BRAKE,
			 * we will be ramping down FWD. If a user switches back
			 * to FWD, we can safely ramp back up.
			 * However if the user moves to REV or MANUAL, we need
			 * to continue to BRAKE and transition there.
			 */
			if (motor_state_next == (motor_state-1)) {
				motor_state -= 2;
				break;
			}

			if (hbridge1 > 0x200) {
				/* Ramp down the motor a little bit each loop */
				hbridge1 -= 2;
				break;
			}
			hbridge1 = 0;
			motor_state = MOTOR_BRAKE;
			break;
		case MOTOR_MANUAL:
			/* The manual motor modes have some interesting math due
			 * to the ADC pullup and linear errors in the potentiometer
			 * itself.
			 *
			 * The max voltage with the ADC pullup as well as a
			 * potentiometer ends up being about 8 VDC. Additionally,
			 * testing puts the center of the potentiometer near 6 VDC.
			 * In order to cleanly have the CW and CCW extremes drive
			 * the motor at its max, with no rotation in its middle,
			 * The range is split in to both of these sides and each
			 * side is scaled from no rotation to max rotation.
			 *
			 * A fix for this would be a "calibration" process, however,
			 * due to the ZPU not having non-volatile memory it would
			 * need to be repeated every power-on which is not ideal
			 * for a traveling demo. Worst case, center is a little off
			 * while on the show floor.
			 */
			adc_sam = muxbus_read_16(REG_ADC_RD);
			if (adc_sam & 0x8000) adc_sam = 0;
			if (adc_sam > 0x4d39) {
				muxbus_write_16(REG_PU_HB, muxbus_read_16(REG_PU_HB) & ~HB_1_DIR);
				adc_sam -= 0x4d3a;
				tmp = ((int)adc_sam * 0x1100) / 0x18e0;
				if (tmp > 0x1100) tmp = 0x1100;
				hbridge1 = tmp;
			} else {
				muxbus_write_16(REG_PU_HB, muxbus_read_16(REG_PU_HB) | HB_1_DIR);
				adc_sam = (0x4d3a - adc_sam);
				tmp = ((int)adc_sam * 0x1100) / 0x4d3a;
				if (tmp > 0x1100) tmp = 0x1100;
				hbridge1 = tmp;
			}

			/* The next state depends on the current rotation but
			 * we want to enter a ramp down state first.
			 */
			if (motor_state_next != motor_state) {
				if (muxbus_read_16(REG_PU_HB) & HB_1_DIR) {
					motor_state = MOTOR_REV_RAMP_DOWN;
				} else {
					motor_state = MOTOR_FWD_RAMP_DOWN;
				}
			}
			break;
		/* We should never be in this state, immediately brake if so */
		default:
			hbridge1 = 0;
			motor_state = MOTOR_BRAKE;
			break;
		}

		/* Finally, write the PWM value to the H-Bridge */
		muxbus_write_16(REG_PWM7, hbridge1);

		/**********************************************************
		 *
		 * Clean up each loop
		 *
		 *********************************************************/

		/* Reset ADC core */
		adc_ctrl &= ~(ADC_RUN);
		adc_ctrl |= ADC_RESET;
		muxbus_write_16(REG_ADC, adc_ctrl);

		/* Running heartbeat on green LED */
		if (cnt & 0x100) {
			led_off(GREEN_LED);
		} else {
			led_on(GREEN_LED);
		}
	}

	return 0;
}
