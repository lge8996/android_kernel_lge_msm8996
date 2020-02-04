/*
 * Copyright (C) 2017 Richtek Technology Corp.
 *
 * TCPC Interface for dual role
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/usb/tcpci.h>
#include <linux/usb/tcpci_typec.h>

#ifdef CONFIG_DUAL_ROLE_USB_INTF
static enum dual_role_property tcpc_dual_role_props[] = {
	DUAL_ROLE_PROP_SUPPORTED_MODES,
	DUAL_ROLE_PROP_MODE,
	DUAL_ROLE_PROP_PR,
	DUAL_ROLE_PROP_DR,
	DUAL_ROLE_PROP_VCONN_SUPPLY,
#ifdef CONFIG_LGE_USB
	DUAL_ROLE_PROP_CC1,
	DUAL_ROLE_PROP_CC2,
	DUAL_ROLE_PROP_PDO1,
	DUAL_ROLE_PROP_PDO2,
	DUAL_ROLE_PROP_PDO3,
	DUAL_ROLE_PROP_PDO4,
	DUAL_ROLE_PROP_PDO5,
	DUAL_ROLE_PROP_PDO6,
	DUAL_ROLE_PROP_PDO7,
	DUAL_ROLE_PROP_RDO,
#endif
};

static int tcpc_dual_role_get_prop(struct dual_role_phy_instance *dual_role,
			enum dual_role_property prop, unsigned int *val)
{
	struct tcpc_device *tcpc = dev_get_drvdata(dual_role->dev.parent);
#ifdef CONFIG_LGE_USB
	struct pd_port *pd_port = &tcpc->pd_port;
	struct pe_data *pe_data = &pd_port->pe_data;
#endif
	int ret = 0;

	switch (prop) {
	case DUAL_ROLE_PROP_SUPPORTED_MODES:
		*val = tcpc->dual_role_supported_modes;
		break;
	case DUAL_ROLE_PROP_MODE:
		*val = tcpc->dual_role_mode;
		break;
	case DUAL_ROLE_PROP_PR:
		*val = tcpc->dual_role_pr;
		break;
	case DUAL_ROLE_PROP_DR:
		*val = tcpc->dual_role_dr;
		break;
	case DUAL_ROLE_PROP_VCONN_SUPPLY:
		*val = tcpc->dual_role_vconn;
		break;
#ifdef CONFIG_LGE_USB
	case DUAL_ROLE_PROP_CC1:
	case DUAL_ROLE_PROP_CC2:
		switch (tcpc->typec_remote_cc[prop - DUAL_ROLE_PROP_CC1]) {
		case TYPEC_CC_VOLT_RA:
			*val = DUAL_ROLE_PROP_CC_RA;
			break;
		case TYPEC_CC_VOLT_RD:
			*val = DUAL_ROLE_PROP_CC_RD;
			break;
		case TYPEC_CC_VOLT_SNK_DFT:
			*val = DUAL_ROLE_PROP_CC_RP_DEFAULT;
			break;
		case TYPEC_CC_VOLT_SNK_1_5:
			*val = DUAL_ROLE_PROP_CC_RP_POWER1P5;
			break;
		case TYPEC_CC_VOLT_SNK_3_0:
			*val = DUAL_ROLE_PROP_CC_RP_POWER3P0;
			break;
		case TYPEC_CC_VOLT_OPEN:
		default:
			*val = DUAL_ROLE_PROP_CC_OPEN;
			break;
		}
		break;
	case DUAL_ROLE_PROP_PDO1:
	case DUAL_ROLE_PROP_PDO2:
	case DUAL_ROLE_PROP_PDO3:
	case DUAL_ROLE_PROP_PDO4:
	case DUAL_ROLE_PROP_PDO5:
	case DUAL_ROLE_PROP_PDO6:
	case DUAL_ROLE_PROP_PDO7:
		switch (tcpc->dual_role_pr) {
		case DUAL_ROLE_PROP_PR_SRC:
			if (pd_port->local_src_cap.nr > (prop - DUAL_ROLE_PROP_PDO1))
				*val = pd_port->local_src_cap.pdos[prop - DUAL_ROLE_PROP_PDO1];
			else
				*val = 0;
			break;
		case DUAL_ROLE_PROP_PR_SNK:
			if (pe_data->remote_src_cap.nr > (prop - DUAL_ROLE_PROP_PDO1))
				*val = pe_data->remote_src_cap.pdos[prop - DUAL_ROLE_PROP_PDO1];
			else
				*val = 0;
			break;
		case DUAL_ROLE_PROP_PR_NONE:
		default:
			*val = 0;
			break;
		}
		break;
	case DUAL_ROLE_PROP_RDO:
		switch (tcpc->dual_role_pr) {
		case DUAL_ROLE_PROP_PR_SRC:
		case DUAL_ROLE_PROP_PR_SNK:
			*val = pd_port->last_rdo;
			break;
		case DUAL_ROLE_PROP_PR_NONE:
		default:
			*val = 0;
			break;
		}
		break;
#endif
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static	int tcpc_dual_role_prop_is_writeable(
	struct dual_role_phy_instance *dual_role, enum dual_role_property prop)
{
	int retval = -EINVAL;
	struct tcpc_device *tcpc = dev_get_drvdata(dual_role->dev.parent);

	switch (prop) {
#ifdef CONFIG_USB_POWER_DELIVERY
#ifdef CONFIG_LGE_USB
	case DUAL_ROLE_PROP_MODE:
#endif
	case DUAL_ROLE_PROP_PR:
	case DUAL_ROLE_PROP_DR:
	case DUAL_ROLE_PROP_VCONN_SUPPLY:
#else
	case DUAL_ROLE_PROP_MODE:
#endif	/* CONFIG_USB_POWER_DELIVERY */
		if (tcpc->dual_role_supported_modes ==
			DUAL_ROLE_SUPPORTED_MODES_DFP_AND_UFP)
			retval = 1;
		break;
	default:
		break;
	}
	return retval;
}

#ifdef CONFIG_USB_POWER_DELIVERY

static int tcpc_dual_role_set_prop_pr(
	struct tcpc_device *tcpc, unsigned int val)
{
	int ret;
	uint8_t role;

	switch (val) {
	case DUAL_ROLE_PROP_PR_SRC:
		role = PD_ROLE_SOURCE;
		break;
	case DUAL_ROLE_PROP_PR_SNK:
		role = PD_ROLE_SINK;
		break;
	default:
		return 0;
	}

	if (val == tcpc->dual_role_pr) {
		pr_info("%s wrong role (%d->%d)\n",
			__func__, tcpc->dual_role_pr, val);
		return 0;
	}

	ret = tcpm_dpm_pd_power_swap(tcpc, role, NULL);
	pr_info("%s power role swap (%d->%d): %d\n",
		__func__, tcpc->dual_role_pr, val, ret);

	if (ret == TCPM_ERROR_NO_PD_CONNECTED) {
		ret = tcpm_typec_role_swap(tcpc);
		pr_info("%s typec role swap (%d->%d): %d\n",
			__func__, tcpc->dual_role_pr, val, ret);
	}

	return ret;
}

static int tcpc_dual_role_set_prop_dr(
	struct tcpc_device *tcpc, unsigned int val)
{
	int ret;
	uint8_t role;

	switch (val) {
	case DUAL_ROLE_PROP_DR_HOST:
		role = PD_ROLE_DFP;
		break;
	case DUAL_ROLE_PROP_DR_DEVICE:
		role = PD_ROLE_UFP;
		break;
	default:
		return 0;
	}

	if (val == tcpc->dual_role_dr) {
		pr_info("%s wrong role (%d->%d)\n",
			__func__, tcpc->dual_role_dr, val);
		return 0;
	}

	ret = tcpm_dpm_pd_data_swap(tcpc, role, NULL);
	pr_info("%s data role swap (%d->%d): %d\n",
		__func__, tcpc->dual_role_dr, val, ret);

	return ret;
}

static int tcpc_dual_role_set_prop_vconn(
	struct tcpc_device *tcpc, unsigned int val)
{
	int ret;
	uint8_t role;

	switch (val) {
	case DUAL_ROLE_PROP_VCONN_SUPPLY_NO:
		role = PD_ROLE_VCONN_OFF;
		break;
	case DUAL_ROLE_PROP_VCONN_SUPPLY_YES:
		role = PD_ROLE_VCONN_ON;
		break;
	default:
		return 0;
	}

	if (val == tcpc->dual_role_vconn) {
		pr_info("%s wrong role (%d->%d)\n",
			__func__, tcpc->dual_role_vconn, val);
		return 0;
	}

	ret = tcpm_dpm_pd_vconn_swap(tcpc, role, NULL);
	pr_info("%s vconn swap (%d->%d): %d\n",
		__func__, tcpc->dual_role_vconn, val, ret);

	return ret;
}
#ifdef CONFIG_LGE_USB
static int tcpc_dual_role_set_prop_mode(
	struct tcpc_device *tcpc, unsigned int val)
{
	int ret;

	if (val == tcpc->dual_role_mode) {
		pr_info("%s wrong role (%d->%d)\n",
			__func__, tcpc->dual_role_mode, val);
		return 0;
	}

	ret = tcpm_typec_role_swap(tcpc);
	pr_info("%s typec role swap (%d->%d): %d\n",
		__func__, tcpc->dual_role_mode, val, ret);

	return ret;
}
#endif
#else	/* TypeC Only */

static int tcpc_dual_role_set_prop_mode(
	struct tcpc_device *tcpc, unsigned int val)
{
	int ret;

	if (val == tcpc->dual_role_mode) {
		pr_info("%s wrong role (%d->%d)\n",
			__func__, tcpc->dual_role_mode, val);
		return 0;
	}

	ret = tcpm_typec_role_swap(tcpc);
	pr_info("%s typec role swap (%d->%d): %d\n",
		__func__, tcpc->dual_role_mode, val, ret);

	return ret;
}

#endif	/* CONFIG_USB_POWER_DELIVERY */

static int tcpc_dual_role_set_prop(struct dual_role_phy_instance *dual_role,
			enum dual_role_property prop, const unsigned int *val)
{
	struct tcpc_device *tcpc = dev_get_drvdata(dual_role->dev.parent);

	switch (prop) {
#ifdef CONFIG_USB_POWER_DELIVERY
#ifdef CONFIG_LGE_USB
	case DUAL_ROLE_PROP_MODE:
		tcpc_dual_role_set_prop_mode(tcpc, *val);
		break;
#endif
	case DUAL_ROLE_PROP_PR:
		tcpc_dual_role_set_prop_pr(tcpc, *val);
		break;
	case DUAL_ROLE_PROP_DR:
		tcpc_dual_role_set_prop_dr(tcpc, *val);
		break;
	case DUAL_ROLE_PROP_VCONN_SUPPLY:
		tcpc_dual_role_set_prop_vconn(tcpc, *val);
		break;
#else /* TypeC Only */
	case DUAL_ROLE_PROP_MODE:
		tcpc_dual_role_set_prop_mode(tcpc, *val);
		break;
#endif /* CONFIG_USB_POWER_DELIVERY */

	default:
		break;
	}

	return 0;
}

static void tcpc_get_dual_desc(struct tcpc_device *tcpc)
{
	struct device_node *np = of_find_node_by_name(NULL, tcpc->desc.name);
	u32 val;

	if (!np)
		return;

	if (of_property_read_u32(np, "tcpc-dual,supported_modes", &val) >= 0) {
		if (val > DUAL_ROLE_PROP_SUPPORTED_MODES_TOTAL)
			tcpc->dual_role_supported_modes =
					DUAL_ROLE_SUPPORTED_MODES_DFP_AND_UFP;
		else
			tcpc->dual_role_supported_modes = val;
	}
}

int tcpc_dual_role_phy_init(
			struct tcpc_device *tcpc)
{
	struct dual_role_phy_desc *dual_desc;
	int len;
	char *str_name;

	tcpc->dr_usb = devm_kzalloc(&tcpc->dev,
				sizeof(*tcpc->dr_usb), GFP_KERNEL);

	dual_desc = devm_kzalloc(&tcpc->dev, sizeof(*dual_desc), GFP_KERNEL);
	if (!dual_desc)
		return -ENOMEM;

	tcpc_get_dual_desc(tcpc);

	len = strlen(tcpc->desc.name);
	str_name = devm_kzalloc(&tcpc->dev, len+11, GFP_KERNEL);
	snprintf(str_name, PAGE_SIZE, "dual-role-%s", tcpc->desc.name);
#ifdef CONFIG_LGE_USB
	dual_desc->name = "otg_default";
#else
	dual_desc->name = str_name;
#endif

	dual_desc->properties = tcpc_dual_role_props;
	dual_desc->num_properties = ARRAY_SIZE(tcpc_dual_role_props);
	dual_desc->get_property = tcpc_dual_role_get_prop;
	dual_desc->set_property = tcpc_dual_role_set_prop;
	dual_desc->property_is_writeable = tcpc_dual_role_prop_is_writeable;

	tcpc->dr_usb = devm_dual_role_instance_register(&tcpc->dev, dual_desc);
	if (IS_ERR(tcpc->dr_usb)) {
		dev_err(&tcpc->dev, "tcpc fail to register dual role usb\n");
		return -EINVAL;
	}
	/* init dual role phy instance property */
	tcpc->dual_role_pr = DUAL_ROLE_PROP_PR_NONE;
	tcpc->dual_role_dr = DUAL_ROLE_PROP_DR_NONE;
	tcpc->dual_role_mode = DUAL_ROLE_PROP_MODE_NONE;
	tcpc->dual_role_vconn = DUAL_ROLE_PROP_VCONN_SUPPLY_NO;
	return 0;
}
#endif /* CONFIG_DUAL_ROLE_USB_INTF */
