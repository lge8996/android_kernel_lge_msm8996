/*
 * ALSA SoC Texas Instruments TAS2560 High Performance 4W Smart Amplifier
 *
 * Copyright (C) 2016 Texas Instruments, Inc.
 *
 * Author: saiprasad
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#ifdef CONFIG_TAS2560_REGMAP_STEREO

//#define DEBUG
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
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
#include <sound/soc.h>

#include "tas2560.h"
#include "tas2560-core.h"
#include "tas2560-misc.h"
#include "tas2560-codec.h"

static int tas2560_change_page(struct tas2560_priv *pTAS2560, enum channel chn, int page)
{
	int ret = 0;

	if (chn&channel_left) {
		if (pTAS2560->mnLCurrentPage != page) {
			pTAS2560->client->addr = pTAS2560->mnLAddr;
			ret = regmap_write(pTAS2560->regmap, TAS2560_BOOKCTL_PAGE, page);
			if (ret < 0)
				dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
					__FUNCTION__, __LINE__, ret);
			else
				pTAS2560->mnLCurrentPage = page;
		}
	}

	if (ret >= 0) {
		if (chn&channel_right) {
			if (pTAS2560->mnRCurrentPage != page) {
				pTAS2560->client->addr = pTAS2560->mnRAddr;
				ret = regmap_write(pTAS2560->regmap, TAS2560_BOOKCTL_PAGE, page);
				if (ret < 0)
					dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
						__FUNCTION__, __LINE__, ret);
				else
					pTAS2560->mnRCurrentPage = page;
			}
		}
	}

	return ret;
}

static int tas2560_change_book(struct tas2560_priv *pTAS2560,  enum channel chn, int book)
{
	int ret = 0;

	if (chn&channel_left) {
		if (pTAS2560->mnLCurrentBook != book) {
			ret = tas2560_change_page(pTAS2560, channel_left, 0);
			if (ret >= 0) {
				pTAS2560->client->addr = pTAS2560->mnLAddr;
				ret = regmap_write(pTAS2560->regmap, TAS2560_BOOKCTL_REG, book);
				if (ret < 0)
					dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
						__FUNCTION__, __LINE__, ret);
				else
					pTAS2560->mnLCurrentBook = book;
			}
		}
	}

	if (ret >= 0) {
		if (chn&channel_right) {
			if (pTAS2560->mnRCurrentBook != book) {
				ret = tas2560_change_page(pTAS2560, channel_right, 0);
				if (ret >= 0) {
					pTAS2560->client->addr = pTAS2560->mnRAddr;
					ret = regmap_write(pTAS2560->regmap, TAS2560_BOOKCTL_REG, book);
					if (ret >= 0)
						pTAS2560->mnRCurrentBook = book;
					else
						dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
							__FUNCTION__, __LINE__, ret);
				}
			}
		}
	}

	return ret;
}
static int tas2560_dev_read(struct tas2560_priv *pTAS2560,
				enum channel chn, unsigned int reg, unsigned int *pValue)
{
	int ret = -1;

	dev_dbg(pTAS2560->dev, "%s: BOOK:PAGE:REG %u:%u:%u\n", __func__,
		TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
		TAS2560_PAGE_REG(reg));

	mutex_lock(&pTAS2560->dev_lock);
	ret = tas2560_change_book(pTAS2560, chn, TAS2560_BOOK_ID(reg));
	if (ret >= 0)
		ret = tas2560_change_page(pTAS2560, chn, TAS2560_PAGE_ID(reg));

	if (ret >= 0) {
		if (chn == channel_left) {
			pTAS2560->client->addr = pTAS2560->mnLAddr;
			ret = regmap_read(pTAS2560->regmap, TAS2560_PAGE_REG(reg), pValue);
			if (ret < 0)
				dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
							__FUNCTION__, __LINE__, ret);
		} else if (chn == channel_right) {
			pTAS2560->client->addr = pTAS2560->mnRAddr;
			ret = regmap_read(pTAS2560->regmap, TAS2560_PAGE_REG(reg), pValue);
			if (ret < 0)
				dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
							__FUNCTION__, __LINE__, ret);
		} else {
			dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
							__FUNCTION__, __LINE__, ret);
		}
	}
	mutex_unlock(&pTAS2560->dev_lock);
	return ret;
}

static int tas2560_dev_write(struct tas2560_priv *pTAS2560,
		enum channel chn, unsigned int reg, unsigned int value)
{
	int ret = -1;

	mutex_lock(&pTAS2560->dev_lock);
	ret = tas2560_change_book(pTAS2560, chn, TAS2560_BOOK_ID(reg));
	if (ret >= 0)
		ret = tas2560_change_page(pTAS2560, chn, TAS2560_PAGE_ID(reg));

	dev_dbg(pTAS2560->dev, "%s: BOOK:PAGE:REG %u:%u:%u, VAL: 0x%02x\n",
		__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
		TAS2560_PAGE_REG(reg), value);
	if (ret >= 0) {
		if (chn&channel_left) {
			pTAS2560->client->addr = pTAS2560->mnLAddr;
			ret = regmap_write(pTAS2560->regmap, TAS2560_PAGE_REG(reg),
				value);
			if (ret < 0)
				dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
							__FUNCTION__, __LINE__, ret);
		}

		if (chn&channel_right) {
			pTAS2560->client->addr = pTAS2560->mnRAddr;
			ret = regmap_write(pTAS2560->regmap, TAS2560_PAGE_REG(reg),
				value);
			if (ret < 0)
				dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
							__FUNCTION__, __LINE__, ret);
		}
	}
	mutex_unlock(&pTAS2560->dev_lock);
	return ret;
}

static int tas2560_dev_bulk_write(struct tas2560_priv *pTAS2560,
		enum channel chn, unsigned int reg,
		unsigned char *pData, unsigned int nLength)
{
	int ret = -1;

	mutex_lock(&pTAS2560->dev_lock);
	ret = tas2560_change_book(pTAS2560, chn, TAS2560_BOOK_ID(reg));
	if (ret >= 0)
		ret = tas2560_change_page(pTAS2560, chn, TAS2560_PAGE_ID(reg));

	dev_dbg(pTAS2560->dev, "%s: BOOK:PAGE:REG %u:%u:%u, len: 0x%02x\n",
		__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
		TAS2560_PAGE_REG(reg), nLength);

	if (ret >= 0) {
		if (chn&channel_left) {
			pTAS2560->client->addr = pTAS2560->mnLAddr;
			ret = regmap_bulk_write(pTAS2560->regmap, TAS2560_PAGE_REG(reg), pData, nLength);
			if (ret < 0)
				dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
							__FUNCTION__, __LINE__, ret);
		}

		if (chn&channel_right) {
			pTAS2560->client->addr = pTAS2560->mnRAddr;
			ret = regmap_bulk_write(pTAS2560->regmap, TAS2560_PAGE_REG(reg), pData, nLength);
			if (ret < 0)
				dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
							__FUNCTION__, __LINE__, ret);
		}
	}
	mutex_unlock(&pTAS2560->dev_lock);
	return ret;
}

static int tas2560_dev_bulk_read(struct tas2560_priv *pTAS2560,
			enum channel chn, unsigned int reg,
			 unsigned char *pData, unsigned int nLength)
{
	int ret = -1;

	mutex_lock(&pTAS2560->dev_lock);
	ret = tas2560_change_book(pTAS2560, chn, TAS2560_BOOK_ID(reg));
	if (ret >= 0)
		ret = tas2560_change_page(pTAS2560, chn, TAS2560_PAGE_ID(reg));

	dev_dbg(pTAS2560->dev, "%s: BOOK:PAGE:REG %u:%u:%u, len: 0x%02x\n",
		__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
		TAS2560_PAGE_REG(reg), nLength);

	if (ret >= 0) {
		if (chn == channel_left) {
			pTAS2560->client->addr = pTAS2560->mnLAddr;
			ret = regmap_bulk_read(pTAS2560->regmap, TAS2560_PAGE_REG(reg), pData, nLength);
			if (ret < 0)
				dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
							__FUNCTION__, __LINE__, ret);
		} else if (chn == channel_right) {
			pTAS2560->client->addr = pTAS2560->mnRAddr;
			ret = regmap_bulk_read(pTAS2560->regmap, TAS2560_PAGE_REG(reg), pData, nLength);
			if (ret < 0)
				dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
							__FUNCTION__, __LINE__, ret);
		} else
			dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
							__FUNCTION__, __LINE__, ret);
	}
	mutex_unlock(&pTAS2560->dev_lock);
	return ret;
}

static int tas2560_dev_update_bits(struct tas2560_priv *pTAS2560,
			enum channel chn, unsigned int reg,
			 unsigned int mask, unsigned int value)
{
	int ret = -1;

	mutex_lock(&pTAS2560->dev_lock);
	ret = tas2560_change_book(pTAS2560, chn, TAS2560_BOOK_ID(reg));
	if (ret >= 0)
		ret = tas2560_change_page(pTAS2560, chn, TAS2560_PAGE_ID(reg));

	dev_dbg(pTAS2560->dev, "%s: BOOK:PAGE:REG %u:%u:%u, mask: 0x%x, val=0x%x\n",
		__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
		TAS2560_PAGE_REG(reg), mask, value);

	if (ret >= 0) {
		if (chn&channel_left) {
			pTAS2560->client->addr = pTAS2560->mnLAddr;
			ret = regmap_update_bits(pTAS2560->regmap, TAS2560_PAGE_REG(reg), mask, value);
			if (ret < 0)
				dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
							__FUNCTION__, __LINE__, ret);
		}

		if (chn&channel_right) {
			pTAS2560->client->addr = pTAS2560->mnRAddr;
			ret = regmap_update_bits(pTAS2560->regmap, TAS2560_PAGE_REG(reg), mask, value);
			if (ret < 0)
				dev_err(pTAS2560->dev, "%s, line %d,ERROR %d\n",
							__FUNCTION__, __LINE__, ret);
		}

	}

	mutex_unlock(&pTAS2560->dev_lock);
	return ret;
}

static bool tas2560_volatile(struct device *dev, unsigned int reg)
{
	return false;
}

static bool tas2560_writeable(struct device *dev, unsigned int reg)
{
	return true;
}

static const struct regmap_config tas2560_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = tas2560_writeable,
	.volatile_reg = tas2560_volatile,
	.cache_type = REGCACHE_NONE,
	.max_register = 128,
};

static void tas2560_hw_reset(struct tas2560_priv *pTAS2560)
{
	if (gpio_is_valid(pTAS2560->mnResetGPIO1)) {
		gpio_direction_output(pTAS2560->mnResetGPIO1, 0);
		msleep(5);
		gpio_direction_output(pTAS2560->mnResetGPIO1, 1);
		msleep(1);
	}
	if (gpio_is_valid(pTAS2560->mnResetGPIO2)) {
		gpio_direction_output(pTAS2560->mnResetGPIO2, 0);
		msleep(5);
		gpio_direction_output(pTAS2560->mnResetGPIO2, 1);
		msleep(1);
	}

	pTAS2560->mnLCurrentBook = -1;
	pTAS2560->mnLCurrentPage = -1;
	pTAS2560->mnRCurrentBook = -1;
	pTAS2560->mnRCurrentPage = -1;
}

/*
static int tas2560_sw_reset(struct tas2560_priv *pTAS2560)
{
	int ret;
	ret = tas2560_dev_write(pTAS2560, channel_both, TAS2560_SW_RESET_REG, 0x01);
	if (ret < 0) {
		dev_err(pTAS2560->dev, "ERROR I2C comm, %d\n", ret);
		return ret;
	}

	udelay(100);
	return ret;
}
*/

void tas2560_clearIRQ(struct tas2560_priv *pTAS2560)
{
	unsigned int nValue;
	int nResult = 0;

	nResult = pTAS2560->read(pTAS2560, channel_left, TAS2560_FLAGS_1, &nValue);
	if (nResult >= 0)
		pTAS2560->read(pTAS2560, channel_left, TAS2560_FLAGS_2, &nValue);

	nResult = pTAS2560->read(pTAS2560, channel_right, TAS2560_FLAGS_1, &nValue);
	if (nResult >= 0)
		pTAS2560->read(pTAS2560, channel_right, TAS2560_FLAGS_2, &nValue);
}

void tas2560_enableIRQ(struct tas2560_priv *pTAS2560, bool enable)
{
	if (enable) {
		if (pTAS2560->mbIRQEnable)
			return;

		if (gpio_is_valid(pTAS2560->mnIRQGPIO1))
			enable_irq(pTAS2560->mnIRQ1);
		if (gpio_is_valid(pTAS2560->mnIRQGPIO2))
			enable_irq(pTAS2560->mnIRQ2);
		schedule_delayed_work(&pTAS2560->irq_work, msecs_to_jiffies(10));
		pTAS2560->mbIRQEnable = true;
	} else {
		if (gpio_is_valid(pTAS2560->mnIRQGPIO1))
			disable_irq_nosync(pTAS2560->mnIRQ1);
		if (gpio_is_valid(pTAS2560->mnIRQGPIO2))
			disable_irq_nosync(pTAS2560->mnIRQ2);

		pTAS2560->mbIRQEnable = false;
	}
}

static bool irq_print(struct tas2560_priv *pTAS2560, int channel)
{
	unsigned int nDevInt1Status = 0, nDevInt2Status = 0;
	int nCounter = 2;
	int nResult = 0;

	dev_info(pTAS2560->dev, "%s", __func__);
	nResult = tas2560_dev_read(pTAS2560, channel, TAS2560_FLAGS_1, &nDevInt1Status);
	if (nResult >= 0)
		nResult = tas2560_dev_read(pTAS2560, channel, TAS2560_FLAGS_2, &nDevInt2Status);
	else
		return true;

	dev_info(pTAS2560->dev, "irq error: 0x%x, 0x%x", nDevInt1Status, nDevInt2Status);
	if (((nDevInt1Status & 0xfc) != 0) || ((nDevInt2Status & 0xc0) != 0)) {
		/* in case of INT_OC, INT_UV, INT_OT, INT_BO, INT_CL, INT_CLK1, INT_CLK2 */
		dev_err(pTAS2560->dev, "IRQ critical Error : 0x%x, 0x%x\n",
			nDevInt1Status, nDevInt2Status);

		if (nDevInt1Status & 0x80) {
			pTAS2560->mnErrCode |= ERROR_OVER_CURRENT;
			dev_err(pTAS2560->dev, "SPK over current!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_OVER_CURRENT;

		if (nDevInt1Status & 0x40) {
			pTAS2560->mnErrCode |= ERROR_UNDER_VOLTAGE;
			dev_err(pTAS2560->dev, "SPK under voltage!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_UNDER_VOLTAGE;

		if (nDevInt1Status & 0x20) {
			pTAS2560->mnErrCode |= ERROR_CLK_HALT;
			dev_err(pTAS2560->dev, "clk halted!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_CLK_HALT;

		if (nDevInt1Status & 0x10) {
			pTAS2560->mnErrCode |= ERROR_DIE_OVERTEMP;
			dev_err(pTAS2560->dev, "die over temperature!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_DIE_OVERTEMP;

		if (nDevInt1Status & 0x08) {
			pTAS2560->mnErrCode |= ERROR_BROWNOUT;
			dev_err(pTAS2560->dev, "brownout!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_BROWNOUT;

		if (nDevInt1Status & 0x04) {
			pTAS2560->mnErrCode |= ERROR_CLK_LOST;
		} else
			pTAS2560->mnErrCode &= ~ERROR_CLK_LOST;

		if (nDevInt2Status & 0x80) {
			pTAS2560->mnErrCode |= ERROR_CLK_DET1;
			dev_err(pTAS2560->dev, "clk detection 1!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_CLK_DET1;

		if (nDevInt2Status & 0x40) {
			pTAS2560->mnErrCode |= ERROR_CLK_DET2;
			dev_err(pTAS2560->dev, "clk detection 2!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_CLK_DET2;

		return true;
	} else {
		dev_dbg(pTAS2560->dev, "IRQ status : 0x%x, 0x%x\n",
				nDevInt1Status, nDevInt2Status);
		nCounter = 2;

		while (nCounter > 0) {
			nResult = tas2560_dev_read(pTAS2560, channel, TAS2560_POWER_UP_FLAG_REG, &nDevInt1Status);
			if (nResult < 0)
				return true;

			if ((nDevInt1Status & 0xc0) == 0xc0)
				break;

			nCounter--;
			if (nCounter > 0) {
				/* in case check pow status just after power on TAS2560 */
				dev_dbg(pTAS2560->dev, "PowSts B: 0x%x, check again after 10ms\n",
					nDevInt1Status);
				msleep(10);
			}
		}

		if ((nDevInt1Status & 0xc0) != 0xc0) {
			dev_err(pTAS2560->dev, "%s, Critical ERROR B[%d]_P[%d]_R[%d]= 0x%x\n",
				__func__,
				TAS2560_BOOK_ID(TAS2560_POWER_UP_FLAG_REG),
				TAS2560_PAGE_ID(TAS2560_POWER_UP_FLAG_REG),
				TAS2560_PAGE_REG(TAS2560_POWER_UP_FLAG_REG),
				nDevInt1Status);
			pTAS2560->mnErrCode |= ERROR_CLASSD_PWR;
			return true;
		}
		pTAS2560->mnErrCode &= ~ERROR_CLASSD_PWR;
	}
	return false;
}
static void irq_work_routine(struct work_struct *work)
{
	struct tas2560_priv *pTAS2560 =
		container_of(work, struct tas2560_priv, irq_work.work);
	enum channel mchannel;

#ifdef CONFIG_TAS2560_CODEC_STEREO
	mutex_lock(&pTAS2560->codec_lock);
#endif
	dev_info(pTAS2560->dev, "enter %s\n", __func__);

#ifdef CONFIG_TAS2560_MISC_STEREO
	mutex_lock(&pTAS2560->file_lock);
#endif
	if (pTAS2560->mbRuntimeSuspend) {
		dev_info(pTAS2560->dev, "%s, Runtime Suspended\n", __func__);
		goto end;
	}

	if((pTAS2560->mbPowerUp[0]) && (pTAS2560->mbPowerUp[1])){
		mchannel = channel_both;
	}
	else if(pTAS2560->mbPowerUp[0]){
		mchannel = channel_left;
	}
	else if(pTAS2560->mbPowerUp[1]){
		mchannel = channel_right;
	}
	else
	{
		dev_err(pTAS2560->dev, "%s, device not powered\n", __func__);
		goto end;
	}

	/*needs to reset*/
	if((mchannel & channel_left) && irq_print(pTAS2560, channel_left))
		goto reload;
	if((mchannel & channel_right) && irq_print(pTAS2560, channel_right))
		goto reload;

	goto end;

reload:
	/* hardware reset and reload */
	tas2560_LoadConfig(pTAS2560, true, mchannel);

end:
	if (!hrtimer_active(&pTAS2560->mtimer)) {
		dev_dbg(pTAS2560->dev, "%s, start timer\n", __func__);
		hrtimer_start(&pTAS2560->mtimer,
			ns_to_ktime((u64)CHECK_PERIOD * NSEC_PER_MSEC), HRTIMER_MODE_REL);
	}
#ifdef CONFIG_TAS2560_MISC_STEREO
	mutex_unlock(&pTAS2560->file_lock);
#endif

#ifdef CONFIG_TAS2560_CODEC_STEREO
	mutex_unlock(&pTAS2560->codec_lock);
#endif
}

static enum hrtimer_restart timer_func(struct hrtimer *timer)
{
	struct tas2560_priv *pTAS2560 = container_of(timer, struct tas2560_priv, mtimer);

	if (pTAS2560->mbPowerUp) {
		if (!delayed_work_pending(&pTAS2560->irq_work))
			schedule_delayed_work(&pTAS2560->irq_work, msecs_to_jiffies(20));
	}

	return HRTIMER_NORESTART;
}

static irqreturn_t tas2560_irq_handler(int irq, void *dev_id)
{
	struct tas2560_priv *pTAS2560 = (struct tas2560_priv *)dev_id;

	tas2560_enableIRQ(pTAS2560, false);

	/* get IRQ status after 100 ms */
	if (!delayed_work_pending(&pTAS2560->irq_work))
		schedule_delayed_work(&pTAS2560->irq_work, msecs_to_jiffies(100));

	return IRQ_HANDLED;
}

static int tas2560_runtime_suspend(struct tas2560_priv *pTAS2560)
{
	dev_dbg(pTAS2560->dev, "%s\n", __func__);

	pTAS2560->mbRuntimeSuspend = true;

	if (hrtimer_active(&pTAS2560->mtimer)) {
		dev_dbg(pTAS2560->dev, "cancel die temp timer\n");
		hrtimer_cancel(&pTAS2560->mtimer);
	}

	if (delayed_work_pending(&pTAS2560->irq_work)) {
		dev_dbg(pTAS2560->dev, "cancel IRQ work\n");
		cancel_delayed_work_sync(&pTAS2560->irq_work);
	}

	return 0;
}

static int tas2560_runtime_resume(struct tas2560_priv *pTAS2560)
{
	dev_dbg(pTAS2560->dev, "%s\n", __func__);

	if (pTAS2560->mbPowerUp) {
		if (!hrtimer_active(&pTAS2560->mtimer)) {
			dev_dbg(pTAS2560->dev, "%s, start check timer\n", __func__);
			hrtimer_start(&pTAS2560->mtimer,
				ns_to_ktime((u64)CHECK_PERIOD * NSEC_PER_MSEC), HRTIMER_MODE_REL);
		}
	}

	pTAS2560->mbRuntimeSuspend = false;

	return 0;
}

static int tas2560_i2c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct tas2560_priv *pTAS2560;
	int nResult;

	dev_info(&client->dev, "%s enter\n", __func__);

	pTAS2560 = devm_kzalloc(&client->dev, sizeof(struct tas2560_priv), GFP_KERNEL);
	if (pTAS2560 == NULL) {
		dev_err(&client->dev, "%s, -ENOMEM \n", __func__);
		nResult = -ENOMEM;
		goto end;
	}

	pTAS2560->dev = &client->dev;
	pTAS2560->client = client;
	i2c_set_clientdata(client, pTAS2560);
	dev_set_drvdata(&client->dev, pTAS2560);

	pTAS2560->regmap = devm_regmap_init_i2c(client, &tas2560_i2c_regmap);
	if (IS_ERR(pTAS2560->regmap)) {
		nResult = PTR_ERR(pTAS2560->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
					nResult);
		goto end;
	}

	if (client->dev.of_node)
		tas2560_parse_dt(&client->dev, pTAS2560);

	if (gpio_is_valid(pTAS2560->mnResetGPIO1))
		nResult = gpio_request(pTAS2560->mnResetGPIO1, "TAS2560_RESET1");
	if (nResult) {
		dev_err(pTAS2560->dev, "%s: Failed to request gpio %d\n", __func__,
			pTAS2560->mnResetGPIO1);
		nResult = -EINVAL;
		goto free_gpio;
	}

	if (gpio_is_valid(pTAS2560->mnResetGPIO2))
		nResult = gpio_request(pTAS2560->mnResetGPIO2, "TAS2560_RESET2");
	if (nResult) {
		dev_err(pTAS2560->dev, "%s: Failed to request gpio %d\n", __func__,
			pTAS2560->mnResetGPIO2);
		nResult = -EINVAL;
		goto free_gpio;
	}

#if 0   // move to end of func
	if (gpio_is_valid(pTAS2560->mnIRQGPIO1)) {
		nResult = gpio_request(pTAS2560->mnIRQGPIO1, "TAS2560-IRQ1");
		if (nResult < 0) {
			dev_err(pTAS2560->dev, "%s: GPIO %d request error\n",
				__func__, pTAS2560->mnIRQGPIO1);
			goto free_gpio;
		}
		gpio_direction_input(pTAS2560->mnIRQGPIO1);
		pTAS2560->mnIRQ1 = gpio_to_irq(pTAS2560->mnIRQGPIO1);
		dev_info(pTAS2560->dev, "irq1 = %d\n", pTAS2560->mnIRQ1);

		nResult = request_threaded_irq(pTAS2560->mnIRQ1, tas2560_irq_handler,
					NULL, IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					client->name, pTAS2560);
		if (nResult < 0) {
			dev_err(pTAS2560->dev,
				"request_irq failed, %d\n", nResult);
			goto free_gpio;
		}
		disable_irq_nosync(pTAS2560->mnIRQ1);
		INIT_DELAYED_WORK(&pTAS2560->irq_work, irq_work_routine);
	}
	else
		dev_err(pTAS2560->dev, "irq1 GPIO failed");
	if (gpio_is_valid(pTAS2560->mnIRQGPIO2)) {
		nResult = gpio_request(pTAS2560->mnIRQGPIO2, "TAS2560-IRQ2");
		if (nResult < 0) {
			dev_err(pTAS2560->dev, "%s: GPIO2 %d request error\n",
				__func__, pTAS2560->mnIRQGPIO2);
			goto free_gpio;
		}
		gpio_direction_input(pTAS2560->mnIRQGPIO2);
		pTAS2560->mnIRQ2 = gpio_to_irq(pTAS2560->mnIRQGPIO2);
		dev_info(pTAS2560->dev, "irq2 = %d\n", pTAS2560->mnIRQ2);

		nResult = request_threaded_irq(pTAS2560->mnIRQ2, tas2560_irq_handler,
					NULL, IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					client->name, pTAS2560);
		if (nResult < 0) {
			dev_err(pTAS2560->dev,
				"request_irq failed, %d\n", nResult);
			goto free_gpio;
		}
		disable_irq_nosync(pTAS2560->mnIRQ2);
		INIT_DELAYED_WORK(&pTAS2560->irq_work, irq_work_routine);
	}
	else
		dev_err(pTAS2560->dev, "irq GPIO failed");
#endif

	pTAS2560->read = tas2560_dev_read;
	pTAS2560->write = tas2560_dev_write;
	pTAS2560->bulk_read = tas2560_dev_bulk_read;
	pTAS2560->bulk_write = tas2560_dev_bulk_write;
	pTAS2560->update_bits = tas2560_dev_update_bits;
	pTAS2560->hw_reset = tas2560_hw_reset;
	pTAS2560->enableIRQ = tas2560_enableIRQ;
	pTAS2560->clearIRQ = tas2560_clearIRQ;
	pTAS2560->runtime_suspend = tas2560_runtime_suspend;
	pTAS2560->runtime_resume = tas2560_runtime_resume;
	mutex_init(&pTAS2560->dev_lock);
	dev_info(&client->dev, "%s tas2560_LoadConfig\n", __func__);
	nResult = tas2560_LoadConfig(pTAS2560, false, channel_both);
	if (nResult < 0)
		goto destroy_mutex;
	dev_info(&client->dev, "%s tas2560_LoadConfig out\n", __func__);
#ifdef CONFIG_TAS2560_CODEC_STEREO
	mutex_init(&pTAS2560->codec_lock);
	tas2560_register_codec(pTAS2560);
#endif

#ifdef CONFIG_TAS2560_MISC_STEREO
	mutex_init(&pTAS2560->file_lock);
	tas2560_register_misc(pTAS2560);
#endif

	hrtimer_init(&pTAS2560->mtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pTAS2560->mtimer.function = timer_func;

#if 1
	if (gpio_is_valid(pTAS2560->mnIRQGPIO1)) {
		nResult = gpio_request(pTAS2560->mnIRQGPIO1, "TAS2560-IRQ1");
		if (nResult < 0) {
			dev_err(pTAS2560->dev, "%s: GPIO %d request error\n",
				__func__, pTAS2560->mnIRQGPIO1);
			goto free_gpio;
		}
		gpio_direction_input(pTAS2560->mnIRQGPIO1);
		pTAS2560->mnIRQ1 = gpio_to_irq(pTAS2560->mnIRQGPIO1);
		dev_info(pTAS2560->dev, "irq1 = %d\n", pTAS2560->mnIRQ1);

		INIT_DELAYED_WORK(&pTAS2560->irq_work, irq_work_routine);
		nResult = request_threaded_irq(pTAS2560->mnIRQ1, tas2560_irq_handler,
					NULL, IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					client->name, pTAS2560);
		if (nResult < 0) {
			dev_err(pTAS2560->dev,
				"request_irq failed, %d\n", nResult);
			goto free_gpio;
		}
		disable_irq_nosync(pTAS2560->mnIRQ1);
	}
	else
		dev_err(pTAS2560->dev, "irq1 GPIO failed");
	if (gpio_is_valid(pTAS2560->mnIRQGPIO2)) {
		nResult = gpio_request(pTAS2560->mnIRQGPIO2, "TAS2560-IRQ2");
		if (nResult < 0) {
			dev_err(pTAS2560->dev, "%s: GPIO2 %d request error\n",
				__func__, pTAS2560->mnIRQGPIO2);
			goto free_gpio;
		}
		gpio_direction_input(pTAS2560->mnIRQGPIO2);
		pTAS2560->mnIRQ2 = gpio_to_irq(pTAS2560->mnIRQGPIO2);
		dev_info(pTAS2560->dev, "irq2 = %d\n", pTAS2560->mnIRQ2);

		INIT_DELAYED_WORK(&pTAS2560->irq_work, irq_work_routine);
		nResult = request_threaded_irq(pTAS2560->mnIRQ2, tas2560_irq_handler,
					NULL, IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					client->name, pTAS2560);
		if (nResult < 0) {
			dev_err(pTAS2560->dev,
				"request_irq failed, %d\n", nResult);
			goto free_gpio;
		}
		disable_irq_nosync(pTAS2560->mnIRQ2);
	}
	else
		dev_err(pTAS2560->dev, "irq GPIO failed");
#endif
destroy_mutex:
	if (nResult < 0)
		mutex_destroy(&pTAS2560->dev_lock);

free_gpio:
	if (nResult < 0) {
		if (gpio_is_valid(pTAS2560->mnResetGPIO1))
			gpio_free(pTAS2560->mnResetGPIO1);
		if (gpio_is_valid(pTAS2560->mnResetGPIO2))
			gpio_free(pTAS2560->mnResetGPIO2);
		if (gpio_is_valid(pTAS2560->mnIRQGPIO1))
			gpio_free(pTAS2560->mnIRQGPIO1);
		if (gpio_is_valid(pTAS2560->mnIRQGPIO2))
			gpio_free(pTAS2560->mnIRQGPIO2);
	}

end:
	return nResult;
}

static int tas2560_i2c_remove(struct i2c_client *client)
{
	struct tas2560_priv *pTAS2560 = i2c_get_clientdata(client);

	dev_info(pTAS2560->dev, "%s\n", __func__);

#ifdef CONFIG_TAS2560_CODEC_STEREO
	tas2560_deregister_codec(pTAS2560);
	mutex_destroy(&pTAS2560->codec_lock);
#endif

#ifdef CONFIG_TAS2560_MISC_STEREO
	tas2560_deregister_misc(pTAS2560);
	mutex_destroy(&pTAS2560->file_lock);
#endif

	if (gpio_is_valid(pTAS2560->mnResetGPIO1))
		gpio_free(pTAS2560->mnResetGPIO1);
	if (gpio_is_valid(pTAS2560->mnResetGPIO2))
		gpio_free(pTAS2560->mnResetGPIO2);
	if (gpio_is_valid(pTAS2560->mnIRQGPIO1))
		gpio_free(pTAS2560->mnIRQGPIO1);
	if (gpio_is_valid(pTAS2560->mnIRQGPIO2))
		gpio_free(pTAS2560->mnIRQGPIO2);
	return 0;
}


static const struct i2c_device_id tas2560_i2c_id[] = {
	{ "tas2560s", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas2560_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id tas2560_of_match[] = {
	{ .compatible = "ti,tas2560s" },
	{},
};
MODULE_DEVICE_TABLE(of, tas2560_of_match);
#endif


static struct i2c_driver tas2560_i2c_driver = {
	.driver = {
		.name   = "tas2560s",
		.owner  = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(tas2560_of_match),
#endif
	},
	.probe      = tas2560_i2c_probe,
	.remove     = tas2560_i2c_remove,
	.id_table   = tas2560_i2c_id,
};

module_i2c_driver(tas2560_i2c_driver);

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TAS2560 I2C Smart Amplifier driver");
MODULE_LICENSE("GPL v2");
#endif
