/* Canvasbio BTP Navigation Driver
 *
 * Copyright (c) 2018 Canvasbio
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 */

//#define DEBUG

#define SUPPORT_TAP_PROCESS

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/wakelock.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/wait.h>


MODULE_AUTHOR("Canvasbio <www.canvasbio.com>");
MODULE_DESCRIPTION("BTP sensor navigation driver.");
MODULE_LICENSE("GPL");


extern ssize_t (* cb_navi_show_event)(struct device *, struct device_attribute *, char *);
extern ssize_t (* cb_navi_store_event)(struct device *, struct device_attribute *, const char *, size_t);


typedef struct _cb_navi_stat {
    int finger_on;
    int delta_x;
    int delta_y;
    int sum_x;
    int sum_y;
    int moved;
    int valid_delta_count;

#if defined (SUPPORT_TAP_PROCESS)
    int tap_finger_on_max_time;
    int tap_finger_on_min_time;
    int tap_finger_off_max_time;
    int tap_finger_off_min_time;
    int tap_delta_sum_limit;
    int valid_delta_min_count;
#endif
} cb_navi_stat;

enum {
    EVENT_NONE         = 0,
    EVENT_UP           = 1,
    EVENT_DOWN,
    EVENT_LEFT,
    EVENT_RIGHT,
    EVENT_DOUBLE_UP    = 11,
    EVENT_DOUBLE_DOWN,
    EVENT_DOUBLE_LEFT,
    EVENT_DOUBLE_RIGHT,
    EVENT_TAP          = 21,
    EVENT_DOUBLETAP,
    EVENT_FINGER_ON    = 31,
    EVENT_FINGER_OFF,
    EVENT_DELTA_VALUE  = 41,
    EVENT_DELTA_NOT_FOUND,
};

static cb_navi_stat g_navi_stat;
static struct input_dev *cb_dev;

#if defined (SUPPORT_TAP_PROCESS)
static wait_queue_head_t g_tap_wait;
static struct task_struct *g_tap_thread_id=NULL;
static int tap_thread_on;
#endif

#define CB_ABS(x)       ((x < 0) ? (-(x)) : (x))

#define NAVI_CONTROL_MODE_RW  (S_IWUSR | S_IRUSR | S_IRGRP | S_IWGRP | S_IROTH )
#define NAVI_CONTROL_MODE_R   (S_IRUSR | S_IRGRP | S_IROTH)

#define GET_NAVI_ATTR(name) g_navi_stat.name
#define NAVI_DEV_ATTR(name)                                             \
static ssize_t cb_navi_dev_show_##name(struct device *dev,              \
                                       struct device_attribute *attr,   \
                                       char *buf)                       \
{                                                                       \
    return scnprintf(buf, PAGE_SIZE, "%d\n", GET_NAVI_ATTR(name));      \
}                                                                       \
static ssize_t cb_navi_dev_store_##name(struct device *dev,             \
                                        struct device_attribute *attr,  \
                                        const char *buf,                \
                                        size_t count)                   \
{                                                                       \
    int val = 0;                                                        \
    if ((sscanf(buf, "%i", &val)) < 0)                                  \
        return -EINVAL;                                                 \
    GET_NAVI_ATTR(name) = val;                                          \
    return strnlen(buf, count);                                         \
}                                                                       \
static DEVICE_ATTR(name,                                                \
                   NAVI_CONTROL_MODE_RW,                                \
                   cb_navi_dev_show_##name,                             \
                   cb_navi_dev_store_##name)

NAVI_DEV_ATTR(finger_on);
NAVI_DEV_ATTR(delta_x);
NAVI_DEV_ATTR(delta_y);
NAVI_DEV_ATTR(sum_x);
NAVI_DEV_ATTR(sum_y);
#if defined (SUPPORT_TAP_PROCESS)
NAVI_DEV_ATTR(tap_finger_on_max_time);
NAVI_DEV_ATTR(tap_finger_on_min_time);
NAVI_DEV_ATTR(tap_finger_off_max_time);
NAVI_DEV_ATTR(tap_finger_off_min_time);
NAVI_DEV_ATTR(tap_delta_sum_limit);
NAVI_DEV_ATTR(valid_delta_min_count);
#endif

static struct attribute *cb_fp_input_attrs[] = {
    &dev_attr_finger_on.attr,
    &dev_attr_delta_x.attr,
    &dev_attr_delta_y.attr,
    &dev_attr_sum_x.attr,
    &dev_attr_sum_y.attr,

#if defined (SUPPORT_TAP_PROCESS)
    &dev_attr_tap_finger_on_max_time.attr,
    &dev_attr_tap_finger_on_min_time.attr,
    &dev_attr_tap_finger_off_max_time.attr,
    &dev_attr_tap_finger_off_min_time.attr,
    &dev_attr_tap_delta_sum_limit.attr,
    &dev_attr_valid_delta_min_count.attr,
#endif
    NULL
};

static const struct attribute_group cb_fp_input_attrs_group = {
    .attrs = cb_fp_input_attrs,
};


#if defined (SUPPORT_TAP_PROCESS)
static int tap_process_kthread(void *arg)
{
    int hold_thr = 0;
    int tap_state = 0;
    while(!kthread_should_stop())
    {
        switch(tap_state)
        {
        case 1:
            hold_thr = g_navi_stat.tap_finger_on_max_time;
            tap_state = 0;
            while(hold_thr--) {
                if(g_navi_stat.finger_on == 0 &&
                   CB_ABS(g_navi_stat.sum_x) < g_navi_stat.tap_delta_sum_limit  &&
                   CB_ABS(g_navi_stat.sum_y) < g_navi_stat.tap_delta_sum_limit) {
                    if(g_navi_stat.valid_delta_count > g_navi_stat.valid_delta_min_count &&
                       g_navi_stat.tap_finger_on_max_time - hold_thr > g_navi_stat.tap_finger_on_min_time)
                        tap_state = 2;
                    break;
                }
                usleep_range(10000, 11000);
            }
            break;
        case 2:
            hold_thr = g_navi_stat.tap_finger_off_max_time;
            tap_state = 0;
            while(hold_thr--) {
                if(g_navi_stat.finger_on == 1) {
                    if(g_navi_stat.tap_finger_off_max_time - hold_thr > g_navi_stat.tap_finger_off_min_time)
                        tap_state = 3;
                    break;
                }
                usleep_range(10000, 11000);
            }
            break;
        case 3:
            hold_thr = g_navi_stat.tap_finger_on_max_time;
            tap_state = 0;
            while(hold_thr--) {
                if(g_navi_stat.finger_on == 0 &&
                   CB_ABS(g_navi_stat.sum_x) < g_navi_stat.tap_delta_sum_limit  &&
                   CB_ABS(g_navi_stat.sum_y) < g_navi_stat.tap_delta_sum_limit ) {
                    if(g_navi_stat.tap_finger_on_max_time - hold_thr > g_navi_stat.tap_finger_on_min_time)
                        tap_state = 4;
                    break;
                }
                usleep_range(10000, 11000);
            }
            break;
        case 4:
            pr_debug(KERN_DEBUG "%s [DEBUG] Double tap event, valid delta:%d\n",__func__,g_navi_stat.valid_delta_count);
            input_report_key(cb_dev, KEY_TAB, 1);
            input_report_key(cb_dev, KEY_TAB, 0);
            input_sync(cb_dev);
            tap_state = 0;
            break;
        default:
            wait_event(g_tap_wait, tap_thread_on);
            tap_thread_on = 0;
            if(g_navi_stat.finger_on)
                tap_state = 1;
            else
                usleep_range(10000, 11000);
        }
    }

    return 0;
}

static int tap_process_kthread_init(void)
{
    pr_debug(KERN_DEBUG "%s \n",__func__);

	init_waitqueue_head(&g_tap_wait);

    if(g_tap_thread_id == NULL)
    {
        g_tap_thread_id = (struct task_struct *)kthread_run(tap_process_kthread, NULL, "tap_process_kthread");
        pr_debug(KERN_DEBUG "%s init done. \n",__func__);
    }

    return 0;
}

static void tap_process_kthread_release(void)
{
    pr_debug(KERN_DEBUG "%s \n",__func__);
    if(g_tap_thread_id)
    {
        kthread_stop(g_tap_thread_id);
        g_tap_thread_id = NULL;
        pr_debug(KERN_DEBUG "%s release done. \n",__func__);
    }
}
#endif  /* SUPPORT_TAP_PROCESS */

static void navi_data_reset(void)
{
    g_navi_stat.delta_x = 0;
    g_navi_stat.delta_y = 0;
    g_navi_stat.sum_x = 0;
    g_navi_stat.sum_y = 0;
    g_navi_stat.valid_delta_count = 0;
}

static ssize_t cb_ori_show_navi_event(struct device *dev, struct device_attribute *attr, char *buf)
{
    pr_debug(KERN_DEBUG  ": [DEBUG] show fourway.\n");
    return 0;
}

static ssize_t cb_ori_store_navi_event(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int val = 0;
    int event = 0;
    signed char delta_x = 0, delta_y = 0;

    if ((sscanf(buf, "%i", &val)) <= 0)
        return -EINVAL;

    event = val & 0xFF;
    switch(event)
    {
    case EVENT_DOUBLETAP:
        input_report_key(cb_dev, KEY_TAB, 1);
        input_report_key(cb_dev, KEY_TAB, 0);
        break;
    case EVENT_FINGER_ON:
        g_navi_stat.finger_on = 1;
        navi_data_reset();
#if defined (SUPPORT_TAP_PROCESS)
        tap_thread_on = 1;
        wake_up(&g_tap_wait);
#endif
        break;
    case EVENT_FINGER_OFF:
        g_navi_stat.finger_on = 0;
        g_navi_stat.moved = 0;
        break;
    case EVENT_DELTA_VALUE:
        delta_x = (val >> 16) & 0xFF;
        delta_y = (val >> 8) & 0xFF;
        g_navi_stat.delta_x = delta_x;
        g_navi_stat.delta_y = delta_y;
        g_navi_stat.sum_x += delta_x;
        g_navi_stat.sum_y += delta_y;
        g_navi_stat.valid_delta_count++;
        if (delta_y) {
            g_navi_stat.moved = delta_y > 0 ? 1 : -1;
            input_report_rel(cb_dev, REL_WHEEL, delta_y);
        } else if (g_navi_stat.moved) {
            input_report_rel(cb_dev, REL_WHEEL, g_navi_stat.moved);
        }

        break;
    case EVENT_DELTA_NOT_FOUND:
        if (g_navi_stat.moved)
            input_report_rel(cb_dev, REL_WHEEL, g_navi_stat.moved);

        break;
    default:
        pr_debug(KERN_DEBUG ": [DEBUG] event %d do nothing.\n", event);
        return -EINVAL;
    }

    pr_debug(KERN_DEBUG ": [DEBUG] event %d delta(%d,%d), sum(%d,%d).\n", event, delta_x, delta_y, g_navi_stat.sum_x, g_navi_stat.sum_y);

    input_sync(cb_dev);

    return strnlen(buf, count);
}

static void navi_stat_init(void)
{
    g_navi_stat.finger_on               = 0;
    g_navi_stat.delta_x                 = 0;
    g_navi_stat.delta_y                 = 0;
    g_navi_stat.sum_x                   = 0;
    g_navi_stat.sum_y                   = 0;
    g_navi_stat.moved                   = 0;
    g_navi_stat.valid_delta_count       = 0;
#if defined (SUPPORT_TAP_PROCESS)
    g_navi_stat.tap_finger_on_max_time  = 25; /* x10ms */
    g_navi_stat.tap_finger_on_min_time  = 0;  /* x10ms */
    g_navi_stat.tap_finger_off_max_time = 20; /* x10ms */
    g_navi_stat.tap_finger_off_min_time = 0;  /* x10ms */
    g_navi_stat.tap_delta_sum_limit     = 5;
    g_navi_stat.valid_delta_min_count   = 0;
#endif
}

static int __init cb_fp_input_init(void)
{
    int err;
    int i;
    int keys[] = {KEY_UP,
                  KEY_LEFT,
                  KEY_RIGHT,
                  KEY_DOWN,
                  KEY_TAB,
                  0};
    pr_debug(KERN_DEBUG  ": [DEBUG] %s:%i %s\n", __FILE__, __LINE__, __func__);

    cb_dev = input_allocate_device();
	if (!cb_dev)
		return -ENOMEM;

	cb_dev->name = "cb_fp_input";
	cb_dev->phys = "cb_fp_input/input0";
	cb_dev->id.bustype = BUS_VIRTUAL;
	cb_dev->id.vendor = 0x0001;
	cb_dev->id.product = 0x0002;
	cb_dev->id.version = 0x0100;

	cb_dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) |
		BIT_MASK(BTN_MIDDLE) | BIT_MASK(BTN_RIGHT);

    for ( i = 0 ; keys[i] ; i++ )
		set_bit(keys[i],cb_dev->keybit);


	cb_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL) | BIT_MASK(EV_ABS) | BIT_MASK(EV_SYN);
    cb_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y) | BIT_MASK(REL_WHEEL);
	cb_dev->absbit[0] = BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) | BIT_MASK(ABS_WHEEL);

	input_set_abs_params(cb_dev, ABS_X, 0, 1080, 0, 0);
	input_set_abs_params(cb_dev, ABS_Y, 0, 1920, 0, 0);
	input_set_capability(cb_dev, EV_KEY, BTN_TOUCH);

    err = input_register_device(cb_dev);
    if (err) {
        input_free_device(cb_dev);
        return err;
	}

    cb_navi_show_event = cb_ori_show_navi_event;
    cb_navi_store_event = cb_ori_store_navi_event;

    navi_stat_init();

	err = sysfs_create_group( &cb_dev->dev.kobj, &cb_fp_input_attrs_group );
    if (err) {
		input_free_device(cb_dev);
		return err;
	}

#if defined (SUPPORT_TAP_PROCESS)
    tap_process_kthread_init();
#endif

    return 0;
}

static void __exit cb_fp_input_exit(void)
{
    pr_debug(KERN_DEBUG  ": [DEBUG] %s:%i %s\n", __FILE__, __LINE__, __func__);
	input_unregister_device(cb_dev);

    cb_navi_show_event = NULL;
    cb_navi_store_event = NULL;

    sysfs_remove_group( &cb_dev->dev.kobj, &cb_fp_input_attrs_group );

#if defined (SUPPORT_TAP_PROCESS)
    tap_process_kthread_release();
#endif
}

late_initcall(cb_fp_input_init);
module_exit(cb_fp_input_exit);
