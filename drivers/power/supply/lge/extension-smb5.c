/*
 * CAUTION! :
 * 	This file will be included at the end of "qpnp-smb5.c".
 * 	So "qpnp-smb5.c" should be touched before you start to build.
 * 	If not, your work will not be applied to the built image
 * 	because the build system may not care the update time of this file.
 */

//#include <linux/gpio.h>
//#include <linux/of_gpio.h>
#include <linux/iio/consumer.h>
//#include <linux/qpnp/qpnp-adc.h>

#include "veneer-primitives.h"

#define VENEER_VOTER_IUSB 	"VENEER_VOTER_IUSB"
#define VENEER_VOTER_IBAT 	"VENEER_VOTER_IBAT"
#define VENEER_VOTER_VFLOAT 	"VENEER_VOTER_VFLOAT"
#define VENEER_VOTER_HVDCP 	"VENEER_VOTER_HVDCP"

static char* log_raw_status(struct smb_charger* chg) {
	u8 reg;

	smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &reg);	// PMI@1006
	reg = reg & BATTERY_CHARGER_STATUS_MASK;			// BIT(2:0)
	switch (reg) {
		case TRICKLE_CHARGE:	return "TRICKLE";
		case PRE_CHARGE:	return "PRE";
		case FULLON_CHARGE:	return "FULLON";
		case TAPER_CHARGE:	return "TAPER";
		case TERMINATE_CHARGE:	return "TERMINATE";
		case INHIBIT_CHARGE:	return "INHIBIT";
		case DISABLE_CHARGE:	return "DISABLE";
		case PAUSE_CHARGE:	return "PAUSE";
		default:		break;
	}
	return "UNKNOWN (UNDEFINED CHARGING)";
}

static char* log_psy_status(int status) {
	switch (status) {
		case POWER_SUPPLY_STATUS_UNKNOWN :	return "UNKNOWN";
		case POWER_SUPPLY_STATUS_CHARGING:	return "CHARGING";
		case POWER_SUPPLY_STATUS_DISCHARGING:	return "DISCHARGING";
		case POWER_SUPPLY_STATUS_NOT_CHARGING:	return "NOTCHARGING";
		case POWER_SUPPLY_STATUS_FULL:		return "FULL";
		default :				break;
	}

	return "UNKNOWN (UNDEFINED STATUS)";
}

static char* log_psy_type(int type) {
       /* Refer to 'enum power_supply_type' in power_supply.h
	* and 'static char *type_text[]' in power_supply_sysfs.c
	*/
	switch (type) {
		case POWER_SUPPLY_TYPE_UNKNOWN :	return "UNKNOWN";
		case POWER_SUPPLY_TYPE_BATTERY :	return "BATTERY";
		case POWER_SUPPLY_TYPE_UPS :		return "UPS";
		case POWER_SUPPLY_TYPE_MAINS :		return "MAINS";
		case POWER_SUPPLY_TYPE_USB :		return "USB";
		case POWER_SUPPLY_TYPE_USB_DCP :	return "DCP";
		case POWER_SUPPLY_TYPE_USB_CDP :	return "CDP";
		case POWER_SUPPLY_TYPE_USB_ACA :	return "ACA";
		case POWER_SUPPLY_TYPE_USB_HVDCP :	return "HVDCP";
		case POWER_SUPPLY_TYPE_USB_HVDCP_3:	return "HVDCP3";
		case POWER_SUPPLY_TYPE_USB_PD :		return "PD";
		case POWER_SUPPLY_TYPE_WIRELESS :	return "WIRELESS";
		case POWER_SUPPLY_TYPE_USB_FLOAT :	return "FLOAT";
		case POWER_SUPPLY_TYPE_BMS :		return "BMS";
		case POWER_SUPPLY_TYPE_PARALLEL :	return "PARALLEL";
		case POWER_SUPPLY_TYPE_MAIN :		return "MAIN";
		case POWER_SUPPLY_TYPE_WIPOWER :	return "WIPOWER";
		case POWER_SUPPLY_TYPE_TYPEC :		return "TYPEC";
		case POWER_SUPPLY_TYPE_UFP :		return "UFP";
		case POWER_SUPPLY_TYPE_DFP :		return "DFP";
#if 0
		case POWER_SUPPLY_TYPE_USB_PD_DRP :		return "DRP";
		case POWER_SUPPLY_TYPE_APPLE_BRICK_ID :		return "APPLE_BRICK";
#endif
		default :				break;
	}
	return "UNKNOWN (UNDEFINED TYPE)";
}

static void debug_dump(struct smb_charger* chg, const char* title, u16 start) {
	u16 reg, i;
	u8 val[16];
	for (reg = start; reg < start + 0x100; reg += 0x10) {
		for (i = 0; i < 0x10; i++) {
			val[i] = 0x99;
			smblib_read(chg, reg+i, &val[i]);
		}
		pr_err("REGDUMP: [%s] 0x%X - %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
			title, reg, val[0], val[1], val[2], val[3], val[4], val[5], val[6], val[7], val[8],
			val[9], val[10], val[11], val[12], val[13], val[14], val[15]);
	}
}

#define DCIN_INPUT_STATUS_REG		(DCIN_BASE + 0x06)
static void debug_polling(struct smb_charger* chg) {
	union power_supply_propval	val = {0, };
	u8				reg = 0;

	bool disabled_ibat  = !!get_effective_result(chg->chg_disable_votable);
	bool disabled_prll  = !!get_effective_result(chg->pl_disable_votable);

	int  capping_ibat   = disabled_ibat ? 0 : get_effective_result(chg->fcc_votable)/1000;
	int  capping_iusb   = get_effective_result(chg->usb_icl_votable)/1000;
	int  capping_vfloat = get_effective_result(chg->fv_votable)/1000;

	bool presence_usb = !smblib_get_prop_usb_present(chg, &val) ? !!val.intval : false;

	#define POLLING_LOGGER_VOTER "POLLING_LOGGER_VOTER"
	vote(chg->awake_votable, POLLING_LOGGER_VOTER, true, 0);
	if (false /* for debug purpose */) {
		static struct power_supply* psy_battery;
		static struct power_supply* psy_bms;
		static struct power_supply* psy_main;
		static struct power_supply* psy_parallel;
		static struct power_supply* psy_pc_port;
		static struct power_supply* psy_usb;
		static struct power_supply* psy_veneer;

		if (!psy_battery)	psy_battery = power_supply_get_by_name("battery");
		if (!psy_bms)		psy_bms = power_supply_get_by_name("bms");
		if (!psy_main)		psy_main = power_supply_get_by_name("main");
		if (!psy_parallel)	psy_parallel = power_supply_get_by_name("parallel");
		if (!psy_pc_port)	psy_pc_port = power_supply_get_by_name("pc_port");
		if (!psy_usb)		psy_usb = power_supply_get_by_name("usb");
		if (!psy_veneer)	psy_veneer = power_supply_get_by_name("veneer");

		pr_info("PMINFO: [REF] battery:%d, bms:%d, main:%d, "
			"parallel:%d, pc_port:%d, usb:%d, veneer:%d\n",
			psy_battery ? atomic_read(&psy_battery->use_cnt) : 0,
			psy_bms ? atomic_read(&psy_bms->use_cnt) : 0,
			psy_main ? atomic_read(&psy_main->use_cnt) : 0,
			psy_parallel ? atomic_read(&psy_parallel->use_cnt) : 0,
			psy_pc_port ? atomic_read(&psy_pc_port->use_cnt) : 0,
			psy_usb ? atomic_read(&psy_usb->use_cnt) : 0,
			psy_veneer ? atomic_read(&psy_veneer->use_cnt) : 0);
	}

	#define LOGGING_ON_BMS 1
	val.intval = LOGGING_ON_BMS;
	if (chg->bms_psy)
		power_supply_set_property(chg->bms_psy, POWER_SUPPLY_PROP_UPDATE_NOW, &val);

	pr_info("PMINFO: [VOT] IUSB:%d(%s), IBAT:%d(%s), FLOAT:%d(%s)\n"
			"PMINFO: [VOT] CHDIS:%d(%s), PLDIS:%d(%s)\n",
		capping_iusb,	get_effective_client(chg->usb_icl_votable),
		capping_ibat,	get_effective_client(disabled_ibat ? chg->chg_disable_votable : chg->fcc_votable),
		capping_vfloat,	get_effective_client(chg->fv_votable),

		disabled_ibat,  get_effective_client(chg->chg_disable_votable),
		disabled_prll,  get_effective_client(chg->pl_disable_votable));

	// If not charging, skip the remained logging
	if (!presence_usb)
		goto out;

	// Basic charging information
	{	int   stat_pwr = smblib_read(chg, POWER_PATH_STATUS_REG, &reg) >= 0
			? reg : -1;
		char* stat_ret = !power_supply_get_property(chg->batt_psy,
			POWER_SUPPLY_PROP_STATUS, &val) ? log_psy_status(val.intval) : NULL;
		char* stat_ori = !smblib_get_prop_batt_status(chg, &val)
			? log_psy_status(val.intval) : NULL;
		char* chg_stat
			= log_raw_status(chg);
		char  chg_name [16] = { 0, };

#if 0
		#define QNOVO_PTTIME_STS		0x1507
		#define QNOVO_PTRAIN_STS		0x1508
		#define QNOVO_ERROR_STS2		0x150A
		#define QNOVO_PE_CTRL			0x1540
		#define QNOVO_PTRAIN_EN			0x1549

		int qnovo_en = smblib_read(chg, QNOVO_PT_ENABLE_CMD_REG, &reg) >= 0
			? reg : -1;
		int qnovo_pt_sts = smblib_read(chg, QNOVO_PTRAIN_STS, &reg) >= 0
			? reg : -1;
		int qnovo_pt_time = smblib_read(chg, QNOVO_PTTIME_STS, &reg) >= 0
			? reg*2 : -1;
		int qnovo_sts = smblib_read(chg, QNOVO_ERROR_STS2, &reg) >= 0
			? reg : -1;
		int qnovo_pe_ctrl = smblib_read(chg, QNOVO_PE_CTRL, &reg) >= 0
			? reg : -1;
		int qnovo_pt_en = smblib_read(chg, QNOVO_PTRAIN_EN, &reg) >= 0
			? reg : -1;
#else
		int qnovo_en = -1;
		int qnovo_pt_sts = -1;
		int qnovo_pt_time = -1;
		int qnovo_sts = -1;
		int qnovo_pe_ctrl = -1;
		int qnovo_pt_en = -1;
#endif
		unified_nodes_show("charger_name", chg_name);

		pr_info("PMINFO: [CHG] NAME:%s, STAT:%s(ret)/%s(ori)/%s(reg), PATH:0x%02x\n"
			"PMINFO: [QNI] en=%d(sts=0x%X, ctrl=0x%X), pt_en=%d(sts=0x%X), pt_t=%d\n",
			chg_name, stat_ret, stat_ori, chg_stat, stat_pwr,
			qnovo_en, qnovo_sts, qnovo_pe_ctrl, qnovo_pt_en, qnovo_pt_sts, qnovo_pt_time);
	}

	if (presence_usb) { // On Wired charging
		char* usb_real
			= log_psy_type(chg->real_charger_type);
		//int usb_vnow = !power_supply_get_property(chg->usb_psy,
		//	POWER_SUPPLY_PROP_VOLTAGE_NOW, &val) ? val.intval/1000 : -1;
		int prll_chgen = !chg->pl.psy ? -2 : (!power_supply_get_property(chg->pl.psy,
			POWER_SUPPLY_PROP_CHARGING_ENABLED, &val) ? !!val.intval : -1);
		int prll_pinen = !chg->pl.psy ? -2 : (!power_supply_get_property(chg->pl.psy,
			POWER_SUPPLY_PROP_PIN_ENABLED, &val) ? !!val.intval : -1);
		int prll_suspd = !chg->pl.psy ? -2 : (!power_supply_get_property(chg->pl.psy,
			POWER_SUPPLY_PROP_INPUT_SUSPEND, &val) ? !!val.intval : -1);
		int iusb_now = power_supply_get_property(chg->usb_psy, 
			POWER_SUPPLY_PROP_INPUT_CURRENT_NOW, &val) ? val.intval/1000 : -1;
		int iusb_set = !smblib_get_prop_input_current_settled(chg, &val)
			? val.intval/1000 : -1;
		int ibat_now = !smblib_get_prop_from_bms(chg, POWER_SUPPLY_PROP_CURRENT_NOW, &val)
			? val.intval/1000 : -1;
		int ibat_pmi = !smblib_get_charge_param(chg, &chg->param.fcc, &val.intval)
			? val.intval/1000 : 0;
		int ibat_smb = (prll_chgen <= 0) ? 0 : (!power_supply_get_property(chg->pl.psy,
			POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &val) ? val.intval/1000 : -1);
		int icl_override_aftapsd = (smblib_read(chg, USBIN_LOAD_CFG_REG, &reg) >= 0)
			? !!(reg & ICL_OVERRIDE_AFTER_APSD_BIT) : -1;
		int icl_override_usbmode = (smblib_read(chg, USBIN_ICL_OPTIONS_REG, &reg) >= 0)
			? reg : -1;

		pr_info("PMINFO: [USB] REAL:%s IUSB:%d(now)<=%d(set)<=%d(cap)\n"
			"PMINFO: [USB] IBAT:%d(now):=%d(pmi)+%d(smb)<=%d(cap)\n"
			"PMINFO: [USB] PRLL:CHGEN(%d)/PINEN(%d)/SUSPN(%d)\n"
			"PMINFO: [OVR] AFTAPSD:%d, USBMODE:0x%02x\n",
			usb_real,
			iusb_now, iusb_set, capping_iusb, ibat_now, ibat_pmi, ibat_smb, capping_ibat,
			prll_chgen, prll_pinen, prll_suspd,
			icl_override_aftapsd, icl_override_usbmode);
	}

out:	pr_info("PMINFO: ---------------------------------------------"
			"-----------------------------------------%s-END.\n",
			unified_bootmode_marker());

	vote(chg->awake_votable, POLLING_LOGGER_VOTER, false, 0);
	return;
}

#define PMI_REG_BASE_CHGR	0x1000
#define PMI_REG_BASE_DCDC	0x1100
#define PMI_REG_BASE_BATIF	0x1200
#define PMI_REG_BASE_USB	0x1300
#define PMI_REG_BASE_DC 	0x1400
#define PMI_REG_BASE_TYPEC	0x1500
#define PMI_REG_BASE_MISC	0x1600
#define PMI_REG_BASE_USBPD	0x1700
#define PMI_REG_BASE_MBG	0x2C00

static const struct base {
	const char* name;
	int base;
} bases [] = {
	/* 0: */ { .name = "POLL",	.base = -1, },	// Dummy for polling logs
	/* 1: */ { .name = "CHGR",	.base = PMI_REG_BASE_CHGR, },
	/* 2: */ { .name = "DCDC",	.base = PMI_REG_BASE_DCDC, },
	/* 3: */ { .name = "BATIF", 	.base = PMI_REG_BASE_BATIF, },
	/* 4: */ { .name = "USB",	.base = PMI_REG_BASE_USB, },
	/* 5: */ { .name = "DC",	.base = PMI_REG_BASE_DC, },
	/* 6: */ { .name = "TYPEC", 	.base = PMI_REG_BASE_TYPEC, },
	/* 7: */ { .name = "MISC",	.base = PMI_REG_BASE_MISC, },
	/* 8: */ { .name = "USBPD", 	.base = PMI_REG_BASE_USBPD, },
	/* 9: */ { .name = "MBG",	.base = PMI_REG_BASE_MBG, },
};

static void debug_battery(struct smb_charger* chg, int func) {
	if (func < 0) {
		int i;
		for (i = 1; i < ARRAY_SIZE(bases); ++i)
			debug_dump(chg, bases[i].name, bases[i].base);
	}
	else if (func == 0)
		debug_polling(chg);
	else if (func < ARRAY_SIZE(bases))
		debug_dump(chg, bases[func].name, bases[func].base);
	else
		; /* Do nothing */
}

#define USBIN_500MA	500000
static int restricted_charging_iusb(struct smb_charger* chg, int mvalue) {
	struct power_supply* veneer;
	union power_supply_propval val;
	int rc = 0;

	if (mvalue != VOTE_TOTALLY_BLOCKED && mvalue != VOTE_TOTALLY_RELEASED) {
		// Releasing undesirable capping on IUSB :

		// In the case of CWC, SW_ICL_MAX_VOTER limits IUSB
		// which is set in the 'previous' TypeC removal
		if (is_client_vote_enabled(chg->usb_icl_votable, SW_ICL_MAX_VOTER)) {
			pr_info("Releasing SW_ICL_MAX_VOTER\n");
			rc |= vote(chg->usb_icl_votable, SW_ICL_MAX_VOTER,
				true, mvalue*1000);
		}

		// In the case of non SDP enumerating by DWC,
		// DWC will not set any value via USB_PSY_VOTER.
		if (is_client_vote_enabled(chg->usb_icl_votable, USB_PSY_VOTER)) {
			veneer = power_supply_get_by_name("veneer");
			if (veneer) {
				power_supply_get_property(veneer, POWER_SUPPLY_PROP_SDP_CURRENT_MAX, &val);
				if(val.intval != VOTE_TOTALLY_RELEASED && val.intval == mvalue * 1000) {
					pr_info("Releasing USB_PSY_VOTER\n");
					rc |= vote(chg->usb_icl_votable, USB_PSY_VOTER, true, mvalue*1000);
				}
				power_supply_put(veneer);
			}
		}

		// In case of Float charger, set SDP_CURRENT_MAX for current setting
		if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_FLOAT) {
			val.intval = USBIN_500MA;
			pr_info("Vote POWER_SUPPLY_PROP_SDP_CURRENT_MAX to 500mA\n");
			rc |= power_supply_set_property(chg->usb_psy,
				POWER_SUPPLY_PROP_SDP_CURRENT_MAX, &val);
			rerun_election(chg->usb_icl_votable);
		}
	} else {
		pr_info("USBIN blocked\n");
	}
	rc |= vote(chg->usb_icl_votable, VENEER_VOTER_IUSB,
		mvalue != VOTE_TOTALLY_RELEASED, mvalue*1000);

	return	rc ? -EINVAL : 0;
}

static int restricted_charging_ibat(struct smb_charger* chg, int mvalue) {
	int rc = 0;

	if (mvalue != VOTE_TOTALLY_BLOCKED) {
		pr_info("Restricted IBAT : %duA\n", mvalue*1000);
		rc |= vote(chg->fcc_votable, VENEER_VOTER_IBAT,
			mvalue != VOTE_TOTALLY_RELEASED, mvalue*1000);

		rc |= vote(chg->chg_disable_votable,
			VENEER_VOTER_IBAT, false, 0);
	}
	else {
		pr_info("Stop charging\n");
		rc = vote(chg->chg_disable_votable,
			VENEER_VOTER_IBAT, true, 0);
	}

	return rc ? -EINVAL : 0;
}

static int restricted_charging_vfloat(struct smb_charger* chg, int mvalue) {
	int uv_float, uv_now, rc = 0;
	union power_supply_propval val;

	if (mvalue != VOTE_TOTALLY_BLOCKED) {
		if (mvalue == VOTE_TOTALLY_RELEASED) {
		       /* Clearing related voters :
			* 1. VENEER_VOTER_VFLOAT for BTP and
			* 2. TAPER_STEPPER_VOTER for pl step charging
			*/
			vote(chg->fv_votable, VENEER_VOTER_VFLOAT,
				false, 0);
			vote(chg->fcc_votable, "TAPER_STEPPER_VOTER",
				false, 0);

		       /* If EoC is met with the restricted vfloat,
			* charging is not resumed automatically with restoring vfloat only.
			* Because SoC is not be lowered, so FG(BMS) does not trigger "Recharging".
			* For work-around, do recharging manually here.
			*/
			rc |= vote(chg->chg_disable_votable, VENEER_VOTER_VFLOAT,
				true, 0);
			rc |= vote(chg->chg_disable_votable, VENEER_VOTER_VFLOAT,
				false, 0);
		}
		else {
		       /* At the normal restriction, vfloat is adjusted to "max(vfloat, vnow)",
			* to avoid bat-ov.
			*/
			uv_float = mvalue*1000;
			rc |= power_supply_get_property(chg->bms_psy,
				POWER_SUPPLY_PROP_VOLTAGE_OCV, &val);
			uv_now = val.intval;
			pr_debug("uv_now : %d\n", uv_now);
			if (uv_now > uv_float
					&& !is_client_vote_enabled(chg->fv_votable, VENEER_VOTER_VFLOAT)) {
				rc |= vote(chg->chg_disable_votable, VENEER_VOTER_VFLOAT,
					true, 0);
			} else {
				rc |= vote(chg->chg_disable_votable, VENEER_VOTER_VFLOAT,
					false, 0);
				rc |= vote(chg->fv_votable, VENEER_VOTER_VFLOAT,
					true, uv_float);
			}
		}
	}
	else {
		pr_info("Non permitted mvalue\n");
		rc = -EINVAL;
	}

	if (rc) {
		pr_err("Failed to restrict vfloat\n");
		vote(chg->fv_votable, VENEER_VOTER_VFLOAT, false, 0);
		vote(chg->chg_disable_votable, VENEER_VOTER_VFLOAT, false, 0);
		rc = -EINVAL;
	}

	return rc;
}

static int restricted_charging_hvdcp(struct smb_charger* chg, int mvalue) {

	if (VOTE_TOTALLY_BLOCKED < mvalue && mvalue < VOTE_TOTALLY_RELEASED) {
		pr_info("Non permitted mvalue for HVDCP voting %d\n", mvalue);
		return -EINVAL;
	}

	if(chg->hvdcp_disable)
		return 0;

	smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG, HVDCP_EN_BIT,
					mvalue == VOTE_TOTALLY_BLOCKED ? HVDCP_EN_BIT : 0);
	return 0;
}

static int restricted_charging(struct smb_charger* chg, const union power_supply_propval *val) {
	int rc;
	enum voter_type type = vote_type(val);
	int limit = vote_limit(val); // in mA

	switch (type) {
	case VOTER_TYPE_IUSB:
		rc = restricted_charging_iusb(chg, limit);
		break;
	case VOTER_TYPE_IBAT:
		rc = restricted_charging_ibat(chg, limit);
		break;
	case VOTER_TYPE_VFLOAT:
		rc = restricted_charging_vfloat(chg, limit);
		break;
	case VOTER_TYPE_HVDCP:
		rc = restricted_charging_hvdcp(chg, limit);
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}

static void update_charging_step(int stat) {
	   char buf [2] = { 0, };
	   enum charging_step chgstep;

	   switch (stat) {
	   case TRICKLE_CHARGE:
	   case PRE_CHARGE:    chgstep = CHARGING_STEP_TRICKLE;
		   break;
	   case FULLON_CHARGE: chgstep = CHARGING_STEP_CC;
		   break;
	   case TAPER_CHARGE:  chgstep = CHARGING_STEP_CV;
		   break;
	   case TERMINATE_CHARGE:  chgstep = CHARGING_STEP_TERMINATED;
		   break;
	   case DISABLE_CHARGE:    chgstep = CHARGING_STEP_NOTCHARGING;
		   break;
	   default: 	   chgstep = CHARGING_STEP_DISCHARGING;
		   break;
	   }

	   snprintf(buf, sizeof(buf), "%d", chgstep);
	   unified_nodes_store("charging_step", buf, strlen(buf));
}

static void support_parallel_checking(struct smb_charger *chg, int disable) {
	char buff [16] = { 0, };
	int  test;
	char* client;

	if (!disable // Enabling parallel charging
		&& unified_nodes_show("support_fastpl", buff)
		&& sscanf(buff, "%d", &test) && test == 1) {

		vote(chg->pl_enable_votable_indirect, USBIN_I_VOTER, true, 0);
		while (get_effective_result(chg->pl_disable_votable)) {
			client = (char*) get_effective_client(chg->pl_disable_votable);
			vote(chg->pl_disable_votable, client, false, 0);
			pr_info("FASTPL: Clearing PL_DISABLE voter %s for test purpose\n", client);
		}
		pr_info("FASTPL: After clearing PL_DISABLE, result:%d, client:%s\n",
			get_effective_result(chg->pl_disable_votable),
			get_effective_client(chg->pl_disable_votable));
	}
}

#define SAFETY_TIMER_ENABLE_CFG_REG	(CHGR_BASE + 0xA0)
#define PRE_CHARGE_SAFETY_TIMER_EN	BIT(1)
#define FAST_CHARGE_SAFETY_TIMER_EN	BIT(0)
static int safety_timer_enabled(struct smb_charger *chg, int* val) {
	u8	reg;
	int	rc = smblib_read(chg, SAFETY_TIMER_ENABLE_CFG_REG, &reg);

	if (rc >= 0)
		*val = !!((reg & PRE_CHARGE_SAFETY_TIMER_EN)
			&& (reg & FAST_CHARGE_SAFETY_TIMER_EN));
	else
		pr_err("Failed to get SAFETY_TIMER_ENABLE_CFG\n");

	return rc;
}

static int safety_timer_enable(struct smb_charger *chg, bool enable) {

	int	val, rc = safety_timer_enabled(chg, &val);
	u8	reg = enable ?
			(PRE_CHARGE_SAFETY_TIMER_EN & FAST_CHARGE_SAFETY_TIMER_EN) : 0;

	if (rc >= 0 && val == !enable)
		return smblib_masked_write(chg, SAFETY_TIMER_ENABLE_CFG_REG,
			PRE_CHARGE_SAFETY_TIMER_EN | FAST_CHARGE_SAFETY_TIMER_EN, reg);

	return rc;
}

///////////////////////////////////////////////////////////////////////////////

#define PROPERTY_CONSUMED_WITH_SUCCESS	0
#define PROPERTY_CONSUMED_WITH_FAIL	EINVAL
#define PROPERTY_BYPASS_REASON_NOENTRY	ENOENT
#define PROPERTY_BYPASS_REASON_ONEMORE	EAGAIN

static enum power_supply_property extension_battery_appended [] = {
	POWER_SUPPLY_PROP_STATUS_RAW,
#ifndef CONFIG_LGE_PM_CCD
	POWER_SUPPLY_PROP_CAPACITY_RAW,
#endif
	POWER_SUPPLY_PROP_BATTERY_TYPE,
	POWER_SUPPLY_PROP_DEBUG_BATTERY,
#ifdef CONFIG_LGE_PM_CCD
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
#endif
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE,
	POWER_SUPPLY_PROP_RESTRICTED_CHARGING,
	POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_PARALLEL_MODE,
};

static int extension_battery_get_property_pre(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val) {
	int rc = PROPERTY_CONSUMED_WITH_SUCCESS;

	struct smb_charger* chg = power_supply_get_drvdata(psy);
	struct power_supply* veneer = power_supply_get_by_name("veneer");
#ifdef CONFIG_LGE_PM_CCD
	char buf[10] = {0, };
#endif

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS :
		if (!veneer || power_supply_get_property(veneer, POWER_SUPPLY_PROP_STATUS, val))
			rc = -PROPERTY_BYPASS_REASON_ONEMORE;
		break;
	case POWER_SUPPLY_PROP_HEALTH :
		if (!veneer || power_supply_get_property(veneer, POWER_SUPPLY_PROP_HEALTH, val))
			rc = -PROPERTY_BYPASS_REASON_ONEMORE;
		break;
#ifndef CONFIG_LGE_PM_CCD
	case POWER_SUPPLY_PROP_CAPACITY_RAW :
		if (!chg->bms_psy || power_supply_get_property(chg->bms_psy, POWER_SUPPLY_PROP_CAPACITY_RAW, val))
			rc = -PROPERTY_CONSUMED_WITH_FAIL;
		break;
#endif
	case POWER_SUPPLY_PROP_BATTERY_TYPE :
		if (!chg->bms_psy || power_supply_get_property(chg->bms_psy, POWER_SUPPLY_PROP_BATTERY_TYPE, val))
			rc = -PROPERTY_CONSUMED_WITH_FAIL;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN :
		if (!chg->bms_psy || power_supply_get_property(chg->bms_psy, POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, val))
			rc = -PROPERTY_CONSUMED_WITH_FAIL;
		break;

	case POWER_SUPPLY_PROP_STATUS_RAW :
		rc = smblib_get_prop_batt_status(chg, val);
		break;
	case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE :
		rc = safety_timer_enabled(chg, &val->intval);
		break;

#ifdef CONFIG_LGE_PM_CCD
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW :
		unified_nodes_show("time_to_full_now", buf);
		if(kstrtoint(buf, 10, &val->intval))
			pr_err("error kstrtoint %s to %d\n", buf, val->intval);
		break;
#endif
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT :
		val->intval = get_effective_result(chg->fcc_votable);
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED :
		val->intval = !get_effective_result(chg->chg_disable_votable);
		break;

	case POWER_SUPPLY_PROP_DEBUG_BATTERY :
	case POWER_SUPPLY_PROP_RESTRICTED_CHARGING:
		/* Do nothing and just consume getting */
		val->intval = -1;
		break;

	case POWER_SUPPLY_PROP_PARALLEL_MODE :
		val->intval = chg->parallel_pct;
		break;

	default:
		rc = -PROPERTY_BYPASS_REASON_NOENTRY;
		break;
	}

	if (veneer)
		power_supply_put(veneer);

	return rc;
}

static int extension_battery_get_property_post(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val, int rc) {

	switch (psp) {
	default:
		break;
	}

	return rc;
}

static int extension_battery_set_property_pre(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val) {
	int rc = PROPERTY_CONSUMED_WITH_SUCCESS;

	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
#ifdef CONFIG_LGE_PM_CCD
	char buf[10] = {0, };
#endif

	switch (psp) {
	case POWER_SUPPLY_PROP_RESTRICTED_CHARGING :
		rc = restricted_charging(chg, val);
		break;

	case POWER_SUPPLY_PROP_CHARGE_TYPE :
		update_charging_step(val->intval);
		break;

	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED :
		vote(chg->chg_disable_votable, USER_VOTER, (bool)!val->intval, 0);
		break;

	case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE :
		rc = safety_timer_enable(chg, !!val->intval);
		break;

	case POWER_SUPPLY_PROP_CURRENT_QNOVO:
		vote(chg->fcc_votable, QNOVO_VOTER,
			(val->intval >= 0), val->intval);
		break;

	case POWER_SUPPLY_PROP_DP_DM :
		pr_info("smblib_dp_dm: dp dm update %d\n", val->intval);
		rc = -PROPERTY_BYPASS_REASON_ONEMORE;
		break;

	case POWER_SUPPLY_PROP_DEBUG_BATTERY :
		debug_battery(chg, val->intval);
		break;

	case POWER_SUPPLY_PROP_PARALLEL_MODE :
		chg->parallel_pct = val->intval;
		break;

#ifdef CONFIG_LGE_PM_CCD
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW :
		snprintf(buf, sizeof(buf), "%d", val->intval);
		unified_nodes_store("time_to_full_now", buf, strlen(buf));
		if(chg->batt_psy)
			power_supply_changed(chg->batt_psy);
		break;
#endif

	default:
		rc = -PROPERTY_BYPASS_REASON_NOENTRY;
	}

	return rc;
}

static int extension_battery_set_property_post(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val, int rc) {

	struct smb_charger* chg = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
		support_parallel_checking(chg, val->intval );
		break;

	default:
		break;
	}

	return rc;
}

///////////////////////////////////////////////////////////////////////////////
enum power_supply_property* extension_battery_properties(void) {
	static enum power_supply_property extended_properties[ARRAY_SIZE(smb5_batt_props) + ARRAY_SIZE(extension_battery_appended)];
	int size_original = ARRAY_SIZE(smb5_batt_props);
	int size_appended = ARRAY_SIZE(extension_battery_appended);

	memcpy(extended_properties, smb5_batt_props,
		size_original * sizeof(enum power_supply_property));
	memcpy(&extended_properties[size_original], extension_battery_appended,
		size_appended * sizeof(enum power_supply_property));

	veneer_extension_pspoverlap(smb5_batt_props, size_original,
		extension_battery_appended, size_appended);

	return extended_properties;
}

size_t extension_battery_num_properties(void) {
	return ARRAY_SIZE(smb5_batt_props) + ARRAY_SIZE(extension_battery_appended);
}

int extension_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val) {

	int rc = extension_battery_get_property_pre(psy, psp, val);
	if (rc == -PROPERTY_BYPASS_REASON_NOENTRY || rc == -PROPERTY_BYPASS_REASON_ONEMORE)
		rc = smb5_batt_get_prop(psy, psp, val);
	rc = extension_battery_get_property_post(psy, psp, val, rc);

	return rc;
}

int extension_battery_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val) {

	int rc = extension_battery_set_property_pre(psy, psp, val);
	if (rc == -PROPERTY_BYPASS_REASON_NOENTRY || rc == -PROPERTY_BYPASS_REASON_ONEMORE)
		rc = smb5_batt_set_prop(psy, psp, val);
	rc = extension_battery_set_property_post(psy, psp, val, rc);

	return rc;
}

int extension_battery_property_is_writeable(struct power_supply *psy,
	enum power_supply_property psp) {
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_DEBUG_BATTERY :
	case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE:
	case POWER_SUPPLY_PROP_RESTRICTED_CHARGING :
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_PARALLEL_MODE :
#ifdef CONFIG_LGE_PM_CCD
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW :
#endif
		rc = 1;
		break;
	default:
		rc = smb5_batt_prop_is_writeable(psy, psp);
		break;
	}
	return rc;
}


/*************************************************************
 * simple extension for usb psy.
 */

// Cached values
static enum charger_usbid
	   cache_usbid_type = CHARGER_USBID_INVALID;
static int cache_usbid_uvoltage = 0;
static int usbid_56k_min_mvol = 0;
static int usbid_56k_max_mvol = 0;
static int usbid_130k_min_mvol = 0;
static int usbid_130k_max_mvol = 0;
static int usbid_910k_min_mvol = 0;
static int usbid_910k_max_mvol = 0;
static int usbid_open_min_mvol = 0;
static int usbid_open_max_mvol = 0;

struct qpnp_vadc_chip   *vadc_dev;

static const char* adc_usbid_name(enum charger_usbid type) {
	switch (type) {
	case CHARGER_USBID_UNKNOWN:	return "UNKNOWN";
	case CHARGER_USBID_56KOHM:	return "56K";
	case CHARGER_USBID_130KOHM:	return "130K";
	case CHARGER_USBID_910KOHM:	return "910K";
	case CHARGER_USBID_OPEN:	return "OPEN";
	default :
		break;
	}

	return "INVALID";
}

static int adc_usbid_uvoltage(struct device* dev, struct qpnp_vadc_chip *vadc_dev) {
	struct qpnp_vadc_result result;
	int val = 0;
	int rc = 0;

// Read ADC if possible
	rc |= wa_avoiding_mbg_fault_usbid(true);
	rc |= qpnp_vadc_read(vadc_dev, VADC_AMUX3_GPIO, &result);
	rc |= wa_avoiding_mbg_fault_usbid(false);
	if (rc < 0)
		pr_info("USB-ID: Failed to read ADC\n");
	val = (int)result.physical;

	return rc >= 0 ? val : 0;
}

struct usbid_entry {
	enum charger_usbid type;
	int 	   min;
	int 	   max;
} usbid_table[4];

static int adc_usbid_range(void) {
	int i;

	usbid_table[0].type = CHARGER_USBID_56KOHM;
	usbid_table[1].type = CHARGER_USBID_130KOHM;
	usbid_table[2].type = CHARGER_USBID_910KOHM;
	usbid_table[3].type = CHARGER_USBID_OPEN;

	usbid_table[0].min = usbid_56k_min_mvol;
	usbid_table[0].max = usbid_56k_max_mvol;
	usbid_table[1].min = usbid_130k_min_mvol;
	usbid_table[1].max = usbid_130k_max_mvol;
	usbid_table[2].min = usbid_910k_min_mvol;
	usbid_table[2].max = usbid_910k_max_mvol;
	usbid_table[3].min = usbid_open_min_mvol;
	usbid_table[3].max = usbid_open_max_mvol;

	for (i = 0; i < ARRAY_SIZE(usbid_table); i++) {
		pr_info("USB-ID : %s min : %d, max %d\n",
					adc_usbid_name(usbid_table[i].type), usbid_table[i].min,  usbid_table[i].max);
	}
	return 0;
}

static enum charger_usbid adc_usbid_type(struct device* dev, int mvoltage) {
	enum charger_usbid 		usbid_ret = CHARGER_USBID_UNKNOWN;
	int i;

	for (i = 0; i < ARRAY_SIZE(usbid_table); i++) {
		if (usbid_table[i].min <= mvoltage && mvoltage <=usbid_table[i].max) {
			if (usbid_ret == CHARGER_USBID_UNKNOWN)
				usbid_ret = usbid_table[i].type;
			else
				pr_err("USB-ID: Overlap in usbid table!\n");
		}
	}

	return usbid_ret;
}

static DEFINE_MUTEX(psy_usbid_mutex);

static bool psy_usbid_update(struct device* dev) {
	int rc = 0;

	mutex_lock(&psy_usbid_mutex);
// Update all
	if(!vadc_dev) {
		if (of_find_property(dev->of_node, "qcom,usb_id-vadc", NULL)) {
			vadc_dev = qpnp_get_vadc(dev, "usb_id");
			if (IS_ERR(vadc_dev)) {
				rc = PTR_ERR(vadc_dev);
				if (rc != -EPROBE_DEFER)
					pr_info("Failed to find usbid VADC node, rc=%d\n",
						rc);
				else
					vadc_dev = NULL;
			}
		}
	}

	if (vadc_dev) {
		cache_usbid_uvoltage = adc_usbid_uvoltage(dev, vadc_dev);
		cache_usbid_type     = adc_usbid_type(dev, cache_usbid_uvoltage/1000);
		pr_info("USB-ID: Updated to %dmvol => %s\n",
			cache_usbid_uvoltage/1000, adc_usbid_name(cache_usbid_type));
	}
	else
		pr_err("USB-ID: Error on getting USBID ADC(mvol)\n");
	mutex_unlock(&psy_usbid_mutex);

// Check validation of result
	return cache_usbid_uvoltage > 0;
}

static enum charger_usbid psy_usbid_get(struct smb_charger* chg) {
	enum charger_usbid bootcable;

	if (cache_usbid_type == CHARGER_USBID_INVALID) {
		mutex_lock(&psy_usbid_mutex);
		if (cache_usbid_type == CHARGER_USBID_INVALID) {
		       /* If cable detection is not initiated, refer to the cmdline */
			bootcable = unified_bootmode_usbid();

			pr_info("USB-ID: Not initiated yet, refer to boot USBID %s\n",
				adc_usbid_name(bootcable));
			cache_usbid_type = bootcable;
		}
		mutex_unlock(&psy_usbid_mutex);
	}

	return cache_usbid_type;
}

// Variables for the moisture detection
static int  moisture_charging = -1;
static bool moisture_detected = false;
#define MOISTURE_VOTER			"MOISTURE_VOTER"

static int moisture_mode_on(struct smb_charger* chg) {
	int rc;
#ifndef CONFIG_LGE_USB_TYPE_C
	union power_supply_propval val = {0, };
#endif

	if (moisture_charging == 0) {
#ifndef CONFIG_LGE_USB_TYPE_C
	// 1. change UVLO to 10.3v
		rc = smblib_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG, USBIN_ADAPTER_ALLOW_12V);
		if (rc < 0) {
			pr_err("Couldn't write 0x%02x to USBIN_ADAPTER_ALLOW_CFG rc=%d\n",
				USBIN_ADAPTER_ALLOW_12V, rc);
			return rc;
		}
#endif

	// 2. disable apsd
		smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG, BC1P2_SRC_DETECT_BIT, 0);

	// 3. Set input suspend
		vote(chg->usb_icl_votable, MOISTURE_VOTER, true, 0);
	}

#ifndef CONFIG_LGE_USB_TYPE_C
	// 4. TYPEC_PR_NONE for power role
	val.intval = POWER_SUPPLY_TYPEC_PR_NONE;
	power_supply_set_property(chg->usb_psy, POWER_SUPPLY_PROP_TYPEC_POWER_ROLE, &val);
#endif

	// 5. disable Dp Dm
	if (chg->dpdm_reg && regulator_is_enabled(chg->dpdm_reg)) {
		rc = regulator_disable(chg->dpdm_reg);
		if (rc < 0) {
			pr_err( "Couldn't disable dpdm regulator rc=%d\n", rc);
			return rc;
		}
	}

	return 0;
}

static int moisture_mode_off(struct smb_charger* chg) {
	int rc;
#ifndef CONFIG_LGE_USB_TYPE_C
	union power_supply_propval val = {0, };
#endif

	// ~5. enable Dp Dm
	if (chg->dpdm_reg && !regulator_is_enabled(chg->dpdm_reg)) {
		rc = regulator_enable(chg->dpdm_reg);
		if (rc < 0) {
			pr_err("Couldn't enable dpdm regulator rc=%d\n", rc);
			return rc;
		}
	}

#ifndef CONFIG_LGE_USB_TYPE_C
	// ~4. TYPEC_PR_DUAL for power role
	val.intval = POWER_SUPPLY_TYPEC_PR_DUAL;
	power_supply_set_property(chg->usb_psy, POWER_SUPPLY_PROP_TYPEC_POWER_ROLE, &val);
#endif

	// ~3. Clear input suspend
	vote(chg->usb_icl_votable, MOISTURE_VOTER, false, 0);

	// ~2. enable apsd
	smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG, BC1P2_SRC_DETECT_BIT, BC1P2_SRC_DETECT_BIT);

#ifndef CONFIG_LGE_USB_TYPE_C
	// ~1. change UVLO to default
	rc = smblib_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG, USBIN_ADAPTER_ALLOW_5V_OR_9V_TO_12V);
	if (rc < 0) {
		pr_err("Couldn't write 0x%02x to USBIN_ADAPTER_ALLOW_CFG rc=%d\n",
			USBIN_ADAPTER_ALLOW_5V_OR_9V_TO_12V, rc);
		return rc;
	}
#endif

	return 0;
}

static int moisture_mode_command(struct smb_charger* chg, bool moisture) {
	int rc = 0;

	/* fetch the DPDM regulator */
	if (!chg->dpdm_reg
		&& of_get_property(chg->dev->of_node, "dpdm-supply", NULL)) {
		chg->dpdm_reg = devm_regulator_get(chg->dev, "dpdm");
		if (IS_ERR(chg->dpdm_reg)) {
			rc = PTR_ERR(chg->dpdm_reg);
			pr_err("Couldn't get dpdm regulator rc=%d\n", rc);
			chg->dpdm_reg = NULL;
			goto out;
		}
	}

	/* commanding only for changed */
	if (moisture_detected != moisture) {
		rc = moisture ? moisture_mode_on(chg) : moisture_mode_off(chg);
		if (rc >= 0)
			moisture_detected = moisture;

		pr_info("PMI: %s: %s to command moisture mode to %d\n", __func__,
			rc >= 0 ? "Success" : "Failed", moisture);
	}
	else
		pr_info("PMI: %s: Skip to %s\n", __func__, moisture ? "enable" : "disable");

out:
	return rc;
}

static bool fake_hvdcp_property(struct smb_charger *chg) {
	char buffer [16] = { 0, };
	int fakehvdcp;

	return unified_nodes_show("fake_hvdcp", buffer)
		&& sscanf(buffer, "%d", &fakehvdcp)
		&& !!fakehvdcp;
}

static bool fake_hvdcp_effected(struct smb_charger *chg) {
	union power_supply_propval val = {0, };

	if (fake_hvdcp_property(chg)
		&& power_supply_get_property(chg->usb_psy, 
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &val) >= 0
		&& val.intval/1000 >= 7000) {
		return true;
	}
	else
		return false;
}

static bool fake_hvdcp_enable(struct smb_charger *chg, bool enable) {
	u8 vallow;
	int rc;

	if (fake_hvdcp_property(chg)) {
		vallow = enable ? USBIN_ADAPTER_ALLOW_5V_TO_9V
			: USBIN_ADAPTER_ALLOW_5V_OR_9V_TO_12V;
		rc = smblib_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG, vallow);
		if (rc >= 0) {
			if (enable)
				vote(chg->usb_icl_votable, SW_ICL_MAX_VOTER, true, 3000000);
			return true;
		}
		else
			pr_err("Couldn't write 0x%02x to USBIN_ADAPTER_ALLOW_CFG rc=%d\n",
				vallow, rc);
	}
	else
		pr_debug("fake_hvdcp is not set\n");

	return false;
}

#define HVDCP_VOLTAGE_MV_MIN	5000
#define HVDCP_VOLTAGE_MV_MAX	9000
#define HVDCP_CURRENT_MA_MAX	1800
static int charger_power_hvdcp(/*@Nonnull*/ struct power_supply* usb, int type) {
	int voltage_mv, current_ma, power = 0;

	if (type == POWER_SUPPLY_TYPE_USB_HVDCP) {
		voltage_mv = wa_detect_standard_hvdcp_check()
			? HVDCP_VOLTAGE_MV_MAX : HVDCP_VOLTAGE_MV_MIN;
		current_ma = /* 1.8A fixed for HVDCP */
			HVDCP_CURRENT_MA_MAX;

		power = voltage_mv * current_ma;
	} else if ( type == POWER_SUPPLY_TYPE_USB_HVDCP_3 ) {
		voltage_mv = /* 9V fixed for HVDCP */
			HVDCP_VOLTAGE_MV_MAX;
		current_ma = /* 1.8A fixed for HVDCP */
			HVDCP_CURRENT_MA_MAX;

		power = voltage_mv * current_ma;
	} else { // Assertion failed
		pr_info("%s: Check the caller\n", __func__);
	}

	return power;
}

#define SCALE_300MA 300
#define FAKING_QC (HVDCP_VOLTAGE_MV_MAX*HVDCP_CURRENT_MA_MAX)
static int charger_power_adaptive(/*@Nonnull*/ struct power_supply* usb, int type) {
	int current_ma, power = 0;
	int voltage_mv = 5000; /* 5V fixed for DCP and CDP */
	union power_supply_propval buf = { .intval = 0, };
	struct smb_charger* chg = power_supply_get_drvdata(usb);

	if (type == POWER_SUPPLY_TYPE_USB_DCP || type == POWER_SUPPLY_TYPE_USB_CDP) {
		current_ma = !smblib_get_prop_input_current_settled(chg, &buf)
			? buf.intval / 1000 : 0;

		current_ma = ((current_ma - 1) / SCALE_300MA + 1) * SCALE_300MA;
		power = voltage_mv * current_ma;
	}
	else {	// Assertion failed
		pr_info("%s: Check the caller\n", __func__);
	}

	return power;
}

static int charger_power_sdp(/*@Nonnull*/ struct power_supply* usb, int type) {
	int current_ma, power = 0;
	union power_supply_propval buf = { .intval = 0, };
	int voltage_mv = 5000; /* 5V fixed for SDP */

	if (type == POWER_SUPPLY_TYPE_USB) {
		current_ma = !smb5_usb_get_prop(usb, POWER_SUPPLY_PROP_SDP_CURRENT_MAX, &buf)
			? buf.intval / 1000 : 0;

		power = voltage_mv * current_ma;
	}
	else {	// Assertion failed
		pr_info("%s: Check the caller\n", __func__);
	}

	return power;
}

static int charger_power_pd(/*@Nonnull*/ struct power_supply* usb, int type) {
	int voltage_mv, current_ma, power = 0;
	union power_supply_propval buf = { .intval = 0, };

	if (type == POWER_SUPPLY_TYPE_USB_PD) {
		voltage_mv = !smb5_usb_get_prop(usb, POWER_SUPPLY_PROP_PD_VOLTAGE_MAX, &buf)
			? buf.intval / 1000 : 0;
		current_ma = !smb5_usb_get_prop(usb, POWER_SUPPLY_PROP_PD_CURRENT_MAX, &buf)
			? buf.intval / 1000 : 0;

		power = voltage_mv * current_ma;
		pr_info("PD power %duW = %dmV * %dmA\n", power, voltage_mv, current_ma);
	}
	else {	// Assertion failed
		pr_info("%s: Check the caller\n", __func__);
	}

	return power;
}

static int charger_power_float(/*@Nonnull*/ struct power_supply* usb, int type) {
	int power = 0;
	int voltage_mv = 5000;
	int current_ma =  500;

	if (type == POWER_SUPPLY_TYPE_USB_FLOAT) {
		power = voltage_mv * current_ma;
	}
	else {	// Assertion failed
		pr_info("%s: Check the caller\n", __func__);
	}

	return power;
}

static bool usbin_ov_check(/*@Nonnull*/ struct smb_charger *chg) {
	int rc = 0;
	u8 stat;

	rc = smblib_read(chg, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		pr_info("%s: Couldn't read USBIN_RT_STS rc=%d\n", __func__, rc);
		return false;
	}

	return (bool)(stat & USBIN_OV_RT_STS_BIT);
}

static bool usb_pcport_check(/*@Nonnull*/ struct smb_charger *chg) {
	enum power_supply_type* pst = &chg->real_charger_type;
	union power_supply_propval val = { 0, };
	u8 reg = 0;
	bool usb_connected = false;
	bool usb_pdtype = false;
	bool usb_pcport = false;

	if (smblib_get_prop_usb_online(chg, &val) < 0) {
		pr_err("PMI: usb_pcport_check: Couldn't read smblib_get_prop_usb_online\n");
		return false;
	}
	else
		usb_connected = !!val.intval;

	if (smblib_read(chg, APSD_RESULT_STATUS_REG, &reg) < 0) {
		pr_err("PMI: usb_pcport_check: Couldn't read APSD_RESULT_STATUS\n");
		return false;
	}
	else
		reg &= APSD_RESULT_STATUS_MASK;

	usb_pdtype = (*pst == POWER_SUPPLY_TYPE_USB_PD)
		&& (reg == SDP_CHARGER_BIT || reg == CDP_CHARGER_BIT);
	usb_pcport = (*pst == POWER_SUPPLY_TYPE_USB
		|| *pst == POWER_SUPPLY_TYPE_USB_CDP);

	return usb_connected && (usb_pdtype || usb_pcport);
}

static int usb_pcport_current(/*@Nonnull*/ struct smb_charger *chg, int req) {
	struct power_supply* veneer = power_supply_get_by_name("veneer");
	if (veneer) {
		union power_supply_propval val;
		if (req == 900000) {
			// Update veneer's supplier type to USB 3.x
			val.intval = POWER_SUPPLY_TYPE_USB;
			power_supply_set_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE, &val);
		}
		power_supply_get_property(veneer, POWER_SUPPLY_PROP_SDP_CURRENT_MAX, &val);
		power_supply_put(veneer);

		if (val.intval != VOTE_TOTALLY_RELEASED)
			return val.intval;
	}

	return req;
}

static int extension_usb_set_sdp_current_max(/*@Nonnull*/ struct power_supply* psy,
		const union power_supply_propval* val) {
	static union power_supply_propval isdp;
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;

	isdp.intval = usb_pcport_current(chg, val->intval);
	if (isdp.intval != val->intval)
		pr_info("PMI: SDP_CURRENT_MAX %d is overridden to %d\n", val->intval, isdp.intval);
	val = &isdp;

	return smb5_usb_set_prop(psy, POWER_SUPPLY_PROP_SDP_CURRENT_MAX, val);
}

static bool extension_usb_get_online(/*@Nonnull*/ struct power_supply *psy) {
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	union power_supply_propval val;
	bool ret = false;

// Getting chg type from veneer
	struct power_supply* veneer
		= power_supply_get_by_name("veneer");
	int chgtype = (veneer && !power_supply_get_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE,
		&val)) ? val.intval : POWER_SUPPLY_TYPE_UNKNOWN;
// Pre-loading conditions
	bool online = !smb5_usb_get_prop(psy, POWER_SUPPLY_PROP_ONLINE, &val)
		? !!val.intval : false;
	bool present = !extension_usb_get_property(psy, POWER_SUPPLY_PROP_PRESENT, &val)
		? !!val.intval : false;
	bool ac = chgtype != POWER_SUPPLY_TYPE_UNKNOWN
		&& chgtype != POWER_SUPPLY_TYPE_USB && chgtype != POWER_SUPPLY_TYPE_USB_CDP;
	bool fo = veneer_voter_suspended(VOTER_TYPE_IUSB)
		== CHARGING_SUSPENDED_WITH_FAKE_OFFLINE;
	bool pc = usb_pcport_check(chg);
	bool usbov = usbin_ov_check(chg);

	if (veneer)
		power_supply_put(veneer);

	pr_debug("chgtype=%s, online=%d, present=%d, ac=%d, fo=%d, pc=%d\n",
		log_psy_type(chgtype), online, present, ac, fo, pc);

// Branched returning
	if (!online && present && ac && !fo) {
		pr_debug("Set ONLINE by force\n");
		ret = true;
	} else if (usbov && online) {
		pr_debug("Unset ONLINE by force\n");
		ret = false;
	} else if (pc) {
		pr_debug("Set OFFLINE due to non-AC\n");
		ret = false;
	}
	else
		ret = online;

	return ret;
}

static void extension_usb_set_pd_active(/*@Nonnull*/ struct smb_charger *chg, int pd_active) {
	struct power_supply* veneer = power_supply_get_by_name("veneer");
	union power_supply_propval pd = { .intval = POWER_SUPPLY_TYPE_USB_PD, };

	if (pd_active) {
		if (veneer) {
			power_supply_set_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE, &pd);
			power_supply_changed(veneer);
			power_supply_put(veneer);
		}
	}
	pr_info("pm8150b_charger: smblib_set_prop_pd_active: update pd active %d \n", pd_active);
	return;
}

///////////////////////////////////////////////////////////////////////////////

static enum power_supply_property extension_usb_appended [] = {
// Below 2 USB-ID properties don't need to be exported to user space.
	POWER_SUPPLY_PROP_RESISTANCE,		/* in uvol */
	POWER_SUPPLY_PROP_RESISTANCE_ID,	/* in ohms */
	POWER_SUPPLY_PROP_POWER_NOW,
};

enum power_supply_property* extension_usb_properties(void) {
	static enum power_supply_property extended_properties[ARRAY_SIZE(smb5_usb_props) + ARRAY_SIZE(extension_usb_appended)];
	int size_original = ARRAY_SIZE(smb5_usb_props);
	int size_appended = ARRAY_SIZE(extension_usb_appended);

	memcpy(extended_properties, smb5_usb_props,
		size_original * sizeof(enum power_supply_property));
	memcpy(&extended_properties[size_original], extension_usb_appended,
		size_appended * sizeof(enum power_supply_property));

	veneer_extension_pspoverlap(smb5_usb_props, size_original,
		extension_usb_appended, size_appended);

	return extended_properties;
}

size_t extension_usb_num_properties(void) {
	return ARRAY_SIZE(smb5_usb_props) + ARRAY_SIZE(extension_usb_appended);
}

int extension_usb_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val) {

	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_POWER_NOW :
		if (!smb5_usb_get_prop(psy, POWER_SUPPLY_PROP_REAL_TYPE, val)) {
			if (fake_hvdcp_enable(chg, true) && fake_hvdcp_effected(chg))
				val->intval = POWER_SUPPLY_TYPE_USB_HVDCP_3;

			switch(val->intval) {
				case POWER_SUPPLY_TYPE_USB_HVDCP:	/* High Voltage DCP */
				case POWER_SUPPLY_TYPE_USB_HVDCP_3:	/* Efficient High Voltage DCP */
					val->intval = charger_power_hvdcp(psy, val->intval);
					break;
				case POWER_SUPPLY_TYPE_USB_DCP:		/* Dedicated Charging Port */
				case POWER_SUPPLY_TYPE_USB_CDP:		/* Charging Downstream Port */
					val->intval = charger_power_adaptive(psy, val->intval);
					break;
				case POWER_SUPPLY_TYPE_USB:		/* Standard Downstream Port */
					val->intval = charger_power_sdp(psy, val->intval);
					break;
				case POWER_SUPPLY_TYPE_USB_PD:		/* Power Delivery */
					val->intval = charger_power_pd(psy, val->intval);
					break;
				case POWER_SUPPLY_TYPE_USB_FLOAT:	/* D+/D- are open but are not data lines */
					val->intval = charger_power_float(psy, val->intval);
					break;
				default :
					val->intval = 0;
					break;
			}
		}
		return 0;

	case POWER_SUPPLY_PROP_ONLINE :
		val->intval = extension_usb_get_online(psy);
		return 0;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW : {
		rc = smb5_usb_get_prop(psy, psp, val);
		return rc;

	}	return 0;

	case POWER_SUPPLY_PROP_PRESENT :
		if (usbin_ov_check(chg)) {
			pr_debug("Unset PRESENT by force\n");
			val->intval = false;
			return 0;
		}
		if (chg->pd_hard_reset && chg->typec_mode != POWER_SUPPLY_TYPEC_NONE
				&& unified_bootmode_chargerlogo()) {
			pr_debug("Set PRESENT by force\n");
			val->intval = true;
			return 0;
		}
		{
			char buff [16] = { 0, };
			if(unified_nodes_show("charging_enable", buff) &&
				sscanf(buff, "%d", &(val->intval)) && !(val->intval))
				return 0;
		}
		break;

	case POWER_SUPPLY_PROP_RESISTANCE :	/* in uvol */
		val->intval = cache_usbid_uvoltage;
		return 0;

	case POWER_SUPPLY_PROP_RESISTANCE_ID :	/* in ohms */
		val->intval = psy_usbid_get(chg);
		return 0;

	case POWER_SUPPLY_PROP_USB_HC :
		val->intval = fake_hvdcp_effected(chg);
		return 0;

	case POWER_SUPPLY_PROP_MOISTURE_DETECTED :
		val->intval = moisture_detected;
		return 0;

	default:
		break;
	}

	return smb5_usb_get_prop(psy, psp, val);
}

int extension_usb_set_property(struct power_supply* psy,
	enum power_supply_property psp, const union power_supply_propval* val) {
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;

	switch (psp) {
	case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
		pr_info("pm8150b_charger: smblib_set_prop_pd_in_hard_reset: update hard reset %d \n", val->intval);
		break;

	case POWER_SUPPLY_PROP_PD_ACTIVE:
		extension_usb_set_pd_active(chg, val->intval);
		break;

	/* _PD_VOLTAGE_MAX, _PD_VOLTAGE_MIN, _USB_HC are defined for fake_hvdcp */
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MAX :
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MIN :
		if (usbin_ov_check(chg)) {
			pr_info("Skip PD %s voltage control(%d mV) by ov\n",
				psp== POWER_SUPPLY_PROP_PD_VOLTAGE_MAX ? "Max":"Min", val->intval/1000);
			return 0;
		}
		if (fake_hvdcp_property(chg)
			&& chg->real_charger_type == POWER_SUPPLY_TYPE_USB_DCP) {
			pr_info("PMI: Skipping PD voltage control\n");
			return 0;
		}
		break;
	case POWER_SUPPLY_PROP_USB_HC :
		fake_hvdcp_enable(chg, !!val->intval);
		return 0;

	case POWER_SUPPLY_PROP_SDP_CURRENT_MAX :
		return extension_usb_set_sdp_current_max(psy, val);

	case POWER_SUPPLY_PROP_RESISTANCE :
		psy_usbid_update(chg->dev);
		return 0;

	case POWER_SUPPLY_PROP_MOISTURE_DETECTED :
		return moisture_mode_command(chg, !!val->intval);

	default:
		break;
	}

	return smb5_usb_set_prop(psy, psp, val);
}

int extension_usb_property_is_writeable(struct power_supply *psy,
	enum power_supply_property psp) {
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_RESISTANCE :
	case POWER_SUPPLY_PROP_MOISTURE_DETECTED:
		rc = 1;
		break;

	default:
		rc = smb5_usb_prop_is_writeable(psy, psp);
		break;
	}

	return rc;
}

/*************************************************************
 * simple extension for usb port psy.
 */

static bool extension_usb_port_get_online(/*@Nonnull*/ struct power_supply *psy) {
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;

	// Prepare condition 'usb type' from veneer
	union power_supply_propval val;
	struct power_supply* veneer
		= power_supply_get_by_name("veneer");
	int  chgtype = (veneer && !power_supply_get_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE,
		&val)) ? val.intval : POWER_SUPPLY_TYPE_UNKNOWN;
	bool online = !smb5_usb_port_get_prop(psy, POWER_SUPPLY_PROP_ONLINE, &val)
		? !!val.intval : false;
	bool usb = chgtype == POWER_SUPPLY_TYPE_USB
		|| chgtype == POWER_SUPPLY_TYPE_USB_CDP;
	bool fo = veneer_voter_suspended(VOTER_TYPE_IUSB)
		== CHARGING_SUSPENDED_WITH_FAKE_OFFLINE;
	bool pc = usb_pcport_check(chg);
	bool ret;

	if (veneer)
		power_supply_put(veneer);
// determine USB online
	ret = ((usb || pc) && !fo) ? true : online;
	return ret;
}

///////////////////////////////////////////////////////////////////////////////

static enum power_supply_property extension_usb_port_appended [] = {
};

enum power_supply_property* extension_usb_port_properties(void) {
	static enum power_supply_property extended_properties[ARRAY_SIZE(smb5_usb_port_props) + ARRAY_SIZE(extension_usb_port_appended)];
	int size_original = ARRAY_SIZE(smb5_usb_port_props);
	int size_appended = ARRAY_SIZE(extension_usb_port_appended);

	memcpy(extended_properties, smb5_usb_port_props,
		size_original * sizeof(enum power_supply_property));
	memcpy(&extended_properties[size_original], extension_usb_port_appended,
		size_appended * sizeof(enum power_supply_property));

	veneer_extension_pspoverlap(smb5_usb_port_props, size_original,
		extension_usb_port_appended, size_appended);

	return extended_properties;
}

size_t extension_usb_port_num_properties(void) {
	return ARRAY_SIZE(smb5_usb_port_props) + ARRAY_SIZE(extension_usb_port_appended);
}

int extension_usb_port_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val) {

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE :
		val->intval = extension_usb_port_get_online(psy);
		return 0;

	default:
		break;
	}

	return smb5_usb_port_get_prop(psy, psp, val);
}


/*************************************************************
 * simple extension for usb main psy.
 */

#define FABCURR 1500000
#define USBIN_25MA	25000
#define USBIN_500MA	500000
static bool extension_usb_main_set_current_max(/*@Nonnull*/struct smb_charger* chg, int current_max) {
	enum charger_usbid usbid = psy_usbid_get(chg);
	bool fabid = usbid == CHARGER_USBID_56KOHM || usbid == CHARGER_USBID_130KOHM || usbid == CHARGER_USBID_910KOHM;
	bool pcport = chg->real_charger_type == POWER_SUPPLY_TYPE_USB;
	bool fabproc = fabid && pcport;

	int icl = current_max;
	bool chgable = USBIN_25MA < icl && icl < INT_MAX;
	int rc = 0;

	if (fabproc && chgable) {
		/* 1. Configure USBIN_ICL_OPTIONS_REG
		(It doesn't need to check result : refer to the 'smblib_set_icl_current') */
		smblib_masked_write(chg, USBIN_ICL_OPTIONS_REG,
			USBIN_MODE_CHG_BIT | CFG_USB3P0_SEL_BIT | USB51_MODE_BIT,
			USBIN_MODE_CHG_BIT);

		/* 2. Configure current */
		rc = smblib_set_charge_param(chg, &chg->param.usb_icl, FABCURR);
		if (rc < 0) {
			pr_err("Couldn't set ICL for fabcable, rc=%d\n", rc);
			return false;
		}

		/* 3. Enforce override */
		rc = smblib_icl_override(chg, true);
		if (rc < 0) {
			pr_err("Couldn't set ICL override rc=%d\n", rc);
			return false;
		}

		/* 4. Unsuspend after configuring current and override */
		rc = smblib_set_usb_suspend(chg, false);
		if (rc < 0) {
			pr_err("Couldn't resume input rc=%d\n", rc);
			return false;
		}

		/* 5. Configure USBIN_CMD_ICL_OVERRIDE_REG */
		rc = wa_command_icl_override(chg);
		if (rc < 0) {
			pr_err("Couldn't set icl override\n");
			return false;
		}

		if (icl != FABCURR)
			pr_info("Success to set IUSB (%d -> %d)mA for fabcable\n", icl/1000, FABCURR/1000);

		return true;
	}

	if (icl <= USBIN_500MA && icl > USBIN_25MA) {
		rc = smblib_set_charge_param(chg, &chg->param.usb_icl, icl);
		if (rc < 0) {
			pr_info("Couldn't set HC ICL rc=%d\n", rc);
		}

		/* unsuspend after configuring current and override */
		rc = smblib_set_usb_suspend(chg, false);
		if (rc < 0) {
			pr_info("Couldn't resume input rc=%d\n", rc);
		}
	}

	return false;
}

static bool extension_usb_main_get_current_max(/*@Nonnull*/struct smb_charger* chg, union power_supply_propval* val) {
	enum charger_usbid usbid = psy_usbid_get(chg);
	bool fabid = usbid == CHARGER_USBID_56KOHM || usbid == CHARGER_USBID_130KOHM || usbid == CHARGER_USBID_910KOHM;
	bool pcport = chg->real_charger_type == POWER_SUPPLY_TYPE_USB;
	bool fabproc = fabid && pcport;

	if (fabproc) {
		int rc = smblib_get_charge_param(chg, &chg->param.usb_icl, &val->intval);
		if (rc < 0) {
			pr_err("Couldn't get ICL for fabcable, rc=%d\n", rc);
			return false;
		}
		else
			return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////

int extension_usb_main_set_property(struct power_supply* psy,
	enum power_supply_property psp, const union power_supply_propval* val) {
	struct smb_charger* chg = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX :
		if (extension_usb_main_set_current_max(chg, val->intval))
			return 0;
		break;
	default:
		break;
	}

	return smb5_usb_main_set_prop(psy, psp, val);
}

int extension_usb_main_get_property(struct power_supply* psy,
	enum power_supply_property psp, union power_supply_propval* val) {
	struct smb_charger* chg = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX :
		if (extension_usb_main_get_current_max(chg, val))
			return 0;
		break;
	default:
		break;
	}

	return smb5_usb_main_get_prop(psy, psp, val);
}

/*************************************************************
 * extension for smb5 probe.
 */

int extension_smb5_probe(struct smb_charger *chg) {
	struct device_node *node = chg->dev->of_node;
	int rc;

	if(!vadc_dev) {
		if (of_find_property(node, "qcom,usb_id-vadc", NULL)) {
			vadc_dev = qpnp_get_vadc(chg->dev, "usb_id");
			if (IS_ERR(vadc_dev)) {
				rc = PTR_ERR(vadc_dev);
				if (rc != -EPROBE_DEFER)
					pr_info("Failed to find usbid VADC node, rc=%d\n",
						rc);
				else
					vadc_dev = NULL;
			}
		}
	}

	rc = of_property_read_s32(of_find_node_by_name(node, "lge-extension-usb"),
		"lge,usbid-56k-min-mvol", &usbid_56k_min_mvol);
	if (rc < 0) {
		pr_err("Fail to get usbid-56k-min-mvol rc = %d \n", rc);
		usbid_56k_min_mvol = 0;
		goto error;
	}

	rc = of_property_read_s32(of_find_node_by_name(node, "lge-extension-usb"),
		"lge,usbid-56k-max-mvol", &usbid_56k_max_mvol);
	if (rc < 0) {
		pr_err("Fail to get usbid-56k-max-mvol rc = %d \n", rc);
		usbid_56k_max_mvol = 0;
		goto error;
	}

	rc = of_property_read_s32(of_find_node_by_name(node, "lge-extension-usb"),
		"lge,usbid-130k-min-mvol", &usbid_130k_min_mvol);
	if (rc < 0) {
		pr_err("Fail to get usbid-130k-min-mvol rc = %d \n", rc);
		usbid_130k_min_mvol = 0;
		goto error;
	}

	rc = of_property_read_s32(of_find_node_by_name(node, "lge-extension-usb"),
		"lge,usbid-130k-max-mvol", &usbid_130k_max_mvol);
	if (rc < 0) {
		pr_err("Fail to get usbid-130k-max-mvol rc = %d \n", rc);
		usbid_130k_max_mvol = 0;
		goto error;
	}

	rc = of_property_read_s32(of_find_node_by_name(node, "lge-extension-usb"),
		"lge,usbid-910k-min-mvol", &usbid_910k_min_mvol);
	if (rc < 0) {
		pr_err("Fail to get usbid-910k-min-mvol rc = %d \n", rc);
		usbid_910k_min_mvol = 0;
		goto error;
	}

	rc = of_property_read_s32(of_find_node_by_name(node, "lge-extension-usb"),
		"lge,usbid-910k-max-mvol", &usbid_910k_max_mvol);
	if (rc < 0) {
		pr_err("Fail to get usbid-910k-max-mvol rc = %d \n", rc);
		usbid_910k_max_mvol = 0;
		goto error;
	}

	rc = of_property_read_s32(of_find_node_by_name(node, "lge-extension-usb"),
		"lge,usbid-open-min-mvol", &usbid_open_min_mvol);
	if (rc < 0) {
		pr_err("Fail to get usbid-open-min-mvol rc = %d \n", rc);
		usbid_open_min_mvol = 0;
		goto error;
	}

	rc = of_property_read_s32(of_find_node_by_name(node, "lge-extension-usb"),
		"lge,usbid-open-max-mvol", &usbid_open_max_mvol);
	if (rc < 0) {
		pr_err("Fail to get usbid-open-max-mvol rc = %d \n", rc);
		usbid_open_max_mvol = 0;
		goto error;
	}
	// Build up USB-ID table
	rc = adc_usbid_range();

	/* fetch the flag of moisture charging */
	rc = of_property_read_u32(of_find_node_by_name(node, "lge-extension-usb"),
		"lge,feature-moisture-charging", &moisture_charging);
	if (rc < 0) {
		pr_err("Fail to get feature-moisture-charging rc = %d \n", rc);
		moisture_charging = -1;
		goto error;
	}

	pr_info("%sing moisture charging\n", !!moisture_charging ? "Support" : "Block");

	rc = of_property_read_u32(node, "lge,parallel-pct", &chg->parallel_pct);
	if (rc < 0) {
		pr_err("Fail to get parallel-pct rc = %d \n", rc);
		chg->parallel_pct = 50;
	}

	/* Disable unused irq */
	disable_irq_nosync(chg->irq_info[BAT_TEMP_IRQ].irq);

	/* Disable rid irq on factory cable */
	if(unified_bootmode_fabproc()) {
		disable_irq_nosync(chg->irq_info[TYPEC_OR_RID_DETECTION_CHANGE_IRQ].irq);
	}

error:
	return rc;
}
