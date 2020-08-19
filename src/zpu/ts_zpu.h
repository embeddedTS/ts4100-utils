/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef __TS_ZPU_H__
#define __TS_ZPU_H__


/*
 * List of static locations in ZPU memory
 * The TS ZPU implementation foregoes some of the standard implementation details.
 * Rather than an IVT table or some of the "standard" peripherals on Phi layout,
 * we have placed our own peripheral layout with IRQs, a Timer, and GPIO access.
 */

/*
 * IRQ registers have a value written to them. In order to clear the IRQ in the
 * FPGA, the host CPU (not the ZPU) needs to do an I2C access to that address.
 * The IRQ is cleared automatically by the FPGA.
 *
 * Note that the FIFO uses IRQ0, and it is advised that customer applications
 * use IRQ1.
 */
#define IRQ0_REG  *(volatile unsigned long *)0x2030
#define IRQ1_REG  *(volatile unsigned long *)0x2034

/*
 * 32-bit free running timer that runs when the FPGA is unreset. Runs at FPGA
 * main clock, 63 MHz
 */
#define TIMER_REG *(volatile unsigned long *)0x2030

/*
 * Input, Output, and Output Enable registers.
 * These are 32-bit wide registers. Each bit position represents a single DIO
 * in contrast to the FPGA I2C address map which has each DIO in its own reg.
 * For example, bit 10 of O_REG1 controls the output state of DIO_5.
 * ((32 * 1) + 10) == 42 == DIO_5 in the FPGA GPIO map. All registers are mapped
 * the same way.
 * O_REG* is read/write, setting and reading back the output value.
 * I_REG* is read only, and reads the input value of each GPIO pin.
 * OE_REG* is read/write, setting a bit to a 1 sets that pin as an output.
 */
#define I_REG0		*(volatile unsigned long *)0x2000
#define I_REG1		*(volatile unsigned long *)0x2004
#define I_REG2		*(volatile unsigned long *)0x2008
#define OE_REG0		*(volatile unsigned long *)0x2010
#define OE_REG1		*(volatile unsigned long *)0x2014
#define OE_REG2		*(volatile unsigned long *)0x2018
#define O_REG0		*(volatile unsigned long *)0x2020
#define O_REG1		*(volatile unsigned long *)0x2024
#define O_REG2		*(volatile unsigned long *)0x2028

#endif // __TS_ZPU_H__
