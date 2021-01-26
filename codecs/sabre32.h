#ifndef _SABRE32_H
#define _SABRE32_H

#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/of_device.h>
#include <sound/soc.h>
#include <linux/mutex.h>

/* ES9018 register space */

#define SABRE32_VOLUME0					0x00
#define SABRE32_VOLUME1					0x01
#define SABRE32_VOLUME2					0x02
#define SABRE32_VOLUME3					0x03
#define SABRE32_VOLUME4					0x04
#define SABRE32_VOLUME5					0x05
#define SABRE32_VOLUME6					0x06
#define SABRE32_VOLUME7					0x07
#define SABRE32_AUTOMUTE_LEV				0x08
#define SABRE32_AUTOMUTE_TIME				0x09
#define SABRE32_MODE_CONTROL1				0x0A
#define SABRE32_MODE_CONTROL2				0x0B
#define SABRE32_MODE_CONTROL3				0x0C
#define SABRE32_DAC_POLARITY				0x0D
#define SABRE32_DAC_SOURCE				0x0E
#define SABRE32_MODE_CONTROL4				0x0F
#define SABRE32_AUTOMUTE_LOOPBACK			0x10
#define SABRE32_MODE_CONTROL5				0x11
#define SABRE32_SPDIF_SOURCE				0x12
#define SABRE32_DACB_POLARITY				0x13
#define SABRE32_MASTER_TRIM1				0x14
#define SABRE32_MASTER_TRIM2				0x15
#define SABRE32_MASTER_TRIM3				0x16
#define SABRE32_MASTER_TRIM4				0x17
#define SABRE32_PHASE_SHIFT				0x18
#define SABRE32_DPLL_MODE				0x19
#define SABRE32_STATUS					0x1B
#define SABRE32_DPLL_NUM1				0x1C
#define SABRE32_DPLL_NUM2				0x1D
#define SABRE32_DPLL_NUM3				0x1E
#define SABRE32_DPLL_NUM4				0x1F
#define SABRE32_FIR_PROG_ENABLE				0x25
#define SABRE32_STAGE1_FIR1				0x26
#define SABRE32_STAGE1_FIR2				0x27
#define SABRE32_STAGE1_FIR3				0x28
#define SABRE32_STAGE1_FIR4				0x29
#define SABRE32_STAGE2_FIR1				0x2A
#define SABRE32_STAGE2_FIR2				0x2B
#define SABRE32_STAGE2_FIR3				0x2C
#define SABRE32_STAGE2_FIR4				0x2D

#endif
