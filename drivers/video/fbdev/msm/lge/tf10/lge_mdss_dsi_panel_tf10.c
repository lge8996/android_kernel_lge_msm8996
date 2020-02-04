/*
 * Copyright(c) 2016, LG Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"[Display] %s: " fmt, __func__

#include <linux/delay.h>
#include "../../mdss_dsi.h"
#include "../lge_mdss_display.h"
#include "../../mdss_dba_utils.h"
#include <soc/qcom/lge/board_lge.h>
#include <linux/lge_panel_notify.h>

extern void mdss_dsi_panel_cmds_send(struct mdss_dsi_ctrl_pdata *ctrl,
		struct dsi_panel_cmds *pcmds, u32 flags);

#if IS_ENABLED(CONFIG_LGE_DISPLAY_OVERRIDE_MDSS_DSI_PANEL_RESET)
int mdss_dsi_request_gpios(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	int rc = 0;

	rc = gpio_request(ctrl_pdata->rst_gpio, "disp_rst_n");
	if (rc) {
		pr_err("request reset gpio failed, rc=%d\n",
			rc);
		goto rst_gpio_err;
	}
#ifdef CONFIG_PXLW_IRIS3_BRIDGE_IC
	if (lge_get_board_revno() < HW_REV_1_0) {
		if (gpio_is_valid(ctrl_pdata->iris_rst_gpio)) {
			rc = gpio_request(ctrl_pdata->iris_rst_gpio,
							"iris_rst_n");
			if (rc) {
				pr_err("request iris reset gpio failed,gpio = %d rc=%d\n",
					     ctrl_pdata->iris_rst_gpio, rc);
				goto iris_rst_gpio_err;
			}
		}
	} //HW_REV_1_0
#endif
	return rc;

#ifdef CONFIG_PXLW_IRIS3_BRIDGE_IC
iris_rst_gpio_err:
	gpio_free(ctrl_pdata->rst_gpio);
#endif
rst_gpio_err:
	return rc;
}
int mdss_dsi_panel_reset(struct mdss_panel_data *pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *pinfo = NULL;
	int i, rc = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pinfo = &(ctrl_pdata->panel_data.panel_info);
	if ((mdss_dsi_is_right_ctrl(ctrl_pdata) &&
		mdss_dsi_is_hw_config_split(ctrl_pdata->shared_data)) ||
			pinfo->is_dba_panel) {
		pr_debug("%s:%d, right ctrl gpio configuration not needed\n",
			__func__, __LINE__);
		return rc;
	}

/* Modified reset sequence liyan 20190520 Begin */
#ifdef CONFIG_PXLW_IRIS3_BRIDGE_IC
	if (lge_get_board_revno() < HW_REV_1_0) {
		if (!gpio_is_valid(ctrl_pdata->iris_rst_gpio)) {
			pr_debug("%s:%d, iris reset line not configured\n",
				   __func__, __LINE__);
		}
	}
#endif
/* Modified reset sequence liyan 20190520 End */

	if (!gpio_is_valid(ctrl_pdata->rst_gpio)) {
		pr_debug("%s:%d, reset line not configured\n",
			   __func__, __LINE__);
		return rc;
	}

	pr_debug("%s: enable = %d\n", __func__, enable);

	if (enable) {
		rc = mdss_dsi_request_gpios(ctrl_pdata);
		if (rc) {
			pr_err("gpio request failed\n");
			return rc;
		}
		if (!pinfo->cont_splash_enabled) {
			/* iris mipi bridge ic reset */
			/* Modified reset sequence liyan 20190520 Begin */
#ifdef CONFIG_PXLW_IRIS3_BRIDGE_IC
			if (lge_get_board_revno() < HW_REV_1_0) {
				if (gpio_is_valid(ctrl_pdata->iris_rst_gpio)) {
					if (pdata->panel_info.iris_rst_seq_len) {
						rc = gpio_direction_output(ctrl_pdata->iris_rst_gpio,
								pdata->panel_info.iris_rst_seq[0]);
						if (rc) {
							pr_err("%s: unable to set dir for iris rst gpio\n",
								__func__);
							goto exit;
						}
					}

					for (i = 0; i < pdata->panel_info.iris_rst_seq_len; ++i) {
						gpio_set_value((ctrl_pdata->iris_rst_gpio),
							pdata->panel_info.iris_rst_seq[i]);
						if (pdata->panel_info.iris_rst_seq[++i])
							usleep_range(pinfo->iris_rst_seq[i] * 1000, pinfo->iris_rst_seq[i] * 1000);
					}
				}
			}//HW_REV_1_0
#endif
			/* Modified reset sequence liyan 20190520 End */
			if (pdata->panel_info.rst_seq_len) {
				rc = gpio_direction_output(ctrl_pdata->rst_gpio,
					pdata->panel_info.rst_seq[0]);
				if (rc) {
					pr_err("%s: unable to set dir for rst gpio\n",
						__func__);
					goto exit;
				}
			}

			for (i = 0; i < pdata->panel_info.rst_seq_len; ++i) {
				gpio_set_value((ctrl_pdata->rst_gpio),
					pdata->panel_info.rst_seq[i]);
				if (pdata->panel_info.rst_seq[++i])
					usleep_range(pinfo->rst_seq[i] * 1000, pinfo->rst_seq[i] * 1000);
			}
		}

		if (ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT) {
			pr_debug("%s: Panel Not properly turned OFF\n",
						__func__);
			ctrl_pdata->ctrl_state &= ~CTRL_STATE_PANEL_INIT;
			pr_debug("%s: Reset panel done\n", __func__);
		}
	} else {
#ifdef CONFIG_PXLW_IRIS3_BRIDGE_IC
		if (lge_get_board_revno() < HW_REV_1_0) {
			if (gpio_is_valid(ctrl_pdata->iris_rst_gpio)) {
				gpio_set_value((ctrl_pdata->iris_rst_gpio), 0);
				gpio_free(ctrl_pdata->iris_rst_gpio);
			}
		}
#endif
		gpio_set_value((ctrl_pdata->rst_gpio), 0);
		gpio_free(ctrl_pdata->rst_gpio);
		if (gpio_is_valid(ctrl_pdata->lcd_mode_sel_gpio)) {
			gpio_set_value(ctrl_pdata->lcd_mode_sel_gpio, 0);
			gpio_free(ctrl_pdata->lcd_mode_sel_gpio);
		}
	}
exit:
	return rc;
}
#endif


#if IS_ENABLED(CONFIG_LGE_DISPLAY_OVERRIDE_MDSS_DSI_PANEL_ON)
int mdss_dsi_panel_on(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_panel_info *pinfo;
	struct dsi_panel_cmds *on_cmds;

	if (pdata == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}

	pinfo = &pdata->panel_info;
	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
			panel_data);

	pr_info("+\n");

	on_cmds = &ctrl->on_cmds;

	pr_debug("ndx=%d cmd_cnt=%d\n",	ctrl->ndx, on_cmds->cmd_cnt);
	if (ctrl->on_cmds.cmd_cnt)
		mdss_dsi_panel_cmds_send(ctrl, &ctrl->on_cmds, CMD_REQ_COMMIT);
	else
		pr_info("no on cmds to send\n");

	lge_panel_notifier_call_chain(LGE_PANEL_EVENT_BLANK, 0, LGE_PANEL_STATE_UNBLANK);

	pr_info("-\n");
	return 0;
}
#endif

#if IS_ENABLED(CONFIG_LGE_DISPLAY_OVERRIDE_MDSS_DSI_PANEL_OFF)
int mdss_dsi_panel_off(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_panel_info *pinfo;

	if (pdata == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}

	pinfo = &pdata->panel_info;
	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
			panel_data);

	pr_info("+\n");

	if (ctrl->off_cmds.cmd_cnt)
		mdss_dsi_panel_cmds_send(ctrl, &ctrl->off_cmds, CMD_REQ_COMMIT);
	else
		pr_info("no off cmds to send\n");

	lge_panel_notifier_call_chain(LGE_PANEL_EVENT_BLANK, 0, LGE_PANEL_STATE_BLANK);

	pr_info("-\n");

	return 0;
}
#endif

int lge_ddic_ops_init(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	pr_info("%s: ddic_ops is not configured\n", __func__);
	return 0;
}
