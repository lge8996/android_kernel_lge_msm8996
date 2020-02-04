/* touch_ft5726.c
 *
 * Copyright (C) 2015 LGE.
 *
 * Author: hyokmin.kwon@lge.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#define TS_MODULE "[ft5726]"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/dma-mapping.h>
#include <linux/async.h>

//#include <mach/board_lge.h>
//#include <linux/i2c.h>
//#include <touch_i2c.h>

/*
 *  Include to touch core Header File
 */
#include <touch_core.h>
#include <touch_hwif.h>

/*
 *  Include to Local Header File
 */
#include "touch_ft5726.h"
#include "touch_ft5726_prd.h"

static const char *tci_debug_type_str[TCI_DEBUG_TYPE_NUM] = {
	"Disable Type",
	"Always Report Type",
	"Buffer Type",
	"Buffer and Always Report Type"
};

static const char *tci_debug_str[TCI_FR_NUM + 1] = {
	"NONE",
	"DISTANCE_INTER_TAP",
	"DISTANCE_TOUCHSLOP",
	"TIMEOUT_INTER_TAP",
	"MULTI_FINGER",
	"DELAY_TIME", /* It means Over Tap */
	"PALM_STATE",
	"Reserved" // Invalid data
};

int ft5726_reg_read(struct device *dev, u16 addr, void *data, int size)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	struct touch_bus_msg msg = {0, };
	int ret = 0;

	mutex_lock(&d->rw_lock);

	ts->tx_buf[0] = addr;

	msg.tx_buf = ts->tx_buf;
	msg.tx_size = 1;

	msg.rx_buf = ts->rx_buf;
	msg.rx_size = size;

	ret = touch_bus_read(dev, &msg);

	if (ret < 0) {
		TOUCH_E("touch bus read error : %d\n", ret);
		mutex_unlock(&d->rw_lock);
		return ret;
	}

	memcpy(data, &ts->rx_buf[0], size);
	mutex_unlock(&d->rw_lock);
	return 0;

}

int ft5726_reg_write(struct device *dev, u16 addr, void *data, int size)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	struct touch_bus_msg msg = {0, };
	int ret = 0;

	mutex_lock(&d->rw_lock);

	ts->tx_buf[0] = addr;
	memcpy(&ts->tx_buf[1], data, size);

	msg.tx_buf = ts->tx_buf;
	msg.tx_size = size + 1;
	msg.rx_buf = NULL;
	msg.rx_size = 0;

	ret = touch_bus_write(dev, &msg);

	if (ret < 0) {
		TOUCH_E("touch bus write error : %d\n", ret);
		mutex_unlock(&d->rw_lock);
		return ret;
	}

	mutex_unlock(&d->rw_lock);

	return 0;
}

static void ft5726_get_tci_info(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);

	TOUCH_TRACE();

	ts->tci.info[TCI_1].tap_count = 2;		// Total Tap Count (2)
	ts->tci.info[TCI_1].min_intertap = 0;	// Time Gap Min (0ms)
	ts->tci.info[TCI_1].max_intertap = 50;	// Time Gap Max (500ms)
	ts->tci.info[TCI_1].touch_slop = 5;	// Touch Slop (5mm)
	ts->tci.info[TCI_1].tap_distance = 10;	// Touch Distance (10mm)
	ts->tci.info[TCI_1].intr_delay = 0;		// Interrupt Delay (500ms or 0ms)

	ts->tci.info[TCI_2].tap_count = 6;
	ts->tci.info[TCI_2].min_intertap = 0;
	ts->tci.info[TCI_2].max_intertap = 50;
	ts->tci.info[TCI_2].touch_slop = 10;
	ts->tci.info[TCI_2].tap_distance = 255;
	ts->tci.info[TCI_2].intr_delay = 20;

	d->lpwg_area.low_byte_x1 = (u8)(LPWG_AREA_MARGIN & 0xff);
	d->lpwg_area.high_byte_x1 = (u8)(LPWG_AREA_MARGIN >> 8);

	d->lpwg_area.low_byte_x2 = (u8)((ts->caps.max_x - LPWG_AREA_MARGIN) & 0xff);
	d->lpwg_area.high_byte_x2 = (u8)((ts->caps.max_x - LPWG_AREA_MARGIN) >> 8);

	d->lpwg_area.low_byte_y1 = (u8)(LPWG_AREA_MARGIN & 0xff);
	d->lpwg_area.high_byte_y1 = (u8)(LPWG_AREA_MARGIN >> 8);

	d->lpwg_area.low_byte_y2 = (u8)((ts->caps.max_y - LPWG_AREA_MARGIN) & 0xff);
	d->lpwg_area.high_byte_y2 = (u8)((ts->caps.max_y - LPWG_AREA_MARGIN) >> 8);

}

int ft5726_ic_info(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	int ret = 0;
	u8 chip_id = 0;
	u8 chip_id_low = 0;
	u8 fw_vendor_id = 0;
	u8 fw_version = 0;
	int i;

	TOUCH_TRACE();

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_E("IC state is deep sleep.");
		return 0;
	}

	// If it is failed to get info without error, just return error
	for (i = 0; i < 2; i++) {
		ret = ft5726_reg_read(dev, FTS_REG_ID, (u8 *)&chip_id, 1);
		ret |= ft5726_reg_read(dev, FTS_REG_ID_LOW, (u8 *)&chip_id_low, 1);
		ret |= ft5726_reg_read(dev, FTS_REG_FW_VER, (u8 *)&fw_version, 1);
		ret |= ft5726_reg_read(dev, FTS_REG_FW_VENDOR_ID, (u8 *)&fw_vendor_id, 1);
		ret |= ft5726_reg_read(dev, FTS_REG_PRODUCT_ID_H, &(d->ic_info.product_id[0]), 1);
		ret |= ft5726_reg_read(dev, FTS_REG_PRODUCT_ID_L, &(d->ic_info.product_id[1]), 1);

		if (ret == 0) {
			TOUCH_I("Success to get ic info data\n");
			break;
		}
	}

	if (i >= 2) {
		TOUCH_E("Failed to get ic info data\n");
		return -EPERM; // Do nothing in caller
	}

	d->ic_info.version.major = (fw_version & 0x80) >> 7;
	d->ic_info.version.minor = fw_version & 0x7F;
	d->ic_info.chip_id = chip_id; // Device ID
	d->ic_info.chip_id_low = chip_id_low;
	d->ic_info.fw_vendor_id = fw_vendor_id; // Vendor ID
	d->ic_info.info_valid = 1;

	TOUCH_I("==================== Version Info ====================\n");
	TOUCH_I("Version: v%d.%02d\n", d->ic_info.version.major, d->ic_info.version.minor);
	TOUCH_I("Chip_id: %x / Chip_id_low : %x\n", d->ic_info.chip_id, d->ic_info.chip_id_low);
	TOUCH_I("Vendor_id: %x\n", d->ic_info.fw_vendor_id);
	TOUCH_I("Product_id: FT%x%x\n", d->ic_info.product_id[0], d->ic_info.product_id[1]);
	TOUCH_I("======================================================\n");

	return ret;
}

void ft5726_grip_info(struct device *dev)
{
	struct ft5726_data *d = to_ft5726_data(dev);

	TOUCH_TRACE();

	d->grip_x = (u8)GRIP_AREA_X;
	d->grip_y = (u8)GRIP_AREA_Y;

	TOUCH_I("%s - x: %d*0.1mm, y: %d*0.1mm\n", __func__, d->grip_x, d->grip_y);

	return;
}

static int ft5726_tci_control(struct device *dev, int type)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	struct tci_info *info1 = &ts->tci.info[TCI_1];
	struct tci_info *info2 = &ts->tci.info[TCI_2];
	u8 data;
	int ret = 0;

	TOUCH_TRACE();

	switch (type) {
	case TCI_CTRL_SET:

		data = ts->tci.mode;
		ret = ft5726_reg_write(dev, LGE_GESTURE_EN, &data, 1);
		break;

	case TCI_CTRL_CONFIG_COMMON:

		data = (((d->tci_debug_type) & TCI_DEBUG_BUFFER) << 3) | ((d->tci_debug_type) & TCI_DEBUG_ALWAYS);
		ret = ft5726_reg_write(dev, LPWG_DEBUG_ENABLE, &data, 1);	// Fail Reason Debug Function Enable

		data = d->lpwg_area.low_byte_x1;
		ret |= ft5726_reg_write(dev, LGE_GESTURE_X1_LOW, &data, 1);
		data = d->lpwg_area.high_byte_x1;
		ret |= ft5726_reg_write(dev, LGE_GESTURE_X1_HIGH, &data, 1);
		data = d->lpwg_area.low_byte_x2;
		ret |= ft5726_reg_write(dev, LGE_GESTURE_X2_LOW, &data, 1);
		data = d->lpwg_area.high_byte_x2;
		ret |= ft5726_reg_write(dev, LGE_GESTURE_X2_HIGH, &data, 1);
		data = d->lpwg_area.low_byte_y1;
		ret |= ft5726_reg_write(dev, LGE_GESTURE_y1_LOW, &data, 1);
		data = d->lpwg_area.high_byte_y1;
		ret |= ft5726_reg_write(dev, LGE_GESTURE_y1_HIGH, &data, 1);
		data = d->lpwg_area.low_byte_y2;
		ret |= ft5726_reg_write(dev, LGE_GESTURE_y2_LOW, &data, 1);
		data = d->lpwg_area.high_byte_y2;
		ret |= ft5726_reg_write(dev, LGE_GESTURE_y2_HIGH, &data, 1);
		break;

	case TCI_CTRL_CONFIG_TCI_1:
		data = (u8)(info1->touch_slop);
		ret = ft5726_reg_write(dev, KNOCK_ON_SLOP, &data, 1);
		data = (u8)(info1->tap_distance);
		ret |= ft5726_reg_write(dev, KNOCK_ON_DIST, &data, 1);
		data = (u8)(info1->min_intertap);
		ret |= ft5726_reg_write(dev, KNOCK_ON_TIME_GAP_MIN, &data, 1); //temp, bring up
		data = (u8)(info1->max_intertap);
		ret |= ft5726_reg_write(dev, KNOCK_ON_TIME_GAP_MAX, &data, 1);
		data = (u8)(info1->tap_count);
		ret |= ft5726_reg_write(dev, KNOCK_ON_CNT, &data, 1);
		data = (u8)(info1->intr_delay);
		ret |= ft5726_reg_write(dev, KNOCK_ON_DELAY, &data, 1);
		break;

	case TCI_CTRL_CONFIG_TCI_2:
		data = (u8)(info2->touch_slop);
		ret = ft5726_reg_write(dev, KNOCK_CODE_SLOP, &data, 1);
		data = (u8)(info2->tap_distance);
		ret |= ft5726_reg_write(dev, KNOCK_CODE_DIST, &data, 1);
		data = (u8)(info1->min_intertap);
		ret |= ft5726_reg_write(dev, KNOCK_CODE_TIME_GAP_MIN, &data, 1); //temp, bring up
		data = (u8)(info2->max_intertap);
		ret |= ft5726_reg_write(dev, KNOCK_CODE_TIME_GAP_MAX, &data, 1);
		data = (u8)(info2->tap_count);
		ret |= ft5726_reg_write(dev, KNOCK_CODE_CNT, &data, 1);
		data = (u8)(info2->intr_delay);
		ret |= ft5726_reg_write(dev, KNOCK_CODE_DELAY, &data, 1);
		break;

	default:
		break;
	}

	return ret;
}


static int ft5726_lpwg_control(struct device *dev, u8 mode)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct tci_info *info1 = &ts->tci.info[TCI_1];
	//struct ft5726_data *d = to_ft5726_data(dev);
	int ret = 0;

	TOUCH_TRACE();
	switch (mode) {

	case LPWG_DOUBLE_TAP:
		ts->tci.mode = 0x01;
		info1->intr_delay = 0;

		ret = ft5726_tci_control(dev, TCI_CTRL_CONFIG_TCI_1);
		ret |= ft5726_tci_control(dev, TCI_CTRL_CONFIG_COMMON);
		ret |= ft5726_tci_control(dev, TCI_CTRL_SET);

		break;

	case LPWG_PASSWORD:
		ts->tci.mode = 0x03;
		info1->intr_delay = ts->tci.double_tap_check ? 50 : 0;

		ret = ft5726_tci_control(dev, TCI_CTRL_CONFIG_TCI_1);
		ret |= ft5726_tci_control(dev, TCI_CTRL_CONFIG_TCI_2);
		ret |= ft5726_tci_control(dev, TCI_CTRL_CONFIG_COMMON);
		ret |= ft5726_tci_control(dev, TCI_CTRL_SET);

		break;

	case LPWG_PASSWORD_ONLY:
		ts->tci.mode = 0x02;
		info1->intr_delay = 0;

		//ret = ft5726_tci_control(dev, TCI_CTRL_CONFIG_TCI_1);
		ret |= ft5726_tci_control(dev, TCI_CTRL_CONFIG_TCI_2);
		ret |= ft5726_tci_control(dev, TCI_CTRL_CONFIG_COMMON);
		ret |= ft5726_tci_control(dev, TCI_CTRL_SET);

		break;

	default:
		ts->tci.mode = 0;

		ret = ft5726_tci_control(dev, TCI_CTRL_SET);

		break;
	}

	TOUCH_I("ft5726_lpwg_control mode = %d\n", mode);

	return ret;
}

static int ft5726_lpwg_mode(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	int ret = 0;

	TOUCH_TRACE();

	if (atomic_read(&d->init) == IC_INIT_NEED) {
		TOUCH_I("%s: Not Ready, Need IC init\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.fb) == FB_SUSPEND) {
		if (ts->mfts_lpwg) {
			/* Forced lpwg set in minios suspend mode */
			ret = ft5726_lpwg_control(dev, LPWG_DOUBLE_TAP);
			if (ret < 0) {
				TOUCH_E("failed to set lpwg control (ret: %d)\n", ret);
				goto error;
			}
			return 0;
		}

		if (ts->lpwg.screen) {
			TOUCH_I("Skip lpwg_mode\n");
			ft5726_report_tci_fr_buffer(dev); // Report fr before touch IC reset
			if (ret < 0) {
				TOUCH_E("failed to print lpwg debug (ret: %d)\n", ret);
				goto error;
			}
		} else if (ts->lpwg.sensor == PROX_NEAR) {
			TOUCH_I("suspend sensor == PROX_NEAR\n");
			ret = ft5726_sleep_control(dev, IC_DEEP_SLEEP);
			if (ret < 0) {
				TOUCH_E("failed to set IC_DEEP_SLEEP (ret: %d)\n", ret);
				goto error;
			}
		} else if (ts->lpwg.qcover == HALL_NEAR) {
			TOUCH_I("Qcover == HALL_NEAR\n");
			ret = ft5726_sleep_control(dev, IC_DEEP_SLEEP);
			if (ret < 0) {
				TOUCH_E("failed to set IC_DEEP_SLEEP (ret: %d)\n", ret);
				goto error;
			}
		} else {
			/* Knock On Case */
			if (ts->lpwg.mode == LPWG_NONE) {
				ret = ft5726_sleep_control(dev, IC_DEEP_SLEEP);
				if (ret < 0) {
					TOUCH_E("failed to set IC_DEEP_SLEEP (ret: %d)\n", ret);
					goto error;
				}
			} else {
				ret = ft5726_sleep_control(dev, IC_NORMAL);
				if (ret < 0) {
					TOUCH_E("skip below functions, lpwg_mode is already Done\n");
					goto error;
				}
				ret = ft5726_lpwg_control(dev, ts->lpwg.mode);
				if (ret < 0) {
					TOUCH_E("failed to set lpwg control (ret: %d)\n", ret);
					goto error;
				}
			}
		}
		return 0;
	}

	/* resume */
	touch_report_all_event(ts);
	if (ts->lpwg.screen) {
		/* normal */
		TOUCH_I("resume ts->lpwg.screen on\n");
		ret = ft5726_sleep_control(dev, IC_NORMAL);
		if (ret < 0) {
			TOUCH_E("skip below functions, lpwg_mode is already Done\n");
			goto error;
		}
		ret = ft5726_lpwg_control(dev, LPWG_NONE);
		if (ret < 0) {
			TOUCH_E("failed to set lpwg control (ret: %d)\n", ret);
			goto error;
		}
	} else if (ts->lpwg.sensor == PROX_NEAR) {
		/* wake up */
		TOUCH_I("resume ts->lpwg.sensor == PROX_NEAR\n");
		ret = ft5726_sleep_control(dev, IC_DEEP_SLEEP);
		if (ret < 0) {
			TOUCH_E("failed to set IC_DEEP_SLEEP (ret: %d)\n", ret);
			goto error;
		}
	} else if (ts->lpwg.qcover == HALL_NEAR) {
		TOUCH_I("resume ts->lpwg.qcover == HALL_NEAR\n");
		ret = ft5726_sleep_control(dev, IC_DEEP_SLEEP);
		if (ret < 0) {
			TOUCH_E("failed to set IC_DEEP_SLEEP (ret: %d)\n", ret);
			goto error;
		}
	} else {
		/* partial */
		TOUCH_I("resume Partial\n");
		ret = ft5726_lpwg_control(dev, ts->lpwg.mode);
		if (ret < 0) {
			TOUCH_E("failed to set lpwg control (ret: %d)\n", ret);
			goto error;
		}
	}

error:
	return ret;

}

static int ft5726_lpwg(struct device *dev, u32 code, void *param)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int *value = (int *)param;

	TOUCH_TRACE();

	switch (code) {
	case LPWG_ACTIVE_AREA:
		ts->tci.area.x1 = value[0];
		ts->tci.area.x2 = value[1];
		ts->tci.area.y1 = value[2];
		ts->tci.area.y2 = value[3];
		TOUCH_I("LPWG_ACTIVE_AREA: x0[%d], x1[%d], x2[%d], x3[%d]\n",
				value[0], value[1], value[2], value[3]);
		break;

	case LPWG_TAP_COUNT:
		ts->tci.info[TCI_2].tap_count = value[0];
		TOUCH_I("LPWG_TAP_COUNT: [%d]\n", value[0]);
		break;

	case LPWG_DOUBLE_TAP_CHECK:
		ts->tci.double_tap_check = value[0];
		TOUCH_I("LPWG_DOUBLE_TAP_CHECK: [%d]\n", value[0]);
		break;

	case LPWG_UPDATE_ALL:
		ts->lpwg.mode = value[0];
		ts->lpwg.screen = value[1];
		ts->lpwg.sensor = value[2];
		ts->lpwg.qcover = value[3];

		TOUCH_I("LPWG_UPDATE_ALL: mode[%d], screen[%s], sensor[%s], qcover[%s]\n",
				value[0],
				value[1] ? "ON" : "OFF",
				value[2] ? "FAR" : "NEAR",
				value[3] ? "CLOSE" : "OPEN");

		ft5726_lpwg_mode(dev);
		break;

	case LPWG_REPLY:
		break;

	}

	return 0;
}

static void ft5726_connect(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	int charger_state = atomic_read(&ts->state.connect);
	//int wireless_state = atomic_read(&ts->state.wireless);
	bool ta_simulator_state =
			(bool)(atomic_read(&ts->state.debug_option_mask) & DEBUG_OPTION_4);

	TOUCH_TRACE();

	d->charger = 0;

	/* code for TA simulator */
	if (charger_state | ta_simulator_state)
		d->charger = 1;
	else
		d->charger = 0;

	if (ts->lpwg.screen != 0) {

		TOUCH_I("%s: write charger_state = 0x%02X\n", __func__, d->charger);
		if (atomic_read(&ts->state.pm) > DEV_PM_RESUME) {
			TOUCH_I("DEV_PM_SUSPEND - Don't try communication\n");
			return;
		}

		ft5726_reg_write(dev, SPR_CHARGER_STS, &d->charger, sizeof(u8));
	}

}

static int ft5726_usb_status(struct device *dev, u32 mode)
{
	struct touch_core_data *ts = to_touch_core(dev);

	TOUCH_TRACE();
	TOUCH_I("TA Type: %d\n", atomic_read(&ts->state.connect));
	ft5726_connect(dev);
	return 0;
}

static int ft5726_debug_option(struct device *dev, u32 *data)
{
	u32 chg_mask = data[0];
	u32 enable = data[1];

	switch (chg_mask) {
	case DEBUG_OPTION_0:
		TOUCH_I("Debug Option 0 %s\n", enable ? "Enable" : "Disable");
		break;
	case DEBUG_OPTION_1:
		TOUCH_I("Debug Option 1 %s\n", enable ? "Enable" : "Disable");
		break;
	case DEBUG_OPTION_2:
		TOUCH_I("Debug Option 2 %s\n", enable ? "Enable" : "Disable");
		break;
	case DEBUG_OPTION_3:
		TOUCH_I("Debug Option 3 %s\n", enable ? "Enable" : "Disable");
		break;
	case DEBUG_OPTION_4:
		TOUCH_I("Debug Option 4 - TA Simulator mode %s\n", enable ? "Enable" : "Disable");
		ft5726_connect(dev);
		break;

	default:
		TOUCH_E("Not supported debug option\n");
		break;
	}

	return 0;
}

static int ft5726_notify(struct device *dev, ulong event, void *data)
{
	struct touch_core_data *ts = to_touch_core(dev);
	u8 status = 0x00;
	int ret = 0;

	TOUCH_TRACE();
	TOUCH_I("%s event=0x%x\n", __func__, (unsigned int)event);
	switch (event) {
	case NOTIFY_TOUCH_RESET:
		TOUCH_I("NOTIFY_TOUCH_RESET! - DO NOTHING (Add-on)\n");
		break;
	case NOTIFY_CONNECTION:
		TOUCH_I("NOTIFY_CONNECTION!\n");
		ret = ft5726_usb_status(dev, *(u32 *)data);
		break;
	case NOTIFY_IME_STATE:
		TOUCH_I("NOTIFY_IME_STATE!\n");
		status = atomic_read(&ts->state.ime);
		ret = ft5726_reg_write(dev, REG_IME_STATE, &status, sizeof(status));
		if (ret)
			TOUCH_E("failed to write reg_ime_state, ret : %d\n", ret);
		break;
	case NOTIFY_CALL_STATE:
		TOUCH_I("NOTIFY_CALL_STATE!\n");
		break;
	case NOTIFY_DEBUG_OPTION:
		TOUCH_I("NOTIFY_DEBUG_OPTION!\n");
		ret = ft5726_debug_option(dev, (u32 *)data);
		break;
	case LCD_EVENT_LCD_BLANK:
		TOUCH_I("LCD_EVENT_LCD_BLANK!\n");
		break;
	case LCD_EVENT_LCD_UNBLANK:
		TOUCH_I("LCD_EVENT_LCD_UNBLANK!\n");
		break;
	case LCD_EVENT_LCD_MODE:
		TOUCH_I("LCD_EVENT_LCD_MODE!\n");
		TOUCH_I("lcd mode : %lu\n", (unsigned long)*(u32 *)data);
		break;
	case LCD_EVENT_READ_REG:
		TOUCH_I("LCD_EVENT_READ_REG\n");
		break;
	default:
		TOUCH_E("%lu is not supported\n", event);
		break;
	}

	return ret;
}

static void ft5726_init_locks(struct ft5726_data *d)
{
	TOUCH_TRACE();

	mutex_init(&d->rw_lock);
}

static int ft5726_probe(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = NULL;

	TOUCH_TRACE();

	d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);

	if (!d) {
		TOUCH_E("failed to allocate ft5726 data\n");
		return -ENOMEM;
	}

	d->dev = dev;
	touch_set_device(ts, d);

	touch_gpio_init(ts->reset_pin, "touch_reset");
	touch_gpio_direction_output(ts->reset_pin, 0);

	touch_gpio_init(ts->int_pin, "touch_int");
	touch_gpio_direction_input(ts->int_pin);

	touch_power_init(dev);

	touch_bus_init(dev, MAX_BUF_SIZE);

	ft5726_init_locks(d);

	ft5726_get_tci_info(dev);

	ft5726_grip_info(dev);

	d->tci_debug_type = TCI_DEBUG_BUFFER;
	d->dual_fwupg_en = 0;
	atomic_set(&ts->state.debug_option_mask, DEBUG_OPTION_2);

	// To be implemented.....
#ifdef FTS_CTL_IIC
	fts_rw_iic_drv_init(to_i2c_client(dev));
#endif
#ifdef FTS_SYSFS_DEBUG
	fts_create_sysfs(to_i2c_client(dev));
#endif
#ifdef FTS_APK_DEBUG
	fts_create_apk_debug_channel(to_i2c_client(dev));
#endif

	pm_qos_add_request(&d->pm_qos_req, PM_QOS_CPU_DMA_LATENCY, PM_QOS_DEFAULT_VALUE);

	return 0;
}

static int ft5726_remove(struct device *dev)
{
	struct ft5726_data *d = to_ft5726_data(dev);

	TOUCH_TRACE();
#ifdef FTS_APK_DEBUG
	fts_release_apk_debug_channel();
#endif

#ifdef FTS_SYSFS_DEBUG
	fts_remove_sysfs(to_i2c_client(dev));
#endif

#ifdef FTS_CTL_IIC
	fts_rw_iic_drv_exit();
#endif

	pm_qos_remove_request(&d->pm_qos_req);

	return 0;
}

static int ft5726_fw_compare(struct device *dev, const struct firmware *fw)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	u8 major = d->ic_info.version.major;
	u8 minor = d->ic_info.version.minor;
	u8 *bin_major = &(d->ic_info.version.bin_major);
	u8 *bin_minor = &(d->ic_info.version.bin_minor);
	u8 fw_version = 0;
	int update = 0;

	TOUCH_TRACE();

	if (d->ic_info.info_valid == 0) {
		// Failed to get ic info
		TOUCH_I("invalid ic info, skip fw upgrade\n");
		return 0;
	}

	fw_version = fw->data[0x1C00+0x010A];

	if (*bin_major == 0 && *bin_minor == 0) {
		// IF fw ver of bin is not initialized
		*bin_major = (fw_version & 0x80) >> 7;
		*bin_minor = fw_version & 0x7F;
	}

	if (ts->force_fwup) {
		update = 1;
	} else if ((major != d->ic_info.version.bin_major)
			|| (minor != d->ic_info.version.bin_minor)) {
		update = 1;
	}

	TOUCH_I("%s : binary[v%d.%d] device[v%d.%d] -> update: %d, force: %d\n",
			__func__, *bin_major, *bin_minor, major, minor,
			update, ts->force_fwup);

	return update;
}

static int ft5726_fwboot_upgrade(struct device *dev, const struct firmware *fw_boot)
{
	struct touch_core_data *ts = to_touch_core(dev);
	const u8 *fw_data = fw_boot->data;
	u32 fw_size = (u32)(fw_boot->size);
	u8 *fw_check_buf = NULL;
	u8 i2c_buf[FTS_PACKET_LENGTH + 12] = {0,};
	int ret;
	int packet_num, i, j, packet_addr, packet_len;
	u8 pramboot_ecc;

	TOUCH_TRACE();
	TOUCH_I("%s - START\n", __func__);

	if (fw_size > 0x10000 || fw_size == 0)
		return -EIO;

	fw_check_buf = kmalloc(fw_size+1, GFP_ATOMIC);
	if (fw_check_buf == NULL)
		return -ENOMEM;

	for (i = 12; i <= 30; i++) {
		// Reset CTPM
		touch_gpio_direction_output(ts->reset_pin, 0);
		touch_msleep(50);
		touch_gpio_direction_output(ts->reset_pin, 1);
		touch_msleep(i);

		// Set Upgrade Mode
		ret = ft5726_reg_write(dev, 0x55, i2c_buf, 0);
		if (ret < 0) {
			TOUCH_E("set upgrade mode write error\n");
			goto FAIL;
		}
		touch_msleep(1);

		// Check ID
		TOUCH_I("%s - Set Upgrade Mode and Check ID : %d ms\n", __func__, i);
		ret = ft5726_reg_read(dev, 0x90, i2c_buf, 2);
		if (ret < 0) {
			TOUCH_E("check id read error\n");
			goto FAIL;
		}

		TOUCH_I("Check ID : 0x%x , 0x%x\n", i2c_buf[0], i2c_buf[1]);

		if (i2c_buf[0] == 0x58 && i2c_buf[1] == 0x22) {
			touch_msleep(50);
			break;
		}

	}

	if (i > 30) {
		TOUCH_E("timeout to set upgrade mode\n");
		goto FAIL;
	}

	// Write F/W (Pramboot) Binary to CTPM
	TOUCH_I("%s - Write F/W (Pramboot)\n", __func__);
	pramboot_ecc = 0;
	packet_num = (fw_size + FTS_PACKET_LENGTH - 1) / FTS_PACKET_LENGTH;
	for (i = 0; i < packet_num; i++) {
		packet_addr = i * FTS_PACKET_LENGTH;
		i2c_buf[0] = 0; //(u8)(((u32)(packet_addr) & 0x00FF0000) >> 16);
		i2c_buf[1] = (u8)(((u32)(packet_addr) & 0x0000FF00) >> 8);
		i2c_buf[2] = (u8)((u32)(packet_addr) & 0x000000FF);
		if (packet_addr + FTS_PACKET_LENGTH > fw_size)
			packet_len = fw_size - packet_addr;
		else
			packet_len = FTS_PACKET_LENGTH;
		i2c_buf[3] = (u8)(((u32)(packet_len) & 0x0000FF00) >> 8);
		i2c_buf[4] = (u8)((u32)(packet_len) & 0x000000FF);
		for (j = 0; j < packet_len; j++) {
			i2c_buf[5 + j] = fw_data[packet_addr + j];
			//i2c_buf[5 + (j/4)*4 + (3 - (j%4))] = fw_data[packet_addr + j];        //Kylin20170315 for LGE
			pramboot_ecc ^= i2c_buf[5 + j];
		}
		//TOUCH_I("#%d : Writing to %d , %d bytes\n", i, packet_addr, packet_len); //kjh
		ret = ft5726_reg_write(dev, 0xAE, i2c_buf, packet_len + 5);
		if (ret < 0) {
			TOUCH_E("f/w(Pramboot) binary to CTPM write error\n");
			goto FAIL;
		}
	}

	// Read out Checksum
	ret = ft5726_reg_read(dev, 0xCC, i2c_buf, 1);
	if (ret < 0) {
		TOUCH_E("0xCC register read error\n");
		goto FAIL;
	}

	TOUCH_I("Make ecc: 0x%x ,Read ecc 0x%x\n", pramboot_ecc, i2c_buf[0]);

	if (i2c_buf[0] != pramboot_ecc) {
		TOUCH_I("Pramboot Verify Failed !!\n");
		goto FAIL;
	}
	TOUCH_I("%s - Pramboot write Verify OK !!\n", __func__);

	// Start App
	TOUCH_I("%s - Start App\n", __func__);
	ret = ft5726_reg_write(dev, 0x08, i2c_buf, 0);
	if (ret < 0) {
		TOUCH_E("Start App error\n");
		goto FAIL;
	}
	touch_msleep(10);
	kfree(fw_check_buf);

	TOUCH_I("===== Firmware (Pramboot) download Okay =====\n");

	return 0;

FAIL:

	kfree(fw_check_buf);
	TOUCH_I("===== Firmware (Pramboot) download FAIL!!! =====\n");

	return -EIO;

}

static int ft5726_fw_upgrade(struct device *dev, const struct firmware *fw)
{
	struct touch_core_data *ts = to_touch_core(dev);
	const u8 *fw_data = fw->data;
	u32 fw_size = (u32)(fw->size);
	u8 i2c_buf[FTS_PACKET_LENGTH + 12] = {0,};
	int ret;
	int packet_num, retry, i, j, packet_addr, packet_len;
	u8 fw_ecc;
	u8 data = 0;

	TOUCH_TRACE();
	TOUCH_I("%s - START\n", __func__);

	// Enter Upgrade Mode and ID check
	for (i = 0; i < 30; i++) {
		// Enter Upgrade Mode
		TOUCH_I("%s - Enter Upgrade Mode and Check ID\n", __func__);

		ret = ft5726_reg_write(dev, 0x55, i2c_buf, 0);
		if (ret < 0) {
			TOUCH_E("upgrade mode enter and check ID error\n");
			return -EIO;
		}
		touch_msleep(1);

		// Check ID

		ret = ft5726_reg_read(dev, 0x90, i2c_buf, 2);

		if (ret < 0) {
			TOUCH_E("check ID error\n");
			return -EIO;
		}

		TOUCH_I("Check ID [%d] : 0x%x , 0x%x\n", i, i2c_buf[0], i2c_buf[1]);

		if (i2c_buf[0] == 0x58 && i2c_buf[1] == 0x2B)
			break;

		touch_msleep(10);
	}

	if (i == 30) {
		TOUCH_E("timeout to set upgrade mode\n");
		goto FAIL;
	}

	// Change to write flash set range
	i2c_buf[0] = 0x0A; //- 0x0A : All, 0x0B : App, 0x0C : Lcd
	ret = ft5726_reg_write(dev, 0x09, i2c_buf, 1);
	if (ret < 0) {
		TOUCH_E("change to write flash set range error\n");
		goto FAIL;
	}
	touch_msleep(50);

	// Erase start
	TOUCH_I("%s - Erase All  Area\n", __func__);

	ret = ft5726_reg_write(dev, 0x61, i2c_buf, 0);
	if (ret < 0) {
		TOUCH_E("Erase All Area error\n");
		goto FAIL;
	}

	touch_msleep(1000);

	retry = 300;
	i = 0;
	do {
		ret = ft5726_reg_read(dev, 0x6A, i2c_buf, 2);
		if (ret < 0) {
			TOUCH_E("0x6A register 2byte read error\n");
			goto FAIL;
		}

		if (i2c_buf[0] == 0xF0 && i2c_buf[1] == 0xAA) {
			TOUCH_I("Erase Done : %d\n", i);
			break;
		}
		i++;
		touch_msleep(20);
	} while (--retry);

	//  Write F/W (All or App) Binary to CTPM
	TOUCH_I("%s - Write F/W (App)\n", __func__);
	fw_ecc = 0;
	packet_num = (fw_size + FTS_PACKET_LENGTH - 1) / FTS_PACKET_LENGTH;
	for (i = 0; i < packet_num; i++) {
		packet_addr = i * FTS_PACKET_LENGTH;
		i2c_buf[0] = (u8)(((u32)(packet_addr) & 0x00FF0000) >> 16);
		i2c_buf[1] = (u8)(((u32)(packet_addr) & 0x0000FF00) >> 8);
		i2c_buf[2] = (u8)((u32)(packet_addr) & 0x000000FF);
		if (packet_addr + FTS_PACKET_LENGTH > fw_size)
			packet_len = fw_size - packet_addr;
		else
			packet_len = FTS_PACKET_LENGTH;
		i2c_buf[3] = (u8)(((u32)(packet_len) & 0x0000FF00) >> 8);
		i2c_buf[4] = (u8)((u32)(packet_len) & 0x000000FF);
		for (j = 0; j < packet_len; j++) {
			i2c_buf[5 + j] = fw_data[packet_addr + j];
			fw_ecc ^= i2c_buf[5 + j];
		}

		ret = ft5726_reg_write(dev, 0xBF, i2c_buf, packet_len + 5);
		if (ret < 0) {
			TOUCH_E("write f/w(app) binary to CTPM error\n");
			goto FAIL;
		}
		//touch_msleep(10);

		// Waiting
		retry = 20;
		do {
			ret = ft5726_reg_read(dev, 0x6A, i2c_buf, 2); //count the number of writing byte
			if (ret < 0) {
				TOUCH_E("wating error\n");
				goto FAIL;
			}

			if ((u32)(i + 0x1000) == (((u32)(i2c_buf[0]) << 8) | ((u32)(i2c_buf[1])))) {
				if ((i & 0x007F) == 0)
					TOUCH_I("Write Done : %d / %d\n", i+1, packet_num);
				break;
			}
			touch_msleep(1);
		} while (--retry);
		if (retry == 0) {
			TOUCH_I("Write Done, Max packet delay : %d / %d : [0x%02x] , [0x%02x]\n", i+1, packet_num, i2c_buf[0], i2c_buf[1]);
		}
	}
	TOUCH_I("Write Finished : Total %d\n", packet_num);

	touch_msleep(50);

	// Read out Checksum
	TOUCH_I("%s - Read out checksum (App) for %d bytes\n", __func__, fw_size);
	ret = ft5726_reg_write(dev, 0x64, i2c_buf, 0);
	if (ret < 0) {
		TOUCH_E("read out checksum error\n");
		goto FAIL;
	}

	touch_msleep(5);

	packet_num = (fw_size + LEN_FLASH_ECC_MAX - 1) / LEN_FLASH_ECC_MAX;  // LEN_FLASH_ECC_MAX
	for (i = 0; i < packet_num; i++) {
		packet_addr = i * LEN_FLASH_ECC_MAX;
		i2c_buf[0] = (u8)(((u32)(packet_addr) & 0x00FF0000) >> 16);
		i2c_buf[1] = (u8)(((u32)(packet_addr) & 0x0000FF00) >> 8);
		i2c_buf[2] = (u8)((u32)(packet_addr) & 0x000000FF);

		if (packet_addr + LEN_FLASH_ECC_MAX > fw_size)
			packet_len = fw_size - packet_addr;
		else
			packet_len = LEN_FLASH_ECC_MAX;
		i2c_buf[3] = (u8)(((u32)(packet_len) & 0x0000FF00) >> 8);
		i2c_buf[4] = (u8)((u32)(packet_len) & 0x000000FF);

		ret = ft5726_reg_write(dev, 0x65, i2c_buf, 5);
		TOUCH_I("cheecksum read range, packet num :  %d\n", packet_num);
		TOUCH_I("cheecksum read range  [0x%02x] ,[0x%02x] , [0x%02x] , [0x%02x]\n",
				i2c_buf[1], i2c_buf[2], i2c_buf[3], i2c_buf[4]);

		touch_msleep(fw_size/256);

		retry = 200;
		do {
			ret = ft5726_reg_read(dev, 0x6A, i2c_buf, 2);
			if(ret < 0) {
				TOUCH_E("i2c error\n");
				goto FAIL;
			}
			if(i2c_buf[0] == 0xF0 && i2c_buf[1] == 0x55) {
				TOUCH_I("0xF055 Flash status ecc OK\n");
				break;
			} else {
				TOUCH_I("Checksum read range, Calc.  [0x%02x] , [0x%02x]\n", i2c_buf[0], i2c_buf[1]);
			}

			touch_msleep(10);

		} while (--retry);
	}

	ret = ft5726_reg_read(dev, 0x66, i2c_buf, 1);
	if (ret < 0) {
		TOUCH_E("0x66 register read error\n");
		goto FAIL;
	}
	TOUCH_I("Reg 0x66 : 0x%x\n", i2c_buf[0]);

	if (i2c_buf[0] != fw_ecc) {
		TOUCH_E("Checksum ERROR : Reg 0x66 [0x%x] , fw_ecc [0x%x]\n", i2c_buf[0], fw_ecc);
		goto FAIL;
	}

	TOUCH_I("Checksum OK : Reg 0x66 [0x%x] , fw_ecc [0x%x]\n", i2c_buf[0], fw_ecc);

	TOUCH_I("===== Firmware download OK!!! =====\n");

	// Exit download mode
	ret = ft5726_reg_write(dev, 0x07, &data, 0);
	touch_msleep(ts->caps.hw_reset_delay);
	if (ret < 0) {
		TOUCH_E("Exit download mode write fail, ret = %d\n", ret);
		goto FAIL;
	}

	return 0;

FAIL:

	TOUCH_I("===== Firmware download FAIL!!! =====\n");

	// Exit download mode
	ret = ft5726_reg_write(dev, 0x07, &data, 0);
	touch_msleep(ts->caps.hw_reset_delay);
	if (ret < 0)
		TOUCH_E("Exit download mode write fail, ret = %d\n", ret);

	return -EIO;

}

static int ft5726_upgrade(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	const struct firmware *fw = NULL;
	const struct firmware *fw_boot = NULL;
	const struct firmware *multi_fw[2] = {NULL, NULL};
	char fwpath[2][255] = {{0, }, {0, }};
	int ret = 0, retry = 0;
	int boot_mode = TOUCH_NORMAL_BOOT;
	int index = 0;

	TOUCH_TRACE();

	boot_mode = touch_check_boot_mode(dev);
	if ((boot_mode == TOUCH_LAF_MODE) || (boot_mode == TOUCH_RECOVERY_MODE) ||
			(boot_mode == TOUCH_CHARGER_MODE)) {
		TOUCH_I("skip fw upgrade : %d (CHARGER:5/LAF:6/RECOVER_MODE:7)\n", boot_mode);
		return -EPERM;

	}

	if (atomic_read(&ts->state.fb) >= FB_SUSPEND) {
		TOUCH_I("state.fb is not FB_RESUME\n");
		return -EPERM;
	}

	if (ts->test_fwpath[0]) {
		memcpy(fwpath[0], ts->def_fwpath[0], sizeof(fwpath[0]));
		memcpy(fwpath[1], ts->test_fwpath, sizeof(fwpath[1]));
		TOUCH_I("only all_bin upgrade\n");
	} else if(d->dual_fwupg_en){
		memcpy(fwpath[0], d->multi_fwpath[0], sizeof(fwpath[0]));
		memcpy(fwpath[1], d->multi_fwpath[1], sizeof(fwpath[1]));
		d->dual_fwupg_en = 0;
		TOUCH_I("multi fw upgrade\n");
	} else if (ts->def_fwcnt) {
		/* 0 : pramboot bin, 1 : All bin */
		memcpy(fwpath[0], ts->def_fwpath[0], sizeof(fwpath[0]));
		memcpy(fwpath[1], ts->def_fwpath[1], sizeof(fwpath[1]));
		TOUCH_I("default fw upgrade\n");
	} else {
		TOUCH_E("no firmware file\n");
		return -EPERM;
	}

	for (index = 0; index < 2; index++) {
		fwpath[index][sizeof(fwpath[index])-1] = '\0';
			if (strlen(fwpath[index]) <= 0) {
				TOUCH_E("error get fw path\n");
				return -EPERM;
			}

			TOUCH_I("get fwpath[%d] from def_fwpath : %s\n", index, fwpath[index]);

			TOUCH_I("fwpath[%s]\n", fwpath[index]);
			ret = request_firmware(&multi_fw[index], fwpath[index], dev);
			if (ret) {
				TOUCH_E("fail to request_firmware fwpath: %s (ret:%d)\n", fwpath[index], ret);
				goto error;
			}

			TOUCH_I("fw size:%zu, data: %p\n", multi_fw[index]->size, multi_fw[index]->data);
	}

	fw_boot = multi_fw[0]; // pram_boot
	fw = multi_fw[1]; // All bin

	if (ft5726_fw_compare(dev, fw)) {

		do {
			ret = ft5726_fwboot_upgrade(dev, fw_boot);
			ret |= ft5726_fw_upgrade(dev, fw);
		} while (++retry < 3 && ret != 0);

		if (retry >= 3) {
			TOUCH_I("f/w upgrade fail!!\n");
			ret = -EPERM;
			goto error;
		}
		TOUCH_I("f/w upgrade complete\n");

	} else {
		TOUCH_I("f/w upgrade is not performed\n");
		ret = -EPERM; // Return non-zero to not reset
	}

error:
		release_firmware(fw);
		release_firmware(fw_boot);

	return ret;
}

static int ft5726_suspend(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	int boot_mode = 0;
	int ret = 0;

	TOUCH_TRACE();

	boot_mode = touch_check_boot_mode(dev);

	switch (boot_mode) {
	case TOUCH_NORMAL_BOOT:
	case TOUCH_MINIOS_AAT:
		break;
	case TOUCH_MINIOS_MFTS_FOLDER:
	case TOUCH_MINIOS_MFTS_FLAT:
	case TOUCH_MINIOS_MFTS_CURVED:
		if (!ts->mfts_lpwg) {
			touch_interrupt_control(dev, INTERRUPT_DISABLE);
			TOUCH_I("%s : touch_suspend - MFTS\n", __func__);
			ft5726_power(dev, POWER_OFF);
			return -EPERM;
		}
		break;
	case TOUCH_CHARGER_MODE:
	case TOUCH_LAF_MODE:
	case TOUCH_RECOVERY_MODE:
		TOUCH_I("%s: Etc boot_mode(%d)!!!\n", __func__, boot_mode);
		return -EPERM;
	default:
		TOUCH_E("%s: invalid boot_mode = %d\n", __func__, boot_mode);
		return -EPERM;
	}

	TOUCH_D(TRACE, "%s : touch_suspend start\n", __func__);

	if (atomic_read(&d->init) == IC_INIT_DONE)
		ft5726_lpwg_mode(dev);
	else /* need init */
		ret = 1;

	return ret;
}

static int ft5726_resume(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int boot_mode = TOUCH_NORMAL_BOOT;

	TOUCH_TRACE();

	boot_mode = touch_check_boot_mode(dev);

	switch (boot_mode) {
	case TOUCH_NORMAL_BOOT:
	case TOUCH_MINIOS_AAT:
		break;
	case TOUCH_MINIOS_MFTS_FOLDER:
	case TOUCH_MINIOS_MFTS_FLAT:
	case TOUCH_MINIOS_MFTS_CURVED:
		if (!ts->mfts_lpwg) {
			ft5726_power(dev, POWER_ON);
			touch_msleep(ts->caps.hw_reset_delay);
			ft5726_ic_info(dev);
		}
		break;
	case TOUCH_CHARGER_MODE:
	case TOUCH_LAF_MODE:
	case TOUCH_RECOVERY_MODE:
		TOUCH_I("%s: Etc boot_mode(%d)!!!\n", __func__, boot_mode);
		ft5726_sleep_control(dev, IC_DEEP_SLEEP);
		return -EPERM;
	default:
		TOUCH_E("%s: invalid boot_mode = %d\n", __func__, boot_mode);
		return -EPERM;
	}

	ft5726_reset_ctrl(dev, HW_RESET_SYNC);
	/* "-EPERM" don't "init", because "sw42000_reset_ctrl" Performing "init" */
	return -EPERM;
}

static int ft5726_init(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	u8 data = 0;
	int ret = 0;

	TOUCH_TRACE();

	ret = ft5726_ic_info(dev);
	if (ret < 0) {
		TOUCH_I("failed to get ic_info, ret:%d\n", ret);
		atomic_set(&d->init, IC_INIT_DONE); // Nothing to init, anyway DONE init

		return 0;
	}

	TOUCH_I("%s: charger_state = 0x%02X\n", __func__, d->charger);
	ret = ft5726_reg_write(dev, SPR_CHARGER_STS, &d->charger, sizeof(u8));
	if (ret)
		TOUCH_E("failed to write \'spr_charger_sts\', ret:%d\n", ret);

	data = atomic_read(&ts->state.ime);
	TOUCH_I("%s: ime_state = %d\n", __func__, data);
	ret = ft5726_reg_write(dev, REG_IME_STATE, &data, sizeof(data));
	if (ret)
		TOUCH_E("failed to write \'reg_ime_state\', ret:%d\n", ret);

	ret = ft5726_grip_suppression_ctrl(dev, d->grip_x, d->grip_y);
	if (ret)
		TOUCH_E("failed to write \'grip_area_state\', ret:%d\n", ret);

	atomic_set(&d->init, IC_INIT_DONE);

	ft5726_lpwg_mode(dev);
	if (ret)
		TOUCH_E("failed to lpwg_control, ret:%d\n", ret);

	return 0;
}

static int ft5726_irq_abs_data(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	struct ft5726_touch_data *data = d->info.data;
	struct touch_data *tdata;
	u32 touch_count = 0;
	u8 finger_index = 0;
	int i = 0;
	static u16 pre_pressure[FTS_MAX_POINTS] = {0, };
	u8 touch_id, event/*, palm*/;

	TOUCH_TRACE();

	touch_count = d->info.touch_cnt;
	ts->new_mask = 0;

	for (i = 0; i < FTS_MAX_POINTS; i++) {
		touch_id = (u8)(data[i].yh) >> 4;

		if (touch_id >= FTS_MAX_ID) {
			//TOUCH_I("invalid interrupt, i[%d], touch_id : %d\n", i, touch_id); // temp, bring up
			break;
		}

		event = (u8)(data[i].xh) >> 6;
//		palm = ((u8)(data[i].xh) >> 4) & 0x01;

/*
		if (palm) {
			if (event == FTS_TOUCH_CONTACT) {
				// FTS_TOUCH_DOWN
				ts->is_cancel = 1;
				TOUCH_I("Palm Detected\n");
			} else if (event == FTS_TOUCH_UP) {
				ts->is_cancel = 0;
				TOUCH_I("Palm Released\n");
			}
			ts->tcount = 0;
			ts->intr_status = TOUCH_IRQ_FINGER;
			return 0;
		}
*/

		if (event == FTS_TOUCH_DOWN || event == FTS_TOUCH_CONTACT) {
			ts->new_mask |= (1 << touch_id);
			tdata = ts->tdata + touch_id;

			tdata->id = touch_id;
			tdata->type = MT_TOOL_FINGER;
			tdata->x = ((u16)(data[i].xh & 0x0F))<<8 | (u16)(data[i].xl);
			tdata->y = ((u16)(data[i].yh & 0x0F))<<8 | (u16)(data[i].yl);
			tdata->pressure = (u8)(data[i].weight);

			tdata->width_major = (u8)((data[i].area)>>4);
			tdata->width_minor = 0;
			tdata->orientation = 0;

			if (pre_pressure[tdata->id] == tdata->pressure) {
				tdata->pressure++;
			}

			pre_pressure[tdata->id] = tdata->pressure;

			finger_index++;

			TOUCH_D(ABS, "tdata [id:%d e:%d x:%d y:%d z:%d - %d,%d,%d]\n",
					tdata->id, event, tdata->x, tdata->y, tdata->pressure,
					tdata->width_major, tdata->width_minor, tdata->orientation);

		}
	}

	ts->tcount = finger_index;
	ts->intr_status = TOUCH_IRQ_FINGER;

	return 0;
}

int ft5726_irq_abs(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	struct ft5726_touch_data *data = d->info.data;

	u8 point_buf[POINT_READ_BUF] = { 0, };
	int ret = -1;

	TOUCH_TRACE();

	ret = ft5726_reg_read(dev, 0, point_buf, POINT_READ_BUF);

	if (ret < 0) {
		TOUCH_E("Fail to read point regs.\n");
		return ret;
	}

	/* check if touch cnt is valid */
	if (/*point_buf[FTS_TOUCH_P_NUM] == 0 || */point_buf[FTS_TOUCH_P_NUM] > ts->caps.max_id) {
		TOUCH_I("%s : touch cnt is invalid - %d\n",
				__func__, point_buf[FTS_TOUCH_P_NUM]);
		return -ERANGE;
	}

	d->info.touch_cnt = point_buf[FTS_TOUCH_P_NUM];

	memcpy(data, point_buf+FTS_TOUCH_EVENT_POS, FTS_ONE_TCH_LEN * FTS_MAX_POINTS);

	return ft5726_irq_abs_data(dev);
}

int ft5726_irq_lpwg(struct device *dev, int tci)
{
	struct touch_core_data *ts = to_touch_core(dev);
	//struct ft5726_data *d = to_ft5726_data(dev);
	int ret = 0, tap_count, i, j;
	u8 tci_data_buf[MAX_TAP_COUNT*4 + 2];

	TOUCH_TRACE();

	if (ts->lpwg.mode == LPWG_NONE || (ts->lpwg.mode == LPWG_DOUBLE_TAP && tci == TCI_2)) {
		TOUCH_I("lpwg irq is invalid!!\n");
		return -EINVAL;
	}

	ret = ft5726_reg_read(dev, 0xD3, tci_data_buf, 2);
	if (ret < 0) {
		TOUCH_E("Fail to read tci data\n");
		return ret;
	}

	TOUCH_I("TCI Data : TCI[%d], Result[%d], TapCount[%d]\n", tci, tci_data_buf[0], tci_data_buf[1]);

	// Validate tci data
	if (!((tci_data_buf[0] == 0x01 && tci == TCI_1) || (tci_data_buf[0] == 0x02 && tci == TCI_2))
			|| tci_data_buf[1] == 0 || tci_data_buf[1] > MAX_TAP_COUNT) {
		TOUCH_I("tci data is invalid!!\n");
		return -EINVAL;
	}

	tap_count = tci_data_buf[1];

	ret = ft5726_reg_read(dev, 0xD3, tci_data_buf, tap_count*4 + 2);
	if (ret < 0) {
		TOUCH_E("Fail to read tci data\n");
		return ret;
	}

	ts->lpwg.code_num = tap_count;
	for (i = 0; i < tap_count; i++) {
		j = i*4+2;
		ts->lpwg.code[i].x = ((int)tci_data_buf[j] << 8) | (int)tci_data_buf[j+1];
		ts->lpwg.code[i].y = ((int)tci_data_buf[j+2] << 8) | (int)tci_data_buf[j+3];

		if ((ts->lpwg.mode >= LPWG_PASSWORD) && (ts->role.hide_coordinate))
			TOUCH_I("LPWG data xxxx, xxxx\n");
		else
			TOUCH_I("LPWG data %d, %d\n", ts->lpwg.code[i].x, ts->lpwg.code[i].y);
	}

	ts->lpwg.code[tap_count].x = -1;
	ts->lpwg.code[tap_count].y = -1;

	if (tci == TCI_1)
		ts->intr_status = TOUCH_IRQ_KNOCK;
	else if (tci == TCI_2)
		ts->intr_status = TOUCH_IRQ_PASSWD;
	else
		ts->intr_status = TOUCH_IRQ_NONE;

	return ret;
}

int ft5726_irq_report_tci_fr(struct device *dev)
{
	//struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	int ret = 0;
	u8 data, tci_1_fr, tci_2_fr;

	TOUCH_TRACE();

	if (d->tci_debug_type != TCI_DEBUG_ALWAYS && d->tci_debug_type != TCI_DEBUG_BUFFER_ALWAYS) {
		TOUCH_I("tci debug in real time is disabled!!\n");
		return 0;
	}

	ret = ft5726_reg_read(dev, LGE_FAIL_REALTIME, &data, 1);
	if (ret < 0) {
		TOUCH_E("i2c error\n");
		return ret;
	}

	tci_1_fr = data & 0x0F; // TCI_1
	tci_2_fr = (data & 0xF0) >> 4; // TCI_2

	if (tci_1_fr < TCI_FR_NUM)
		TOUCH_I("Knock-on Failure Reason Reported : [%s]\n", tci_debug_str[tci_1_fr]);
	else
		TOUCH_I("Knock-on Failure Reason Reported : [%s]\n", tci_debug_str[TCI_FR_NUM]);

	if (tci_2_fr < TCI_FR_NUM)
		TOUCH_I("Knock-code Failure Reason Reported : [%s]\n", tci_debug_str[tci_2_fr]);
	else
		TOUCH_I("Knock-code Failure Reason Reported : [%s]\n", tci_debug_str[TCI_FR_NUM]);

	return ret;
}

int ft5726_report_tci_fr_buffer(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	int ret = 0, i;
	u8 tci_fr_buffer[1 + TCI_FR_BUF_LEN], tci_fr_cnt, tci_fr;

	TOUCH_TRACE();

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("IC state is deep sleep\n");
		return 0;
	}

	if (d->tci_debug_type != TCI_DEBUG_BUFFER && d->tci_debug_type != TCI_DEBUG_BUFFER_ALWAYS) {
		TOUCH_I("tci debug in buffer is disabled!!\n");
		return 0;
	}

	// Knock-on
	for (i = 0; i < 25; i++) {
		ret = ft5726_reg_read(dev, LGE_FAIL_KNOCKON, &tci_fr_buffer, sizeof(tci_fr_buffer));
		if (!ret)
			break;
		touch_msleep(2);
	}

	if (i == 25) {
		TOUCH_E("i2c error\n");
		return ret;
	}

	tci_fr_cnt = tci_fr_buffer[0];
	if (tci_fr_cnt > TCI_FR_BUF_LEN) {
		TOUCH_I("Knock-on Failure Reason Buffer Count Invalid\n");
	} else if (tci_fr_cnt == 0) {
		TOUCH_I("Knock-on Failure Reason Buffer NONE\n");
	} else {
		for (i = 0; i < tci_fr_cnt; i++) {
			tci_fr = tci_fr_buffer[1 + i];
			if (tci_fr < TCI_FR_NUM)
				TOUCH_I("Knock-on Failure Reason Buffer [%02d] : [%s]\n", i+1, tci_debug_str[tci_fr]);
			else
				TOUCH_I("Knock-on Failure Reason Buffer [%02d] : [%s]\n", i+1, tci_debug_str[TCI_FR_NUM]);
		}
	}

	// Knock-code (Same as knock-on case except for reg addr)
	for (i = 0; i < 25; i++) {
		ret = ft5726_reg_read(dev, LGE_FAIL_KNOCKCODE, &tci_fr_buffer, sizeof(tci_fr_buffer));
		if (!ret)
			break;
		touch_msleep(2);
	}

	if (i == 25) {
		TOUCH_E("i2c error\n");
		return ret;
	}

	tci_fr_cnt = tci_fr_buffer[0];
	if (tci_fr_cnt > TCI_FR_BUF_LEN) {
		TOUCH_I("Knock-code Failure Reason Buffer Count Invalid\n");
	} else if (tci_fr_cnt == 0) {
		TOUCH_I("Knock-code Failure Reason Buffer NONE\n");
	} else {
		for (i = 0; i < tci_fr_cnt; i++) {
			tci_fr = tci_fr_buffer[1 + i];
			if (tci_fr < TCI_FR_NUM)
				TOUCH_I("Knock-code Failure Reason Buffer [%02d] : [%s]\n", i+1, tci_debug_str[tci_fr]);
			else
				TOUCH_I("Knock-code Failure Reason Buffer [%02d] : [%s]\n", i+1, tci_debug_str[TCI_FR_NUM]);
		}
	}

	return ret;
}


int ft5726_irq_handler(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	int ret = 0;
	u8 int_status = 0;

	TOUCH_TRACE();

	pm_qos_update_request(&d->pm_qos_req, 10);

	ret = ft5726_reg_read(dev, 0x01, &int_status, 1);
	if (ret < 0)
		goto exit;

	if (int_status == 0x01) {
		ret = ft5726_irq_abs(dev);
	} else if (int_status == 0x02) {
		ret = ft5726_irq_lpwg(dev, TCI_1);
	} else if (int_status == 0x03) {
		ret = ft5726_irq_lpwg(dev, TCI_2);
	} else if (int_status == 0x04) {
		// LPWG Fail Reason Report (RT)
		ret = ft5726_irq_report_tci_fr(dev);
	} else if (int_status == 0x05) {
		TOUCH_I("ESD interrupt !!\n");
		ret = -EHWRESET_ASYNC;
	} else if (int_status == 0x0B) {
		TOUCH_I("Palm Detected\n");
		ts->new_mask = 0;
		ts->is_cancel = 1;
		ts->tcount = 0;
		ts->intr_status = TOUCH_IRQ_FINGER;
	} else if (int_status == 0x0C) {
		TOUCH_I("Palm Released\n");
	} else if (int_status == 0x00) {
		TOUCH_I("No interrupt status\n");
	} else {
		TOUCH_E("Invalid interrupt status : %d\n", int_status);
		ret = -EHWRESET_ASYNC;
	}

exit:
	pm_qos_update_request(&d->pm_qos_req, PM_QOS_DEFAULT_VALUE);

	return ret;

}

static int ft5726_sw_reset(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);
	u8 data = 0;

	TOUCH_TRACE();

	TOUCH_I("%s, SW Reset\n", __func__);

	touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);
	touch_report_all_event(ts);

	data = 0x55;
	ft5726_reg_write(dev, 0xFC, &data, 1);
	touch_msleep(10);
	data = 0x66;
	ft5726_reg_write(dev, 0xFC, &data, 1);
	touch_msleep(ts->caps.sw_reset_delay);

	atomic_set(&d->init, IC_INIT_NEED);

	queue_delayed_work(ts->wq, &ts->init_work, 0);

	return 0;
}

static int ft5726_hw_reset(struct device *dev, int mode)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);

	TOUCH_TRACE();

	TOUCH_I("%s : HW Reset(mode:%d)\n", __func__, mode);
	touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);
	touch_report_all_event(ts);

	touch_gpio_direction_output(ts->reset_pin, 0);

	touch_msleep(1);

	touch_gpio_direction_output(ts->reset_pin, 1);
	atomic_set(&d->init, IC_INIT_NEED);
	atomic_set(&ts->state.sleep, IC_NORMAL);
	touch_msleep(ts->caps.hw_reset_delay);

	if (mode == HW_RESET_ASYNC) {
		queue_delayed_work(ts->wq, &ts->init_work, 0);
	} else if (mode == HW_RESET_SYNC) {
		ts->driver->init(dev);
		touch_interrupt_control(ts->dev, INTERRUPT_ENABLE);
	} else if (mode == HW_RESET_NO_INIT) {
		TOUCH_I("%s : Only HW Reset, No init!!\n", __func__);
	} else {
		TOUCH_E("%s : Invalid HW reset mode!!\n", __func__);
	}

	return 0;
}

static int ft5726_power_reset(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);

	TOUCH_TRACE();

	TOUCH_I("%s : Power Reset\n", __func__);
	touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);

	ft5726_power(dev, POWER_OFF);
	ft5726_power(dev, POWER_ON);
	touch_msleep(ts->caps.hw_reset_delay);

	atomic_set(&d->init, IC_INIT_NEED);
	atomic_set(&ts->state.sleep, IC_NORMAL);

	queue_delayed_work(ts->wq, &ts->init_work, 0);

	return 0;
}

int ft5726_reset_ctrl(struct device *dev, int ctrl)
{

	TOUCH_TRACE();

	switch (ctrl) {
	case SW_RESET:
		ft5726_sw_reset(dev);
		break;
	case HW_RESET_SYNC:
	case HW_RESET_ASYNC:
	case HW_RESET_NO_INIT:
		ft5726_hw_reset(dev, ctrl);
		break;
	case HW_RESET_POWER:
		ft5726_power_reset(dev);
		break;
	default:
		TOUCH_I("%s, Unknown reset ctrl!!!!\n", __func__);
		return 0;
	}

	return 0;
}

int ft5726_power(struct device *dev, int ctrl)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);

	TOUCH_TRACE();

	switch (ctrl) {
	case POWER_OFF:
		TOUCH_I("%s, off\n", __func__);
		atomic_set(&d->init, IC_INIT_NEED);
		touch_gpio_direction_output(ts->reset_pin, 0);
		touch_msleep(1); //tvdr
		touch_gpio_direction_output(ts->vio_pin, 0);
		touch_gpio_direction_output(ts->vdd_pin, 0);
		touch_msleep(1);
		break;

	case POWER_ON:
		TOUCH_I("%s, on\n", __func__);
		touch_gpio_direction_output(ts->reset_pin, 0);
		touch_msleep(1); //trtp
		touch_gpio_direction_output(ts->vio_pin, 1); // temp bring up.. before vio, after vdd? confirm Jason and engineer
		touch_msleep(10); //between trtp and tivd
		touch_gpio_direction_output(ts->vdd_pin, 1);
		touch_msleep(1); //tvdr
		touch_gpio_direction_output(ts->reset_pin, 1);
		break;

	case POWER_HW_RESET_SYNC:
		ft5726_reset_ctrl(dev, HW_RESET_SYNC);
		break;

	case POWER_HW_RESET_ASYNC:
		ft5726_reset_ctrl(dev, HW_RESET_ASYNC);
		break;

	case POWER_SW_RESET:
		ft5726_reset_ctrl(dev, SW_RESET);
		break;

	default:
		TOUCH_I("%s, Unknown Power Ctrl!!!!\n", __func__);
		break;
	}

	return 0;
}

int ft5726_sleep_control(struct device *dev, int mode)
{
	struct touch_core_data *ts = to_touch_core(dev);
	u8 data;

	TOUCH_TRACE();

	TOUCH_I("ft5726_sleep_control = %d\n", mode);

	if (mode) {
		if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP)
			return 0;
		data = 0x03;
		atomic_set(&ts->state.sleep, IC_DEEP_SLEEP);
		return ft5726_reg_write(dev, 0xA5, &data, 1);
	} else {
		if (atomic_read(&ts->state.sleep) == IC_NORMAL)
			return 0;
		ft5726_reset_ctrl(dev, HW_RESET_SYNC);
		/* Return value is -EPERM because init and lpwg_mode is already done.*/
		return -EPERM;
	}
}

int ft5726_grip_suppression_ctrl(struct device *dev, u8 x, u8 y)
{
	int ret = 0;
	u8 data = 0x0;

	TOUCH_TRACE();

	data = x; // unit: 0.1mm = 1
	ret = ft5726_reg_write(dev, LGE_GRIP_SUPPRESSION_X, &data, sizeof(data));

	if (ret < 0) {
		TOUCH_E("Write grip suppression x cmd error\n");
		return ret;
	}

	data = y; // unit: 0.1mm = 1
	ret = ft5726_reg_write(dev, LGE_GRIP_SUPPRESSION_Y, &data, sizeof(data));

	if (ret < 0) {
		TOUCH_E("Write grip suppression y cmd error\n");
		return ret;
	}

	TOUCH_I("%s - x: %d*0.1mm, y: %d*0.1mm\n", __func__, x, y);

	return ret;
}

static ssize_t store_grip_ctrl(struct device *dev,
		const char *buf, size_t count)
{
	u32 val_x = 0;
	u32 val_y = 0;
	struct ft5726_data *d = to_ft5726_data(dev);
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;

	TOUCH_TRACE();

	if (sscanf(buf, "%x %x", &val_x, &val_y) <= 0)
		return count;

	mutex_lock(&ts->lock);

	d->grip_x = (u8)val_x;
	d->grip_y = (u8)val_y;

	ret = ft5726_grip_suppression_ctrl(dev, d->grip_x, d->grip_y);
	if (ret)
		TOUCH_E("%s, grip_suppression setting fail!\n", __func__);

	mutex_unlock(&ts->lock);

	return count;
}

static ssize_t store_reg_ctrl(struct device *dev,
		const char *buf, size_t count)
{
	struct touch_core_data *ts = to_touch_core(dev);
	char command[6] = {0};
	u32 reg = 0;
	u32 value = 0;
	u8 data = 0;
	u8 reg_addr;

	TOUCH_TRACE();

	if (sscanf(buf, "%5s %x %x", command, &reg, &value) <= 0)
		return count;

	mutex_lock(&ts->lock);

	reg_addr = (u8)reg;
	if (!strcmp(command, "write")) {
		data = (u8)value;
		if (ft5726_reg_write(dev, reg_addr, &data, sizeof(u8)) < 0)
			TOUCH_E("reg addr 0x%x write fail\n", reg_addr);
		else
			TOUCH_I("reg[%x] = 0x%x\n", reg_addr, data);
	} else if (!strcmp(command, "read")) {
		if (ft5726_reg_read(dev, reg_addr, &data, sizeof(u8)) < 0)
			TOUCH_E("reg addr 0x%x read fail\n", reg_addr);
		else
			TOUCH_I("reg[%x] = 0x%x\n", reg_addr, data);
	} else {
		TOUCH_D(BASE_INFO, "Usage\n");
		TOUCH_D(BASE_INFO, "Write reg value\n");
		TOUCH_D(BASE_INFO, "Read reg\n");
	}

	mutex_unlock(&ts->lock);

	return count;
}

static ssize_t show_tci_debug(struct device *dev, char *buf)
{
	struct ft5726_data *d = to_ft5726_data(dev);
	int ret = 0;

	TOUCH_TRACE();

	ret = snprintf(buf + ret, PAGE_SIZE,
			"Current TCI Debug Type = %s\n",
			(d->tci_debug_type < 4) ? tci_debug_type_str[d->tci_debug_type] : "Invalid");

	TOUCH_I("Current TCI Debug Type = %s\n",
			(d->tci_debug_type < 4) ? tci_debug_type_str[d->tci_debug_type] : "Invalid");

	return ret;
}

static ssize_t store_tci_debug(struct device *dev,
		const char *buf, size_t count)
{
	struct ft5726_data *d = to_ft5726_data(dev);
	int value = 0;

	TOUCH_TRACE();

	if (kstrtos32(buf, 10, &value) < 0 || value < 0 || value > 3) {
		TOUCH_I("Invalid TCI Debug Type, please input 0~3\n");
		return count;
	}

	d->tci_debug_type = (u8)value;

	TOUCH_I("Set TCI Debug Type = %s\n", (d->tci_debug_type < 4) ? tci_debug_type_str[d->tci_debug_type] : "Invalid");

	return count;
}

static ssize_t store_reset_ctrl(struct device *dev, const char *buf, size_t count)
{
	int value = 0;

	TOUCH_TRACE();

	if (kstrtos32(buf, 10, &value) < 0)
		return count;

	ft5726_reset_ctrl(dev, value);

	return count;
}

static ssize_t show_pinstate(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;

	TOUCH_TRACE();

	ret = snprintf(buf, PAGE_SIZE, "VDD:%d, VIO:%d, RST:%d, INT:%d\n",
			gpio_get_value(ts->vdd_pin), gpio_get_value(ts->vio_pin),
			gpio_get_value(ts->reset_pin), gpio_get_value(ts->int_pin));

	TOUCH_I("%s : %s", __func__, buf);

	return ret;
}

static ssize_t store_multi_fw_upgrade(struct device *dev,
		const char *buf, size_t count)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);

	TOUCH_TRACE();

	TOUCH_I("size : %d, %d\n", (int)sizeof(d->multi_fwpath), (int)(sizeof(d->multi_fwpath[0])*2));
	memset(d->multi_fwpath, 0, sizeof(d->multi_fwpath[0])*2);

	if (ts->lpwg.screen == 0) {
		TOUCH_E("LCD OFF state. please turn on the display\n");
		return count;
	}

	if (sscanf(buf, "%255s %255s", &d->multi_fwpath[0][0], &d->multi_fwpath[1][0]) <= 0)
		return count;

	d->dual_fwupg_en = 1;
	ts->force_fwup = 1;
	queue_delayed_work(ts->wq, &ts->upgrade_work, 0);

	return count;
}

static TOUCH_ATTR(reg_ctrl, NULL, store_reg_ctrl);
static TOUCH_ATTR(tci_debug, show_tci_debug, store_tci_debug);
static TOUCH_ATTR(reset_ctrl, NULL, store_reset_ctrl);
static TOUCH_ATTR(pinstate, show_pinstate, NULL);
static TOUCH_ATTR(multi_fw_upgrade, NULL, store_multi_fw_upgrade);
static TOUCH_ATTR(grip_ctrl, NULL, store_grip_ctrl);

static struct attribute *ft5726_attribute_list[] = {
	&touch_attr_reg_ctrl.attr,
	&touch_attr_tci_debug.attr,
	&touch_attr_reset_ctrl.attr,
	&touch_attr_pinstate.attr,
	&touch_attr_multi_fw_upgrade.attr,
	&touch_attr_grip_ctrl.attr,
	NULL,
};

static const struct attribute_group ft5726_attribute_group = {
	.attrs = ft5726_attribute_list,
};

static int ft5726_register_sysfs(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;

	TOUCH_TRACE();

	ret = sysfs_create_group(&ts->kobj, &ft5726_attribute_group);
	if (ret < 0)
		TOUCH_E("ft5726 sysfs register failed\n");

	ft5726_prd_register_sysfs(dev);

	return 0;
}

static int ft5726_get_cmd_version(struct device *dev, char *buf)
{
	struct ft5726_data *d = to_ft5726_data(dev);
	int offset = 0;
	int ret = 0;

	TOUCH_TRACE();

	ret = ft5726_ic_info(dev);
	if (ret < 0) {
		offset += snprintf(buf + offset, PAGE_SIZE, "-1\n");
		offset += snprintf(buf + offset, PAGE_SIZE - offset,
				"Read Fail Touch IC Info\n");
		return offset;
	}

	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"==================== Version Info ====================\n");
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"Version: v%d.%02d\n", d->ic_info.version.major, d->ic_info.version.minor);
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"Chip_id: %x / Chip_id_low : %x\n", d->ic_info.chip_id, d->ic_info.chip_id_low);
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"Vendor_id: %x\n", d->ic_info.fw_vendor_id);
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"Product_id: [FT%x%x]\n", d->ic_info.product_id[0], d->ic_info.product_id[1]);
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"======================================================\n");

	return offset;
}

static int ft5726_get_cmd_atcmd_version(struct device *dev, char *buf)
{
	struct ft5726_data *d = to_ft5726_data(dev);
	int offset = 0;
	int ret = 0;

	TOUCH_TRACE();

	ret = ft5726_ic_info(dev);
	if (ret < 0) {
		offset += snprintf(buf + offset, PAGE_SIZE, "-1\n");
		offset += snprintf(buf + offset, PAGE_SIZE - offset,
				"Read Fail Touch IC Info\n");
		return offset;
	}

	offset = snprintf(buf, PAGE_SIZE, "V%d.%02d\n",
			d->ic_info.version.major, d->ic_info.version.minor);

	return offset;
}

static int ft5726_esd_recovery(struct device *dev)
{
	TOUCH_TRACE();

	return 0;
}

static int ft5726_swipe_enable(struct device *dev, bool enable)
{
	TOUCH_TRACE();

	return 0;
}

static int ft5726_init_pm(struct device *dev)
{
	TOUCH_TRACE();

	return 0;
}

static int ft5726_set(struct device *dev, u32 cmd, void *input, void *output)
{
	TOUCH_TRACE();

	return 0;
}

static int ft5726_get(struct device *dev, u32 cmd, void *input, void *output)
{
	int ret = 0;

	TOUCH_D(BASE_INFO, "%s : cmd %d\n", __func__, cmd);

	switch (cmd) {
	case CMD_VERSION:
		ret = ft5726_get_cmd_version(dev, (char *)output);
		break;

	case CMD_ATCMD_VERSION:
		ret = ft5726_get_cmd_atcmd_version(dev, (char *)output);
		break;

	default:
		break;
	}

	return ret;
}

static int ft5726_shutdown(struct device *dev)
{

	struct ft5726_data *d = to_ft5726_data(dev);

	TOUCH_TRACE();

	pm_qos_remove_request(&d->pm_qos_req);

	return 0;
}

static struct touch_driver touch_driver = {
	.probe = ft5726_probe,
	.remove = ft5726_remove,
	.suspend = ft5726_suspend,
	.shutdown = ft5726_shutdown,
	.resume = ft5726_resume,
	.init = ft5726_init,
	.irq_handler = ft5726_irq_handler,
	.power = ft5726_power,
	.upgrade = ft5726_upgrade,
	.esd_recovery = ft5726_esd_recovery,
	.lpwg = ft5726_lpwg,
	.swipe_enable = ft5726_swipe_enable,
	.notify = ft5726_notify,
	.init_pm = ft5726_init_pm,
	.register_sysfs = ft5726_register_sysfs,
	.set = ft5726_set,
	.get = ft5726_get,
};

#define MATCH_NAME			"focaltech,ft5726"

static const struct of_device_id touch_match_ids[] = {
	{ .compatible = MATCH_NAME, },
	{},
};

static struct touch_hwif hwif = {
	.bus_type = HWIF_I2C,
	.name = LGE_TOUCH_NAME,
	.owner = THIS_MODULE,
	.of_match_table = of_match_ptr(touch_match_ids),
};

static int __init touch_device_init(void)
{
	TOUCH_TRACE();

	TOUCH_I("%s, focaltech ft5726 init!\n", __func__);
	return touch_bus_device_init(&hwif, &touch_driver);
}

static void __exit touch_device_exit(void)
{
	TOUCH_TRACE();

	TOUCH_I("%s, IC is focaltech ft5726!\n", __func__);
	touch_bus_device_exit(&hwif);
}

module_init(touch_device_init);
module_exit(touch_device_exit);

MODULE_AUTHOR("BSP-TOUCH@lge.com");
MODULE_DESCRIPTION("LGE touch driver v3");
MODULE_LICENSE("GPL");
