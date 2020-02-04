/* Canvasbio BTP Sensor Driver
 *
 * Copyright (c) 2017 Canvasbio
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 */

#define DEBUG

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/wakelock.h>
#include <linux/platform_device.h>


#define CB_VERSION                      "1.0.0"

/**************************debug******************************/
#define ERR_LOG     (0)
#define INFO_LOG    (1)
#define DEBUG_LOG   (2)

/* debug log setting */
u8 debug_level = DEBUG_LOG;

#define CB_LOG(level, fmt, args...) do { \
    if (debug_level >= level) { \
         printk("[CBFP] " fmt, ##args); \
    } \
} while (0)

/* -------------------------------------------------------------------- */
/* driver constants                                                     */
/* -------------------------------------------------------------------- */
#define CBFP_DEV_NAME           "btp"

#define CB_WAKE_LOCK_HOLD_TIME  1000

/* Constants for THERMAL_CONTROL and LCD_CONTROL */
#define CBFP_IRQ_FP_OFF         0  /* Reading value from GPIO */
#define CBPF_IRQ_FP_ON          1  /* Reading value from GPIO */
#define CBFP_IRQ_FP_STANDBY     2
#define CBFP_IRQ_FP_DISABLED    3

/* -------------------------------------------------------------------- */
/* sysfs permission                                                     */
/* -------------------------------------------------------------------- */
#define HW_CONTROL_MODE_RW   (S_IWUSR | S_IRUSR | S_IRGRP | S_IWGRP | S_IROTH )
#define HW_CONTROL_MODE_R    (S_IRUSR | S_IRGRP | S_IROTH)


/* -------------------------------------------------------------------- */
/* data types                                                           */
/* -------------------------------------------------------------------- */
struct cb_data {
    struct platform_device *pdev;
    struct device *device;
    struct semaphore mutex;
    u32 reset_gpio;
    u32 irq_gpio;
    u32 irq;
    int interrupt_done;
    struct wake_lock fp_wake_lock;

    u32 fp_vdd_gpio;
    u32 fp_vddio_gpio;
};
typedef struct cb_data cbfp_data_t;

ssize_t (* cb_navi_show_event)(struct device *, struct device_attribute *, char *) = NULL;
ssize_t (* cb_navi_store_event)(struct device *, struct device_attribute *, char *, size_t) = NULL;

/* -------------------------------------------------------------------- */
/* function prototypes                          */
/* -------------------------------------------------------------------- */
static ssize_t cb_show_attr_reset(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t cb_store_attr_reset(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t cb_show_attr_irq(struct device *dev, struct device_attribute *attr, char *buf);

static ssize_t cb_show_attr_navi_event(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t cb_store_attr_navi_event(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

static irqreturn_t cb_interrupt_handler(int irq, void * data);
static int cb_manage_sysfs(struct cb_data *cbfp, struct platform_device *pdev, bool create);

static int cbfp_hw_reset(cbfp_data_t *cbfp);
static int cbfp_get_gpio_dts_info(cbfp_data_t *cbfp);
static void cbfp_gpio_free(cbfp_data_t *cbfp);
static int cbfp_cfg_irq_gpio(cbfp_data_t *cbfp);
static void cbfp_hw_power_enable(cbfp_data_t *cbfp, u8 onoff);
//static void cbfp_reset_gpio_control(cbfp_data_t *cbfp, u8 assert);

/* -------------------------------------------------------------------- */
/* External interface                                                   */
/* -------------------------------------------------------------------- */

/* -------------------------------------------------------------------- */
/* devfs                                                                */
/* -------------------------------------------------------------------- */
static DEVICE_ATTR(reset,  HW_CONTROL_MODE_RW, cb_show_attr_reset, cb_store_attr_reset);
static DEVICE_ATTR(irq,   HW_CONTROL_MODE_R, cb_show_attr_irq, NULL);
static DEVICE_ATTR(navi_event,  HW_CONTROL_MODE_RW, cb_show_attr_navi_event, cb_store_attr_navi_event);

static struct attribute *cb_hw_control_attrs[] = {
    &dev_attr_reset.attr,
    &dev_attr_irq.attr,
    &dev_attr_navi_event.attr,
    NULL
};

static const struct attribute_group cb_hw_control_attr_group = {
    .attrs = cb_hw_control_attrs,
};

/* -------------------------------------------------------------------- */
/* function definitions                                                 */
/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */
static ssize_t cb_show_attr_irq(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct cb_data *cbfp = dev_get_drvdata(dev);

    // Finger is on sensor or not.
    return scnprintf(buf, PAGE_SIZE, "%d\n", gpio_get_value(cbfp->irq_gpio));
}

/* -------------------------------------------------------------------- */
static ssize_t cb_show_attr_reset(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct cb_data *cbfp = dev_get_drvdata(dev);

    return scnprintf(buf, PAGE_SIZE, "%d\n", gpio_get_value(cbfp->reset_gpio));
}

/* -------------------------------------------------------------------- */
static ssize_t cb_store_attr_reset(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret;
    char cmd_buf[16];
    struct cb_data *cbfp = dev_get_drvdata(dev);

    ret = sscanf(buf, "%s", cmd_buf);
    if (ret != 1)
        return -EINVAL;

    if (!strcasecmp(cmd_buf, "high")) {
        gpio_set_value(cbfp->reset_gpio, 1);
    } else if (!strcasecmp(cmd_buf, "low")) {
        gpio_set_value(cbfp->reset_gpio, 0);
    } else if (!strcasecmp(cmd_buf, "reset")) {
        cbfp_hw_reset(cbfp);
    } else {
        CB_LOG(ERR_LOG, "%s: reset command is wrong(%s)\n", __func__, cmd_buf);
        return -EINVAL;
    }

    return strnlen(buf, count);
}

static ssize_t cb_show_attr_navi_event(struct device *dev, struct device_attribute *attr, char *buf)
{
    if(cb_navi_show_event != NULL)
        return cb_navi_show_event(dev, attr, buf);

    return 0;
}

static ssize_t cb_store_attr_navi_event(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    if(cb_navi_store_event != NULL)
        return cb_navi_store_event(dev, attr, (char *)buf, count);

    return 0;
}

// 1. set handler to thread_fn
/* -------------------------------------------------------------------- */
static irqreturn_t cb_interrupt_handler(int irq, void * data)
{
    struct cb_data *cbfp = NULL;
    if (!data)
        return IRQ_NONE;

    cbfp = (struct cb_data *) data;
    if (!cbfp)
        return IRQ_NONE;

    CB_LOG(INFO_LOG, "%s\n", __func__);
    smp_rmb();
    wake_lock_timeout(&cbfp->fp_wake_lock, msecs_to_jiffies(CB_WAKE_LOCK_HOLD_TIME));

    cbfp->interrupt_done = 1;

    sysfs_notify(&cbfp->pdev->dev.kobj, NULL, dev_attr_irq.attr.name);

    return IRQ_HANDLED;
    //return IRQ_WAKE_THREAD;
}

/* -------------------------------------------------------------------- */
static int cbfp_hw_reset(struct cb_data *cbfp)
{
    u8 value = 0;

    CB_LOG(INFO_LOG, "%s\n", __func__);

    if (!gpio_is_valid(cbfp->reset_gpio)) {
        CB_LOG(ERR_LOG, "%s: RESET GPIO(%d) is INVALID\n", __func__, cbfp->reset_gpio);
        return 0;
    }

    value = gpio_get_value(cbfp->reset_gpio);
    CB_LOG(DEBUG_LOG, "%s: RESET GPIO(%d) state is %s\n", __func__, cbfp->reset_gpio, (value == 0) ? "low" : "high");
    if (value != 0) {
        gpio_set_value(cbfp->reset_gpio, 0);
        mdelay(1);
    }

    gpio_set_value(cbfp->reset_gpio, 1);
    mdelay(1);

    value = gpio_get_value(cbfp->reset_gpio);
    CB_LOG(DEBUG_LOG, "%s: FIANL RESET GPIO(%d) state is %s\n", __func__, cbfp->reset_gpio, (value == 0) ? "low" : "high");

    //disable_irq_nosync(cbfp->irq);
    disable_irq(cbfp->irq);
    cbfp->interrupt_done = 0;

    mdelay(1);
    enable_irq(cbfp->irq);

    return 0;
}

/* -------------------------------------------------------------------- */
static int cb_request_named_gpio(struct cb_data * cbfp, const char *label, int *gpio)
{
    struct device *dev = &cbfp->pdev->dev;
    struct device_node *np = dev->of_node;

    int rc = 0;

    CB_LOG(INFO_LOG, "%s\n", __func__);

    rc = of_get_named_gpio(np, label, 0);
    if (rc < 0) {
        CB_LOG(ERR_LOG, "%s: Failed to get gpio(%s)\n", __func__, label);
        return rc;
    }

    *gpio = rc;
    CB_LOG(DEBUG_LOG, "%s: Succeed to get gpio [%s(%d)], Trying to request gpio\n", __func__, label, rc);

    rc = devm_gpio_request(dev, *gpio, label);
    if (rc) {
        CB_LOG(ERR_LOG, "%s: Failed to request gpio(%d)\n", __func__, *gpio);
        return rc;
    }

    return 0;
}

/* -------------------------------------------------------------------- */
static int cb_manage_sysfs(struct cb_data *cbfp, struct platform_device *pdev, bool create)
{
    int rc = 0;

    CB_LOG(INFO_LOG, "%s\n", __func__);
    if (create) {
        rc = sysfs_create_group(&cbfp->pdev->dev.kobj, &cb_hw_control_attr_group);
        if (rc) {
            CB_LOG(ERR_LOG, "%s: sysfs_create_group failed. %d\n", __func__, rc);
        }
    } else {
        sysfs_remove_group(&cbfp->pdev->dev.kobj, &cb_hw_control_attr_group);
    }

    return rc;
}


static int cbfp_get_gpio_dts_info(cbfp_data_t *cbfp)
{
    int rc = 0;

    CB_LOG(INFO_LOG, "%s\n", __func__);
    rc = cb_request_named_gpio(cbfp, "fp-vdd-en", &cbfp->fp_vdd_gpio);
    if (rc) {
        return -1;
    }
    gpio_direction_output(cbfp->fp_vdd_gpio, 0);

    rc = cb_request_named_gpio(cbfp, "fp-vddio-en", &cbfp->fp_vddio_gpio);
    if (rc) {
        return -1;
    }
    gpio_direction_output(cbfp->fp_vddio_gpio, 0);

    rc = cb_request_named_gpio(cbfp, "fpint-gpio", &cbfp->irq_gpio);
    if (rc) {
        return -1;
    }
    gpio_direction_input(cbfp->irq_gpio);

    rc = cb_request_named_gpio(cbfp, "fpreset-gpio", &cbfp->reset_gpio);
    if (rc) {
        return -1;
    }
    gpio_direction_output(cbfp->reset_gpio, 0);

    CB_LOG(DEBUG_LOG, "%s: VDD[%d] VDDIO[%d] INT[%d] RST[%d]\n", __func__,
            cbfp->fp_vdd_gpio, cbfp->fp_vddio_gpio, cbfp->irq_gpio, cbfp->reset_gpio);

    return 0;
}

static void cbfp_gpio_free(cbfp_data_t *cbfp)
{
    struct device * dev = &cbfp->pdev->dev;

    CB_LOG(INFO_LOG, "%s\n", __func__);
    if (cbfp->irq_gpio != 0) {
        if(cbfp->irq) free_irq(cbfp->irq, cbfp);
        devm_gpio_free(dev, cbfp->irq_gpio);
    }

    if (cbfp->reset_gpio != 0) {
        devm_gpio_free(dev, cbfp->reset_gpio);
    }

    if (cbfp->fp_vdd_gpio != 0) {
        devm_gpio_free(dev, cbfp->fp_vdd_gpio);
    }

    if (cbfp->fp_vddio_gpio != 0) {
        devm_gpio_free(dev, cbfp->fp_vddio_gpio);
    }
}

static int cbfp_cfg_irq_gpio(cbfp_data_t *cbfp)
{
    int rc = 0;

    CB_LOG(INFO_LOG, "%s\n", __func__);
    cbfp->irq = gpio_to_irq(cbfp->irq_gpio);
    if (cbfp->irq < 0) {
        CB_LOG(ERR_LOG, "%s: Failed to get irq\n", __func__);
        return -1;
    }

    rc = devm_request_threaded_irq(&cbfp->pdev->dev, cbfp->irq,
            NULL, cb_interrupt_handler,
            IRQF_TRIGGER_RISING | IRQF_ONESHOT,
            dev_name(&cbfp->pdev->dev), cbfp);
    if (rc) {
        CB_LOG(ERR_LOG, "%s: Failed to request devm_request_threaded_irq(%i)\n", __func__, cbfp->irq);
        cbfp->irq = -EINVAL;
        return -1;
    }

    //enable_irq_wake(cbfp->irq);
    //disable_irq(cbfp->irq);

    disable_irq(cbfp->irq);
    cbfp->interrupt_done = 0;
    enable_irq(cbfp->irq);
    enable_irq_wake(cbfp->irq);

    return 0;
}

static void cbfp_hw_power_enable(cbfp_data_t *cbfp, u8 onoff)
{
    CB_LOG(INFO_LOG, "%s\n", __func__);

    if (onoff) {
        // on
        if (gpio_is_valid(cbfp->fp_vddio_gpio)) {
            gpio_set_value(cbfp->fp_vddio_gpio, 1);
            mdelay(1);
        }

        if (gpio_is_valid(cbfp->fp_vdd_gpio))
            gpio_set_value(cbfp->fp_vdd_gpio, 1);
    } else {
        // off
        if (gpio_is_valid(cbfp->fp_vddio_gpio)) {
            gpio_set_value(cbfp->fp_vddio_gpio, 0);
            mdelay(1);
        }

        if (gpio_is_valid(cbfp->fp_vdd_gpio))
            gpio_set_value(cbfp->fp_vdd_gpio, 0);
    }
}

#if 0
static void cbfp_reset_gpio_control(cbfp_data_t *cbfp, u8 assert)
{
    if (assert) {
        // LOW
        if (gpio_is_valid(cbfp->reset_gpio)) {
            gpio_set_value(cbfp->reset_gpio, 0);
        }
    } else {
        // HIGH
        if (gpio_is_valid(cbfp->reset_gpio)) {
            gpio_set_value(cbfp->reset_gpio, 1);
        }
    }
}
#endif /* NOT USE NOW */

/* -------------------------------------------------------------------- */
/* Platform driver description                                          */
/* -------------------------------------------------------------------- */
static int cbfp_drv_probe(struct platform_device *pdev)
{
    cbfp_data_t *cbfp = NULL;
    int rc = 0;

    CB_LOG(INFO_LOG, "%s\n", __func__);

    cbfp = kzalloc(sizeof(struct cb_data), GFP_KERNEL);
    if (!cbfp) {
        CB_LOG(ERR_LOG, "%s: Failed to allocate memory for struct cb_data\n", __func__);
        return -ENOMEM;
    }

    sema_init(&cbfp->mutex, 0);

    cbfp->pdev = pdev;

    cbfp->reset_gpio = -EINVAL;
    cbfp->irq_gpio = -EINVAL;
    cbfp->irq = -EINVAL;

    rc = cbfp_get_gpio_dts_info(cbfp);
    if (rc) {
        CB_LOG(ERR_LOG, "%s: Failed to get gpio(%d)\n", __func__, rc);
        goto err;
    }

    rc = cbfp_cfg_irq_gpio(cbfp);
    if (rc) {
        CB_LOG(ERR_LOG, "%s: Failed to configure irq(%d)\n", __func__, rc);
        goto err;
    }
    CB_LOG(DEBUG_LOG, "%s: INT(%d) RST(%d)\n", __func__, cbfp->irq_gpio, cbfp->reset_gpio);

    rc = cb_manage_sysfs(cbfp, pdev, true);
    if (rc)
        goto err1;

    wake_lock_init(&cbfp->fp_wake_lock, WAKE_LOCK_SUSPEND, "fp_wake_lock");
    platform_set_drvdata(pdev, cbfp);

    cbfp_hw_power_enable(cbfp, 1);
    //cbfp_reset_gpio_control(cbfp, 0);
    cbfp_hw_reset(cbfp);



    CB_LOG(DEBUG_LOG, "%s: VDD(%s)\n", __func__,
            (gpio_get_value(cbfp->fp_vdd_gpio) == 1) ? "high" : "low");
    CB_LOG(DEBUG_LOG, "%s: VDDIO(%s)\n", __func__,
            (gpio_get_value(cbfp->fp_vddio_gpio) == 1) ? "high" : "low");
    CB_LOG(DEBUG_LOG, "%s: INT(%s)\n", __func__,
            (gpio_get_value(cbfp->irq_gpio) == 1) ? "high" : "low");
    CB_LOG(DEBUG_LOG, "%s: RST(%s)\n", __func__,
            (gpio_get_value(cbfp->reset_gpio) == 1) ? "high" : "low");

    up(&cbfp->mutex);
    return 0;

err1:
    cbfp_gpio_free(cbfp);

err:
    up(&cbfp->mutex);

    kfree(cbfp);
    return rc;
}

/* -------------------------------------------------------------------- */
static int cbfp_drv_remove(struct platform_device *pdev)
{
    struct cb_data *cbfp = platform_get_drvdata(pdev);

    CB_LOG(INFO_LOG, "%s\n", __func__);

    sysfs_remove_group(&cbfp->pdev->dev.kobj, &cb_hw_control_attr_group);
    cbfp_gpio_free(cbfp);

    wake_lock_destroy(&cbfp->fp_wake_lock);
    kfree(cbfp);

    return 0;
}

#ifdef CONFIG_OF
static struct of_device_id cb_of_match[] = {
  { .compatible = "cb,btp", },
  {}
};

MODULE_DEVICE_TABLE(of, cb_of_match);
#endif

static struct platform_driver cb_driver = {
    .driver = {
        .name   = CBFP_DEV_NAME,
        .owner  = THIS_MODULE,
#ifdef CONFIG_OF
        .of_match_table = cb_of_match,
#endif
    },
    .probe  = cbfp_drv_probe,
    .remove = cbfp_drv_remove,
};

static int cbfp_drv_init(void)
{
    CB_LOG(INFO_LOG, "%s\n", __func__);

    if (platform_driver_register(&cb_driver))
        return -EINVAL;

    return 0;
}

/* -------------------------------------------------------------------- */
static void cbfp_drv_exit(void)
{
    CB_LOG(INFO_LOG, "%s\n", __func__);

    platform_driver_unregister(&cb_driver);
}


module_init(cbfp_drv_init);
module_exit(cbfp_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Canvasbio <www.canvasbio.com>");
MODULE_DESCRIPTION("BTP sensor platform driver.");
