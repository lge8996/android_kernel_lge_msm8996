/* touch_ft5726_prd.h
 *
 * Copyright (C) 2015 LGE.
 *
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

#ifndef PRODUCTION_TEST_H
#define PRODUCTION_TEST_H

#define MAX_LOG_FILE_SIZE	(10 * 1024 * 1024) /* 10 M byte */
#define MAX_LOG_FILE_COUNT	(4)

enum {
	RAW_DATA_TEST = 0,
	JITTER_TEST,
	DELTA_SHOW,
	SCAP_RAW_DATA_TEST,
	CB_TEST,
	LPWG_RAW_DATA_TEST,
	LPWG_JITTER_TEST,
	THE_NUMBER_OF_TEST,
};

enum {
	TEST_FAIL = 0,
	TEST_PASS,
};

/* Temporary spec range for passing sd test */
#define RAW_DATA_MAX		20000
#define RAW_DATA_MIN		0
#define RAW_DATA_MARGIN		0
#define SCAP_RAW_DATA_MAX	20000
#define SCAP_RAW_DATA_MIN	2000
#define SCAP_RAW_DATA_MARGIN	0
#define JITTER_MAX		140
#define JITTER_MIN		0
#define CB_MAX			250
#define CB_MIN			0
#define I_MIN_CC		500
#define LPWG_RAW_DATA_MAX	20000
#define LPWG_RAW_DATA_MIN	0
#define LPWG_JITTER_MAX		140
#define LPWG_JITTER_MIN		0

#define LOG_BUF_SIZE		(4096 * 4)
#define FAIL_LOG_BUF_SIZE	(500)

#define PRINT_BUF_SIZE		(256)
#define ADC_BUF_SIZE		(1+1+MAX_ROW+MAX_COL+1+MAX_ROW+MAX_COL)
#define SCAP_BUF_SIZE		(MAX_ROW+MAX_COL)

#define FTS_WORK_MODE		0x00
#define FTS_FACTORY_MODE	0x40

#define FTS_MODE_CHANGE_LOOP	20

#define TEST_PACKET_LENGTH	342 // 255

/* Number of channel */
#define MAX_ROW		42	//Tx
#define MAX_COL		27	//Rx

enum {
	TIME_INFO_SKIP,
	TIME_INFO_WRITE,
};

enum {
	NORMAL_MODE = 0,
	PRODUCTION_MODE,
};

extern void touch_msleep(unsigned int msecs);
int ft5726_prd_register_sysfs(struct device *dev);

#endif


