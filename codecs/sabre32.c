/*
 * ESS Technology Sabre32 family Audio DAC support
 *
 * Miroslav Rudisin <miero@seznam.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/of_platform.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include "sabre32.h"

/* External module: include config */
#include <generated/autoconf.h>

#define SABRE32_CODEC_NAME "sabre32-codec"
#define SABRE32_CODEC_DAI_NAME "sabre32-hifi"

static const struct reg_default sabre32_reg_defaults[] = {
	{ SABRE32_VOLUME0,		0x00 },
	{ SABRE32_VOLUME1,		0x00 },
	{ SABRE32_VOLUME2,		0x00 },
	{ SABRE32_VOLUME3,		0x00 },
	{ SABRE32_VOLUME4,		0x00 },
	{ SABRE32_VOLUME5,		0x00 },
	{ SABRE32_VOLUME6,		0x00 },
	{ SABRE32_VOLUME7,		0x00 },
	/* First bit of AUTOMUTE_LEV controls SPDIF_ENABLE: 0 (I2S or DSD), 1 SPDIF */
	{ SABRE32_AUTOMUTE_LEV,		0x68 },
	{ SABRE32_AUTOMUTE_TIME,	0x04 }, /* Time in seconds: 2096896 / (REG_VALUE * DATA_CLK) */
	/* Mode control1 documentation: 
	 * Bit 7:6 toggle bit depth for serial data
	 * 00: 24bit
	 * 01: 20bit
	 * 10: 16bit
	 * 11: 32bit (default)
	 * Bit 5:4 control serial data mode
	 * 00: I2S (default)
	 * 01: left justified
	 * 10: right justified
	 * 11: I2S
	 * Bit 3 needs to be set to 1
	 * Bit 2 toggles jitter reduction (1: Use (default), 0: Bypass)
	 * Bit 1 toggles de-emphasis filter (1: Bypass (default), 0: Use)
	 * Bit 0 is mute control (0: Unmute (default), 1: Mute)
	 */
	{ SABRE32_MODE_CONTROL1,	0xCE },
	/* Mode control2 doc:
	 * Bit 7 needs to be set to 1
	 * Bit 6:5 reserved (0 at default)
	 * Bit 4:2 DPLL bandwidth control:
	 * 000: No bandwidth
	 * 001: Lowest bandwidth (default)
	 * 010: Low bandwidth
	 * 011: Medium-low bandwidth
	 * 100: Medium bandwidth
	 * 101: Medium-high bandwidth
	 * 110: High bandwidth
	 * 111: Highest bandwidth
	 * Bit 1:0 selects deemphasis filter
	 * 00: 32khz
	 * 01: 44.1khz
	 * 10: 48khz
	 * 11: reserved
	 */
	{ SABRE32_MODE_CONTROL2,	0x85 },
	/* Notch delay
	 *  |0|x|x|x|x|x|x|x| Dither Control: Apply
	 *  |1|x|x|x|x|x|x|x| Dither Control: Use fixed rotation pattern
	 *  |x|0|x|x|x|x|x|x| Rotator Input: NS-mod input
	 *  |x|1|x|x|x|x|x|x| Rotator Input: External input
	 *  |x|x|0|x|x|x|x|x| Remapping: No remap
	 *  |x|x|1|x|x|x|x|x| Remapping: Remap DIG outputs for “max phase separation in analog cell”
	 *
	 * One other note. In 6 bit mode to get the best performance you should use the n/64 notch delay.
	 *  |x|x|x|0|0|0|0|0| No Notch
	 *  |x|x|x|0|0|0|0|1| Notch at MCLK/4
	 *  |x|x|x|0|0|0|1|1| Notch at MCLK/8
	 *  |x|x|x|0|0|1|1|1| Notch at MCLK/16
	 *  |x|x|x|0|1|1|1|1| Notch at MCLK/32
	 *  |x|x|x|1|1|1|1|1| Notch at MCLK/64
	 *
	 *  |0|0|1|0|0|0|0|0| Power-on Default
	 */
	{ SABRE32_MODE_CONTROL3,	0x20 }, /* reserved */
	{ SABRE32_DAC_POLARITY,		0x00 }, /* each bit corresponds to one DAC, 0 in phase, 1 anti-phase */
	/* DAC_SOURCE: Input mapping and filter control
	 * Bit 7: Source of DAC8 is DAC6 (1) or DAC8 (0)
	 * Bit 6: Source of DAC7 is DAC5 (1) or DAC7 (0)
	 * Bit 5: Source of DAC4 is DAC2 (1) or DAC4 (0)
	 * Bit 4: Source of DAC3 is DAC1 (1) or DAC3 (0)
	 * Bit 3 reserved (1 for true differential, 0 for pseudo differential --> quantizer setting)
	 *    Set it to pseudo differential per default (stereo, 9-bit quantizer setting)
	 * Bit 2:1 IIR bandwidth:
	 * 00: Normal (for least in-band ripple for PCM data)
	 * 01: 50k (default)
	 * 10: 60k
	 * 11: 70k
	 * Bit 0 FIR roll-off speed (0 slow, 1 fast (default))
	 */
	{ SABRE32_DAC_SOURCE,		0x0B },
	/* Quantizer setting
	 * The quantizer value affects one pair of DACs (independently) as follows:
	 *
	 * 00= 6-bit
	 * 01= 7-bit
	 * 10= 8-bit
	 * 11= 9-bit
	 *
	 * The 4 pair of DACs are controlled by the 8-bit register:
	 *
	 * |xx|xx|xx|xx| (the 8-bit register)
	 * |DACs 6, 8|DACs 2, 4|DACs 5, 7|DACs 1, 3|
	 *
	 * We set this to 9-bit for all DACs to have a stereo
	 * DAC as a result, with Left / Right volume control
	 * for DAC1 and DAC2 (other digital DAC sections would
	 * not be used).
	 */
	{ SABRE32_MODE_CONTROL4,	0x00 }, /*reserved */
	{ SABRE32_AUTOMUTE_LOOPBACK,	0x00 }, /* All bits except 3 reserved, Bit 3: 1 enable automte, 0 disable automute */
	/* Mode control5 doc:
	 * Bit 7: 1 right channel or 0 left channel for mono mode
	 * Bit 6: 1 bypass OSF: Send data directly from I2S input to IIR filter at 8x,
	 * 	bypasses FIR and deemphasis filters, still applies volume control
	 * 	0 default use of OSF
	 * Bit 5: 1 Force relock of DPLL, 0 normal operation
	 * Bit 4: 1 Apply deemphasis filter frequency based on SPDIF data automatically
	 * 	0 No automatic selection of deemph freq
	 * Bit 3: SPDIF autodetect (1) or manual SPDIF (0), 0 should only be set if I2S is not applied
	 * Bit 2: FIR filter length: 1 (second stage has 28 coefficients), 0 (27)
	 * Bit 1: DPLL phase inversion: 1 to invert, 0 not to invert
	 * Bit 0: 1 enable all mono mode, 0 disable (use 8 channels)
	 */
	{ SABRE32_MODE_CONTROL5,	0x1C },
	{ SABRE32_SPDIF_SOURCE,		0x01 }, /* each bit corresponds to 1 input pin */
	{ SABRE32_DACB_POLARITY,	0x00 }, /* 1 (in-phase), 0 (anti-phase, default) for each DACB polarity */
	/* Volume master trim is a 32bit value setting the
	 * 0dB level for all volume controls. This is a
	 * signed number that never should exceed
	 * 7F FF FF FF (which is 2^31 - 1).
	 */
	{ SABRE32_MASTER_TRIM1,		0xFF },
	{ SABRE32_MASTER_TRIM2,		0xFF },
	{ SABRE32_MASTER_TRIM3,		0xFF },
	{ SABRE32_MASTER_TRIM4,		0x7F },
	/* Phase shift register:
	 * Bit 7:4 reserved (default 0011)
	 * Bit 3:0 specifies phase shift:
	 * 0000 default
	 * 0001 + 1/clk delay
	 * 0010 + 2/clk delay
	 * ...
	 * 1111 + 15/clk delay
	 */
	{ SABRE32_PHASE_SHIFT,		0x30 },
	/* DPLL mode:
	 * Bit 7:2 reserved (set to 0)
	 * Bit 1: 1 use best DPLL bandwidth (auto, default), 0 allow all settings
	 * Bit 0: Multiplay bandwidth by 128 (1), use bandwidth (0)
	 */
	{ SABRE32_DPLL_MODE,		0x02 },
	/* Bit 7:6 reserved
	 * Bit 5: Use custom coefficients (1) or builtin (0) for FIR stage 1
	 * Bit 4: Enable (1) writing coefficients or disable (0) for FIR stage 1
	 * Bit 3:2 reserved
	 * Bit 1: Use custom coefficients (1) or builtin (0) for FIR stage 2
	 * Bit 0: Enable (1) writing coefficients or disable (0) for FIR stage 2
	 */
	{ SABRE32_FIR_PROG_ENABLE,	0x00 },
};

static bool sabre32_readable_reg(struct device *dev, unsigned int reg)
{
	if( reg <= 0x1F && reg != 0x1A )
		return 1;
	else if ( reg >= 0x25 && reg <= 0x47 )
		return 1;
	else
		return 0;
}

static bool sabre32_writeable_reg(struct device *dev, unsigned int reg)
{
	if (reg > 0x47)
		return 0;
	else if (reg <= 0x24 && reg >= 0x1A)
		return 0;
	else
		return 1;
}

static bool sabre32_volatile_reg(struct device *dev, unsigned int reg)
{
	if( reg <= 0x1F && reg >= 0x1C)
		return 1;
	else return 0;
}

struct sabre32_priv {
	struct regmap *regmap;
	int stream_muted;
	int dpll_mode;
};

#define SABRE32_FORMATS (\
            SNDRV_PCM_FMTBIT_S16_LE | \
            SNDRV_PCM_FMTBIT_S24_3LE | \
            SNDRV_PCM_FMTBIT_S24_LE | \
            SNDRV_PCM_FMTBIT_S32_LE | \
            SNDRV_PCM_FMTBIT_DSD_U32_LE | \
            0)

static const char * const sabre32_dpll_texts[] = {
	"1x Auto",
	"128x Auto",
	"None",
	"1x", "2x", "4x", "8x", "16x", "32x", "64x",
	"128x", "256x", "512x", "1024x", "2048x", "4096x", "8192x",
};

static const struct soc_enum sabre32_dpll_enum = SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sabre32_dpll_texts), sabre32_dpll_texts);

static int sabre32_dpll_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct sabre32_priv *sabre32_data = snd_soc_component_get_drvdata(component);
	int value = ucontrol->value.enumerated.item[0];
	
	if(sabre32_data->dpll_mode == value)
		return 0;

	if (value < 2) {
		/* Set auto or auto 128x */
		snd_soc_component_update_bits(component, SABRE32_DPLL_MODE, 0x03, 2 + value);
		/* Reset DPLL bandwidth to default */
		snd_soc_component_update_bits(component, SABRE32_MODE_CONTROL2, 0x1C, 0x01);
	}
	else {
		value -= 2;
		if (value <= 7) {
			snd_soc_component_update_bits(component, SABRE32_MODE_CONTROL2, 0x1C, value << 2);
			value = 0;
		}
		else {
			value -= 7;
			snd_soc_component_update_bits(component, SABRE32_MODE_CONTROL2, 0x1C, value << 2);
			value = 1;
		}
		snd_soc_component_update_bits(component, SABRE32_DPLL_MODE, 0x03, value);
	}
	sabre32_data->dpll_mode = ucontrol->value.enumerated.item[0];
	return 0;
}

static int sabre32_dpll_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
        struct sabre32_priv *sabre32_data = snd_soc_component_get_drvdata(component);
	ucontrol->value.enumerated.item[0] = sabre32_data->dpll_mode;
	return 0;
}

static int sabre32_mute_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct sabre32_priv *sabre32_data = snd_soc_component_get_drvdata(component);
	int value = ucontrol->value.enumerated.item[0];

	if (value)
	{
		sabre32_data->stream_muted = 0;
		snd_soc_component_update_bits(component, SABRE32_MODE_CONTROL1, 0x01, 0x00);
	}
	else
	{
		sabre32_data->stream_muted = 1;
		snd_soc_component_update_bits(component, SABRE32_MODE_CONTROL1, 0x01, 0x01);
	}
	return 0;
}

static int sabre32_mute_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct sabre32_priv *sabre32_data = snd_soc_component_get_drvdata(component);
	ucontrol->value.enumerated.item[0] = !sabre32_data->stream_muted;
	return 0;
}

static const char * const sabre32_spdif_input_text[] = {
    "1", "2", "3", "4", "5", "6", "7", "8"
};

static const unsigned int sabre32_spdif_input_values[] = {
	1,
	1 << 1,
	1 << 2,
	1 << 3,
	1 << 4,
	1 << 5,
	1 << 6,
	1 << 7,
};

static SOC_VALUE_ENUM_SINGLE_DECL(sabre32_spdif_input, SABRE32_SPDIF_SOURCE , 0, 0xFF, sabre32_spdif_input_text, sabre32_spdif_input_values);

static const char * const bypass_or_use_text[] = {
    "Bypass", "Use"
};

static SOC_ENUM_SINGLE_DECL(sabre32_jitter_reduction, SABRE32_MODE_CONTROL1, 2, bypass_or_use_text);

static const char * const sabre32_fir_rolloff_text[] = {
    "Slow", "Fast"
};

static SOC_ENUM_SINGLE_DECL(sabre32_fir_rolloff, SABRE32_DAC_SOURCE, 0, sabre32_fir_rolloff_text);

/*static const char *true_mono_text[] = {
    "Left", "Off", "Right"
};

static SOC_ENUM_SINGLE_DECL(true_mono, 8, 0, true_mono_text);
*/

static const char * const sabre32_dpll_phase_text[] = {
    "Normal", "Flip"
};

static SOC_ENUM_SINGLE_DECL(sabre32_dpll_phase, SABRE32_MODE_CONTROL5, 1, sabre32_dpll_phase_text);

static const char * const sabre32_os_filter_text[] = {
    "Use", "Bypass"
};

static SOC_ENUM_SINGLE_DECL(sabre32_os_filter, SABRE32_MODE_CONTROL5, 6, sabre32_os_filter_text);

/*static const char *remap_inputs_text[] = {
    "12345678", "12345676", "12345658", "12345656",
    "12325678", "12325676", "12325658", "12325656",
    "12145678", "12145676", "12145658", "12145656",
    "12125678", "12125676", "12125658", "12125656",
};

static SOC_ENUM_SINGLE_DECL(remap_inputs, 12, 0, remap_inputs_text);


static const char *mclk_notch_text[] = {
    "No Notch", "MCLK/4", "MCLK/8", "MCLK/16", "MCLK/32", "MCLK/64"
};

static SOC_ENUM_SINGLE_DECL(mclk_notch, 13, 0, mclk_notch_text);
*/

static const DECLARE_TLV_DB_SCALE(sabre32_dac_tlv, -12750, 50, 0);

static const struct snd_kcontrol_new sabre32_controls[] = {
	SOC_DOUBLE_R_TLV("Master Playback Volume", SABRE32_VOLUME0, SABRE32_VOLUME1, 0, 0xFF, 1, sabre32_dac_tlv),
	SOC_SINGLE_BOOL_EXT("Master Playback Switch", 0, sabre32_mute_get, sabre32_mute_set),
	SOC_ENUM("SPDIF Source", sabre32_spdif_input),
	SOC_ENUM("Jitter Reduction", sabre32_jitter_reduction),
	SOC_ENUM_EXT("DPLL", sabre32_dpll_enum, sabre32_dpll_get, sabre32_dpll_set),
	SOC_ENUM("FIR Rolloff", sabre32_fir_rolloff),
	SOC_ENUM("DPLL Phase", sabre32_dpll_phase),
	SOC_ENUM("Oversampling Filter", sabre32_os_filter),
};

const struct regmap_config sabre32_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 71,
	.reg_defaults = sabre32_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(sabre32_reg_defaults),
	.writeable_reg = sabre32_writeable_reg,
	.readable_reg = sabre32_readable_reg,
	.volatile_reg = sabre32_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(sabre32_regmap);

static int sabre32_component_probe(struct snd_soc_component *component)
{
	/* Setup some default register settings */
	
	/* Set pseudo differential */
	snd_soc_component_update_bits(component, SABRE32_DAC_SOURCE, 0x08, 0x00);
	/* Set 9-bit quantizer for stereo */
	snd_soc_component_write(component, SABRE32_MODE_CONTROL4, 0xFF);
	return 0;
}

static struct snd_soc_component_driver sabre32_component_driver = {
    .controls = sabre32_controls,
    .probe = sabre32_component_probe,
    .num_controls = ARRAY_SIZE(sabre32_controls),
    .idle_bias_on = 1,
    .use_pmdown_time = 1,
    .endianness = 1,
    .non_legacy_dai_naming = 1,
};

static int sabre32_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
    struct snd_soc_component *component = dai->component;

    switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
    case SND_SOC_DAIFMT_I2S:
        snd_soc_component_update_bits(component, SABRE32_MODE_CONTROL1, 0x30, 0x00);
        break;
    case SND_SOC_DAIFMT_LEFT_J:
        snd_soc_component_update_bits(component, SABRE32_MODE_CONTROL1, 0x30, 0x10);
        break;
    case SND_SOC_DAIFMT_RIGHT_J:
        snd_soc_component_update_bits(component, SABRE32_MODE_CONTROL1, 0x30, 0x20);
        break;
    default:
        dev_warn(component->dev, "unsupported DAI fmt %d", fmt);
        return -EINVAL;
        break;
    }

    return 0;
}

static int sabre32_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	struct sabre32_priv *sabre32_data = snd_soc_component_get_drvdata(component);

	if(stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	/* Only unmute the DAC if not explicitly muted by the ALSA control */
	if(!mute)
		mute = sabre32_data->stream_muted;

	snd_soc_component_update_bits(component, SABRE32_MODE_CONTROL1, 0x01, mute ? 0x01 : 0x00);

	return 0;
}

static int sabre32_hw_params(struct snd_pcm_substream *substream,
        struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
    struct snd_soc_component *component = dai->component;

    switch (params_format(params)) {
    case SNDRV_PCM_FORMAT_S16_LE:
	    /* set bit depth */
	    snd_soc_component_update_bits(component, SABRE32_MODE_CONTROL1, 0xc0, 0x80);
	    /* set IIR bandwidth to Normal */
	    snd_soc_component_update_bits(component, SABRE32_DAC_SOURCE, 0x06, 0x00);
	    break;

    case SNDRV_PCM_FORMAT_S24_3LE:
    case SNDRV_PCM_FORMAT_S24_LE:
        snd_soc_component_update_bits(component, SABRE32_MODE_CONTROL1, 0xc0, 0x00);
        snd_soc_component_update_bits(component, SABRE32_DAC_SOURCE, 0x06, 0x00);
	break;

    case SNDRV_PCM_FORMAT_S32_LE:
	snd_soc_component_update_bits(component, SABRE32_MODE_CONTROL1, 0xc0, 0xc0);
	snd_soc_component_update_bits(component, SABRE32_DAC_SOURCE, 0x06, 0x00);
	break;
    case SNDRV_PCM_FORMAT_DSD_U8:
    case SNDRV_PCM_FORMAT_DSD_U16_LE:
    case SNDRV_PCM_FORMAT_DSD_U32_LE:
        snd_soc_component_update_bits(component, SABRE32_MODE_CONTROL1, 0xc0, 0xc0);
	/* set IIR bandwidth to 60k */
	snd_soc_component_update_bits(component, SABRE32_DAC_SOURCE, 0x06, 0x04);
        break;

    default:
        dev_warn(component->dev, "unsupported PCM format %d", params_format(params));
        return -EINVAL;
    }

    return 0;
}

static const struct snd_soc_dai_ops sabre32_dai_ops = {
    .set_fmt = sabre32_set_fmt,
    .mute_stream = sabre32_mute,
    .hw_params = sabre32_hw_params,
};

static struct snd_soc_dai_driver sabre32_dai = {
	.name = "sabre32-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = SABRE32_FORMATS,
	},
	.ops = &sabre32_dai_ops,
};

int sabre32_probe(struct device *dev, struct regmap *regmap)
{
	struct sabre32_priv *sabre32;
	int ret = 0;

	sabre32 = devm_kzalloc(dev, sizeof(struct sabre32_priv), GFP_KERNEL);
	if (!sabre32)
		return -ENOMEM;					
	dev_set_drvdata(dev, sabre32);
	/* Initialize internal data */
	sabre32->regmap = regmap;
	sabre32->stream_muted = 0;
	sabre32->dpll_mode = 0;

	ret = devm_snd_soc_register_component(dev, &sabre32_component_driver, &sabre32_dai, 1);
	
	if (ret != 0) {
		dev_err(dev, "Failed to register CODEC: %d\n", ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sabre32_probe);

#if defined(CONFIG_OF)
static const struct of_device_id sabre32_dt_ids[] = {
	    { .compatible = "ess,sabre32", },
	        { }
};

MODULE_DEVICE_TABLE(of, sabre32_dt_ids);
#endif

static const struct i2c_device_id sabre32_i2c_id[] = {
		{ "sabre32", 0 },
			{ }
};
MODULE_DEVICE_TABLE(i2c, sabre32_i2c_id);

static int sabre32_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	int ret;
	struct regmap *regmap;
	struct regmap_config config = sabre32_regmap;

	regmap = devm_regmap_init_i2c(i2c, &config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "Failed to create regmap: %d\n", ret);
		return ret;
	}

	return sabre32_probe(&i2c->dev, regmap);
}

static struct i2c_driver sabre32_i2c_driver = {
	.driver = {
		.name	= "sabre32",
		.of_match_table = of_match_ptr(sabre32_dt_ids),
	},
	.id_table = sabre32_i2c_id,
	.probe = sabre32_i2c_probe,
};

module_i2c_driver(sabre32_i2c_driver);

MODULE_AUTHOR("Christian Kroener");
MODULE_DESCRIPTION("ESS Technology Sabre32 Audio DAC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:asoc-sabre32-codec");
