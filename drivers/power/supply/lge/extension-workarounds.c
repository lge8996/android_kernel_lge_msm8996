#define pr_fmt(fmt) "WA: %s: " fmt, __func__
#define pr_wa(fmt, ...) pr_err(fmt, ##__VA_ARGS__)
#define pr_dbg_wa(fmt, ...) pr_debug(fmt, ##__VA_ARGS__)

#include <linux/delay.h>
#include <linux/pmic-voter.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>

#if defined(CONFIG_QPNP_SMB5) || defined(CONFIG_QPNP_QG)
#include "../qcom/smb5-reg.h"
#include "../qcom/smb5-lib.h"


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Helper functions
////////////////////////////////////////////////////////////////////////////
static struct smb_charger* wa_helper_chg(void) {
	// getting smb_charger from air
	struct power_supply*	psy
		= power_supply_get_by_name("battery");
	struct smb_charger*	chg
		= psy ? power_supply_get_drvdata(psy) : NULL;
	if (psy)
		power_supply_put(psy);

	return chg;
}

#define APSD_RERUN_DELAY_MS 4000
static DEFINE_MUTEX(wa_lock);
static bool wa_command_apsd(/*@Nonnull*/ struct smb_charger* chg) {
	bool	ret = false;
	int rc;

	mutex_lock(&wa_lock);
	rc = smblib_masked_write(chg, CMD_APSD_REG,
				APSD_RERUN_BIT, APSD_RERUN_BIT);
	if (rc < 0) {
		pr_wa("Couldn't re-run APSD rc=%d\n", rc);
		goto failed;
	}
	ret = true;

failed:
	mutex_unlock(&wa_lock);
	return ret;
}

#define USBIN_CMD_ICL_OVERRIDE_REG (USBIN_BASE + 0x42)
#define ICL_OVERRIDE_BIT BIT(0)
bool wa_command_icl_override(/*@Nonnull*/ struct smb_charger* chg) {
	if (smblib_masked_write(chg, USBIN_CMD_ICL_OVERRIDE_REG, ICL_OVERRIDE_BIT, ICL_OVERRIDE_BIT) < 0) {
		pr_wa("Couldn't icl override\n");
		return false;
	}

	return true;
}

static void wa_get_pmic_dump_func(struct work_struct *unused) {
	struct smb_charger* chg = wa_helper_chg();
	union power_supply_propval debug  = {-1, };

	power_supply_set_property(chg->batt_psy,
			POWER_SUPPLY_PROP_DEBUG_BATTERY, &debug);
}
static DECLARE_WORK(wa_get_pmic_dump_work, wa_get_pmic_dump_func);

void wa_get_pmic_dump(void) {
	schedule_work(&wa_get_pmic_dump_work);
}
bool wa_avoiding_mbg_fault_uart(bool enable) { return false; };
bool wa_avoiding_mbg_fault_usbid(bool enable) { return false; };


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Detection of Standard HVDCP2
////////////////////////////////////////////////////////////////////////////

#define DSH_VOLTAGE_THRESHOLD  7000
static bool wa_is_standard_hvdcp = false;
static bool wa_detect_standard_hvdcp_done = false;
static void wa_detect_standard_hvdcp_main(struct work_struct *unused) {
	struct smb_charger*  chg = wa_helper_chg();
	union power_supply_propval	val = {0, };
	int rc = 0;
	int usb_vnow = 0;
	wa_detect_standard_hvdcp_done = true;

	if (!chg) {
		pr_wa("'chg' is not ready\n");
		return;
	}

	rc = smblib_dp_dm(chg, POWER_SUPPLY_DP_DM_FORCE_9V);
	if (rc < 0) {
		pr_wa("Couldn't force 9V rc=%d\n", rc);
		return;
	}

	msleep(200);
	usb_vnow = (chg->usb_psy && !power_supply_get_property(
				chg->usb_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &val))
						? val.intval/1000 : -1;
	if ( usb_vnow >= DSH_VOLTAGE_THRESHOLD) {
		wa_is_standard_hvdcp = true;
	}

	pr_wa("Check standard hvdcp. %d mV\n", usb_vnow);
	rc = smblib_dp_dm(chg, POWER_SUPPLY_DP_DM_FORCE_5V);
	if (rc < 0) {
		pr_wa("Couldn't force 5v rc=%d\n", rc);
		return;
	}

	return;
}
static DECLARE_DELAYED_WORK(wa_detect_standard_hvdcp_dwork, wa_detect_standard_hvdcp_main);

void wa_detect_standard_hvdcp_trigger(struct smb_charger* chg) {
	u8 stat;
	int rc;

	rc = smblib_read(chg, APSD_STATUS_REG, &stat);
	if (rc < 0) {
		pr_wa("Couldn't read APSD_STATUS_REG rc=%d\n", rc);
		return;
	}

	pr_dbg_wa("apsd_status 0x%x, type %d\n", stat, chg->real_charger_type);
	if ((stat & QC_AUTH_DONE_STATUS_BIT)
			&& chg->real_charger_type == POWER_SUPPLY_TYPE_USB_HVDCP
			&& !delayed_work_pending(&wa_detect_standard_hvdcp_dwork)
			&& !wa_detect_standard_hvdcp_done) {
		schedule_delayed_work(&wa_detect_standard_hvdcp_dwork, msecs_to_jiffies(0));
	}
}

void wa_detect_standard_hvdcp_clear(void) {
	wa_is_standard_hvdcp = false;
	wa_detect_standard_hvdcp_done = false;
}

bool wa_detect_standard_hvdcp_check(void) {
	return wa_is_standard_hvdcp;
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Rerun apsd for dcp charger
////////////////////////////////////////////////////////////////////////////

static bool wa_rerun_apsd_done = false;

static void wa_rerun_apsd_for_dcp_main(struct work_struct *unused) {
	struct smb_charger* chg = wa_helper_chg();

	if (!chg || chg->pd_active ||!wa_rerun_apsd_done) {
		pr_wa("stop apsd done. apsd(%d), pd(%d)\n", wa_rerun_apsd_done, chg ? chg->pd_active : -1);
		return;
	}

	pr_wa("Rerun apsd\n");
	wa_command_apsd(chg);
}

static DECLARE_DELAYED_WORK(wa_rerun_apsd_for_dcp_dwork, wa_rerun_apsd_for_dcp_main);

void wa_rerun_apsd_for_dcp_trigger(struct smb_charger *chg) {
	union power_supply_propval val = { 0, };
	bool usb_type_dcp = chg->real_charger_type == POWER_SUPPLY_TYPE_USB_DCP;
	bool usb_vbus_high
		= !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val) ? !!val.intval : false;
	u8 stat;
	int rc;

	rc = smblib_read(chg, APSD_STATUS_REG, &stat);
	if (rc < 0) {
		pr_wa("Couldn't read APSD_STATUS_REG rc=%d\n", rc);
		return;
	}

	pr_dbg_wa("legacy(%d), done(%d), TO(%d), DCP(%d), Vbus(%d)\n",
		chg->typec_legacy, wa_rerun_apsd_done, stat, usb_type_dcp, usb_vbus_high);

	if (chg->typec_legacy && !wa_rerun_apsd_done
			&& (stat & HVDCP_CHECK_TIMEOUT_BIT) && usb_type_dcp && usb_vbus_high) {
		wa_rerun_apsd_done = true;
		schedule_delayed_work(&wa_rerun_apsd_for_dcp_dwork,
			round_jiffies_relative(msecs_to_jiffies(APSD_RERUN_DELAY_MS)));
	}
}

void wa_rerun_apsd_for_dcp_clear(void) {
	wa_rerun_apsd_done = false;
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Charging without CC
////////////////////////////////////////////////////////////////////////////

/* CWC has two works for APSD and HVDCP, and this implementation handles the
 * works independently with different delay.
 * but you can see that retrying HVDCP detection work is depends on rerunning
 * APSD. i.e, APSD work derives HVDCP work.
 */
#define CWC_DELAY_MS  1000
static bool wa_charging_without_cc_processed = false;
static bool wa_cwc_early_usb_attach = true;
static bool wa_charging_without_cc_required(struct smb_charger *chg) {
	union power_supply_propval val = { 0, };
	bool pd_hard_reset, usb_vbus_high, typec_mode_none,
		usb_type_unknown, early_usb_attach, workaround_required;

	if (!chg) {
		pr_wa("'chg' is not ready\n");
		return false;
	}

	pd_hard_reset = chg->pd_hard_reset;
	usb_vbus_high = !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val)
		? !!val.intval : false;
	typec_mode_none = chg->typec_mode == POWER_SUPPLY_TYPEC_NONE;
	usb_type_unknown = chg->real_charger_type == POWER_SUPPLY_TYPE_UNKNOWN;
	early_usb_attach = wa_cwc_early_usb_attach;

	workaround_required = !pd_hard_reset && usb_vbus_high && typec_mode_none && (usb_type_unknown || early_usb_attach);

	if (!workaround_required)
		pr_err("Don't need CWC (pd_hard_reset:%d, usb_vbus_high:%d, typec_mode_none:%d,:  usb_type_unknown:%d,"
			" early_usb_attach %d)\n", pd_hard_reset, usb_vbus_high, typec_mode_none, usb_type_unknown, early_usb_attach);

	return workaround_required;
}

static void wa_charging_without_cc_main(struct work_struct *unused) {
	struct smb_charger*  chg = wa_helper_chg();
	int rc;

	if (wa_charging_without_cc_required(chg)) {
		pr_wa("CC line is not recovered until now, Start W/A\n");
		wa_charging_without_cc_processed = true;
		chg->typec_legacy = true;
		rc = smblib_configure_hvdcp_apsd(chg, true);
		if (rc < 0) {
			pr_wa("Couldn't enable APSD rc=%d\n", rc);
			return;
		}
		smblib_rerun_apsd_if_required(chg);
		wa_command_icl_override(chg);
	}

	return;
}
static DECLARE_DELAYED_WORK(wa_charging_without_cc_dwork, wa_charging_without_cc_main);

void wa_charging_without_cc_trigger(struct smb_charger* chg, bool vbus) {
	// This may be triggered in the IRQ context of the USBIN rising.
	// So main function to start 'charging without cc', is deferred via delayed_work of kernel.
	// Just check and register (if needed) the work in this call.

	if (vbus && wa_charging_without_cc_required(chg)) {
		if (delayed_work_pending(&wa_charging_without_cc_dwork)) {
			pr_wa(" Cancel the pended trying apsd . . .\n");
			cancel_delayed_work(&wa_charging_without_cc_dwork);
		}

		schedule_delayed_work(&wa_charging_without_cc_dwork,
			msecs_to_jiffies(CWC_DELAY_MS));
	} else if (!vbus && wa_charging_without_cc_processed) {
		wa_charging_without_cc_processed = false;
		cancel_delayed_work(&wa_charging_without_cc_dwork);
		pr_wa("Call typec_removal by force\n");
		extension_typec_src_removal(chg);
	}

	if(!vbus)
		wa_cwc_early_usb_attach = false;

}

////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Rerun apsd for unknown charger
////////////////////////////////////////////////////////////////////////////
static void wa_charging_for_unknown_cable_main(struct work_struct *unused);
static DECLARE_DELAYED_WORK(wa_charging_for_unknown_cable_dwork, wa_charging_for_unknown_cable_main);

#define FLOAT_SETTING_DELAY_MS	1000
static void wa_charging_for_unknown_cable_main(struct work_struct *unused) {
	struct smb_charger*  chg = wa_helper_chg();
	struct power_supply* veneer = power_supply_get_by_name("veneer");
	union power_supply_propval floated
		= { .intval = POWER_SUPPLY_TYPE_USB_FLOAT, };
	union power_supply_propval val = { 0, };
	bool pd_hard_reset, usb_type_unknown, moisture_detected, usb_vbus_high,
			workaround_required, apsd_done, typec_mode_sink, ok_to_pd;
	bool vbus_valid = false;
	u8 stat;
	int rc;

	if (!chg) {
		pr_wa("'chg' is not ready\n");
		goto out_charging_for_unknown;
	}

	rc = smblib_read(chg, APSD_STATUS_REG, &stat);
	if (rc < 0) {
		pr_wa("Couldn't read APSD_STATUS_REG rc=%d\n", rc);
		goto out_charging_for_unknown;
	}
	apsd_done = (stat & APSD_DTC_STATUS_DONE_BIT);

	rc = smblib_read(chg, TYPE_C_MISC_STATUS_REG, &stat);
	if (rc < 0) {
		pr_wa("Couldn't read TYPE_C_MISC_STATUS_REG rc=%d\n", rc);
		goto out_charging_for_unknown;
	}
	typec_mode_sink = (stat & SNK_SRC_MODE_BIT);

	pd_hard_reset = chg->pd_hard_reset;
	usb_type_unknown = chg->real_charger_type == POWER_SUPPLY_TYPE_UNKNOWN;
	moisture_detected
		= !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_MOISTURE_DETECTED, &val)
		? !!val.intval : false;
	usb_vbus_high
		= !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val)
		? true : false;
	ok_to_pd = chg->ok_to_pd;

	workaround_required = !pd_hard_reset
				&& !moisture_detected
				&& !typec_mode_sink
				&& usb_type_unknown
				&& usb_vbus_high
				&& !ok_to_pd;

	pr_err("check(!(pd_hard_reset:%d, MD:%d, typec_mode_sink:%d)"
			" usb_type_unknown:%d, usb_vbus_high:%d, ok_to_pd:%d,"
			" apsd_done:%d wa_charging_cc:%d, pending work:%d)\n",
			pd_hard_reset, moisture_detected, typec_mode_sink,
			usb_type_unknown, usb_vbus_high, ok_to_pd, apsd_done,
			wa_charging_without_cc_processed,
			delayed_work_pending(&wa_charging_without_cc_dwork));

	if (!workaround_required) {
		pr_dbg_wa("check(!(pd_hard_reset:%d, MD:%d, typec_mode_sink:%d)"
			" usb_type_unknown:%d, usb_vbus_high:%d, ok_to_pd:%d,"
			" apsd_done:%d wa_charging_cc:%d, pending work:%d)\n",
			pd_hard_reset, moisture_detected, typec_mode_sink,
			usb_type_unknown, usb_vbus_high, ok_to_pd, apsd_done,
			wa_charging_without_cc_processed,
			delayed_work_pending(&wa_charging_without_cc_dwork));

		goto out_charging_for_unknown;
	}

	if (apsd_done && !delayed_work_pending(&wa_charging_without_cc_dwork)) {
		rc = smblib_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG,
				USBIN_ADAPTER_ALLOW_5V);
		if (rc < 0) {
			pr_wa("Couldn't write 0x%02x to"
					" USBIN_ADAPTER_ALLOW_CFG_REG rc=%d\n",
					USBIN_ADAPTER_ALLOW_CFG_REG, rc);
			goto out_charging_for_unknown;
		}
		vbus_valid = !power_supply_get_property(chg->usb_psy,
				POWER_SUPPLY_PROP_PRESENT, &val)
				? !!val.intval : false;
		if (vbus_valid) {
			pr_wa("Force setting cable as FLOAT if it is UNKNOWN after APSD\n");
			power_supply_set_property(veneer,
					POWER_SUPPLY_PROP_REAL_TYPE, &floated);
			power_supply_changed(veneer);
		}
		else {
			pr_wa("VBUS is not valid\n");
		}
	}
	else {
		schedule_delayed_work(&wa_charging_for_unknown_cable_dwork,
			round_jiffies_relative(msecs_to_jiffies(FLOAT_SETTING_DELAY_MS)));
	}

out_charging_for_unknown:
	power_supply_put(veneer);
}

void wa_charging_for_unknown_cable_trigger(struct smb_charger* chg) {
	union power_supply_propval val = { 0, };
	bool usb_vbus_high
		= !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val)
		? true : false;
	if (usb_vbus_high
		&& (wa_charging_without_cc_processed ||
			delayed_work_pending(&wa_charging_without_cc_dwork))) {
		schedule_delayed_work(&wa_charging_for_unknown_cable_dwork,
			round_jiffies_relative(msecs_to_jiffies(FLOAT_SETTING_DELAY_MS)));
	}
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Support for weak battery pack
////////////////////////////////////////////////////////////////////////////

#define WEAK_SUPPLY_VOTER "WEAK_SUPPLY_VOTER"
#define WEAK_DELAY_MS		500
#define WEAK_DETECTION_COUNT	3
#define DEFAULT_WEAK_ICL_MA 1000

#define POWER_PATH_MASK		GENMASK(2, 1)
#define POWER_PATH_BATTERY	BIT(1)
#define POWER_PATH_USB		BIT(2)

static int  wa_support_weak_supply_count = 0;
static bool wa_support_weak_supply_running = false;

static void wa_support_weak_supply_func(struct work_struct *unused) {
	struct smb_charger* chg = wa_helper_chg();
	u8 stat;

	if (!wa_support_weak_supply_running)
		return;

	if (chg && !smblib_read(chg, POWER_PATH_STATUS_REG, &stat)) {
		if ((stat & POWER_PATH_MASK) == POWER_PATH_USB) {
			wa_support_weak_supply_count++;
			pr_wa("wa_support_weak_supply_count = %d\n",
				wa_support_weak_supply_count);
			if (wa_support_weak_supply_count >= WEAK_DETECTION_COUNT) {
				pr_wa("Weak battery is detected, set ICL to 1A\n");
				vote(chg->usb_icl_votable, WEAK_SUPPLY_VOTER,
					true, DEFAULT_WEAK_ICL_MA*1000);
			}
		}
	}
	wa_support_weak_supply_running = false;
}

static DECLARE_DELAYED_WORK(wa_support_weak_supply_dwork, wa_support_weak_supply_func);

void wa_support_weak_supply_trigger(struct smb_charger* chg, u8 stat) {
	bool trigger = !!(stat & USE_USBIN_BIT);

	if ((stat & POWER_PATH_MASK) != POWER_PATH_BATTERY)
		return;

	if (trigger) {
		if (!delayed_work_pending(&wa_support_weak_supply_dwork))
			schedule_delayed_work(&wa_support_weak_supply_dwork,
				round_jiffies_relative(msecs_to_jiffies(WEAK_DELAY_MS)));
	}
	else if (!!wa_support_weak_supply_count) {
		pr_wa("Clear wa_support_weak_supply_count\n");
		wa_support_weak_supply_count = 0;
		vote(chg->usb_icl_votable, WEAK_SUPPLY_VOTER, false, 0);
	}
	else
		; /* Do nothing */
}

void wa_support_weak_supply_check(void) {
	if (delayed_work_pending(&wa_support_weak_supply_dwork))
		wa_support_weak_supply_running = true;
}
#endif
