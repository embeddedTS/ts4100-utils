/*  Copyright 2011, Unpublished Work of Technologic Systems
 *  All Rights Reserved.
 *
 *  THIS WORK IS AN UNPUBLISHED WORK AND CONTAINS CONFIDENTIAL,
 *  PROPRIETARY AND TRADE SECRET INFORMATION OF TECHNOLOGIC SYSTEMS.
 *  ACCESS TO THIS WORK IS RESTRICTED TO (I) TECHNOLOGIC SYSTEMS EMPLOYEES
 *  WHO HAVE A NEED TO KNOW TO PERFORM TASKS WITHIN THE SCOPE OF THEIR
 *  ASSIGNMENTS  AND (II) ENTITIES OTHER THAN TECHNOLOGIC SYSTEMS WHO
 *  HAVE ENTERED INTO  APPROPRIATE LICENSE AGREEMENTS.  NO PART OF THIS
 *  WORK MAY BE USED, PRACTICED, PERFORMED, COPIED, DISTRIBUTED, REVISED,
 *  MODIFIED, TRANSLATED, ABRIDGED, CONDENSED, EXPANDED, COLLECTED,
 *  COMPILED,LINKED,RECAST, TRANSFORMED, ADAPTED IN ANY FORM OR BY ANY
 *  MEANS,MANUAL, MECHANICAL, CHEMICAL, ELECTRICAL, ELECTRONIC, OPTICAL,
 *  BIOLOGICAL, OR OTHERWISE WITHOUT THE PRIOR WRITTEN PERMISSION AND
 *  CONSENT OF TECHNOLOGIC SYSTEMS . ANY USE OR EXPLOITATION OF THIS WORK
 *  WITHOUT THE PRIOR WRITTEN CONSENT OF TECHNOLOGIC SYSTEMS  COULD
 *  SUBJECT THE PERPETRATOR TO CRIMINAL AND CIVIL LIABILITY.
 */

/* ts8820_x functions provide access to functionality on the TS-8820.
 * These are directly portable to any module that has the MUXBUS directly
 * memory mapped.
 */

/* Call this function once before using the other ts8820 functions.
 * See ts8820ctl.c for an example of usage.
 */
int ts8820_init(int twifd);

/* int ts8820_adc_acq(int hz, int n, unsigned short mask)
 * Samples ADCs n times at hz Hz and sends raw data to stdout.  Only channels
 * active in the mask are sampled.  The ordering of data words in the output
 * is not as expected.  See ts8820.c for more detail.
 */
int ts8820_adc_acq(int, int, unsigned short);

/* int ts8820_adc_sam(int hz, int n, int range_in)
 * Prints n rows of human readable data on all channels to stdout, sampled 
 * at hz Hz.
 */
int ts8820_adc_sam(int hz, int n, int range_in);

/* ts8820_dac_set(int dac, int mv)
 * mv is the DAC setting in millivolts, 0 to 10000.
 * dac is the channel, 1 to 4.
 */
void ts8820_dac_set(int, int);

/* ts8820_do_set(unsigned int lval)
 * bits 5:0 of lval are set as digital output values.  For pins where PWMs are
 * in use, the values have no meaning until the PWM is disabled.
 */
void ts8820_do_set(unsigned int);

/* returns a 14 bit value representing current levels on the digital inputs.
 */
unsigned int ts8820_di_get(void);

/* ts8820_pwm_disable(int n)
 * Disables the PWM on pin n (1-6) allowing the digital output setting to 
 * control the pin.
 */
void ts8820_pwm_disable(int);

/* ts8820_pwm_set(int n, int prescalar, int val)
 * Enables PWM on digital output n (1-6).  val (0-0x1000) is the duty cycle.
 * The PWM frequency is approximately 12207/(2^prescalar) where prescalar
 * ranges from 0 to 7.
 */
void ts8820_pwm_set(int, int, int);

/* ts8820_hb_set(int n, int dir)
 * Enables H-bridge n (1 or 2) running in direction dir. (1=forward, 0=back)
 */
void ts8820_hb_set(int, int);

/* ts8820_hb_disable(int n) disables H-bridge n.
 */
void ts8820_hb_disable(int);

/* ts8820_counter(int n) returns the counter value for digital input n.
 */
int ts8820_counter(int);

/* ts8820_sram_write(int bytes) reads the specified number of bytes from 
 * stdin and writes them to the static RAM starting at offset 0.
 */
void ts8820_sram_write(int);

/* ts8820_sram_read(int bytes) reads the specified number of bytes from the
 * static RAM and writes them to stdout.
 */
void ts8820_sram_read(int);

/* ts8820_read/write
 * Do an arbitrary single register read or write of TS-8820 FPGA space
 */
unsigned short ts8820_read(unsigned short adr);
void ts8820_write(unsigned short adr, unsigned short val);
