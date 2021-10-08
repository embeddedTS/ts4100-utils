/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef __TS_8820_H__
#define __TS_8820_H__

/* Register and bitmask mappings of TS-8820 FPGA */
#define REG_ID			0x00

#define REG_PU_HB		0x02
#define PU_58_EN		0x0400
#define PU_34_EN		0x0200
#define PU_12_EN		0x0100
#define HB_2_EN			0x0080
#define HB_1_EN			0x0040
#define HB_2_DIR		0x0020
#define HB_1_DIR		0x0010
#define REV_mask		0x000F
#define REV_offs		0

#define REG_DIN			0x04
#define DIN_mask		0x3FFF

#define REG_SRAM		0x06

#define REG_DOUT		0x08
#define DOUT_mask		0x003F
#define DOUT_offs		0
#define DOUT_PWM_EN_offs	6
#define DOUT_PWM_EN_mask	0x0FC0

#define REG_PWM1		0x10
#define REG_PWM2		0x12
#define REG_PWM3		0x14
#define REG_PWM4		0x16
#define REG_PWM5		0x18
#define REG_PWM6		0x1A
#define REG_PWM7		0x1C
#define REG_PWM8		0x1E
#define PWM_DUTY_mask		0x01FF
#define PWM_DUTY_offs		0
#define PWM_PRE_mask		0xE000
#define PWM_PRE_offs		13

#define REG_PULSE1		0x20
#define REG_PULSE2		0x22
#define REG_PULSE3		0x24
#define REG_PULSE4		0x26
#define REG_PULSE5		0x28
#define REG_PULSE6		0x2A
#define REG_PULSE7		0x2C
#define REG_PULSE8		0x2E
#define REG_PULSE9		0x30
#define REG_PULSE10		0x32
#define REG_PULSE11		0x34
#define REG_PULSE12		0x36
#define REG_PULSE13		0x38
#define REG_PULSE14		0x3A

#define REG_ADC_ID		0x80
#define REG_ADC			0x82
#define ADC_CHAN_offs		8
#define ADC_CHAN_mask		0xFF00
#define ADC_CHIP_offs		6
#define ADC_CHIP_mask		0x00C0
#define ADC_FORCE_STBY		0x0020
#define ADC_STBY		0x0010
#define ADC_DMA_IRQ		0x0008
#define ADC_IRQ_EN		0x0004
#define ADC_RUN			0x0002
#define ADC_RESET		0x0001
#define REG_ADC_FIFO		0x84
#define REG_ADC_RD		0x86
#define REG_ADC_PERIOD_LSB	0x88
#define REG_ADC_PERIOD_MSB	0x8A

#define REG_DAC1		0xA0
#define REG_DAC2		0xA2
#define REG_DAC3		0xA4
#define REG_DAC4		0xA6
#endif // __TS_8820_H__
