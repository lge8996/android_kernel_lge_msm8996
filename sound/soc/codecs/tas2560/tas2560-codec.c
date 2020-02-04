/*
** =============================================================================
** Copyright (c) 2016  Texas Instruments Inc.
**
** This program is free software; you can redistribute it and/or modify it under
** the terms of the GNU General Public License as published by the Free Software
** Foundation; version 2.
**
** This program is distributed in the hope that it will be useful, but WITHOUT
** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
** FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
**
** File:
**     tas2560-codec.c
**
** Description:
**     ALSA SoC driver for Texas Instruments TAS2560 High Performance 4W Smart Amplifier
**
** =============================================================================
*/

#ifdef CONFIG_TAS2560_CODEC_STEREO

//#define DEBUG
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/firmware.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "tas2560.h"
#include "tas2560-core.h"

#include <sound/smart_amp.h>
#include <sound/q6afe-v2.h>

#define TAS2560_MDELAY 0xFFFFFFFE
#define KCONTROL_CODEC

static int spk_r_control = 0;
static int spk_l_control = 0;

struct tas_dsp_pkt {
	u8 slave_id;
	u8 book;
	u8 page;
	u8 offset;
	u8 data[TAS_PAYLOAD_SIZE * 4];
};
typedef struct tas_dsp_pkt tas_dsp_pkt;

typedef enum {
	READ_RE = 1,
	READ_F0,
	READ_Q,
	READ_Tv,
	CALIB_INIT,
	CALIB_DEINIT,
	SET_PROFILE,
	SET_RE,
	OPTS_MAX
} param_id_t;

typedef enum {
	MODE_UNDEFINED,
	MODE_READ,
	MODE_WRITE
}mode_rw;

bool make_dsp_pkt (struct tas_dsp_pkt *ppacket, param_id_t param_id, int mode
		, int channel_number, int param_value);


bool make_dsp_pkt (struct tas_dsp_pkt *ppacket, param_id_t param_id, int mode
		, int channel_number, int param_value)
{
	struct tas_dsp_pkt *pkt;
	pkt = ppacket;
	if (!pkt)
		return false;

	pkt->book = 0x8c;

	if (channel_number == 1) {
		pkt->slave_id = 0x98;
	} else if (channel_number == 2) {
		pkt->slave_id = 0x9a;
	} else {
		pr_err("[SmartPA-%d]make_dsp_pkt:invalid channel number!\n",__LINE__);
		return false;
	}

	switch (param_id) {
	case READ_RE:
		pkt->page = 0x80;
		pkt->offset = 0x14;
		break;

	case READ_F0:
		pkt->page = 0x80;
		pkt->offset = 0x8;
		break;

	case READ_Q:
		pkt->page = 0x80;
		pkt->offset = 0xc;
		break;

	case READ_Tv:
		pkt->page = 0x80;
		pkt->offset = 0x10;
		break;

	case CALIB_INIT:
		pkt->page = 0x80;
		pkt->offset = 0x18;
		break;

	case CALIB_DEINIT:
		pkt->page = 0x80;
		pkt->offset = 0x1c;
		break;

	case SET_PROFILE:
		if (param_value == -1)
			param_value = 0;

		pr_info("[SmartPA-%d]make_dsp_pkt:Requesting for setting profile number =%d\n", __LINE__,param_value);
		pkt->page = 0x80;
		pkt->offset = 0x24;
		memset (pkt->data, 0, sizeof(pkt->data));
		memcpy (pkt->data, &param_value, sizeof(param_value));
		break;

	case SET_RE:
		if (param_value == -1) {
			pr_info("[SmartPA-%d]make_dsp_pkt:Please provide a valid re value with -i\n",__LINE__);
			return 0;
		}

		pr_info("[SmartPA-%d]make_dsp_pkt:Setting spk Re 0x%x\n", __LINE__,param_value);
		pkt->page = 0x80;
		if (pkt->slave_id == 0x98) {
			pkt->offset = (0x1c + 0xc);
		} else {
			pkt->offset = (0x1c + 0xc + 0x4);
		}
		memset (pkt->data, 0, sizeof(pkt->data));
		memcpy (pkt->data, &param_value, sizeof(param_value));
		break;

	default:
		pr_info("[SmartPA-%d]make_dsp_pkt:Unhandled : default case!!!\n",__LINE__);
		return false;
	}

	return true;
}

static unsigned int tas2560_codec_read(struct snd_soc_codec *codec,  unsigned int reg)
{
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	dev_err(pTAS2560->dev, "%s, should not get here\n", __func__);

	return 0;
}

static int tas2560_codec_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	dev_err(pTAS2560->dev, "%s, should not get here\n", __func__);
	return 1;/*pTAS2560->write(pTAS2560, reg, value);*/
}

static int tas2560_codec_suspend(struct snd_soc_codec *codec)
{
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	mutex_lock(&pTAS2560->codec_lock);

	dev_dbg(pTAS2560->dev, "%s\n", __func__);
	pTAS2560->runtime_suspend(pTAS2560);

	mutex_unlock(&pTAS2560->codec_lock);
	return ret;
}

static int tas2560_codec_resume(struct snd_soc_codec *codec)
{
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	mutex_lock(&pTAS2560->codec_lock);

	dev_dbg(pTAS2560->dev, "%s\n", __func__);
	pTAS2560->runtime_resume(pTAS2560);

	mutex_unlock(&pTAS2560->codec_lock);
	return ret;
}

static int tas2560_AIF_post_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	//struct snd_soc_codec *codec = w->codec;
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		dev_dbg(pTAS2560->dev, "SND_SOC_DAPM_POST_PMU");
	break;
	case SND_SOC_DAPM_POST_PMD:
		dev_dbg(pTAS2560->dev, "SND_SOC_DAPM_POST_PMD");
	break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget tas2560_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN_E("ASI1", "ASI1 Playback", 0, SND_SOC_NOPM, 0, 0,
				tas2560_AIF_post_event, SND_SOC_DAPM_POST_PMU |
				SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUT_DRV("ClassD", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("OUT")
};

static const struct snd_soc_dapm_route tas2560_audio_map[] = {
	{"DAC", NULL, "ASI1"},
	{"ClassD", NULL, "DAC"},
	{"OUT", NULL, "ClassD"},
	{"DAC", NULL, "PLL"},
};

static int tas2560_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(pTAS2560->dev, "%s\n", __func__);

	return 0;
}

static void tas2560_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(pTAS2560->dev, "%s\n", __func__);
}

static int tas2560_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	mutex_lock(&pTAS2560->codec_lock);
	dev_dbg(pTAS2560->dev, "%s, %d\n", __func__, mute);

	//start
	pr_info("%s: spk_l_control = %d,spk_r_control = %d,mute = %d\n", __func__,spk_l_control,spk_r_control,mute);

	if(1 == spk_l_control){
		pr_info("%s, SPK_L\n", __func__);
		tas2560_enable(pTAS2560, !mute, channel_left);
	}
	if(1 == spk_r_control){
		pr_info("%s, SPK_R\n", __func__);
		tas2560_enable(pTAS2560, !mute, channel_right);
	}

	if((1 == spk_l_control)&&(1 == spk_r_control)){
		pr_info("%s, SPK_LR\n", __func__);
		tas2560_enable(pTAS2560, !mute, channel_both);
	}
	/* end */
	mutex_unlock(&pTAS2560->codec_lock);
	return 0;
}

static int tas2560_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
			unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	dev_dbg(pTAS2560->dev, "%s\n", __func__);

	return ret;
}

static int tas2560_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(pTAS2560->dev, "%s\n", __func__);

	return 0;
}

static int tas2560_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(pTAS2560->dev, "%s\n", __func__);

	return 0;
}

static int tas2560_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	dev_dbg(pTAS2560->dev, "%s, format=0x%x\n", __func__, fmt);

	return ret;
}

static struct snd_soc_dai_ops tas2560_dai_ops = {
	.startup = tas2560_startup,
	.shutdown = tas2560_shutdown,
	.digital_mute = tas2560_mute,
	.hw_params  = tas2560_hw_params,
	.prepare    = tas2560_prepare,
	.set_sysclk = tas2560_set_dai_sysclk,
	.set_fmt    = tas2560_set_dai_fmt,
};

#define TAS2560_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)
static struct snd_soc_dai_driver tas2560_dai_driver[] = {
	{
		.name = "tas2560 Stereo ASI1",
		.id = 0,
		.playback = {
			.stream_name    = "ASI1 Playback",
			.channels_min   = 2,
			.channels_max   = 2,
			.rates      = SNDRV_PCM_RATE_8000_192000,
			.formats    = TAS2560_FORMATS,
		},
		.ops = &tas2560_dai_ops,
	},
};

static int tas2560_codec_probe(struct snd_soc_codec *codec)
{
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

#ifdef SMART_AMP
	codec_smartamp_add_controls(codec);
#endif
	dev_err(pTAS2560->dev, "%s\n", __func__);

	return 0;
}

static int tas2560_codec_remove(struct snd_soc_codec *codec)
{
#ifdef SMART_AMP
	codec_smartamp_remove_controls(codec);
#endif
	return 0;
}

static int tas2560_get_load(struct snd_kcontrol *pKcontrol,
			struct snd_ctl_elem_value *pUcontrol)
{
#ifdef KCONTROL_CODEC
	struct snd_soc_codec *pCodec = snd_soc_kcontrol_codec(pKcontrol);
#else
	struct snd_soc_codec *pCodec = snd_kcontrol_chip(pKcontrol);
#endif
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(pCodec);

	if (!strcmp(pKcontrol->id.name, "TAS2560 Left Boost load"))
		pUcontrol->value.integer.value[0] = pTAS2560->mnLeftLoad;
	else
		pUcontrol->value.integer.value[0] = pTAS2560->mnRightLoad;

	return 0;
}

static int tas2560_set_load(struct snd_kcontrol *pKcontrol,
			struct snd_ctl_elem_value *pUcontrol)
{
#ifdef KCONTROL_CODEC
	struct snd_soc_codec *pCodec = snd_soc_kcontrol_codec(pKcontrol);
#else
	struct snd_soc_codec *pCodec = snd_kcontrol_chip(pKcontrol);
#endif
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(pCodec);

	dev_dbg(pCodec->dev, "%s:\n", __func__);

	if (!strcmp(pKcontrol->id.name, "TAS2560 Left Boost load")) {
		pTAS2560->mnLeftLoad = pUcontrol->value.integer.value[0];
		tas2560_setLoad(pTAS2560, channel_left, pTAS2560->mnLeftLoad);
	} else {
		pTAS2560->mnRightLoad = pUcontrol->value.integer.value[0];
		tas2560_setLoad(pTAS2560, channel_right, pTAS2560->mnRightLoad);
	}
	return 0;
}

static int tas2560_get_left_speaker_switch(struct snd_kcontrol *pKcontrol,
			struct snd_ctl_elem_value *pUcontrol)
{
	pr_info("%s: pUcontrolL = %ld\n", __func__,pUcontrol->value.integer.value[0]);
	pUcontrol->value.integer.value[0] = spk_l_control;

	return 0;
}

static int tas2560_set_left_speaker_switch(struct snd_kcontrol *pKcontrol,
			struct snd_ctl_elem_value *pUcontrol)
{
	pr_info("%s: pUcontrolR = %ld,spk_l_control = %d\n", __func__,pUcontrol->value.integer.value[0],spk_l_control);
	spk_l_control = pUcontrol->value.integer.value[0];
	return 1;
}


static int tas2560_get_right_speaker_switch(struct snd_kcontrol *pKcontrol,
			struct snd_ctl_elem_value *pUcontrol)
{
	pr_info("%s: pUcontrol = %ld\n", __func__,pUcontrol->value.integer.value[0]);
	pUcontrol->value.integer.value[0] = spk_r_control;

	return 0;
}

static int tas2560_set_right_speaker_switch(struct snd_kcontrol *pKcontrol,
			struct snd_ctl_elem_value *pUcontrol)
{
	pr_info("%s: pUcontrol = %ld,spk_r_control = %d\n", __func__,pUcontrol->value.integer.value[0],spk_r_control);
		spk_r_control = pUcontrol->value.integer.value[0];
	return 1;
}

static int tas2560_get_Sampling_Rate(struct snd_kcontrol *pKcontrol,
				struct snd_ctl_elem_value *pUcontrol)
{
#ifdef KCONTROL_CODEC
	struct snd_soc_codec *pCodec = snd_soc_kcontrol_codec(pKcontrol);
#else
	struct snd_soc_codec *pCodec = snd_kcontrol_chip(pKcontrol);
#endif
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(pCodec);

	pUcontrol->value.integer.value[0] = pTAS2560->mnSamplingRate;
	dev_dbg(pCodec->dev, "%s: %d\n", __func__,
			pTAS2560->mnSamplingRate);
	return 0;
}

static int tas2560_set_Sampling_Rate(struct snd_kcontrol *pKcontrol,
				struct snd_ctl_elem_value *pUcontrol)
{
#ifdef KCONTROL_CODEC
	struct snd_soc_codec *pCodec = snd_soc_kcontrol_codec(pKcontrol);
#else
	struct snd_soc_codec *pCodec = snd_kcontrol_chip(pKcontrol);
#endif
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(pCodec);
	int sampleRate = pUcontrol->value.integer.value[0];

	mutex_lock(&pTAS2560->codec_lock);
	dev_dbg(pCodec->dev, "%s: %d\n", __func__, sampleRate);
	tas2560_set_SampleRate(pTAS2560, sampleRate);
	mutex_unlock(&pTAS2560->codec_lock);

	return 0;
}

static int tas2560_power_ctrl_get(struct snd_kcontrol *pKcontrol,
				struct snd_ctl_elem_value *pValue)
{
#ifdef KCONTROL_CODEC
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
#else
	struct snd_soc_codec *codec = snd_kcontrol_chip(pKcontrol);
#endif
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	pValue->value.integer.value[0] = pTAS2560->mbPowerUp[0];
	dev_dbg(codec->dev, "tas2560_power_ctrl_get = 0x%x\n",
					pTAS2560->mbPowerUp[0]);

	return 0;
}

static int tas2560_power_ctrl_put(struct snd_kcontrol *pKcontrol,
				struct snd_ctl_elem_value *pValue)
{
#ifdef KCONTROL_CODEC
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
#else
	struct snd_soc_codec *codec = snd_kcontrol_chip(pKcontrol);
#endif
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	int bPowerUp = pValue->value.integer.value[0];

	mutex_lock(&pTAS2560->codec_lock);
	tas2560_enable(pTAS2560, bPowerUp, channel_both);
	mutex_unlock(&pTAS2560->codec_lock);

	return 0;
}

static const char *load_text[] = {"8_Ohm", "6_Ohm", "4_Ohm"};

static const struct soc_enum load_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(load_text), load_text),
};

static const char *speaker_switch_text[] = {"Off", "On"};

static const struct soc_enum spk_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(speaker_switch_text), speaker_switch_text),
};

static const char *Sampling_Rate_text[] = {"48_khz", "44.1_khz", "16_khz", "8_khz"};

static const struct soc_enum Sampling_Rate_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(Sampling_Rate_text), Sampling_Rate_text),
};

static const char *Channel_Index_text[] = {"left", "right"};

static const struct soc_enum Channel_Index_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(Channel_Index_text), Channel_Index_text),
};

static const char *Profile_Index_text[] = {"Music","Voice","RingTone","Calib"};

static const struct soc_enum Profile_Index_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(Profile_Index_text), Profile_Index_text),
};

/*
 * DAC digital volumes. From 0 to 15 dB in 1 dB steps
 */
static DECLARE_TLV_DB_SCALE(dac_tlv, 0, 100, 0);

static int tas_ctl_noimpl(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =	(struct soc_mixer_control *)kcontrol->private_value;
	int shift = mc->shift;
	printk(KERN_INFO "%s: module=0x%x(%d)", __func__, shift, shift);
	return 0;
}

static int tas_spkr_prot_put_vi_ch_port(struct snd_kcontrol *kcontrol,	struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	uint32_t enable = ucontrol->value.integer.value[0];
	if (enable) {
		pr_info("[smartamp] Setting the feedback module info for TAS");
		ret = afe_spk_prot_feed_back_cfg(TAS_TX_PORT, TAS_RX_PORT, 1, 0, 1);
	}
	return ret;
}

static const struct snd_kcontrol_new tas2560_snd_controls[] = {
	SOC_SINGLE_TLV("DAC Playback Volume", TAS2560_SPK_CTRL_REG, 0, 0x0f, 0,
			dac_tlv),
	SOC_ENUM_EXT("TAS2560 Left Boost load", load_enum[0],
			tas2560_get_load, tas2560_set_load),
	SOC_ENUM_EXT("TAS2560 Right Boost load", load_enum[0],
			tas2560_get_load, tas2560_set_load),

	SOC_ENUM_EXT("TAS2560 Left Speaker Switch", spk_enum[0],
			tas2560_get_left_speaker_switch, tas2560_set_left_speaker_switch),
	SOC_ENUM_EXT("TAS2560 Right Speaker Switch", spk_enum[0],
			tas2560_get_right_speaker_switch, tas2560_set_right_speaker_switch),

	SOC_ENUM_EXT("TAS2560 Sampling Rate", Sampling_Rate_enum[0],
			tas2560_get_Sampling_Rate, tas2560_set_Sampling_Rate),
	SOC_SINGLE_EXT("TAS2560 PowerCtrl", SND_SOC_NOPM, 0, 0x0001, 0,
			tas2560_power_ctrl_get, tas2560_power_ctrl_put),

	SOC_SINGLE_EXT("CAPI_V2_TAS_FEEDBACK_INFO", 0, 0, 1, 0,
	        tas_ctl_noimpl, tas_spkr_prot_put_vi_ch_port),

};

static struct snd_soc_codec_driver soc_codec_driver_tas2560 = {
	.probe			= tas2560_codec_probe,
	.remove			= tas2560_codec_remove,
	.read			= tas2560_codec_read,
	.write			= tas2560_codec_write,
	.suspend		= tas2560_codec_suspend,
	.resume			= tas2560_codec_resume,
	.component_driver = {
		.controls		= tas2560_snd_controls,
		.num_controls		= ARRAY_SIZE(tas2560_snd_controls),
		.dapm_widgets		= tas2560_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(tas2560_dapm_widgets),
		.dapm_routes		= tas2560_audio_map,
		.num_dapm_routes	= ARRAY_SIZE(tas2560_audio_map),
	},
};

int tas2560_register_codec(struct tas2560_priv *pTAS2560)
{
	int nResult = 0;

	dev_info(pTAS2560->dev, "%s, enter\n", __func__);
	nResult = snd_soc_register_codec(pTAS2560->dev,
		&soc_codec_driver_tas2560,
		tas2560_dai_driver, ARRAY_SIZE(tas2560_dai_driver));

	return nResult;
}

int tas2560_deregister_codec(struct tas2560_priv *pTAS2560)
{
	snd_soc_unregister_codec(pTAS2560->dev);

	return 0;
}

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TAS2560 ALSA SOC Smart Amplifier driver");
MODULE_LICENSE("GPL v2");
#endif /* CONFIG_TAS2560_CODEC_STEREO */
