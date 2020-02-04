/* touch_ft5726.h
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

#ifndef LGE_TOUCH_FT5726_H
#define LGE_TOUCH_FT5726_H

#include <linux/pm_qos.h>

#define FTS_CTL_IIC
#define FTS_APK_DEBUG
#define FTS_SYSFS_DEBUG

struct ft5726_touch_data {
	u8 xh;
	u8 xl;
	u8 yh;
	u8 yl;
	u8 weight;
	u8 area;
} __packed;


struct ft5726_touch_info {
	u32 ic_status;
	u32 device_status;
	u32 wakeup_type:8;
	u32 touch_cnt:5;
	u32 button_cnt:3;
	u32 palm_bit:16;
	struct ft5726_touch_data data[10];
	/* debug info */
	//struct ft5726_touch_debug debug; //temp, bring up
} __packed;

/* Focaltech Register */
#define LGE_GESTURE_X1_LOW			0x92
#define LGE_GESTURE_X1_HIGH			0xB5
#define LGE_GESTURE_X2_LOW			0xB6
#define LGE_GESTURE_X2_HIGH			0xB7
#define LGE_GESTURE_y1_LOW			0xB8
#define LGE_GESTURE_y1_HIGH			0xB9
#define LGE_GESTURE_y2_LOW			0xBA
#define LGE_GESTURE_y2_HIGH			0xBB
#define LGE_GRIP_SUPPRESSION_X			0xCE
#define LGE_GRIP_SUPPRESSION_Y			0xCF

#define KNOCK_ON_SLOP			0xBC
#define KNOCK_CODE_SLOP			0xBD
#define KNOCK_ON_DIST			0xC4
#define KNOCK_CODE_DIST			0xC5
#define KNOCK_ON_TIME_GAP_MAX			0xC6
#define KNOCK_CODE_TIME_GAP_MAX			0xC7
#define KNOCK_ON_TIME_GAP_MIN			0xC8 //temp, bring up.
#define KNOCK_CODE_TIME_GAP_MIN			0xC9
#define KNOCK_ON_CNT			0xCA
#define KNOCK_CODE_CNT			0xCB
#define KNOCK_ON_DELAY			0xCC
#define KNOCK_CODE_DELAY		0xCD

#define LPWG_DEBUG_ENABLE		0xE5

#define LGE_GESTURE_EN			0xD0

#define LGE_FAIL_REALTIME		0xE6
#define LGE_FAIL_KNOCKON		0xE7
#define LGE_FAIL_KNOCKCODE		0xE9

/* Definitions for FTS */
#define FTS_PACKET_LENGTH		128

#define MAX_TAP_COUNT			12

#define FT5X06_ID			0x55
#define FT5X16_ID			0x0A
#define FT5X36_ID			0x14
#define FT6X06_ID			0x06
#define FT6X36_ID			0x36

#define FT5316_ID			0x0A
#define FT5306I_ID			0x55

#define LEN_FLASH_ECC_MAX		0xFFFE

#define FTS_MAX_POINTS			10

#define FTS_WORKQUEUE_NAME		"fts_wq"

#define FTS_DEBUG_DIR_NAME		"fts_debug"

#define FTS_INFO_MAX_LEN		512
#define FTS_FW_NAME_MAX_LEN		50

#define FTS_REG_ID			0xA3	// Chip Selecting (High)
#define FTS_REG_ID_LOW			0x9F	// Chip Selecting (Low)
#define FTS_REG_FW_VER			0xA6	// Firmware Version (Major)
#define FTS_REG_FW_VENDOR_ID		0xA8	// Focaltech's Panel ID
#define FTS_REG_PRODUCT_ID_H		0xA1	// Product ID (High)
#define FTS_REG_PRODUCT_ID_L		0xA2	// Product ID (Low)
#define FTS_REG_FW_VER_MINOR		0xB2	// Firmware Version (Minor)
#define FTS_REG_FW_VER_SUB_MINOR	0xB3	// Firmware Version (Sub-Minor)


#define FTS_REG_POINT_RATE		0x88

#define FTS_FACTORYMODE_VALUE	0x40
#define FTS_WORKMODE_VALUE		0x00

#define FTS_META_REGS			3
#define FTS_ONE_TCH_LEN			6
#define FTS_TCH_LEN(x)			(FTS_META_REGS + FTS_ONE_TCH_LEN * x)

#define FTS_PRESS			0x7F
#define FTS_MAX_ID			0x0F
#define FTS_TOUCH_P_NUM			2
#define FTS_TOUCH_X_H_POS		3
#define FTS_TOUCH_X_L_POS		4
#define FTS_TOUCH_Y_H_POS		5
#define FTS_TOUCH_Y_L_POS		6
#define FTS_TOUCH_PRE_POS		7
#define FTS_TOUCH_AREA_POS		8
#define FTS_TOUCH_POINT_NUM		2
#define FTS_TOUCH_EVENT_POS		3
#define FTS_TOUCH_ID_POS		5

#define FTS_TOUCH_DOWN			0
#define FTS_TOUCH_UP			1
#define FTS_TOUCH_CONTACT		2

#define POINT_READ_BUF			(3 + FTS_ONE_TCH_LEN * FTS_MAX_POINTS)

/* ime status */
#define REG_IME_STATE			(0xFA)

/* charger status */
#define SPR_CHARGER_STS			(0x8B)
#define SPR_QUICKCOVER_STS		(0xC1)

#define LPWG_AREA_MARGIN		(40)
#define GRIP_AREA_X				(28)
#define GRIP_AREA_Y				(30)

#define TCI_DEBUG_TYPE_NUM		4
#define TCI_FR_BUF_LEN			10
#define TCI_FR_NUM				7

// Definitions for Debugging Failure Reason in LPWG
enum {
	TCI_DEBUG_DISABLE = 0,
	TCI_DEBUG_ALWAYS,
	TCI_DEBUG_BUFFER,
	TCI_DEBUG_BUFFER_ALWAYS,
};

enum {
	SW_RESET = 0,
	HW_RESET_SYNC,
	HW_RESET_ASYNC,
	HW_RESET_NO_INIT,
	HW_RESET_POWER,
};

enum {
	ABS_MODE = 0,
	KNOCK_1,
	KNOCK_2,
	CUSTOM_DEBUG = 200,
	KNOCK_OVERTAP = 201,
};

enum {
	IC_INIT_NEED = 0,
	IC_INIT_DONE,
};

enum {
	TCI_CTRL_SET = 0,
	TCI_CTRL_CONFIG_COMMON,
	TCI_CTRL_CONFIG_TCI_1,
	TCI_CTRL_CONFIG_TCI_2,
};

struct ft5726_area {
	u8 low_byte_x1;
	u8 low_byte_x2;
	u8 low_byte_y1;
	u8 low_byte_y2;

	u8 high_byte_x1;
	u8 high_byte_x2;
	u8 high_byte_y1;
	u8 high_byte_y2;
};

struct ft5726_version {
	u8 major;
	u8 minor;
	u8 bin_major;
	u8 bin_minor;
};

struct ft5726_ic_info {
	struct ft5726_version version;
	u8 info_valid;
	u8 chip_id;
	u8 chip_id_low;
	u8 fw_vendor_id;
	u8 product_id[2];
};

enum {
	CONNECT_NONE = 0,
	CONNECT_USB,
	CONNECT_TA,
	CONNECT_OTG,
	CONNECT_WIRELESS,
};

struct ft5726_data {
	struct device *dev;
	struct kobject kobj;
	struct ft5726_touch_info info;
	struct ft5726_ic_info ic_info;
	struct ft5726_area lpwg_area;
//	struct ft5726_asc_info asc;	/* ASC */
	struct workqueue_struct *wq_log;
	u8 lcd_mode;
	u8 prev_lcd_mode;
	u8 driving_mode;
	u8 u3fake;
	u8 charger;
//	struct watch_data watch;
//	struct mutex spi_lock;
	struct mutex rw_lock;
	struct delayed_work font_download_work;
	struct delayed_work fb_notify_work;
	struct delayed_work debug_info_work;
	u32 earjack;
	u32 frame_cnt;
	u8 tci_debug_type;
	atomic_t block_watch_cfg;
	atomic_t init;
	u8 state;
	u8 chip_rev;
	u8 en_i2c_lpwg;
	struct notifier_block fb_notif;
	struct pm_qos_request pm_qos_req;
	u8 dual_fwupg_en;
	char multi_fwpath[2][256];
	u8 grip_x;
	u8 grip_y;
};


#define DISTANCE_INTER_TAP		(0x1 << 1) /* 2 */
#define DISTANCE_TOUCHSLOP		(0x1 << 2) /* 4 */
#define TIMEOUT_INTER_TAP_LONG		(0x1 << 3) /* 8 */
#define MULTI_FINGER			(0x1 << 4) /* 16 */
#define DELAY_TIME			(0x1 << 5) /* 32 */
#define TIMEOUT_INTER_TAP_SHORT		(0x1 << 6) /* 64 */
#define PALM_STATE			(0x1 << 7) /* 128 */
#define TAP_TIMEOVER			(0x1 << 8) /* 256 */
#define TCI_DEBUG_ALL (DISTANCE_INTER_TAP | DISTANCE_TOUCHSLOP |\
	TIMEOUT_INTER_TAP_LONG | MULTI_FINGER | DELAY_TIME |\
	TIMEOUT_INTER_TAP_SHORT | PALM_STATE | TAP_TIMEOVER)

static inline struct ft5726_data *to_ft5726_data(struct device *dev)
{
	return (struct ft5726_data *)touch_get_device(to_touch_core(dev));
}

static inline struct ft5726_data *to_ft5726_data_from_kobj(struct kobject *kobj)
{
	return (struct ft5726_data *)container_of(kobj,
			struct ft5726_data, kobj);
}

int ft5726_reg_read(struct device *dev, u16 addr, void *data, int size);
int ft5726_reg_write(struct device *dev, u16 addr, void *data, int size);
int ft5726_ic_info(struct device *dev);
int ft5726_tc_driving(struct device *dev, int mode);
int ft5726_irq_abs(struct device *dev);
int ft5726_irq_lpwg(struct device *dev, int tci);
int ft5726_irq_handler(struct device *dev);
int ft5726_check_status(struct device *dev);
int ft5726_debug_info(struct device *dev, int mode);
int ft5726_report_tci_fr_buffer(struct device *dev);
int ft5726_reset_ctrl(struct device *dev, int ctrl);
int ft5726_power(struct device *dev, int ctrl); //temp, bring up : To compare before.
int ft5726_sleep_control(struct device *dev, int mode);
int ft5726_grip_suppression_ctrl(struct device *dev, u8 x, u8 y);

//Apk and functions
extern int fts_create_apk_debug_channel(struct i2c_client *client);
extern void fts_release_apk_debug_channel(void);

//ADB functions
extern int fts_create_sysfs(struct i2c_client *client);
extern int fts_remove_sysfs(struct i2c_client *client);

//char device for old apk
extern int fts_rw_iic_drv_init(struct i2c_client *client);
extern void  fts_rw_iic_drv_exit(void);
#endif /* LGE_TOUCH_FT5726_H */
