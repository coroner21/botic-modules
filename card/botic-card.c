/*
 * ASoC simple sound card support
 *
 * Miroslav Rudisin <miero@seznam.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <linux/gpio/consumer.h>
#include <linux/clk.h>

/* External module: include config */
#include <generated/autoconf.h>

static int dai_format = SND_SOC_DAIFMT_CBS_CFS | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_I2S;

static int blr_ratio = 64;

struct botic_priv {
	unsigned long clk44_freq;
	unsigned long clk48_freq;
	struct clk *mux, *clk44, *clk48;
	struct gpio_desc *power_switch;
	struct gpio_desc *dsd_switch;
};

static int botic_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params) {
	
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct botic_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	unsigned int sysclk, bclk, divisor;
	int ret;
	
	unsigned int rate = params_rate(params);

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_format);
	if ((ret < 0) && (ret != -ENOTSUPP))
		return ret;
	
	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, dai_format);
	if (ret < 0)
		return ret;

	/* select correct clock for requested sample rate */
	if (priv->clk44_freq % rate == 0) {
		sysclk = priv->clk44_freq;
		clk_set_parent(priv->mux, priv->clk44);
	} else if (priv->clk48_freq % rate == 0) {
		sysclk = priv->clk48_freq;
		clk_set_parent(priv->mux, priv->clk48);
	} else {
		printk("unsupported rate %d\n", rate);
		return -EINVAL;
	}

	/* set the codec system clock */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, sysclk, SND_SOC_CLOCK_IN);
	if ((ret < 0) && (ret != -ENOTSUPP))
		return ret;

	/* use the external clock */
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, sysclk, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		printk(KERN_WARNING "botic-card: unable to set clock to CPU; ret=%d", ret);
		return ret;
	}

	switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_DSD_U8:
			/* Enable DSD switch */
			gpiod_set_value(priv->dsd_switch, 1);
			/* Clock rate for DSD matches bitrate */
			ret = snd_soc_dai_set_clkdiv(cpu_dai, 2, 0);
			bclk = 8 * rate;
			break;
		
		case SNDRV_PCM_FORMAT_DSD_U16_LE:
			gpiod_set_value(priv->dsd_switch, 1);
			/* Clock rate for DSD matches bitrate */
			ret = snd_soc_dai_set_clkdiv(cpu_dai, 2, 0);
			bclk = 16 * rate;
			break;

		case SNDRV_PCM_FORMAT_DSD_U32_LE:
			gpiod_set_value(priv->dsd_switch, 1);
			/* Clock rate for DSD matches bitrate */
			ret = snd_soc_dai_set_clkdiv(cpu_dai, 2, 0);
			bclk = 32 * rate;
			break;

		default:
			/* Disable DSD switch */
			gpiod_set_value(priv->dsd_switch, 0);
			/* PCM */
			ret = snd_soc_dai_set_clkdiv(cpu_dai, 2, blr_ratio);
			if (blr_ratio != 0) {
				bclk = blr_ratio * rate;
			} else {
				bclk = snd_soc_params_to_bclk(params);
			}
			break;
	}
	if (ret < 0) {
		printk(KERN_WARNING "botic-card: unsupported BCLK/LRCLK ratio");
		return ret;
	}
	
	divisor = (sysclk + (bclk / 2) )/ bclk;
	ret = snd_soc_dai_set_clkdiv(cpu_dai, 1, divisor);
	if (ret < 0) {
		printk(KERN_WARNING "botic-card: unsupported set_clkdiv1");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops botic_ops = {
	.hw_params = botic_hw_params,
};

static struct snd_soc_dai_link_component botic_cpus,
					 botic_codecs,
					 botic_platforms;

static struct snd_soc_dai_link botic_dai = {
	.name = "Botic",
	.stream_name = "external",
	.cpus = &botic_cpus,
	.num_cpus = 1,
	.codecs = &botic_codecs,
	.num_codecs = 1,
	.platforms = &botic_platforms,
	.num_platforms = 1,
	.ops = &botic_ops,
};

static struct snd_soc_card botic_card = {
	.name = "Botic",
	.owner = THIS_MODULE,
	.dai_link = &botic_dai,
	.num_links = 1,
};

#if defined(CONFIG_OF)
static const struct of_device_id asoc_botic_card_dt_ids[] = {
    { .compatible = "botic-audio-card",
    },
    { }
};
MODULE_DEVICE_TABLE(of, asoc_botic_card_dt_ids);

static int asoc_botic_card_probe(struct platform_device *pdev) {

	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_dai_link *dai;
	struct botic_priv *priv;
	int ret;

	dai = botic_card.dai_link;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	
	priv->power_switch = devm_gpiod_get(&pdev->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(priv->power_switch)) {
		ret = PTR_ERR(priv->power_switch);
		if (ret == -EPROBE_DEFER)
			pr_debug("%pOFn: %s: GPIOs not yet available, retry later\n",
					np, __func__);
		else
			pr_err("%pOFn: %s: Can't get '%s' named GPIO property\n",
					np, __func__,
					"enable");
		return ret;
	}

	priv->dsd_switch = devm_gpiod_get(&pdev->dev, "dsd", GPIOD_OUT_LOW);
	if (IS_ERR(priv->dsd_switch)) {
		ret = PTR_ERR(priv->dsd_switch);
		if (ret == -EPROBE_DEFER)
			pr_debug("%pOFn: %s: GPIOs not yet available, retry later\n",
					np, __func__);
		else
			pr_err("%pOFn: %s: Can't get '%s' named GPIO property\n",
					np, __func__,
					"dsd");
		return ret;
	}

	/* Parse clocks from device tree using CCF */
	priv->clk48 = devm_clk_get(&pdev->dev, "clk48");
	if (IS_ERR(priv->clk48)) {
		dev_err(&pdev->dev, "unable to get clock for 48khz multiples");
		return PTR_ERR(priv->clk48);
	}
	clk_prepare_enable(priv->clk48);
	priv->clk48_freq = clk_get_rate(priv->clk48);
	priv->clk44 = devm_clk_get(&pdev->dev, "clk44");
	if (IS_ERR(priv->clk44)) {
		dev_err(&pdev->dev, "unable to get clock for 44khz multiples");
		return PTR_ERR(priv->clk44);
	}
	clk_prepare_enable(priv->clk44);
	priv->clk44_freq = clk_get_rate(priv->clk44);
	priv->mux = devm_clk_get(&pdev->dev, "mux");
	if (IS_ERR(priv->clk44)) {
		dev_err(&pdev->dev, "unable to get the mux clock for frequency switches");
		return PTR_ERR(priv->mux);
	}
	
	dai->codecs->of_node = of_parse_phandle(np, "audio-codec", 0);
	if (dai->codecs->of_node) {
		ret = of_property_read_string_index(np, "audio-codec-dai", 0,
				&dai->codecs->dai_name);
		if (ret < 0)
			return ret;
	}
	else
		return -ENOENT;
	dai->cpus->of_node = of_parse_phandle(np, "audio-port", 0);
	if (!dai->cpus->of_node)
		return -ENOENT;
	
	dai->platforms->of_node = dai->cpus->of_node;
	botic_card.dev = &pdev->dev;
	
	snd_soc_card_set_drvdata(&botic_card, priv);

	/* register card with ALSA core*/
	ret = devm_snd_soc_register_card(&pdev->dev, &botic_card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		return ret;
	}

	/* switch the card on */
	gpiod_set_value(priv->power_switch, 1);

	return 0;
}
#endif

static int asoc_botic_card_remove(struct platform_device *pdev) {
	
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct botic_priv *priv = snd_soc_card_get_drvdata(card);

	gpiod_set_value(priv->power_switch, 0);
	snd_soc_unregister_card(card);

	return 0;
}

static void asoc_botic_card_shutdown(struct platform_device *pdev) {
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct botic_priv *priv = snd_soc_card_get_drvdata(card);

	gpiod_set_value(priv->power_switch, 0);
}

#ifdef CONFIG_PM_SLEEP
static int asoc_botic_card_suspend(struct platform_device *pdev, pm_message_t state) {
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct botic_priv *priv = snd_soc_card_get_drvdata(card);

	gpiod_set_value(priv->power_switch, 0);
        /* switch the card off before going suspend */

	return 0;
}

static int asoc_botic_card_resume(struct platform_device *pdev) {
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct botic_priv *priv = snd_soc_card_get_drvdata(card);

	gpiod_set_value(priv->power_switch, 1);
	/* switch the card on after resuming from suspend */

	return 0;
}
#else
#define asoc_botic_card_suspend NULL
#define asoc_botic_card_resume NULL
#endif

static struct platform_driver asoc_botic_card_driver = {
	.probe = asoc_botic_card_probe,
	.remove = asoc_botic_card_remove,
	.shutdown = asoc_botic_card_shutdown,
	.suspend = asoc_botic_card_suspend,
	.resume = asoc_botic_card_resume,
	.driver = {
		.name = "asoc-botic-card",
		.of_match_table = of_match_ptr(asoc_botic_card_dt_ids),
	},
};

module_platform_driver(asoc_botic_card_driver);

module_param(blr_ratio, int, 0644);
MODULE_PARM_DESC(blr_ratio, "force BCLK/LRCLK ratio");

module_param(dai_format, int, 0644);
MODULE_PARM_DESC(dai_format, "Set DAI format to non-default setting (e.g. right justified).");

MODULE_AUTHOR("Christian Kröner");
MODULE_DESCRIPTION("ASoC Botic sound card (rewrite)");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:asoc-botic-card");
