/* touch_ft5726_prd.c
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
#define TS_MODULE "[prd]"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
//#include <linux/regulator/consumer.h>

/*
 *  Include to touch core Header File
 */
#include <touch_hwif.h>
#include <touch_core.h>

/*
 *  Include to Local Header File
 */
#include "touch_ft5726.h"
#include "touch_ft5726_prd.h"

static char line[50000];
static u16 LowerImage[MAX_ROW][MAX_COL];
static u16 UpperImage[MAX_ROW][MAX_COL];

static s16 fts_data[MAX_ROW][MAX_COL];
//static int fGShortResistance[MAX_ROW+MAX_COL];
static int fMShortResistance[MAX_ROW+MAX_COL];
static int fts_data_adc_buff[ADC_BUF_SIZE*2];
static int fts_scap_data[SCAP_BUF_SIZE];

static u8 fail_log_buf[FAIL_LOG_BUF_SIZE];
static u8 i2c_data[MAX_COL*MAX_ROW*2+10];
static u8 log_buf[LOG_BUF_SIZE]; /* !!!!!!!!!Should not exceed the log size !!!!!!!! */
static char print_buf[PRINT_BUF_SIZE];

u8 ic_type = 0x00; // not changed, because only one IC. So, Please use that ic == x0ff not ||(or).

static char *prd_str[THE_NUMBER_OF_TEST] = {
	"raw_data_test!",
	"jitter_test!",
	"delta_test!",
	"scap_raw_data_test",
	"cb_test",
	"lpwg_raw_data_test",
	"lpwg_jitter_test"
};

void print_sd_log(char *buf)
{
	int i = 0;
	int index = 0;

	TOUCH_I("%s : start", __func__);

	while (index < strlen(buf) && buf[index] != '\0' && i < LOG_BUF_SIZE - 1) {
		print_buf[i++] = buf[index];

		/* Final character is not '\n' */
		if ((index == strlen(buf) - 1 || i == PRINT_BUF_SIZE - 2)
				&& print_buf[i - 1] != '\n')
			print_buf[i++] = '\n';

		if (print_buf[i - 1] == '\n') {
			print_buf[i - 1] = '\0';
			if (i - 1 != 0)
				TOUCH_I("%s\n", print_buf);

			i = 0;
		}
		index++;
	}
	TOUCH_I("%s : end", __func__);
}

static void log_file_size_check(struct device *dev)
{
	char *fname = NULL;
	struct file *file;
	loff_t file_size = 0;
	int i = 0;
	char buf1[128] = {0};
	char buf2[128] = {0};
	mm_segment_t old_fs = get_fs();
	int ret = 0;
	int boot_mode = 0;

	set_fs(KERNEL_DS);

	boot_mode = touch_check_boot_mode(dev);

	TOUCH_TRACE();

	switch (boot_mode) {
	case TOUCH_NORMAL_BOOT:
		fname = "/data/vendor/touch/touch_self_test.txt";
		break;
	case TOUCH_MINIOS_AAT:
		fname = "/data/touch/touch_self_test.txt";
		break;
	case TOUCH_MINIOS_MFTS_FOLDER:
	case TOUCH_MINIOS_MFTS_FLAT:
	case TOUCH_MINIOS_MFTS_CURVED:
		fname = "/data/touch/touch_self_mfts.txt";
		break;
	default:
		TOUCH_I("%s : not support mode\n", __func__);
		break;
	}

	if (fname) {
		file = filp_open(fname, O_RDONLY, 0666);
		sys_chmod(fname, 0666);
	} else {
		TOUCH_E("%s : fname is NULL, can not open FILE\n",
				__func__);
		goto error;
	}

	if (IS_ERR(file)) {
		TOUCH_E("%s : ERR(%ld) Open file error [%s]\n",
				__func__, PTR_ERR(file), fname);
		goto error;
	}

	file_size = vfs_llseek(file, 0, SEEK_END);
	TOUCH_I("%s : [%s] file_size = %lld\n",
			__func__, fname, file_size);

	filp_close(file, 0);

	if (file_size > MAX_LOG_FILE_SIZE) {
		TOUCH_I("%s : [%s] file_size(%lld) > MAX_LOG_FILE_SIZE(%d)\n",
				__func__, fname, file_size, MAX_LOG_FILE_SIZE);

		for (i = MAX_LOG_FILE_COUNT - 1; i >= 0; i--) {
			if (i == 0)
				sprintf(buf1, "%s", fname);
			else
				sprintf(buf1, "%s.%d", fname, i);

			ret = sys_access(buf1, 0);

			if (ret == 0) {
				TOUCH_I("%s : file [%s] exist\n",
						__func__, buf1);

				if (i == (MAX_LOG_FILE_COUNT - 1)) {
					if (sys_unlink(buf1) < 0) {
						TOUCH_E("%s : failed to remove file [%s]\n",
								__func__, buf1);
						goto error;
					}

					TOUCH_I("%s : remove file [%s]\n",
							__func__, buf1);
				} else {
					sprintf(buf2, "%s.%d", fname, (i + 1));

					if (sys_rename(buf1, buf2) < 0) {
						TOUCH_E("%s : failed to rename file [%s] -> [%s]\n",
								__func__, buf1, buf2);
						goto error;
					}

					TOUCH_I("%s : rename file [%s] -> [%s]\n",
							__func__, buf1, buf2);
				}
			} else {
				TOUCH_E("%s : file [%s] does not exist (ret = %d)\n",
						__func__, buf1, ret);
			}
		}
	}

error:
	set_fs(old_fs);

}

static void write_file(struct device *dev, char *data, int write_time)
{
	int fd = 0;
	char *fname = NULL;
	char time_string[64] = {0};
	struct timespec my_time = {0, };
	struct tm my_date = {0, };
	mm_segment_t old_fs = get_fs();
	int boot_mode = 0;

	set_fs(KERNEL_DS);

	boot_mode = touch_check_boot_mode(dev);

	TOUCH_TRACE();

	switch (boot_mode) {
	case TOUCH_NORMAL_BOOT:
		fname = "/data/vendor/touch/touch_self_test.txt";
		break;
	case TOUCH_MINIOS_AAT:
		fname = "/data/touch/touch_self_test.txt";
		break;
	case TOUCH_MINIOS_MFTS_FOLDER:
	case TOUCH_MINIOS_MFTS_FLAT:
	case TOUCH_MINIOS_MFTS_CURVED:
		fname = "/data/touch/touch_self_mfts.txt";
		break;
	default:
		TOUCH_I("%s : not support mode\n", __func__);
		break;
	}

	if (fname) {
		fd = sys_open(fname, O_WRONLY|O_CREAT|O_APPEND, 0666);
		sys_chmod(fname, 0666);
	} else {
		TOUCH_E("%s : fname is NULL, can not open FILE\n", __func__);
		set_fs(old_fs);
		return;
	}

	if (fd >= 0) {
		if (write_time == TIME_INFO_WRITE) {
			my_time = __current_kernel_time();
			time_to_tm(my_time.tv_sec,
					sys_tz.tz_minuteswest * 60 * (-1),
					&my_date);
			snprintf(time_string, 64,
					"\n[%02d-%02d %02d:%02d:%02d.%03lu]\n",
					my_date.tm_mon + 1,
					my_date.tm_mday, my_date.tm_hour,
					my_date.tm_min, my_date.tm_sec,
					(unsigned long) my_time.tv_nsec / 1000000);
			sys_write(fd, time_string, strlen(time_string));
		}
		sys_write(fd, data, strlen(data));
		print_sd_log(data);
		sys_close(fd);
	} else {
		TOUCH_E("File open failed\n");
	}
	set_fs(old_fs);
}

static int spec_file_read(struct device *dev)
{
	int ret = 0;
	struct touch_core_data *ts = to_touch_core(dev);
	const struct firmware *fwlimit = NULL;
	const char *path[2] = {ts->panel_spec, ts->panel_spec_mfts};
	int boot_mode = 0;
	int path_idx = 0;

	TOUCH_TRACE();

	boot_mode = touch_check_boot_mode(dev);
	if ((boot_mode == TOUCH_MINIOS_MFTS_FOLDER)
			|| (boot_mode == TOUCH_MINIOS_MFTS_FLAT)
			|| (boot_mode == TOUCH_MINIOS_MFTS_CURVED))
		path_idx = 1;
	else
		path_idx = 0;

	if (ts->panel_spec == NULL || ts->panel_spec_mfts == NULL) {
		TOUCH_E("dual_panel_spec file name is null\n");
		ret = -ENOENT;
		goto error;
	}

	TOUCH_I("touch_panel_spec file path = %s\n", path[path_idx]);

	ret = request_firmware(&fwlimit, path[path_idx], dev);

	if (ret) {
		TOUCH_E("%s : request firmware failed(%d)\n", __func__, ret);
		goto error;
	}

	if (fwlimit->data == NULL) {
		TOUCH_E("fwlimit->data is NULL\n");
		ret = -EINVAL;
		goto error;
	}

	if (fwlimit->size == 0) {
		TOUCH_E("fwlimit->size is 0\n");
		ret = -EINVAL;
		goto error;
	}
	strlcpy(line, fwlimit->data, fwlimit->size);

	TOUCH_I("spec_file_read success\n");

error:
	if (fwlimit)
		release_firmware(fwlimit);

	return ret;
}

static int spec_get_limit(struct device *dev, char *breakpoint, u16 limit_data[MAX_ROW][MAX_COL])
{
	int p = 0;
	int q = 0;
	int r = 0;
	int cipher = 1;
	int ret = 0;
	int retval = 0;
	char *found;
	int tx_num = 0;
	int rx_num = 0;

	TOUCH_TRACE();

	if (breakpoint == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	retval = spec_file_read(dev);
	if (retval) {
		ret = retval;
		goto error;
	}


	if (line == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	found = strnstr(line, breakpoint, sizeof(line));
	if (found != NULL) {
		q = found - line;
	} else {
		TOUCH_E("failed to find breakpoint. The panel_spec_file is wrong\n");
		ret = -EAGAIN;
		goto error;
	}

	memset(limit_data, 0, MAX_ROW * MAX_COL * 2);

	while (1) {
		if (line[q] == ',') {
			cipher = 1;
			for (p = 1; (line[q - p] >= '0') &&
					(line[q - p] <= '9'); p++) {
				limit_data[tx_num][rx_num] += ((line[q - p] - '0') * cipher);
				cipher *= 10;
			}
			r++;
			if (r % (int)MAX_COL == 0) {
				rx_num = 0;
				tx_num++;
			} else {
				rx_num++;
			}
		}
		q++;
		if (r == (int)MAX_ROW * (int)MAX_COL) {
			TOUCH_I("[%s] panel_spec_file scanning is success\n", breakpoint);
			break;
		}
	}

error:
	return ret;
}

int ft5726_change_op_mode(struct device *dev, u8 op_mode)
{

	int i = 0;
	u8 ret;
	u8 data;

	TOUCH_I("%s : op_mode = 0x%02x\n", __func__, op_mode);

	data = 0x00;
	ret = ft5726_reg_read(dev, 0x00, &data, 1);
	if (ret < 0) {
		TOUCH_E("0x00 register read error\n");
		return ret;
	}

	if (data == op_mode) {
		TOUCH_I("Already mode changed\n");
		return 0;
	}

	data = op_mode;
	ret = ft5726_reg_write(dev, 0x00, &data, 1);
	if (ret < 0) {
		TOUCH_E("0x00 register op_mode write error\n");
		return ret;
	}

	touch_msleep(10);

	for (i = 0; i < FTS_MODE_CHANGE_LOOP; i++) {
		data = 0x00;
		ret = ft5726_reg_read(dev, 0x00, &data, 1);
		if (ret < 0) {
			TOUCH_E("op_mode change check error\n");
			return ret;
		}

		if (data == op_mode)
			break;
		touch_msleep(50);
	}

	if (i >= FTS_MODE_CHANGE_LOOP) {
		TOUCH_E("Timeout to change op mode\n");
		return -EPERM;
	}
	TOUCH_I("Operation mode changed\n");
	touch_msleep(200);

	return 0;
}


int ft5726_switch_cal(struct device *dev, u8 cal_en)
{
	u8 ret;
	u8 data;

	TOUCH_I("%s : cal_en = 0x%02x\n", __func__, cal_en);

	ret = ft5726_reg_read(dev, 0xEE, &data, 1);
	if (ret < 0) {
		TOUCH_E("0xC2 register(calibration) read error\n");
		return ret;
	}

	if (data == cal_en) {
		TOUCH_I("Already switch_cal changed\n");
		return 0;
	}

	data = cal_en;
	ret = ft5726_reg_write(dev, 0xEE, &data, 1);
	if (ret < 0) {
		TOUCH_E("0xC2 register(calibration) write error\n");
		return ret;
	}
	touch_msleep(10);
	return 0;
}


int ft5726_prd_check_ch_num(struct device *dev)
{
	u8 ret;
	u8 data;

	TOUCH_I("%s\n", __func__);

	/* Channel number check */
	ret = ft5726_reg_read(dev, 0x02, &data, 1);
	if (ret < 0) {
		TOUCH_E("tx number register read error\n");
		return ret;
	}
	TOUCH_I("TX Channel : %d\n", data);

	touch_msleep(3);

	if (data != MAX_ROW) {
		TOUCH_E("Invalid TX Channel Num.\n");
		return -EPERM;
	}

	ret = ft5726_reg_read(dev, 0x03, &data, 1);
	if (ret < 0) {
		TOUCH_E("rx number register read error\n");
		return ret;
	}

	TOUCH_I("RX Channel : %d\n", data);

	touch_msleep(3);

	if (data != MAX_COL) {
		TOUCH_E("Invalid RX Channel Num.\n");
		return -EPERM;
	}

	return 0;
}

int ft5726_prd_get_raw_data(struct device *dev)
{
	int i, j, k;
	int ret = 0;
	u8 data = 0x00;
	u8 rawdata = 0x00;
	int test_retry_max = 10;
	int scan_retry_max = 10;

	TOUCH_I("%s : start\n", __func__);

	memset(i2c_data, 0, sizeof(i2c_data));

	// Data Select to raw data
	data = 0x00;
	ret = ft5726_reg_write(dev, 0x06, &data, 1);
	if (ret < 0) {
		TOUCH_E("data select to rawdata error\n");
		return ret;
	}
	touch_msleep(100);

	/* Start SCAN */
	for (k = 0; k < test_retry_max; k++) { //temp, bring up
		TOUCH_I("Start SCAN (%d/%d)\n", k+1, test_retry_max);
		memset(i2c_data, 0, sizeof(i2c_data));
		ret = ft5726_reg_read(dev, 0x00, &data, 1);
		if (ret < 0) {
			TOUCH_E("start scan read error\n");
			return ret;
		}
		data |= 0x80; // 0x40|0x80
		ret = ft5726_reg_write(dev, 0x00, &data, 1);
		if (ret < 0) {
			TOUCH_E("0x00 register 0x80 write error\n");
			return ret;
		}

		touch_msleep(20);

		for (i = 0; i < scan_retry_max; i++) {
			ret = ft5726_reg_read(dev, 0x00, &data, 1);
			if (ret < 0) {
				TOUCH_E("Scan error\n");
				return ret;
			}
			if (data == 0x40) {
				TOUCH_I("SCAN Success : 0x%X, %d ms\n", data, i*20);
				break;
			}

			touch_msleep(20);
		}

		if (i < scan_retry_max) {
			/* Read Raw data */
			rawdata = 0xAA;    // FT5726 RAWDATA ADDRESS
			ret = ft5726_reg_write(dev, 0x01, &rawdata, 1);
			if (ret < 0) {
				TOUCH_E("read raw data error\n");
				return ret;
			}
			touch_msleep(10);

			TOUCH_I("Read Raw data at once\n");

			ret = ft5726_reg_read(dev, 0x36, &i2c_data[0], MAX_COL*MAX_ROW*2);
			if (ret < 0) {
				TOUCH_E("0x36 register read error\n");
				return ret;
			}
			break;

		} else {
			TOUCH_E("SCAN Fail (%d/%d)\n", k+1, test_retry_max);
		}
	}

	if (k >= test_retry_max) {
		TOUCH_E("SCAN fail\n");
		return -EPERM;
	}

	// Data Select to raw data
	data = 0x00;
	ret = ft5726_reg_write(dev, 0x06, &data, 1);  // 0: RAWDATA
	if (ret < 0)
		TOUCH_E("default data select to rawdata error\n");
	touch_msleep(100); //temp, bring up, why delay?

	for (i = 0; i < MAX_ROW; i++) {
		for (j = 0; j < MAX_COL; j++) {
			k = ((i * MAX_COL) + j) << 1;
			fts_data[i][j] = (i2c_data[k] << 8) + i2c_data[k+1];
		}
	}

	return 0;
}

int ft5726_prd_get_raw_data_sd(struct device *dev)
{
	int i, j, k;
	int ret = 0;
	u8 data = 0x00;
	u8 rawdata = 0x00;
	int test_retry_max = 10;
	int scan_retry_max = 10;

	TOUCH_I("%s : start\n", __func__);

	memset(i2c_data, 0, sizeof(i2c_data));

	// Data Select to raw data
	data = 0x00;
	ret = ft5726_reg_write(dev, 0x06, &data, 1); // 0: RAWDATA
	if (ret < 0) {
		TOUCH_E("data select to rawdata error\n");
		return ret;
	}
	touch_msleep(100);

	data = 0x80;  // lowest frequency point in the frequency hopping table
	ret = ft5726_reg_write(dev, 0x0A, &data, 1);
	if (ret < 0) {
		TOUCH_E("low frequency setting error\n");
		return ret;
	}
	touch_msleep(10);

	data = 0x00; //FIR filter off
	ret = ft5726_reg_write(dev, 0xFB, &data, 1);
	if (ret < 0) {
		TOUCH_E("FIR disable error\n");
		return ret;
	}
	touch_msleep(150);

	/* Start SCAN */
	for (k = 0; k < test_retry_max; k++) {
		TOUCH_I("Start SCAN (%d/%d)(low freq/FIR off)\n", k+1, test_retry_max);
		memset(i2c_data, 0, sizeof(i2c_data));
		ret = ft5726_reg_read(dev, 0x00, &data, 1);
		if (ret < 0) {
			TOUCH_E("start scan read error\n");
			return ret;
		}
		data |= 0x80; // 0x40|0x80
		ret = ft5726_reg_write(dev, 0x00, &data, 1);
		if (ret < 0) {
			TOUCH_E("0x00 register 0x80 write error\n");
			return ret;
		}

		touch_msleep(20);

		for (i = 0; i < scan_retry_max; i++) {
			ret = ft5726_reg_read(dev, 0x00, &data, 1);
			if (ret < 0) {
				TOUCH_E("scan read error(low freq/FIR off)\n");
				return ret;
			}
			if (data == 0x40) {
				TOUCH_I("SCAN Success : 0x%X, %d ms\n", data, i*20);
				break;
			}

			touch_msleep(20);
		}

		if (i < scan_retry_max) {
			/* Read Raw data */
			rawdata = 0xAA;
			ret = ft5726_reg_write(dev, 0x01, &rawdata, 1);
			if (ret < 0) {
				TOUCH_E("read raw data error\n");
				return ret;
			}
			touch_msleep(10);

			TOUCH_I("Read Raw data_sd at once\n");

			ret = ft5726_reg_read(dev, 0x36, &i2c_data[0], MAX_COL*MAX_ROW*2);
			if (ret < 0) {
				TOUCH_E("0x36 register read error\n");
				return ret;
			}
			break;

		} else {
			TOUCH_E("SCAN Fail(low freq/FIR off) (%d/%d)\n", k+1, test_retry_max);
		}
	}

	if (k >= test_retry_max) {
		TOUCH_E("SCAN fail\n");
		return -EPERM;
	}

	//FIR filter on(restore default value)
	data = 0x01;
	ret = ft5726_reg_write(dev, 0xFB, &data, 1);
	if (ret < 0) {
		TOUCH_E("FIR enable error\n");
		return ret;
	}
	touch_msleep(150);

	// Frequency hopping table default 0
	data = 0x00;
	ret = ft5726_reg_write(dev, 0x0A, &data, 1);
	if (ret < 0) {
		TOUCH_E("frequency hopping table default 0 error\n");
		return ret;
	}
	touch_msleep(10);

	// Data Select to raw data
	data = 0x00;
	ret = ft5726_reg_write(dev, 0x06, &data, 1);  // 0: RAWDATA
	if (ret < 0)
		TOUCH_E("default data select to rawdata error\n");
	touch_msleep(100);

	for (i = 0; i < MAX_ROW; i++) {
		for (j = 0; j < MAX_COL; j++) {
			k = ((i * MAX_COL) + j) << 1;
			fts_data[i][j] = (i2c_data[k] << 8) + i2c_data[k+1];
		}
	}

	return 0;
}

int ft5726_prd_get_scap_raw_data(struct device *dev)
{
	int i, k;
	int ret = 0;
	u8 data = 0x00;
	u8 scap_rawdata = 0x00;
	int test_retry_max = 10;
	int scan_retry_max = 10;

	TOUCH_I("%s : start\n", __func__);

	memset(i2c_data, 0, sizeof(i2c_data));

	// Data Select to raw data
	data = 0x00;
	ret = ft5726_reg_write(dev, 0x06, &data, 1);
	if (ret < 0) {
		TOUCH_E("data select to rawdata error\n");
		return ret;
	}
	touch_msleep(100);

	/* Start SCAN */
	for (k = 0; k < test_retry_max; k++) {
		TOUCH_I("Start SCAN (%d/%d)\n", k+1, test_retry_max);
		ret = ft5726_reg_read(dev, 0x00, &data, 1);
		if (ret < 0) {
			TOUCH_E("start scan read error\n");
			return ret;
		}
		data |= 0x80; // 0x40|0x80
		ret = ft5726_reg_write(dev, 0x00, &data, 1);
		if (ret < 0) {
			TOUCH_E("0x00 register 0xC0 write error\n");
			return ret;
		}

		touch_msleep(20);

		for (i = 0; i < scan_retry_max; i++) {
			ret = ft5726_reg_read(dev, 0x00, &data, 1);
			if (ret < 0) {
				TOUCH_E("scan check error\n");
				return ret;
			}
			if (data == 0x40) {
				TOUCH_I("SCAN Success : 0x%X, %d ms\n", data, i*20);
				break;
			}

			touch_msleep(20);
		}

		if (i < scan_retry_max)
			break;
		else
			TOUCH_E("SCAN Fail (%d/%d)\n", k+1, test_retry_max);
	}

	if (k >= test_retry_max) {
		TOUCH_E("SCAN fail\n");
		return -EPERM;
	}

	/* Sc Water, RawData Address register points to self-contained water proof */
	scap_rawdata = 0xAC;
	ret = ft5726_reg_write(dev, 0x01, &scap_rawdata, 1);
	if (ret < 0) {
		TOUCH_E("0x01 register 0xAC write error\n");
		return ret;
	}
	touch_msleep(10);

	TOUCH_I("Read Self-Raw data at once\n");

	ret = ft5726_reg_read(dev, 0x36, &i2c_data[0], SCAP_BUF_SIZE * 2);
	if (ret < 0) {
		TOUCH_E("Read Self-Raw Data error\n");
		return ret;
	}

	data = 0x00;
	ret = ft5726_reg_write(dev, 0x06, &data, 1); // 0: RAWDATA
	if (ret < 0) {
		TOUCH_E("default data select to rawdata error\n");
		return ret;
	}
	touch_msleep(100);

	/* Combine */
	for (i = 0; i < SCAP_BUF_SIZE; i++)
		fts_scap_data[i] = (i2c_data[(i * 2)] << 8) + i2c_data[(i * 2) + 1];

	return 0;
}

int ft5726_prd_get_cb_data(struct device *dev)
{
	int i, k;
	int ret = 0;
	u8 data = 0x00;
	u8 cb_data = 0x00;
	int test_retry_max = 10;
	int scan_retry_max = 10;

	TOUCH_I("%s : start\n", __func__);

	memset(i2c_data, 0, sizeof(i2c_data));

	cb_data = 0x01;
	ret = ft5726_reg_write(dev, 0x44, &cb_data, 1);
	if (ret < 0) {
		TOUCH_E("Scap Water proof enable error\n");
		return ret;
	}

	touch_msleep(20);

	data = 0x00;
	/* Start SCAN */
	for (k = 0; k < test_retry_max; k++) {
		TOUCH_I("Start Second SCAN (%d/%d)\n", k+1, test_retry_max);
		ret = ft5726_reg_read(dev, 0x00, &data, 1);
		if (ret < 0) {
			TOUCH_E("start Second scan read error\n");
			return ret;
		}
		data |= 0x80; // 0x40|0x80
		ret = ft5726_reg_write(dev, 0x00, &data, 1);
		if (ret < 0) {
			TOUCH_E("0x00 register 0x80 write error\n");
			return ret;
		}

		touch_msleep(20);

		for (i = 0; i < scan_retry_max; i++) {
			ret = ft5726_reg_read(dev, 0x00, &data, 1);
			if (ret < 0) {
				TOUCH_E("scan check error\n");
				return ret;
			}
			if (data == 0x40) {
				TOUCH_I("Second SCAN Success : 0x%X, %d ms\n", data, i*20);
				break;
			}
			touch_msleep(20);
		}

		if (i < scan_retry_max)
			break;
		else
			TOUCH_E("Second SCAN Fail (%d/%d)\n", k+1, test_retry_max);
	}

	if (k >= test_retry_max) {
		TOUCH_E("Second SCAN failed\n");
		return -EPERM;
	}

	/* Read CB data */
	cb_data = 0x00;
	ret = ft5726_reg_write(dev, 0x45, &cb_data, 1);
	if (ret < 0) {
		TOUCH_E("Self cap CB Read Address error\n");
		return ret;
	}
	touch_msleep(20);

	TOUCH_I("Read CB data at once\n");

	ret = ft5726_reg_read(dev, 0x4E, &i2c_data[0], SCAP_BUF_SIZE);
	if (ret < 0) {
		TOUCH_E("0x4E register read error\n");
		return ret;
	}

	/* Combine */
	for (i = 0; i < SCAP_BUF_SIZE; i++)
		fts_scap_data[i] = i2c_data[i];

	return 0;
}

int ft5726_prd_get_jitter_data(struct device *dev)
{
	int i, j, k;
	int ret = 0;
	u8 data = 0x00;
	int scan_retry_max = 100;

	TOUCH_I("%s : start\n", __func__);

	memset(i2c_data, 0, sizeof(i2c_data));

	// Set jitter Test Frame Count
	data = 0x32; //low  byte
	ret = ft5726_reg_write(dev, 0x14, &data, 1);
	if (ret < 0) {
		TOUCH_E("set jitter test frame count(low byte) error\n");
		return ret;
	}
	touch_msleep(10);
/*
	data = 0x00; //high  byte
	ret = ft5726_reg_write(dev, 0x13, &data, 1);
	if (ret < 0) {
		TOUCH_E("set jitter test frame count(high byte) error\n");
		return ret;
	}
	touch_msleep(10);
*/
	// Start Noise Test
	data = 0x01;
	ret = ft5726_reg_write(dev, 0x13, &data, 1);
	if (ret < 0) {
		TOUCH_E("start jitter test error\n");
		return ret;
	}
	touch_msleep(100);

	// Check Scan is finished
	for (i = 0; i < scan_retry_max; i++) {
		ret = ft5726_reg_read(dev, 0x13, &data, 1);
		if (ret < 0) {
			TOUCH_E("check scan error\n");
			return ret;
		}
		if ((data & 0xff) == 0x00) {
			TOUCH_I("Scan finished : %d ms, data = %x\n", i*50, data);
			break;
		}
		touch_msleep(50); //touch_msleep(20);
	}

	if (i >= scan_retry_max) {
		TOUCH_E("Scan failed\n");
		return -EPERM;
	}

	// Get Noise data
	TOUCH_I("Read jitter data at once\n");

	// (Get RMS data)->(Get MaxNoise Data)
	ret = ft5726_reg_read(dev, 0x8D, &i2c_data[0], MAX_COL*MAX_ROW*2);
	if (ret < 0) {
		TOUCH_E("read jitter data error\n");
		return ret;
	}
	touch_msleep(100);

	for (i = 0; i < MAX_ROW; i++) {
		for (j = 0; j < MAX_COL; j++) {
			k = ((i * MAX_COL) + j) << 1;
			fts_data[i][j] = abs((i2c_data[k] << 8) + i2c_data[k+1]);
		}
	}

	return 0;
}

int ft5726_prd_get_delta_data(struct device *dev)
{
	int i, j, k;
	int ret = 0;
	u8 data = 0x00;
	int test_retry_max = 10;
	int scan_retry_max = 10;

	TOUCH_I("%s : start\n", __func__);

	memset(i2c_data, 0, sizeof(i2c_data));

	// Data Select to diff data
	data = 0x01;
	ret = ft5726_reg_write(dev, 0x06, &data, 1);
	if (ret < 0) {
		TOUCH_E("data select to diff data write error\n");
		return ret;
	}
	touch_msleep(100);

	/* Start SCAN */
	for (k = 0; k < test_retry_max; k++) {
		TOUCH_I("Start SCAN (%d/%d)\n", k+1, test_retry_max);
		ret = ft5726_reg_read(dev, 0x00, &data, 1);
		if (ret < 0) {
			TOUCH_E("start scan read error\n");
			return ret;
		}
		data |= 0x80; // 0x40|0x80
		ret = ft5726_reg_write(dev, 0x00, &data, 1);
		if (ret < 0) {
			TOUCH_E("0x00 register 0x80 write error\n");
			return ret;
		}

		touch_msleep(10);

		for (i = 0; i < scan_retry_max; i++) {
			ret = ft5726_reg_read(dev, 0x00, &data, 1);
			if (ret < 0) {
				TOUCH_E("scan check error\n");
				return ret;
			}
			if (data == 0x40) {
				TOUCH_I("SCAN Success : 0x%X, %d ms\n", data, i*20);
				break;
			}
			touch_msleep(20);
		}

		if (i < scan_retry_max)
			break;
		else
			TOUCH_E("SCAN Fail (%d/%d)\n", k+1, test_retry_max);
	}

	if (k >= test_retry_max) {
		TOUCH_E("SCAN failed\n");
		return -EPERM;
	}

	/* Read Raw data */
	data = 0xAA;    // FT5726 RAWDATA ADDRESS
	ret = ft5726_reg_write(dev, 0x01, &data, 1);
	if (ret < 0) {
		TOUCH_E("read rawdata error\n");
		return ret;
	}
	touch_msleep(10);

	TOUCH_I("Read Delta at once\n");

	ret = ft5726_reg_read(dev, 0x36, &i2c_data[0], MAX_COL*MAX_ROW*2);   // TH8 Column:34 Row:21
	if (ret < 0) {
		TOUCH_E("0x36 register read error\n");
		return ret;
	}

	// Data Select to raw data
	data = 0x00;
	ret = ft5726_reg_write(dev, 0x06, &data, 1);
	if (ret < 0)
		TOUCH_E("data select to rawdata error\n");

	touch_msleep(100);

	for (i = 0; i < MAX_ROW; i++) {
		for (j = 0; j < MAX_COL; j++) {
			k = ((i * MAX_COL) + j) << 1;
			fts_data[i][j] = (i2c_data[k] << 8) + i2c_data[k+1];
		}
	}

	return 0;
}

int ft5726_prd_get_adc_data(struct device *dev)
{
	int i;
	int ret = 0;
	u8 data = 0x00;
	int scan_retry_max = 10;

	TOUCH_I("%s : start\n", __func__);

	memset(i2c_data, 0, sizeof(i2c_data));

	/* Start RAWDATA SCAN */
	TOUCH_I("Start RAWDATA SCAN\n");
	ret = ft5726_reg_read(dev, 0x00, &data, 1);
	if (ret < 0) {
		TOUCH_E("adc rawdata scan read error\n");
		return ret;
	}
	data |= 0x80; // 0x40|0x80
	ret = ft5726_reg_write(dev, 0x00, &data, 1);
	if (ret < 0) {
		TOUCH_E("0x00 register 0x80 write error\n");
		return ret;
	}

	touch_msleep(20);

	for (i = 0; i < scan_retry_max; i++) {
		ret = ft5726_reg_read(dev, 0x00, &data, 1);
		if (ret < 0) {
			TOUCH_E("RAWDATA SCAN read error\n");
			return ret;
		}

		if (data == 0x40) {
			TOUCH_I("RAWDATA SCAN Success : 0x%X, %d ms\n", data, i*20);
			break;
		}

		touch_msleep(20);
	}

	if (i >= scan_retry_max) {
		TOUCH_E("RAWDATA SCAN Fail.\n");
		return -EPERM;
	}

	/* Start ADC SCAN */
	data = 0x01;
	ret = ft5726_reg_write(dev, 0x07, &data, 1);
	if (ret < 0) {
		TOUCH_E("0x07 register 0x01 write error\n");
		return ret;
	}

	touch_msleep(300); // wait to do Scan.

	for (i = 0; i < scan_retry_max ; i++) {
		ret = ft5726_reg_read(dev, 0x07, &data, 1);
		if (ret < 0) {
			TOUCH_E("ADC SCAN Success read error\n");
			return ret;
		}
		if (data == 0x00) {
			TOUCH_I("ADC SCAN Success : 0x%X, %d ms\n", data, i*20);
			break;
		}

		touch_msleep(20);
	}

	if (i < scan_retry_max) {
		/* Read ADC data */
		ret = ft5726_reg_read(dev, 0xF4, &i2c_data[0], ADC_BUF_SIZE*2);
		//ADC Value [1 + 1 + TX_NUM_MAX + RX_NUM_MAX + 1 + TX_NUM_MAX + RX_NUM_MAX]

		if (ret < 0) {
			TOUCH_E("read ADC data error\n");
			return ret;
		}
	} else {
		TOUCH_E("ADC SCAN Fail.\n");
		return -EPERM;
	}

	/* ADC data get */
	for (i = 0; i < ADC_BUF_SIZE; i++)
		fts_data_adc_buff[i] = (i2c_data[i*2] << 8) + i2c_data[i*2+1];

	return 0;
}

int ft5726_prd_test_data_adc(struct device *dev, int *test_result)
{
	struct ft5726_data *d = to_ft5726_data(dev);

	int i;
	int fail_count = 0;
	int ret = 0, fail_buf_ret = 0;
	int index = 0;

	int iGDrefn   = 0;
	int iDoffset  = 0;
	int iMDrefn   = 0;
	int iDsen = 0;

	memset(fail_log_buf, 0, FAIL_LOG_BUF_SIZE);

	iGDrefn   = fts_data_adc_buff[1];
	iDoffset  = fts_data_adc_buff[0] - 1024;
	iMDrefn   = fts_data_adc_buff[2 + MAX_ROW + MAX_COL];

	*test_result = TEST_PASS;

	/* print ADC DATA */
	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret,
			"Version: v%d.%02d, Bin_version: v%d.%02d\n",
			d->ic_info.version.major, d->ic_info.version.minor,
			d->ic_info.version.bin_major, d->ic_info.version.bin_minor);
	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "[Information] iGDrefn : %5d, iDoffset : %5d, iMDrefn : %5d\n", iGDrefn, iDoffset, iMDrefn);
	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "============================= ADC DATA ===============================\n");

	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "\n< ADC : Only Channel to Channel >\n");

	/* print ADC for CC */
	index = 1 + 1 + MAX_ROW + MAX_COL + 1; // 1 , 1 , TX_NUM , RX_NUM , 1 -> (TX_NUM+RX_NUM) : index change

	for (i = 0; i < MAX_ROW + MAX_COL; i++)
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "[%2d] %5d\n", i + 1, fts_data_adc_buff[i + index]);

	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "==================================================\n\n");

	/* channel to channel Test : CC Test*/
	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret,
			"============= ADC Open Short Test Result : Channel to Channel=============\n");

	fail_count = 0;

	for (i = 0; i < MAX_ROW + MAX_COL; i++) {
		iDsen = fts_data_adc_buff[i + MAX_ROW + MAX_COL + 3]; // 1 + 1 + TX_NUM + RX_NUM + 1 + (TX_NUM+RX_NUM)

		if (iMDrefn - iDsen <= 0) {
			fMShortResistance[i] = I_MIN_CC;  // WeakShortTest_CC 1200
			continue;
		}

//		fMShortResistance[i] = (float)(202 * (iDsen - iDoffset) + 79786) / (float)(iMDrefn - iDsen)/* - 3*/;
		fMShortResistance[i] = (202 * (iDsen - iDoffset) + 79786) / (iMDrefn - iDsen)/* - 3*/;

		if (fMShortResistance[i] < 0)
			fMShortResistance[i] = 0;

		if (I_MIN_CC > fMShortResistance[i]) {

			++fail_count;
			*test_result = TEST_FAIL;
			TOUCH_I("RT CC Test : fMShortResistance[%d] = %d, iDsen - iDoffset = %d\n",
					i, fMShortResistance[i], iDsen - iDoffset);
			if (fail_count < 5) {
				fail_buf_ret += snprintf(fail_log_buf + fail_buf_ret, FAIL_LOG_BUF_SIZE - fail_buf_ret,
						"CC Test : fMShortResistance[%d] = %d, iDsen - iDoffset = %d\n",
						i, fMShortResistance[i], iDsen - iDoffset);
			}
		}
	}

	for (i = 0; i < MAX_ROW + MAX_COL; i++)
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "[%2d] %6d\n", i+1, fMShortResistance[i]);

	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "==================================================\n");

	if (fail_count > 0)
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "Test FAIL : %d Errors\n", fail_count);
	else
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "Test PASS : No Errors\n");

	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, fail_log_buf);

	return ret;
}

int ft5726_prd_test_data(struct device *dev, int test_type, int *test_result)
{
	//struct touch_core_data *ts = to_touch_core(dev);
	struct ft5726_data *d = to_ft5726_data(dev);

	int i = 0, j = 0;
	int min_i = 0, min_j = 0;
	int max_i = 0, max_j = 0;
	int ret = 0, fail_buf_ret = 0;

	int limit_upper = 0, limit_lower = 0;
	int min, max/*, aver, stdev*/;
	int fail_count = 0;
	int check_limit = 1;

	memset(fail_log_buf, 0, FAIL_LOG_BUF_SIZE);

	*test_result = TEST_FAIL;

	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret,
			"Version: v%d.%02d, Bin_version: v%d.%02d\n",
			d->ic_info.version.major, d->ic_info.version.minor,
			d->ic_info.version.bin_major, d->ic_info.version.bin_minor);

	switch (test_type) {
	case RAW_DATA_TEST:
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "============= Raw Data Test Result =============\n");
		limit_upper = RAW_DATA_MAX + RAW_DATA_MARGIN;
		limit_lower = RAW_DATA_MIN - RAW_DATA_MARGIN;
		spec_get_limit(dev, "LowerImageLimit", LowerImage);
		spec_get_limit(dev, "UpperImageLimit", UpperImage);
		break;
	case JITTER_TEST:
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "============= jitter Test Result =============\n");
		limit_upper = JITTER_MAX;
		limit_lower = JITTER_MIN;
		break;
	case DELTA_SHOW:
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "============= Delta Result =============\n");
		check_limit = 0;
		break;
	case LPWG_RAW_DATA_TEST:
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "============= LPWG Raw Data Test Result =============\n");
		limit_upper = RAW_DATA_MAX + RAW_DATA_MARGIN;
		limit_lower = RAW_DATA_MIN - RAW_DATA_MARGIN;
		spec_get_limit(dev, "LowerImageLimit", LowerImage);
		spec_get_limit(dev, "UpperImageLimit", UpperImage);
		break;
	case LPWG_JITTER_TEST:
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "============= LPWG Jitter Test Result =============\n");
		limit_upper = LPWG_JITTER_MAX;
		limit_lower = LPWG_JITTER_MIN;
		break;
	default:
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "Test Failed (Invalid test type)\n");
		return ret;
	}

	max = min = fts_data[0][0];

	for (i = 0; i < MAX_ROW; i++) {
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "[%2d] ", i+1);
		for (j = 0; j < MAX_COL; j++) {

			if (test_type == RAW_DATA_TEST || test_type == LPWG_RAW_DATA_TEST)
				ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "%5d ", fts_data[i][j]);
			else
				ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "%4d ", fts_data[i][j]);

			if (test_type == RAW_DATA_TEST || test_type == LPWG_RAW_DATA_TEST) {
				if (check_limit && (fts_data[i][j] < LowerImage[i][j] || fts_data[i][j] > UpperImage[i][j])) {
					fail_count++;
					TOUCH_I("RT Test : %s, data[%d][%d] = %d\n", prd_str[test_type], i, j, fts_data[i][j]);
					if (fail_count < 5) {
						fail_buf_ret += snprintf(fail_log_buf + fail_buf_ret, FAIL_LOG_BUF_SIZE - fail_buf_ret,
								"Test : %s, data[%d][%d] = %d\n", prd_str[test_type], i, j, fts_data[i][j]);
					}
				}
			} else {
				if (check_limit && (fts_data[i][j] < limit_lower || fts_data[i][j] > limit_upper)) {
					fail_count++;
					TOUCH_I("RT Test : %s, data[%d][%d] = %d\n", prd_str[test_type], i, j, fts_data[i][j]);
					if (fail_count < 5) {
						fail_buf_ret += snprintf(fail_log_buf + fail_buf_ret, FAIL_LOG_BUF_SIZE - fail_buf_ret,
								"Test : %s, data[%d][%d] = %d\n", prd_str[test_type], i, j, fts_data[i][j]);
					}
				}
			}
			if (fts_data[i][j] < min) {
				min_i = i;
				min_j = j;
				min = fts_data[i][j];
			}
			if (fts_data[i][j] > max) {
				max_i = i;
				max_j = j;
				max = fts_data[i][j];
			}
		}
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "\n");
	}

	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "==================================================\n");

	if (fail_count && check_limit) {
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "Test FAIL : %d Errors\n", fail_count);
	} else {
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "Test PASS : No Errors\n");
		*test_result = TEST_PASS;
	}

	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "MIN[%d][%d] = %d, MAX[%d][%d] = %d, Upper = %d, Lower = %d\n\n",
			min_i, min_j, min, max_i, max_j, max, limit_upper, limit_lower);
	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, fail_log_buf);

	return ret;
}

int ft5726_prd_scap_test_data(struct device *dev, int test_type, int *test_result)
{
	struct ft5726_data *d = to_ft5726_data(dev);

	int i;
	int ret = 0, fail_buf_ret = 0;

	int limit_upper = 0, limit_lower = 0;
	int fail_count = 0;
	int min, max;
	int check_limit = 1;

	memset(fail_log_buf, 0, FAIL_LOG_BUF_SIZE);

	*test_result = TEST_FAIL;

	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret,
			"Version: v%d.%02d, Bin_version: v%d.%02d\n",
			d->ic_info.version.major, d->ic_info.version.minor,
			d->ic_info.version.bin_major, d->ic_info.version.bin_minor);

	switch (test_type) {
	case SCAP_RAW_DATA_TEST:
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "============= SCAP Raw Data Test Result =============\n");
		limit_upper = SCAP_RAW_DATA_MAX + SCAP_RAW_DATA_MARGIN;
		limit_lower = SCAP_RAW_DATA_MIN - SCAP_RAW_DATA_MARGIN;
		break;
	case CB_TEST:
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "============= CB Test Result Rx=============\n");
		limit_upper = CB_MAX;
		limit_lower = CB_MIN;
		break;
	default:
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "Test Failed (Invalid test type)\n");
		return ret;
	}

	max = min = fts_scap_data[0];

	for (i = 0; i < SCAP_BUF_SIZE; i++) {

		if (test_type == CB_TEST && i == MAX_COL)
			ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "============= CB Test Result TX=============\n");

		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "[%2d] ", i+1);

		if (test_type == SCAP_RAW_DATA_TEST)
			ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "%5d ", fts_scap_data[i]);
		else
			ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "%4d ", fts_scap_data[i]);

		if (i >= MAX_COL) {
			if (check_limit && (fts_scap_data[i] < limit_lower || fts_scap_data[i] > limit_upper)) {
				fail_count++;
				TOUCH_I("RT Scap Test : fts_scap_data[%d] = %d\n", i, fts_scap_data[i]);
				if (fail_count < 5) {
					fail_buf_ret += snprintf(fail_log_buf + fail_buf_ret, FAIL_LOG_BUF_SIZE - fail_buf_ret,
							"Scap Test : fts_scap_data[%d] = %d\n", i, fts_scap_data[i]);
				}
			}

			if (fts_scap_data[i] < min)
				min = fts_scap_data[i];
			if (fts_scap_data[i] > max)
				max = fts_scap_data[i];
		}

		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "\n");

	}

	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "==================================================\n");

	if (fail_count > 0) {
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "Test FAIL : %d Errors\n", fail_count);
	} else {
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "Test PASS : No Errors\n");
		*test_result = TEST_PASS;
	}

	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "MAX = %d, MIN = %d, Upper = %d, Lower = %d\n\n", max, min, limit_upper, limit_lower);

	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, fail_log_buf);

	return ret;
}

static ssize_t show_sd(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;
	int ret_size = 0, ret_total_size = 0;
	int rawdata_test_result = TEST_FAIL;
	int scap_rawdata_test_result = TEST_FAIL;
	int channel_status_test_result = TEST_FAIL;
	int jitter_test_result = TEST_FAIL;
	int adc_test_result = TEST_FAIL;
	int cb_test_result = TEST_FAIL;
	u8 data = 0x00;

	TOUCH_I("%s\n", __func__);

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_E("IC state is deep sleep.\n");
		ret = snprintf(buf, PAGE_SIZE, "IC state is deep sleep.\n");
		return ret;
	}

	if (atomic_read(&ts->state.fb) != FB_RESUME) {
		TOUCH_E("fb state is not resume.\n");
		ret = snprintf(buf, PAGE_SIZE, "fb state is not resume.\n");
		return ret;
	}

	if (!ts->lpwg.screen) {
		TOUCH_E("Screen state is not ON.\n");
		ret = snprintf(buf, PAGE_SIZE, "Screen state is not ON.\n");
		return ret;
	}

	/* file create , time log */
	TOUCH_I("Show_sd Test Start\n");
	write_file(dev, "\nShow_sd Test Start", TIME_INFO_SKIP);
	write_file(dev, "\n", TIME_INFO_WRITE);

	touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);
	mutex_lock(&ts->lock);

	ft5726_reset_ctrl(dev, HW_RESET_NO_INIT);

	ret = ft5726_reg_read(dev, 0xB1, &data, 1);  // IC_TYPE READ
	if (ret < 0) {
		TOUCH_E("read IC_TYPE error\n");
		goto EXIT;
	}

	ic_type = data;
	TOUCH_I("ic_type : %d", data);

	// Change clb switch
	ret = ft5726_switch_cal(dev, 1);
	if (ret < 0)
		goto EXIT;

	// Change to factory mode
	ret = ft5726_change_op_mode(dev, FTS_FACTORY_MODE);
	if (ret < 0)
		goto EXIT;

	// Start to raw data test
	TOUCH_I("Show_sd : Raw data test\n");
	memset(log_buf, 0, LOG_BUF_SIZE);

	ret = ft5726_prd_check_ch_num(dev);
	if (ret < 0)
		goto EXIT;

#if 1
	ret = ft5726_prd_get_raw_data_sd(dev);
#else
	ret = ft5726_prd_get_raw_data(dev);
#endif

	if (ret < 0)
		goto EXIT;

	ret_size = ft5726_prd_test_data(dev, RAW_DATA_TEST, &rawdata_test_result);

	TOUCH_I("Raw Data Test Result : %d\n", rawdata_test_result);
	write_file(dev, log_buf, TIME_INFO_SKIP);

	touch_msleep(30);

	// Start to jitter test
	TOUCH_I("Show_sd : jitter test\n");
	memset(log_buf, 0, LOG_BUF_SIZE);

	ret = ft5726_prd_get_jitter_data(dev);
	if (ret < 0)
		goto EXIT;

	ret_size = ft5726_prd_test_data(dev, JITTER_TEST, &jitter_test_result);

	TOUCH_I("jitter Test Result : %d\n", jitter_test_result);
	write_file(dev, log_buf, TIME_INFO_SKIP);

	touch_msleep(30);

	// Start to SCAP Rawdata test
	TOUCH_I("Show_sd : SCAP Raw data test\n");
	memset(log_buf, 0, LOG_BUF_SIZE);

	ret = ft5726_prd_get_scap_raw_data(dev);
	if (ret < 0)
		goto EXIT;

	ret_size = ft5726_prd_scap_test_data(dev, SCAP_RAW_DATA_TEST, &scap_rawdata_test_result);

	TOUCH_I("SCAP Raw Data Test Result : %d\n", scap_rawdata_test_result);
	write_file(dev, log_buf, TIME_INFO_SKIP);

	touch_msleep(30);

	// Start to SCAP CB test
	TOUCH_I("Show_sd : SCAP CB test\n");
	memset(log_buf, 0, LOG_BUF_SIZE);

	ret = ft5726_prd_get_cb_data(dev);
	if (ret < 0)
		goto EXIT;

	ret_size = ft5726_prd_scap_test_data(dev, CB_TEST, &cb_test_result);

	TOUCH_I("SCAP CB Test Result : %d\n", cb_test_result);
	write_file(dev, log_buf, TIME_INFO_SKIP);

	touch_msleep(30);

	/* ADC open_short test */
	ft5726_reset_ctrl(dev, HW_RESET_NO_INIT);

	ret = ft5726_change_op_mode(dev, FTS_FACTORY_MODE);
	if (ret < 0)
		goto EXIT;

	TOUCH_I("Show_sd : Weak Short Circuit test(Open/Short Test)\n");
	memset(log_buf, 0, LOG_BUF_SIZE);

	ret = ft5726_prd_get_adc_data(dev);
	if (ret < 0)
		goto EXIT;

	ret_size = ft5726_prd_test_data_adc(dev, &adc_test_result);

	TOUCH_I("Weak Short Circuit Test Result : %d\n", adc_test_result);
	write_file(dev, log_buf, TIME_INFO_SKIP);

	touch_msleep(30);

EXIT:

	if (jitter_test_result && adc_test_result && scap_rawdata_test_result && cb_test_result)
		channel_status_test_result = TEST_PASS;

	// Test result
	ret = snprintf(log_buf, LOG_BUF_SIZE, "\n========RESULT=======\n");
	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "Raw Data : %s", (rawdata_test_result == TEST_PASS) ? "Pass\n" : "Fail\n");
	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "Channel Status : %s", (channel_status_test_result == TEST_PASS) ? "Pass\n" : "Fail\n");

	if (channel_status_test_result != TEST_PASS)
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "Jitter(%d),Short(%d),SCap(%d),CB(%d)\n",
				jitter_test_result, adc_test_result, scap_rawdata_test_result, cb_test_result);

	write_file(dev, log_buf, TIME_INFO_SKIP);
	write_file(dev, "Show_sd Test End\n", TIME_INFO_WRITE);

	log_file_size_check(dev);

	memcpy(buf + ret_total_size, log_buf, ret);
	ret_total_size += ret;

	TOUCH_I("========RESULT=======\n");
	TOUCH_I("Raw Data : %s", (rawdata_test_result == TEST_PASS) ? "Pass\n" : "Fail\n");
	TOUCH_I("Channel Status : %s", (channel_status_test_result == TEST_PASS) ? "Pass\n" : "Fail\n");
	TOUCH_I("Jitter Test : %s", (jitter_test_result == TEST_PASS) ? "Pass\n" : "Fail\n");
	TOUCH_I("Weak short Circuit Test : %s", (adc_test_result == TEST_PASS) ? "Pass\n" : "Fail\n");
	TOUCH_I("Scap Raw Data Test : %s", (scap_rawdata_test_result == TEST_PASS) ? "Pass\n" : "Fail\n");
	TOUCH_I("CB Test : %s", (cb_test_result == TEST_PASS) ? "Pass\n" : "Fail\n");
	TOUCH_I("Show_sd Test End\n");

	mutex_unlock(&ts->lock);
	ft5726_reset_ctrl(dev, HW_RESET_SYNC);

	return ret_total_size;

}

static ssize_t show_lpwg_sd(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;
	int ret_size = 0, ret_total_size = 0;
	int rawdata_test_result = TEST_FAIL;
	int jitter_test_result = TEST_FAIL;
	int total_lpwg_test_result = TEST_FAIL;
	u8 data = 0x00;

	TOUCH_I("%s\n", __func__);

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_E("IC state is deep sleep.\n");
		if (ts->lpwg.mode == LPWG_NONE) {
			TOUCH_E("Please turn on Knock_On\n");
			ret = snprintf(buf, PAGE_SIZE, "Please turn on Knock_On\n");
		}
		return ret;
	}

	if (atomic_read(&ts->state.fb) != FB_SUSPEND) {
		TOUCH_E("fb state is not suspend.\n");
		ret = snprintf(buf, PAGE_SIZE, "fb state is not suspend.\n");
		return ret;
	}

	if (ts->lpwg.screen) {
		if (!ts->mfts_lpwg) {
			TOUCH_E("Screen state is not OFF.\n");
			ret = snprintf(buf, PAGE_SIZE, "Screen state is not OFF.\n");
			return ret;
		}
	}

	/* file create , time log */
	TOUCH_I("Show_lpwg_sd Test Start\n");
	write_file(dev, "\nShow_lpwg_sd Test Start", TIME_INFO_SKIP);
	write_file(dev, "\n", TIME_INFO_WRITE);

	touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);
	mutex_lock(&ts->lock);

	ft5726_reset_ctrl(dev, HW_RESET_NO_INIT);

	// IC_TYPE READ for ADC Test
	ret = ft5726_reg_read(dev, 0xB1, &data, 1);
	if (ret < 0) {
		TOUCH_E("i2c error\n");
		goto EXIT;
	}

	ic_type = data;
	TOUCH_I("ic_type : %d", data);

	// Change clb switch
	ret = ft5726_switch_cal(dev, 1);
	if (ret < 0)
		goto EXIT;

	// Change to factory mode
	ret = ft5726_change_op_mode(dev, FTS_FACTORY_MODE);
	if (ret < 0)
		goto EXIT;

	// Start to lpwg raw data test
	TOUCH_I("Show_lpwg_sd : LPWG Raw data test\n");
	memset(log_buf, 0, LOG_BUF_SIZE);

	ret = ft5726_prd_check_ch_num(dev);
	if (ret < 0)
		goto EXIT;

#if 1
	ret = ft5726_prd_get_raw_data_sd(dev);
#else
	ret = ft5726_prd_get_raw_data(dev);
#endif

	if (ret < 0)
		goto EXIT;

	ret_size = ft5726_prd_test_data(dev, LPWG_RAW_DATA_TEST, &rawdata_test_result);

	TOUCH_I("LPWG Raw Data Test Result : %d\n", rawdata_test_result);
	write_file(dev, log_buf, TIME_INFO_SKIP);

	touch_msleep(30);

	// Start to lpwg jitter test
	TOUCH_I("Show_lpwg_sd : lpwg jitter test\n");
	memset(log_buf, 0, LOG_BUF_SIZE);

	ft5726_reset_ctrl(dev, HW_RESET_NO_INIT);

	ret = ft5726_change_op_mode(dev, FTS_FACTORY_MODE);
	if (ret < 0)
		goto EXIT;

	ret = ft5726_prd_get_jitter_data(dev);
	if (ret < 0)
		goto EXIT;

	ret_size = ft5726_prd_test_data(dev, LPWG_JITTER_TEST, &jitter_test_result);

	TOUCH_I("jitter Test Result : %d\n", jitter_test_result);
	write_file(dev, log_buf, TIME_INFO_SKIP);

	touch_msleep(30);

EXIT:

	if (rawdata_test_result && jitter_test_result)
		total_lpwg_test_result = TEST_PASS;

	// Test result
	ret = snprintf(log_buf, LOG_BUF_SIZE, "\n========RESULT=======\n");
	ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "LPWG RawData : %s",
			(total_lpwg_test_result == TEST_PASS) ? "Pass\n" : "Fail\n");

	if (total_lpwg_test_result != TEST_PASS)
		ret += snprintf(log_buf + ret, LOG_BUF_SIZE - ret, "Raw(%d),Jitter(%d)\n",
				rawdata_test_result, jitter_test_result);

	write_file(dev, log_buf, TIME_INFO_SKIP);
	write_file(dev, "Show_lpwg_sd Test End\n", TIME_INFO_WRITE);

	log_file_size_check(dev);

	memcpy(buf + ret_total_size, log_buf, ret);
	ret_total_size += ret;

	TOUCH_I("========RESULT=======\n");
	TOUCH_I("LPWG Raw Data : %s", (rawdata_test_result == TEST_PASS) ? "Pass\n" : "Fail\n");
	TOUCH_I("Jitter Test : %s", (jitter_test_result == TEST_PASS) ? "Pass\n" : "Fail\n");
	TOUCH_I("Show_lpwg_sd Test End\n");

	mutex_unlock(&ts->lock);
	ft5726_reset_ctrl(dev, HW_RESET_SYNC);

	return ret_total_size;

}

static ssize_t show_scap_rawdata(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;
	int ret_size = 0;
	int test_result;

	TOUCH_I("Show SCAP Raw Data\n");

	if (atomic_read(&ts->state.fb) >= FB_SUSPEND) {
		TOUCH_I("state.fb is not FB_RESUME\n");
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "LCD off!!! Can not scap_rawdata.\n");
		return ret;
	}

	mutex_lock(&ts->lock);

	// Change clb switch
	ret = ft5726_switch_cal(dev, 0);
	if (ret < 0)
		goto FAIL;

	ret = ft5726_change_op_mode(dev, FTS_FACTORY_MODE);
	if (ret < 0)
		goto FAIL;

	ret = ft5726_prd_get_scap_raw_data(dev);
	if (ret < 0)
		goto FAIL;

	ret = ft5726_change_op_mode(dev, FTS_WORK_MODE);
	if (ret < 0)
		goto FAIL;

	TOUCH_I("Show SCAP Raw Data OK !!!\n");

	ret = ft5726_switch_cal(dev, 1);  // 1: Calibration ON
	if (ret < 0)
		goto FAIL;

	ret_size = ft5726_prd_scap_test_data(dev, SCAP_RAW_DATA_TEST, &test_result);
	memcpy(buf, log_buf, ret_size);
	TOUCH_I("SCAP Raw Data Test Result : %d\n", test_result);

	mutex_unlock(&ts->lock);

	return ret_size;

FAIL:

	TOUCH_I("Show SCAP Raw Data FAIL !!!\n");
	mutex_unlock(&ts->lock);

	return 0;
}

static ssize_t show_cb(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;
	int ret_size = 0;
	int test_result;
	//u8 data = 0x00;

	TOUCH_I("Show CB Data\n");

	if (atomic_read(&ts->state.fb) >= FB_SUSPEND) {
		TOUCH_I("state.fb is not FB_RESUME\n");
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "LCD off!!! Can not cb.\n");
		return ret;
	}

	mutex_lock(&ts->lock);

	// Change clb switch
	ret = ft5726_switch_cal(dev, 1);
	if (ret < 0)
		goto FAIL;

	ret = ft5726_change_op_mode(dev, FTS_FACTORY_MODE);
	if (ret < 0)
		goto FAIL;

	ret = ft5726_prd_get_cb_data(dev);
	if (ret < 0)
		goto FAIL;

	ret = ft5726_change_op_mode(dev, FTS_WORK_MODE);
	if (ret < 0)
		goto FAIL;

	TOUCH_I("Show CB Data OK !!!\n");

	ret = ft5726_switch_cal(dev, 1);  // 1: Calibration ON
	if (ret < 0)
		goto FAIL;

	ret_size = ft5726_prd_scap_test_data(dev, CB_TEST, &test_result);
	memcpy(buf, log_buf, ret_size);
	TOUCH_I("CB Data Test Result : %d\n", test_result);

	mutex_unlock(&ts->lock);

	return ret_size;

FAIL:

	TOUCH_I("Show CB Data FAIL !!!\n");
	mutex_unlock(&ts->lock);

	return 0;
}

static ssize_t show_short(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;
	int ret_size = 0;
	int test_result;
	u8 data = 0x00;

	TOUCH_I("Show Short Data\n");

	if (atomic_read(&ts->state.fb) >= FB_SUSPEND) {
		TOUCH_I("state.fb is not FB_RESUME\n");
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "LCD off!!! Can not sd.\n");
		return ret;
	}
	mutex_lock(&ts->lock);
	touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);

	// IC_TYPE READ for ADC Test
	ret = ft5726_reg_read(dev, 0xB1, &data, 1);
	if (ret < 0) {
		TOUCH_E("read IC_TYPE error\n");
		return ret;
	}

	ic_type = data;
	TOUCH_I("ic_type : %d", data);

	// Change clb switch
	ret = ft5726_switch_cal(dev, 0);
	if (ret < 0)
		goto FAIL;

	ret = ft5726_change_op_mode(dev, FTS_FACTORY_MODE);
	if (ret < 0)
		goto FAIL;

	ret = ft5726_prd_get_adc_data(dev);
	if (ret < 0)
		goto FAIL;

	ret = ft5726_change_op_mode(dev, FTS_WORK_MODE);
	if (ret < 0)
		goto FAIL;

	// Change clb switch
	ret = ft5726_switch_cal(dev, 1);
	if (ret < 0)
		goto FAIL;

	TOUCH_I("Show Short Data OK !!!\n");

	ret_size = ft5726_prd_test_data_adc(dev, &test_result);

	memcpy(buf, log_buf, ret_size);
	TOUCH_I("Show Short Data Result : %d\n", test_result);

	touch_interrupt_control(ts->dev, INTERRUPT_ENABLE);
	mutex_unlock(&ts->lock);

	return ret_size;

FAIL:

	TOUCH_I("Show Delta Data FAIL !!!\n");
	touch_interrupt_control(ts->dev, INTERRUPT_ENABLE);
	mutex_unlock(&ts->lock);

	return 0;
}

static ssize_t show_rawdata_bin(struct file *filp,
		struct kobject *kobj, struct bin_attribute *bin_attr,
		char *buf, loff_t off, size_t count)
{
	struct touch_core_data *ts = container_of(kobj, struct touch_core_data, kobj);
	int ret = 0;
	int test_result = 0;
	static int ret_size = 0;

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_E("IC state is deep sleep.");
		return 0;
	}

	TOUCH_I("%s : off[%d] count[%d]\n", __func__, (int)off, (int)count);

	if (off == 0) {

		TOUCH_I("Show Raw Data\n");

		memset(log_buf, 0, LOG_BUF_SIZE);

		mutex_lock(&ts->lock);

		// Change clb switch
		ret = ft5726_switch_cal(ts->dev, 0);  // 0: Calibration OFF
		if (ret < 0)
			goto FAIL;

		ret = ft5726_change_op_mode(ts->dev, FTS_FACTORY_MODE);
		if (ret < 0)
			goto FAIL;

		ret = ft5726_prd_get_raw_data(ts->dev);
		if (ret < 0)
			goto FAIL;

		ret = ft5726_change_op_mode(ts->dev, FTS_WORK_MODE);
		if (ret < 0)
			goto FAIL;

		TOUCH_I("Show Raw Data OK !!!\n");

		//do not recalibration
		ret = ft5726_switch_cal(ts->dev, 0);  // 1: Calibration ON
		if (ret < 0)
			goto FAIL;

		ret_size = ft5726_prd_test_data(ts->dev, RAW_DATA_TEST, &test_result);
		TOUCH_I("Raw Data Test Result : %d\n", test_result);

FAIL:
		mutex_unlock(&ts->lock);

		if (ret < 0) {
			TOUCH_I("Show Raw Data FAIL !!!\n");
			return 0;
		}

	}

	if (off + count > LOG_BUF_SIZE) {
		TOUCH_I("%s size error offset[%d] size[%d]\n", __func__, (int)off, (int)count);
	} else {
		if (ret_size > count) {
			ret_size -= count;
		} else {
			count = ret_size;
			ret_size = 0;
		}
		memcpy(buf, &log_buf[off], count);
	}

	return count;

}

static ssize_t show_delta_bin(struct file *filp,
		struct kobject *kobj, struct bin_attribute *bin_attr,
		char *buf, loff_t off, size_t count)
{
	struct touch_core_data *ts = container_of(kobj, struct touch_core_data, kobj);
	int ret = 0;
	int test_result = 0;
	static int ret_size = 0;

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_E("IC state is deep sleep.");
		return 0;
	}

	TOUCH_I("%s : off[%d] count[%d]\n", __func__, (int)off, (int)count);

	if (off == 0) {

		TOUCH_I("Show Delta Data\n");

		memset(log_buf, 0, LOG_BUF_SIZE);

		mutex_lock(&ts->lock);

		// Change clb switch
		ret = ft5726_switch_cal(ts->dev, 0);
		if (ret < 0)
			goto FAIL;

		ret = ft5726_change_op_mode(ts->dev, FTS_FACTORY_MODE);
		if (ret < 0)
			goto FAIL;

		ret = ft5726_prd_get_delta_data(ts->dev);
		if (ret < 0)
			goto FAIL;

		ret = ft5726_change_op_mode(ts->dev, FTS_WORK_MODE);
		if (ret < 0)
			goto FAIL;

		TOUCH_I("Show Delta Data OK !!!\n");

		//do not recalibration
		ret = ft5726_switch_cal(ts->dev, 0);  // 1: Calibration ON
		if (ret < 0)
			goto FAIL;

		ret_size = ft5726_prd_test_data(ts->dev, DELTA_SHOW, &test_result);
		TOUCH_I("Show Delta Data Result : %d\n", test_result);

FAIL:
		mutex_unlock(&ts->lock);

		if (ret < 0) {
			TOUCH_I("Show Delta Data FAIL !!!\n");
			return 0;
		}

	}

	if (off + count > LOG_BUF_SIZE) {
		TOUCH_I("%s size error offset[%d] size[%d]\n", __func__, (int)off, (int)count);
	} else {
		if (ret_size > count) {
			ret_size -= count;
		} else {
			count = ret_size;
			ret_size = 0;
		}
		memcpy(buf, &log_buf[off], count);
	}

	return count;

}

static ssize_t show_jitter_bin(struct file *filp,
		struct kobject *kobj, struct bin_attribute *bin_attr,
		char *buf, loff_t off, size_t count)
{
	struct touch_core_data *ts = container_of(kobj, struct touch_core_data, kobj);
	int ret = 0;
	int test_result = 0;
	static int ret_size = 0;

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_E("IC state is deep sleep.");
		return 0;
	}

	TOUCH_I("%s : off[%d] count[%d]\n", __func__, (int)off, (int)count);

	if (off == 0) {

		TOUCH_I("Show jitter\n");

		memset(log_buf, 0, LOG_BUF_SIZE);

		mutex_lock(&ts->lock);

		ft5726_reset_ctrl(ts->dev, HW_RESET_NO_INIT);

		ret = ft5726_change_op_mode(ts->dev, FTS_FACTORY_MODE);
		if (ret < 0)
			goto FAIL;

		ret = ft5726_prd_get_jitter_data(ts->dev);
		if (ret < 0)
			goto FAIL;

		ret = ft5726_change_op_mode(ts->dev, FTS_WORK_MODE);
		if (ret < 0)
			goto FAIL;

		TOUCH_I("Show jitter OK !!!\n");

		ret_size = ft5726_prd_test_data(ts->dev, JITTER_TEST, &test_result);
		TOUCH_I("jitter Test Result : %d\n", test_result);

FAIL:

		mutex_unlock(&ts->lock);
		ft5726_reset_ctrl(ts->dev, HW_RESET_SYNC);

		if (ret < 0) {
			TOUCH_I("Show jitter FAIL !!!\n");
			return 0;
		}

	}

	if (off + count > LOG_BUF_SIZE) {
		TOUCH_I("%s size error offset[%d] size[%d]\n", __func__, (int)off, (int)count);
	} else {
		if (ret_size > count) {
			ret_size -= count;
		} else {
			count = ret_size;
			ret_size = 0;
		}
		memcpy(buf, &log_buf[off], count);
	}

	return count;

}

static ssize_t show_limit_bin(struct file *filp,
		struct kobject *kobj, struct bin_attribute *bin_attr,
		char *buf, loff_t off, size_t count)
{
	struct touch_core_data *ts = container_of(kobj, struct touch_core_data, kobj);
	static int ret_size = 0;
	int i = 0, j = 0;

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_E("IC state is deep sleep.");
		return 0;
	}

	TOUCH_I("%s : off[%d] count[%d]\n", __func__, (int)off, (int)count);

	if (off == 0) {

		TOUCH_I("Get Limit\n");

		memset(log_buf, 0, LOG_BUF_SIZE);

		spec_get_limit(ts->dev, "LowerImageLimit", LowerImage);
		spec_get_limit(ts->dev, "UpperImageLimit", UpperImage);

		ret_size = snprintf(log_buf + ret_size, LOG_BUF_SIZE - ret_size, "======LowerImage====\n");

		for (i = 0; i < MAX_ROW; i++) {
			ret_size += snprintf(log_buf + ret_size, LOG_BUF_SIZE - ret_size, "[%2d] ", i+1);

			for (j = 0; j < MAX_COL; j++)
				ret_size += snprintf(log_buf + ret_size, LOG_BUF_SIZE - ret_size, "%5d ", LowerImage[i][j]);

			ret_size += snprintf(log_buf + ret_size, LOG_BUF_SIZE - ret_size, "\n");
		}

		ret_size += snprintf(log_buf + ret_size, LOG_BUF_SIZE - ret_size, "======UpperImage====\n");

		for (i = 0; i < MAX_ROW; i++) {
			ret_size += snprintf(log_buf + ret_size, LOG_BUF_SIZE - ret_size, "[%2d] ", i+1);

			for (j = 0; j < MAX_COL; j++)
				ret_size += snprintf(log_buf + ret_size, LOG_BUF_SIZE - ret_size, "%5d ", UpperImage[i][j]);

			ret_size += snprintf(log_buf + ret_size, LOG_BUF_SIZE - ret_size, "\n");
		}

	}

	if (off + count > LOG_BUF_SIZE) {
		TOUCH_I("%s size error offset[%d] size[%d]\n", __func__, (int)off, (int)count);
	} else {
		if (ret_size > count) {
			ret_size -= count;
		} else {
			count = ret_size;
			ret_size = 0;
		}
		memcpy(buf, &log_buf[off], count);
	}

	return count;

}

static ssize_t show_rawdata_fir_off_bin(struct file *filp,
		struct kobject *kobj, struct bin_attribute *bin_attr,
		char *buf, loff_t off, size_t count)
{
	struct touch_core_data *ts = container_of(kobj, struct touch_core_data, kobj);
	int ret = 0;
	int test_result = 0;
	static int ret_size = 0;

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_E("IC state is deep sleep.");
		return 0;
	}

	TOUCH_I("%s : off[%d] count[%d]\n", __func__, (int)off, (int)count);

	if (off == 0) {

		TOUCH_I("Show Raw Data(FIR filter off)\n");

		memset(log_buf, 0, LOG_BUF_SIZE);

		mutex_lock(&ts->lock);

		// Change clb switch
		ret = ft5726_switch_cal(ts->dev, 0);  // 0: Calibration OFF
		if (ret < 0)
			goto FAIL;

		ret = ft5726_change_op_mode(ts->dev, FTS_FACTORY_MODE);
		if (ret < 0)
			goto FAIL;

		ret = ft5726_prd_get_raw_data_sd(ts->dev);
		if (ret < 0)
			goto FAIL;

		ret = ft5726_change_op_mode(ts->dev, FTS_WORK_MODE);
		if (ret < 0)
			goto FAIL;
		TOUCH_I("Show Raw Data(FIR filter off) OK !!!\n");

		ret = ft5726_switch_cal(ts->dev, 1);  // 1: Calibration ON
		if (ret < 0)
			goto FAIL;

		ret_size = ft5726_prd_test_data(ts->dev, RAW_DATA_TEST, &test_result);
		TOUCH_I("Raw Data(FIR filter off) Test Result : %d\n", test_result);

FAIL:

		mutex_unlock(&ts->lock);
		ft5726_reset_ctrl(ts->dev, HW_RESET_SYNC); //temp, bring up, dose it need this function?

		if (ret < 0) {
			TOUCH_I("Show Raw Data FAIL !!!\n");
			return 0;
		}

	}


	if (off + count > LOG_BUF_SIZE) {
		TOUCH_I("%s size error offset[%d] size[%d]\n", __func__, (int)off, (int)count);
	} else {
		if (ret_size > count) {
			ret_size -= count;
		} else {
			count = ret_size;
			ret_size = 0;
		}
		memcpy(buf, &log_buf[off], count);
	}

	return count;

}

static TOUCH_ATTR(sd, show_sd, NULL);
static TOUCH_ATTR(lpwg_sd, show_lpwg_sd, NULL);
static TOUCH_ATTR(short_test, show_short, NULL);
static TOUCH_ATTR(scap_rawdata, show_scap_rawdata, NULL);
static TOUCH_ATTR(cb, show_cb, NULL);

#define TOUCH_BIN_ATTR(_name, _read, _write, _size)		\
			struct bin_attribute touch_attr_##_name	\
			= __BIN_ATTR(_name, S_IRUGO | S_IWUSR, _read, _write, _size)

static TOUCH_BIN_ATTR(rawdata, show_rawdata_bin, NULL, LOG_BUF_SIZE);
static TOUCH_BIN_ATTR(delta, show_delta_bin, NULL, LOG_BUF_SIZE);
static TOUCH_BIN_ATTR(jitter, show_jitter_bin, NULL, LOG_BUF_SIZE);
static TOUCH_BIN_ATTR(limit, show_limit_bin, NULL, LOG_BUF_SIZE);
static TOUCH_BIN_ATTR(rawdata_fir_off, show_rawdata_fir_off_bin, NULL, LOG_BUF_SIZE);


static struct attribute *prd_attribute_list[] = {
	&touch_attr_sd.attr,
	&touch_attr_lpwg_sd.attr,
	&touch_attr_short_test.attr,
	&touch_attr_scap_rawdata.attr,
	&touch_attr_cb.attr,
	NULL,
};

static struct bin_attribute *prd_attribute_bin_list[] = {
	&touch_attr_rawdata,
	&touch_attr_delta,
	&touch_attr_jitter,
	&touch_attr_limit,
	&touch_attr_rawdata_fir_off,
	NULL,
};

static const struct attribute_group prd_attribute_group = {
	.attrs = prd_attribute_list,
	.bin_attrs = prd_attribute_bin_list,
};

int ft5726_prd_register_sysfs(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;

	TOUCH_TRACE();

	ret = sysfs_create_group(&ts->kobj, &prd_attribute_group);

	if (ret < 0) {
		TOUCH_E("failed to create sysfs\n");
		return ret;
	}

	return ret;
}

