/*
 * CAUTION! :
 * 	This file will be included at the end of "qpnp-smb5.c".
 * 	So "qpnp-smb5.c" should be touched before you start to build.
 * 	If not, your work will not be applied to the built image
 * 	because the build system may not care the update time of this file.
 */

#include "veneer-primitives.h"
#include <linux/input/qpnp-power-on.h>

#define VENEER_VOTER_IUSB 	"VENEER_VOTER_IUSB"
#define VENEER_VOTER_IBAT 	"VENEER_VOTER_IBAT"
#define VENEER_VOTER_VFLOAT 	"VENEER_VOTER_VFLOAT"
#define VENEER_VOTER_HVDCP 	"VENEER_VOTER_HVDCP"

enum {
	NO_CHARGE = 0,
	PRE_CHARGE,
	FAST_CHARGE,
	TAPER_CHARGE,
};

static char* log_raw_status(struct smbchg_chip *chip) {
	int rc;
	u8 reg = 0, chg_type;

	rc = smbchg_read(chip, &reg, chip->chgr_base + RT_STS, 1);
	if (rc < 0)
		dev_err(chip->dev, "Unable to read RT_STS rc = %d\n", rc);

	if (reg & CHG_INHIBIT_BIT)
		return "INHIBIT";

	if (reg & BAT_TCC_REACHED_BIT)
		return "TERMINATE";

	rc = smbchg_read(chip, &reg, chip->chgr_base + CHGR_STS, 1);
	if (rc < 0)
		dev_err(chip->dev, "Unable to read CHGR_STS rc = %d\n", rc);
	chg_type = (reg & CHG_TYPE_MASK) >> CHG_TYPE_SHIFT;
	
	switch (chg_type) {
		case NO_CHARGE:			return "NO";
		case PRE_CHARGE:		return "PRE";
		case FAST_CHARGE:		return "FAST";
		case TAPER_CHARGE:		return "TAPER";
		default:				break;
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

static void debug_dump(struct smbchg_chip *chip, const char* title, u16 start) {
	u16 reg, i;
	u8 val[16];
	for (reg = start; reg < start + 0x100; reg += 0x10) {
		for (i = 0; i < 0x10; i++) {
			val[i] = 0x99;
			smbchg_read(chip, &val[i], reg+i, 1);
		}
		pr_err("REGDUMP: [%s] 0x%X - %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
			title, reg, val[0], val[1], val[2], val[3], val[4], val[5], val[6], val[7], val[8],
			val[9], val[10], val[11], val[12], val[13], val[14], val[15]);
	}
}
struct qpnp_vadc_chip   *usbin_vadc_dev;

#define DEFAULT_VBUS              (-1)
static int get_usb_adc(struct smbchg_chip *chip)
{
    struct qpnp_vadc_result results;
    int rc, usbin_vol;

    if (!is_usb_present(chip))
        return 0;

    
    if (IS_ERR_OR_NULL(usbin_vadc_dev)) {
        usbin_vadc_dev =
              qpnp_get_vadc(chip->dev, "usbin");
        if (IS_ERR_OR_NULL(usbin_vadc_dev)) {
            pr_err("vadc is not init yet\n");
            return DEFAULT_VBUS;
        }
    }

    rc = qpnp_vadc_read(usbin_vadc_dev, USBIN, &results);
    if (rc < 0) {
        pr_err("failed to read usb in voltage. rc = %d\n", rc);
        return DEFAULT_VBUS;
    }

    usbin_vol = (int)results.physical;
    pr_debug("usbin voltage = %dmV\n", usbin_vol/1000);

    return usbin_vol;
}

static u8 get_charging_status(struct smbchg_chip *chip)
{
   int rc;
   u8 reg = 0;

   rc = smbchg_read(chip, &reg, chip->chgr_base + CHGR_STS, 1);
   if (rc < 0) {
       dev_err(chip->dev, "Unable to read CHGR_STS rc = %d\n", rc);
   }

   return reg;
}

static void debug_polling(struct smbchg_chip *chip) {
	union power_supply_propval val = {0, };
	bool presence_usb = !smbchg_usb_get_property(chip->usb_psy,
				POWER_SUPPLY_PROP_PRESENT, &val) ? !!val.intval : false;
	bool disabled_ibat  = !!get_effective_result(chip->battchg_suspend_votable);
	bool disabled_iusb  = !!get_effective_result(chip->usb_suspend_votable);
	bool disabled_idc   = !!get_effective_result(chip->dc_suspend_votable);
	int  capping_ibat   = disabled_ibat ? 0 : get_effective_result(chip->fcc_votable);
	int  capping_iusb   = get_effective_result(chip->usb_icl_votable);
	int  capping_vfloat = get_effective_result(chip->fv_votable);
	int  rc;
	u8 reg;

	smbchg_stay_awake(chip, PM_POLLING_LOGGER);

	if (false /* for debug purpose */) {
		static struct power_supply* psy_battery;
		static struct power_supply* psy_bms;
		static struct power_supply* psy_parallel;
		static struct power_supply* psy_usb;
		static struct power_supply* psy_veneer;

		if (!psy_battery)       psy_battery = power_supply_get_by_name("battery");
		if (!psy_bms)           psy_bms = power_supply_get_by_name("bms");
		if (!psy_parallel)      psy_parallel = power_supply_get_by_name("usb-parallel");
		if (!psy_usb)           psy_usb = power_supply_get_by_name("usb");
		if (!psy_veneer)        psy_veneer = power_supply_get_by_name("veneer");

		printk("PMINFO: [REF] battery:%d, bms:%d, "
				"parallel:%d, usb:%d, veneer:%d\n",
					psy_battery ? atomic_read(&psy_battery->use_cnt) : 0,
					psy_bms ? atomic_read(&psy_bms->use_cnt) : 0,
					psy_parallel ? atomic_read(&psy_parallel->use_cnt) : 0,
					psy_usb ? atomic_read(&psy_usb->use_cnt) : 0,
					psy_veneer ? atomic_read(&psy_veneer->use_cnt) : 0);
	}

	#define LOGGING_ON_BMS 1
	val.intval = LOGGING_ON_BMS;
	if (chip->bms_psy)
		power_supply_set_property(chip->bms_psy, POWER_SUPPLY_PROP_UPDATE_NOW, &val);

	printk("PMINFO: [VOT] IUSB:%d(%s), IBAT:%d(%s), VFLOAT:%d(%s)\n"
			"PMINFO: [VOT] CHDISIUSB:%d(%s), CHDISIDC:%d(%s), CHDISIBAT:%d(%s)\n",
			capping_iusb,   get_effective_client(chip->usb_icl_votable),
			capping_ibat,   get_effective_client(disabled_ibat ? chip->battchg_suspend_votable : chip->fcc_votable),
			capping_vfloat, get_effective_client(chip->fv_votable),
			disabled_iusb,  get_effective_client(chip->usb_suspend_votable),
			disabled_idc,   get_effective_client(chip->dc_suspend_votable),
			disabled_ibat,  get_effective_client(chip->battchg_suspend_votable));

	// If not charging, skip the remained logging
	if (!presence_usb)
		goto out;

	// Basic charging information
	{
		int   stat_pwr = smbchg_read(chip, &reg, chip->usb_chgpth_base + PWR_PATH, 1) >= 0
			? (reg & PWR_PATH_MASK) : -1;
		char* stat_ret = !power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_STATUS, &val) ? log_psy_status(val.intval) : NULL;
		char* stat_ori = get_prop_batt_status(chip) >= 0
			? log_psy_status(get_prop_batt_status(chip)) : NULL;
		char* chg_stat
			= log_raw_status(chip);
		char  chg_name [16] = { 0, };

#if 0
#define QNOVO_PTTIME_STS                0x1507
#define QNOVO_PTRAIN_STS                0x1508
#define QNOVO_ERROR_STS2                0x150A
#define QNOVO_PE_CTRL                   0x1540
#define QNOVO_PTRAIN_EN                 0x1549

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

		printk("PMINFO: [CHG] NAME:%s, STAT:%s(ret)/%s(ori)/%s(reg), PATH:0x%02x\n"
				"PMINFO: [QNI] en=%d(sts=0x%X, ctrl=0x%X), pt_en=%d(sts=0x%X), pt_t=%d\n",
				chg_name, stat_ret, stat_ori, chg_stat, stat_pwr,
				qnovo_en, qnovo_sts, qnovo_pe_ctrl, qnovo_pt_en, qnovo_pt_sts, qnovo_pt_time);
	}

	if (presence_usb) { // On Wired charging
		char* usb_real = log_psy_type(chip->usb_supply_type);
		char fake_batt[2] = {0, };
		char current_max[2] = {0, };
		int usb_vnow = get_usb_adc(chip)/1000; // unit : mv
		int iusb_now = smbchg_get_iusb(chip) / 1000;
		int iusb_set = smbchg_get_aicl_level_ma(chip);
		int ibat_now = get_prop_batt_current_now(chip) / 1000;
		int ibat_pmi = chip->fastchg_current_ma;
		int ibat_smb = 0;
		u8 chgr_status = get_charging_status(chip);

		if (chip->parallel.avail)
		{
			struct power_supply *parallel_psy = get_parallel_psy(chip);
			if(parallel_psy) {
				rc = power_supply_get_property(parallel_psy,
						POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &val);
				if(rc)
					pr_info("error occured at getting parallel's FCC\n");
				else
					ibat_smb = val.intval / 1000; 
			}
		}

		unified_nodes_show("fake_battery", fake_batt);
		unified_nodes_show("fake_sdpmax", current_max);

		printk("PMINFO: [USB] REAL:%s, VNOW:%d\n"
				"PMINFO: [USB] IUSB:%d(now)<=%d(set)<=%d(cap), IBAT:%d(now):=%d(pmi)+%d(smb)<=%d(cap)\n"
				"PMINFO: [USB] CHGRSTS:0x%02x\n"
				"PMINFO: [USB] FAKE_BATT:%s, USB_CURRENT_MAX:%s\n",
				usb_real, usb_vnow,
				iusb_now, iusb_set, capping_iusb, ibat_now, ibat_pmi, ibat_smb, capping_ibat,
				chgr_status,
				fake_batt, current_max);
	}

out:	printk("PMINFO: ---------------------------------------------"
				"-----------------------------------------%s-END.\n",
				unified_bootmode_marker());
	smbchg_relax(chip, PM_POLLING_LOGGER);
	return;
}

#define PMI_REG_BASE_CHGR	0x1000
#define PMI_REG_BASE_OTG	0x1100
#define PMI_REG_BASE_BATIF	0x1200
#define PMI_REG_BASE_USB	0x1300
#define PMI_REG_BASE_DC 	0x1400

#define PMI_REG_BASE_MISC	0x1600

static const struct base {
	const char* name;
	u16 base;
} bases [] = {
	/* 0: */ { .name = "POLL",	.base = -1, },	// Dummy for polling logs
	/* 1: */ { .name = "CHGR",	.base = PMI_REG_BASE_CHGR, },
	/* 2: */ { .name = "OTG", 	.base = PMI_REG_BASE_OTG, },
	/* 3: */ { .name = "BATIF", .base = PMI_REG_BASE_BATIF, },
	/* 4: */ { .name = "USB",	.base = PMI_REG_BASE_USB, },
	/* 5: */ { .name = "DC",	.base = PMI_REG_BASE_DC, },
	/* 6: */ { .name = "EMPTY", .base = -1, },	// Dummy for empty register
	/* 7: */ { .name = "MISC",	.base = PMI_REG_BASE_MISC, },
};

static void debug_battery(struct smbchg_chip *chip, int func) {
	if (func < 0) {
		int i;
		for (i = 1; i < ARRAY_SIZE(bases); ++i)
			debug_dump(chip, bases[i].name, bases[i].base);
	}
	else if (func == 0)
		debug_polling(chip);
	else if (func < ARRAY_SIZE(bases))
		debug_dump(chip, bases[func].name, bases[func].base);
	else
		; /* Do nothing */
}

#define USBIN_500MA	500000
static int restricted_charging_iusb(struct smbchg_chip *chip, int mvalue) {
	struct power_supply* veneer;
	union power_supply_propval val;
	int rc = 0;

	if (mvalue != VOTE_TOTALLY_BLOCKED && mvalue != VOTE_TOTALLY_RELEASED) {
		// Releasing undesirable capping on IUSB :

		// In the case of CWC, SW_ICL_MAX_VOTER limits IUSB
		// which is set in the 'previous' TypeC removal
/*
                if (is_client_vote_enabled(chip->usb_icl_votable, PSY_ICL_VOTER)) {
			pr_info("Releasing PSY_ICL_VOTER\n");
			rc |= vote(chip->usb_icl_votable, PSY_ICL_VOTER,
				true, mvalue*1000);
		}
*/		
                // In the case of non SDP enumerating by DWC,
		// DWC will not set any value via USB_PSY_VOTER.
		if (is_client_vote_enabled(chip->usb_icl_votable, PSY_ICL_VOTER)) {
			veneer = power_supply_get_by_name("veneer");
			if (veneer) {
				power_supply_get_property(veneer, POWER_SUPPLY_PROP_SDP_CURRENT_MAX, &val);
				if(val.intval != VOTE_TOTALLY_RELEASED && val.intval == mvalue * 1000) {
					pr_info("Releasing PSY_ICL_VOTER\n");
					rc |= vote(chip->usb_icl_votable, PSY_ICL_VOTER, true, mvalue);
				}
				power_supply_put(veneer);
			}
		}

		// In case of Float charger, set SDP_CURRENT_MAX for current setting
		veneer = power_supply_get_by_name("veneer");
		if (veneer) {
			power_supply_get_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE, &val);
			if (chip->usb_supply_type == POWER_SUPPLY_TYPE_USB && val.intval == POWER_SUPPLY_TYPE_USB_FLOAT) {
				val.intval = USBIN_500MA;
				pr_info("Vote POWER_SUPPLY_PROP_SDP_CURRENT_MAX to 500mA\n");
				rc |= power_supply_set_property(chip->usb_psy,
							POWER_SUPPLY_PROP_SDP_CURRENT_MAX, &val);
				rerun_election(chip->usb_icl_votable);
			}
			power_supply_put(veneer);
		}
	} else {
		pr_info("USBIN blocked\n");
	}
	rc |= vote(chip->usb_icl_votable, VENEER_VOTER_IUSB,
		mvalue != VOTE_TOTALLY_RELEASED, mvalue);

	// to detect slow charger, init to veneer->usbin_aicl value
	if (mvalue != VOTE_TOTALLY_RELEASED) {
		veneer = power_supply_get_by_name("veneer");
		val.intval = mvalue * 1000;

		if (veneer) {
			power_supply_set_property(veneer, POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED, &val);
			power_supply_put(veneer);
		}
	}

	return	rc ? -EINVAL : 0;
}

static int restricted_charging_ibat(struct smbchg_chip *chip, int mvalue) {
	int rc = 0;

	if (mvalue != VOTE_TOTALLY_BLOCKED) {
		pr_info("Restricted IBAT : %duA\n", mvalue*1000);
		rc |= vote(chip->fcc_votable, VENEER_VOTER_IBAT,
			mvalue != VOTE_TOTALLY_RELEASED, mvalue);

		rc |= vote(chip->battchg_suspend_votable,
			VENEER_VOTER_IBAT, false, 0);
	}
	else {
		pr_info("Stop charging\n");
		rc = vote(chip->battchg_suspend_votable,
			VENEER_VOTER_IBAT, true, 0);
	}

	return rc ? -EINVAL : 0;
}

static int restricted_charging_vfloat(struct smbchg_chip *chip, int mvalue) {
	int uv_float, uv_now, rc = 0;
	union power_supply_propval val;

	if (mvalue != VOTE_TOTALLY_BLOCKED) {
		if (mvalue == VOTE_TOTALLY_RELEASED) {
		       /* Clearing related voters :
			* 1. VENEER_VOTER_VFLOAT for BTP and
			*/
			vote(chip->fv_votable, VENEER_VOTER_VFLOAT,
				false, 0);

		       /* If EoC is met with the restricted vfloat,
			* charging is not resumed automatically with restoring vfloat only.
			* Because SoC is not be lowered, so FG(BMS) does not trigger "Recharging".
			* For work-around, do recharging manually here.
			*/
			rc |= vote(chip->battchg_suspend_votable, VENEER_VOTER_VFLOAT,  //need to check
				true, 0);
			rc |= vote(chip->battchg_suspend_votable, VENEER_VOTER_VFLOAT,
				false, 0);
		}
		else {
		       /* At the normal restriction, vfloat is adjusted to "max(vfloat, vnow)",
			* to avoid bat-ov.
			*/
			uv_float = mvalue * 1000;
			rc |= power_supply_get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_VOLTAGE_OCV, &val);
			uv_now = val.intval;
			pr_debug("uv_now : %d\n", uv_now);
			if (uv_now > uv_float
					&& !is_client_vote_enabled(chip->fv_votable, VENEER_VOTER_VFLOAT)) {
				rc |= vote(chip->battchg_suspend_votable, VENEER_VOTER_VFLOAT,
						true, 0);
			} else {
				rc |= vote(chip->battchg_suspend_votable, VENEER_VOTER_VFLOAT,
						false, 0);
				rc |= vote(chip->fv_votable, VENEER_VOTER_VFLOAT,
						true, uv_float / 1000);
			}
		}
	}
	else {
		pr_info("Non permitted mvalue\n");
		rc = -EINVAL;
	}

	if (rc) {
		pr_err("Failed to restrict vfloat\n");
		vote(chip->fv_votable, VENEER_VOTER_VFLOAT, false, 0);
		vote(chip->battchg_suspend_votable, VENEER_VOTER_VFLOAT, false, 0);
		rc = -EINVAL;
	}

	return rc;
}

#define HVDCP_EN_BIT	BIT(3)
static int restricted_charging_hvdcp(struct smbchg_chip *chip, int mvalue) {
	int rc = 0;

	if (VOTE_TOTALLY_BLOCKED < mvalue && mvalue < VOTE_TOTALLY_RELEASED) {
		pr_info("Non permitted mvalue for HVDCP voting %d\n", mvalue);
		return -EINVAL;
	}

	rc = smbchg_sec_masked_write(chip,
				chip->usb_chgpth_base + CHGPTH_CFG,
				HVDCP_EN_BIT, mvalue == VOTE_TOTALLY_BLOCKED ? HVDCP_EN_BIT : 0);
	if (rc < 0)
			dev_err(chip->dev, "Couldn't %s HVDCP rc=%d\n",
				mvalue == VOTE_TOTALLY_BLOCKED ? "enable" : "disable", rc);

	return 0;
}

static int restricted_charging(struct smbchg_chip *chip, const union power_supply_propval *val) {
	int rc;
	enum voter_type type = vote_type(val);
	int limit = vote_limit(val); // in mA

	switch (type) {
	case VOTER_TYPE_IUSB:
		rc = restricted_charging_iusb(chip, limit);
		break;
	case VOTER_TYPE_IBAT:
		rc = restricted_charging_ibat(chip, limit);
		break;
	case VOTER_TYPE_VFLOAT:
		rc = restricted_charging_vfloat(chip, limit);
		break;
        case VOTER_TYPE_HVDCP:
		rc = restricted_charging_hvdcp(chip, limit);
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}

static void update_charging_step(struct smbchg_chip *chip) {
	char buf [2] = { 0, };
	enum charging_step chgstep;
	int rc;
	u8 reg = 0, chg_type = 0;
	bool charger_present;

	if (!chip) {
		chgstep = CHARGING_STEP_DISCHARGING;
		goto out;
	}

	charger_present = is_usb_present(chip) | is_dc_present(chip) |
			chip->hvdcp_3_det_ignore_uv;
	if (!charger_present) {
		chgstep = CHARGING_STEP_DISCHARGING;
	}

	rc = smbchg_read(chip, &reg, chip->chgr_base + RT_STS, 1);
	if (rc < 0)
		dev_err(chip->dev, "Unable to read RT_STS rc = %d\n", rc);

	if (reg & CHG_INHIBIT_BIT) {
		chgstep = CHARGING_STEP_DISCHARGING;
		goto out;
	}

	if (reg & BAT_TCC_REACHED_BIT) {
		chgstep = CHARGING_STEP_TERMINATED;
		goto out;
	}

	rc = smbchg_read(chip, &reg, chip->chgr_base + CHGR_STS, 1);
	if (rc < 0)
		dev_err(chip->dev, "Unable to read CHGR_STS rc = %d\n", rc);

	chg_type = (reg & CHG_TYPE_MASK) >> CHG_TYPE_SHIFT;

	switch (chg_type) {
		case NO_CHARGE:                 chgstep = CHARGING_STEP_NOTCHARGING;
				break;
		case PRE_CHARGE:                chgstep = CHARGING_STEP_TRICKLE;
				break;
		case FAST_CHARGE:               chgstep = CHARGING_STEP_CC;
				break;
		case TAPER_CHARGE:              chgstep = CHARGING_STEP_CV;
				break;
		default:                        chgstep = CHARGING_STEP_DISCHARGING;
				break;
	}

out:
	snprintf(buf, sizeof(buf), "%d", chgstep);
	unified_nodes_store("charging_step", buf, strlen(buf));
}

int smbchg_get_prop_batt_charge_done(struct smbchg_chip *chip,
			union power_supply_propval *val)
{
	int rc;
	u8 stat = 0;

	rc = smbchg_read(chip, &stat, chip->chgr_base + RT_STS, 1);
	if (rc < 0) {
		dev_err(chip->dev, "Unable to read RT_STS rc = %d\n", rc);
		return rc;
	}

	val->intval = (stat & BAT_TCC_REACHED_BIT);

	return 0;
}

static int smbchg_get_prop_batt_charge_counter(struct smbchg_chip *chip)
{
	int rc;
	union power_supply_propval val;

	if (!chip->bms_psy)
		return -EINVAL;

	rc = power_supply_get_property(chip->bms_psy,
						POWER_SUPPLY_PROP_CHARGE_COUNTER, &val);
	if (rc < 0) {
		pr_smb(PR_STATUS, "Couldn't get charge count rc = %d\n", rc);
		return rc;
	}

	return val.intval;
}
#if 0
static void support_parallel_checking(struct smbchg_chip *chip, int disable) {
	char buff [16] = { 0, };
	int  test;
	char* client;

	if (!disable // Enabling parallel charging
		&& unified_nodes_show("support_fastpl", buff)
		&& sscanf(buff, "%d", &test) && test == 1) {

		vote(chip->pl_enable_votable_indirect, USBIN_I_VOTER, true, 0);
		while (get_effective_result(chip->pl_disable_votable)) {
			client = (char*) get_effective_client(chip->pl_disable_votable);
			vote(chip->pl_disable_votable, client, false, 0);
			pr_info("FASTPL: Clearing PL_DISABLE voter %s for test purpose\n", client);
		}
		pr_info("FASTPL: After clearing PL_DISABLE, result:%d, client:%s\n",
			get_effective_result(chip->pl_disable_votable),
			get_effective_client(chip->pl_disable_votable));
	}
}
#endif

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
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_DONE,
	POWER_SUPPLY_PROP_RESTRICTED_CHARGING,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
#if 0
	POWER_SUPPLY_PROP_PARALLEL_MODE,
#endif
};

static int extension_battery_get_property_pre(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val) {
	int rc = PROPERTY_CONSUMED_WITH_SUCCESS;

	struct smbchg_chip *chip = power_supply_get_drvdata(psy);
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
		if (!chip->bms_psy || power_supply_get_property(chip->bms_psy, POWER_SUPPLY_PROP_CAPACITY_RAW, val))
			rc = -PROPERTY_CONSUMED_WITH_FAIL;
		break;
#endif
	case POWER_SUPPLY_PROP_BATTERY_TYPE :
		if (!chip->bms_psy || power_supply_get_property(chip->bms_psy, POWER_SUPPLY_PROP_BATTERY_TYPE, val))
			rc = -PROPERTY_CONSUMED_WITH_FAIL;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN :
		if (!chip->bms_psy || power_supply_get_property(chip->bms_psy, POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, val))
			rc = -PROPERTY_CONSUMED_WITH_FAIL;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER :
		val->intval = smbchg_get_prop_batt_charge_counter(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_DONE :
		rc = smbchg_get_prop_batt_charge_done(chip, val);
		break;
	case POWER_SUPPLY_PROP_STATUS_RAW :
		if  (!chip->batt_psy || power_supply_get_property(chip->batt_psy, POWER_SUPPLY_PROP_STATUS, val))
			rc = -PROPERTY_CONSUMED_WITH_FAIL;
		break;
#if 0
	case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE :
		rc = safety_timer_enabled(chip, &val->intval);
		break;
#endif
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT :
		val->intval = get_effective_result(chip->fcc_votable);
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED :
		val->intval = !get_effective_result(chip->battchg_suspend_votable);
		break;
#ifdef CONFIG_LGE_PM_CCD
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW :
		unified_nodes_show("time_to_full_now", buf);
		if(kstrtoint(buf, 10, &val->intval))
			pr_err("error kstrtoint %s to %d\n", buf, val->intval);
		break;
#endif

	case POWER_SUPPLY_PROP_DEBUG_BATTERY :
	case POWER_SUPPLY_PROP_RESTRICTED_CHARGING:
                /* Do nothing and just consume getting */
		val->intval = -1;
		break;
#if 0
	case POWER_SUPPLY_PROP_PARALLEL_MODE :
		val->intval = chip->parallel_pct;
		break;
#endif
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

	struct smbchg_chip *chip = power_supply_get_drvdata(psy);
#ifdef CONFIG_LGE_PM_CCD
	char buf[10] = {0, };
#endif

	switch (psp) {
	case POWER_SUPPLY_PROP_RESTRICTED_CHARGING :
		rc = restricted_charging(chip, val);
		break;

        case POWER_SUPPLY_PROP_CHARGE_TYPE :
                pr_err("veneer power supply prop charge type\n");
		update_charging_step(chip);
		break;

	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED :
		vote(chip->battchg_suspend_votable, BATTCHG_USER_EN_VOTER, (bool)!val->intval, 0);
		break;

        case POWER_SUPPLY_PROP_DP_DM :
		pr_info("smblib_dp_dm: dp dm update %d\n", val->intval);
		rc = -PROPERTY_BYPASS_REASON_ONEMORE;
		break;
#ifdef CONFIG_LGE_PM_CCD
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW :
		snprintf(buf, sizeof(buf), "%d", val->intval);
		unified_nodes_store("time_to_full_now", buf, strlen(buf));
		if(chip->batt_psy)
			power_supply_changed(chip->batt_psy);
		break;
#endif

	case POWER_SUPPLY_PROP_DEBUG_BATTERY :
		debug_battery(chip, val->intval);
		break;

        default:
		rc = -PROPERTY_BYPASS_REASON_NOENTRY;
	}

	return rc;
}

static int extension_battery_set_property_post(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val, int rc) {

	switch (psp) {
#if 0
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
		support_parallel_checking(chip, val->intval );
		break;
#endif
	default:
		break;
	}

	return rc;
}

///////////////////////////////////////////////////////////////////////////////
enum power_supply_property* extension_battery_properties(void) {
	static enum power_supply_property extended_properties[ARRAY_SIZE(smbchg_battery_properties) + ARRAY_SIZE(extension_battery_appended)];
	int size_original = ARRAY_SIZE(smbchg_battery_properties);
	int size_appended = ARRAY_SIZE(extension_battery_appended);

	memcpy(extended_properties, smbchg_battery_properties,
		size_original * sizeof(enum power_supply_property));
	memcpy(&extended_properties[size_original], extension_battery_appended,
		size_appended * sizeof(enum power_supply_property));

	veneer_extension_pspoverlap(smbchg_battery_properties, size_original,
		extension_battery_appended, size_appended);

	return extended_properties;
}

size_t extension_battery_num_properties(void) {
	return ARRAY_SIZE(smbchg_battery_properties) + ARRAY_SIZE(extension_battery_appended);
}

int extension_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val) {

	int rc = extension_battery_get_property_pre(psy, psp, val);
	if (rc == -PROPERTY_BYPASS_REASON_NOENTRY || rc == -PROPERTY_BYPASS_REASON_ONEMORE)
		rc = smbchg_battery_get_property(psy, psp, val);
	rc = extension_battery_get_property_post(psy, psp, val, rc);

	return rc;
}

int extension_battery_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val) {

	int rc = extension_battery_set_property_pre(psy, psp, val);
	if (rc == -PROPERTY_BYPASS_REASON_NOENTRY || rc == -PROPERTY_BYPASS_REASON_ONEMORE)
		rc = smbchg_battery_set_property(psy, psp, val);
	rc = extension_battery_set_property_post(psy, psp, val, rc);

	return rc;
}

int extension_battery_property_is_writeable(struct power_supply *psy,
	enum power_supply_property psp) {
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_DEBUG_BATTERY :
	case POWER_SUPPLY_PROP_RESTRICTED_CHARGING :
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
#ifdef CONFIG_LGE_PM_CCD
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW :
#endif
#if 0
        case POWER_SUPPLY_PROP_PARALLEL_MODE :
#endif
		rc = 1;
		break;
	default:
		rc = smbchg_battery_is_writeable(psy, psp);
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
	rc |= qpnp_vadc_read(vadc_dev, LR_MUX10_USB_ID_LV, &result);
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

static enum charger_usbid psy_usbid_get(struct smbchg_chip *chip) {
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

static bool fake_hvdcp_property(struct smbchg_chip *chip) {
	char buffer [16] = { 0, };
	int fakehvdcp;

        if(chip->hvdcp_not_supported)
                return false;

	return unified_nodes_show("fake_hvdcp", buffer)
		&& sscanf(buffer, "%d", &fakehvdcp)
		&& !!fakehvdcp;
}

static bool fake_hvdcp_effected(struct smbchg_chip *chip) {
	union power_supply_propval val = {0, };

	if (fake_hvdcp_property(chip)
		&& power_supply_get_property(chip->usb_psy, 
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &val) >= 0
		&& val.intval/1000 >= 7000) {
		return true;
	}
	else
		return false;
}

static bool fake_hvdcp_enable(struct smbchg_chip *chip, bool enable) {  //need to check
	u8 vallow;
	int rc;

	if (fake_hvdcp_property(chip)) {
		vallow = enable ? USBIN_ADAPTER_5V_9V_CONT
			: USBIN_ADAPTER_5V_UNREGULATED_9V;
		rc = smbchg_sec_masked_write(chip, chip->usb_chgpth_base + USBIN_CHGR_CFG, 
                        ADAPTER_ALLOWANCE_MASK, vallow);
		if (rc >= 0) {
			if (enable)
//				vote(chip->usb_icl_votable, SW_ICL_MAX_VOTER, true, 3000000);
				vote(chip->usb_icl_votable, PSY_ICL_VOTER, true, 3000);
			return true;
		}
		else
			pr_err("Couldn't write 0x%02x to USBIN_ADAPTER_ALLOW_CFG rc=%d\n",
				vallow, rc);
	}
	else
		pr_info("fake_hvdcp is not set\n");

	return false;
}

#define HVDCP_VOLTAGE_MV_MIN	5000
#define HVDCP_VOLTAGE_MV_MAX	9000
#define HVDCP_CURRENT_MA_MAX	1800
static int charger_power_hvdcp(/*@Nonnull*/ struct power_supply* usb, int type) {
	int voltage_mv, current_ma, power = 0;

	if (type == POWER_SUPPLY_TYPE_USB_HVDCP) {
		voltage_mv = //wa_detect_standard_hvdcp_check()  //need to check
			//? HVDCP_VOLTAGE_MV_MAX : HVDCP_VOLTAGE_MV_MIN;
                    HVDCP_VOLTAGE_MV_MAX;
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
//	union power_supply_propval buf = { .intval = 0, };
	struct smbchg_chip *chip = power_supply_get_drvdata(usb);

	if (type == POWER_SUPPLY_TYPE_USB_DCP || type == POWER_SUPPLY_TYPE_USB_CDP) {
		current_ma = smbchg_get_aicl_level_ma(chip);

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
		smbchg_usb_get_property(usb, POWER_SUPPLY_PROP_SDP_CURRENT_MAX, &buf);
		current_ma = buf.intval / 1000;	
		
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
		voltage_mv = !smbchg_usb_get_property(usb, POWER_SUPPLY_PROP_PD_VOLTAGE_MAX, &buf)
			? buf.intval / 1000 : 0;
		current_ma = !smbchg_usb_get_property(usb, POWER_SUPPLY_PROP_PD_CURRENT_MAX, &buf)
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

static bool usbin_ov_check(/*@Nonnull*/ struct smbchg_chip *chip) {
	int rc = 0;
	u8 reg = 0;

	rc = smbchg_read(chip, &reg, chip->usb_chgpth_base + RT_STS, 1);
	if (rc < 0) {
		dev_err(chip->dev, "Unable to read CHGR_STS rc = %d\n", rc);
		return false;
	}

	return (bool)(reg & USBIN_OV_BIT);
}

static bool usb_pcport_check(/*@Nonnull*/ struct smbchg_chip *chip) {
	enum power_supply_type* pst = &chip->usb_supply_type;
	union power_supply_propval val = { 0, };
	bool usb_connected = false;
	bool usb_pdtype = false;
	bool usb_pcport = false;
        enum power_supply_type usb_supply_type;
        char *usb_type_name = "null";

	if (smbchg_usb_get_property(chip->usb_psy, POWER_SUPPLY_PROP_ONLINE, &val) < 0) {
		pr_err("PMI: usb_pcport_check: Couldn't read smblib_get_prop_usb_online\n");
		return false;
	}
	else
		usb_connected = !!val.intval;

	read_usb_type(chip, &usb_type_name, &usb_supply_type); //APSD Result

	usb_pdtype = (*pst == POWER_SUPPLY_TYPE_USB_PD)
		&& (usb_supply_type == POWER_SUPPLY_TYPE_USB || usb_supply_type == POWER_SUPPLY_TYPE_USB_CDP);
	usb_pcport = (*pst == POWER_SUPPLY_TYPE_USB
		|| *pst == POWER_SUPPLY_TYPE_USB_CDP);

	return usb_connected && (usb_pdtype || usb_pcport);
}

static int usb_pcport_current(/*@Nonnull*/ struct smbchg_chip *chip, int req) {
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

#define FABCURR 1500000
#define USBIN_25MA      25000
#define USBIN_500MA     500000
static bool extension_usb_set_current_max(/*@Nonnull*/struct smbchg_chip *chip, int current_max) {
	enum charger_usbid usbid = psy_usbid_get(chip);
	bool fabid = usbid == CHARGER_USBID_56KOHM || usbid == CHARGER_USBID_130KOHM || usbid == CHARGER_USBID_910KOHM;
	bool pcport = chip->usb_supply_type == POWER_SUPPLY_TYPE_USB;
	bool fabproc = fabid && pcport;

	int icl = current_max;
	bool chgable = USBIN_25MA < icl && icl < INT_MAX;   //need to check

	if (fabproc && chgable) {
		smbchg_set_sdp_current(chip, FABCURR);
		if (icl != FABCURR)
			pr_info("Success to set IUSB (%d -> %d)mA for fabcable\n", icl/1000, FABCURR/1000);

		return true;
	}

	if (icl <= USBIN_500MA && icl > USBIN_25MA) {
		return false;
	}
	return false;
}

static bool extension_usb_get_current_max(/*@Nonnull*/struct smbchg_chip* chip, union power_supply_propval* val) {
    enum charger_usbid usbid = psy_usbid_get(chip);
    bool fabid = usbid == CHARGER_USBID_56KOHM || usbid == CHARGER_USBID_130KOHM || usbid == CHARGER_USBID_910KOHM;
    bool pcport = chip->usb_supply_type == POWER_SUPPLY_TYPE_USB;
    bool fabproc = fabid && pcport;

    if (fabproc) {
        if (chip->usb_icl_votable) {
            val->intval = get_client_vote(chip->usb_icl_votable,
                        PSY_ICL_VOTER) * 1000;
            return true;
        } else {
            val->intval = 0;
            return false;
        }
    }

	if (!strcmp(get_effective_client(chip->usb_icl_votable), "VENEER_VOTER_IUSB")) {
		val->intval = get_client_vote(chip->usb_icl_votable,
					VENEER_VOTER_IUSB) * 1000;
		return true;
	}

    return false;
}

static int extension_usb_set_sdp_current_max(/*@Nonnull*/ struct power_supply* psy,
		const union power_supply_propval* val) {
	static union power_supply_propval isdp;
	struct smbchg_chip *chip = power_supply_get_drvdata(psy);

	isdp.intval = usb_pcport_current(chip, val->intval);
	if (isdp.intval != val->intval)
		pr_info("PMI: SDP_CURRENT_MAX %d is overridden to %d\n", val->intval, isdp.intval);
	val = &isdp;

	return smbchg_usb_set_property(psy, POWER_SUPPLY_PROP_SDP_CURRENT_MAX, val);
}

static bool extension_usb_get_online(/*@Nonnull*/ struct power_supply *psy) {
	struct smbchg_chip *chip = power_supply_get_drvdata(psy);
	union power_supply_propval val;
	bool ret = false;

// Getting chg type from veneer
	struct power_supply* veneer
		= power_supply_get_by_name("veneer");
	int chgtype = (veneer && !power_supply_get_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE,
		&val)) ? val.intval : POWER_SUPPLY_TYPE_UNKNOWN;
// Pre-loading conditions
	bool online = !smbchg_usb_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val)
		? !!val.intval : false;
	bool present = !extension_usb_get_property(psy, POWER_SUPPLY_PROP_PRESENT, &val)
		? !!val.intval : false;
	bool ac = chgtype != POWER_SUPPLY_TYPE_UNKNOWN
		&& chgtype != POWER_SUPPLY_TYPE_USB && chgtype != POWER_SUPPLY_TYPE_USB_CDP;
	bool fo = veneer_voter_suspended(VOTER_TYPE_IUSB)
		== CHARGING_SUSPENDED_WITH_FAKE_OFFLINE;
	bool pc = usb_pcport_check(chip);
	bool usbov = usbin_ov_check(chip);

	if (veneer)
		power_supply_put(veneer);

// Branched returning
	if (!online && present && ac && !fo) {
		pr_err("Set ONLINE by force\n");
		ret = true;
	} else if (usbov && online) {
		pr_err("Unset ONLINE by force\n");
		ret = false;
	} else if (pc) {
		pr_err("Set OFFLINE due to non-AC\n");
		ret = false;
	}
	else
		if(!fo)
			ret = online;
		else
			ret = false;

	if(unified_bootmode_chargerlogo() && chip->typec_plugged){
		pr_err("Chargerlogo & Type C plugged. \n");
		if (chip->pd_active || chip->pd_hard_reset_done){
			pr_err("PD active or hard reset done. Set ONLINE by force \n");
			ret = true;
		}
	}

	pr_err("chgtype=%s, online=%d, present=%d, ac=%d, fo=%d, pc=%d ret=%d\n",
		log_psy_type(chgtype), online, present, ac, fo, pc, ret);

	return ret;
}

static bool extension_usb_port_get_online(/*@Nonnull*/ struct power_supply *psy) {
	struct smbchg_chip *chip = power_supply_get_drvdata(psy);

	// Prepare condition 'usb type' from veneer
	union power_supply_propval val;
	struct power_supply* veneer
		= power_supply_get_by_name("veneer");
	int  chgtype = (veneer && !power_supply_get_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE,
		&val)) ? val.intval : POWER_SUPPLY_TYPE_UNKNOWN;
	bool online = !smbchg_usb_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val)
		? !!val.intval : false;
	bool usb = chgtype == POWER_SUPPLY_TYPE_USB
		|| chgtype == POWER_SUPPLY_TYPE_USB_CDP;
	bool fo = veneer_voter_suspended(VOTER_TYPE_IUSB)
		== CHARGING_SUSPENDED_WITH_FAKE_OFFLINE;
	bool pc = usb_pcport_check(chip);
	bool ret;

	if (veneer)
		power_supply_put(veneer);
// determine USB online
	ret = ((usb || pc) && !fo) ? true : false;

	pr_err("port check usb=%d, pc=%d, online=%d, fo=%d ret=%d\n",
		usb, pc, online, fo, ret);
	return ret;
}

static void recheck_charger_type(/*@Nonnull*/ struct smbchg_chip *chip,
		const union power_supply_propval* val) {
	struct power_supply* veneer;
	union power_supply_propval prop = {0, };
	enum power_supply_type usb_supply_type;
	char *usb_type_name = "null";
	char buf[2] = {0, };
	int rc;

	veneer = power_supply_get_by_name("veneer");
	if (val->intval == POWER_SUPPLY_TYPE_USB_FLOAT) {
		chip->usb_psy_d.type = POWER_SUPPLY_TYPE_USB_DCP;
		usb_psy_desc_extension.type = chip->usb_psy_d.type;
		snprintf(buf, sizeof(buf), "%d", 1);
		unified_nodes_store("aicl_done", buf, strlen(buf));
		if (veneer) {
			power_supply_set_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE, val);
			power_supply_changed(veneer);
			power_supply_put(veneer);
		}
	} else {
		read_usb_type(chip, &usb_type_name, &usb_supply_type);
		if ((val->intval != POWER_SUPPLY_TYPE_USB) && (usb_supply_type == POWER_SUPPLY_TYPE_USB)
				&& is_usb_present(chip)){
			/* incorrect type detected */
			pr_smb(PR_MISC,
				"Incorrect charger type detetced - rerun APSD\n");
			chip->hvdcp_3_det_ignore_uv = true;

			pr_smb(PR_MISC, "setting usb psy dp=f dm=f\n");
			smbchg_request_dpdm(chip, true);

			rc = rerun_apsd(chip);
			if (rc)
				pr_err("APSD re-run failed\n");

			chip->hvdcp_3_det_ignore_uv = false;

			if (!is_src_detect_high(chip)) {
				pr_smb(PR_MISC, "Charger removed - force removal\n");
				update_usb_status(chip, is_usb_present(chip), true);
				return;
			}

			read_usb_type(chip, &usb_type_name, &usb_supply_type);
			if (usb_supply_type == POWER_SUPPLY_TYPE_USB_DCP) {
				schedule_delayed_work(&chip->hvdcp_det_work,
					msecs_to_jiffies(HVDCP_NOTIFY_MS));
#if 0  // neet to check when parallel charger support
				if (chip->parallel.use_parallel_aicl) {
					reinit_completion(&chip->hvdcp_det_done);
					pr_smb(PR_MISC, "init hvdcp_det_done\n");
				}
#endif
				smbchg_change_usb_supply_type(chip, usb_supply_type);
			}
			read_usb_type(chip, &usb_type_name, &usb_supply_type);
			if (usb_supply_type == POWER_SUPPLY_TYPE_USB_DCP)
				schedule_delayed_work(&chip->hvdcp_det_work,
					msecs_to_jiffies(HVDCP_NOTIFY_MS));

			prop.intval = usb_supply_type;
			if (veneer){
				power_supply_set_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE, &prop);
				power_supply_changed(veneer);
				power_supply_put(veneer);
			}
		}
	}

	if (chip->batt_psy)
		power_supply_changed(chip->batt_psy);
}


void extension_usb_set_pd_active(/*@Nonnull*/ struct smbchg_chip *chip, int pd_active) {
	struct power_supply* veneer = power_supply_get_by_name("veneer");
	union power_supply_propval pd = { .intval = POWER_SUPPLY_TYPE_USB_PD, };

	if (pd_active) {
		if (veneer) {
			power_supply_set_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE, &pd);
			power_supply_changed(veneer);
			power_supply_put(veneer);
		}
	}
	pr_info("smblib_set_prop_pd_active: update pd active %d \n", pd_active);
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
	static enum power_supply_property extended_properties[ARRAY_SIZE(smbchg_usb_properties) + ARRAY_SIZE(extension_usb_appended)];
	int size_original = ARRAY_SIZE(smbchg_usb_properties);
	int size_appended = ARRAY_SIZE(extension_usb_appended);

	memcpy(extended_properties, smbchg_usb_properties,
		size_original * sizeof(enum power_supply_property));
	memcpy(&extended_properties[size_original], extension_usb_appended,
		size_appended * sizeof(enum power_supply_property));

	veneer_extension_pspoverlap(smbchg_usb_properties, size_original,
		extension_usb_appended, size_appended);

	return extended_properties;
}

size_t extension_usb_num_properties(void) {
	return ARRAY_SIZE(smbchg_usb_properties) + ARRAY_SIZE(extension_usb_appended);
}

int extension_usb_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val) {

	struct smbchg_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_POWER_NOW :
		if (!smbchg_usb_get_property(psy, POWER_SUPPLY_PROP_REAL_TYPE, val)) {
			if (fake_hvdcp_enable(chip, true) && fake_hvdcp_effected(chip))
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
		val->intval = extension_usb_get_online(psy) | extension_usb_port_get_online(psy);
#ifdef CONFIG_LGE_USB_TYPE_C
		if (chip->pd_hard_reset) {
			pr_err("PD Hardreset+Chargerlogo Set ONLINE by force\n");
			val->intval = 1;
		}
#endif
		return 0;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW :
		val->intval = get_usb_adc(chip);
		return 0;

	case POWER_SUPPLY_PROP_PRESENT :
		if (usbin_ov_check(chip)) {
			pr_debug("Unset PRESENT by force\n");
			val->intval = false;
			return 0;
		}
#ifdef CONFIG_LGE_USB_TYPE_C
		if (chip->pd_hard_reset) {
			pr_err("PD Hardreset+Chargerlogo Set PRESENT by force\n");
			val->intval = true;
			return 0;
		}
		if(unified_bootmode_chargerlogo() && chip->typec_plugged){
			pr_err("Chargerlogo & Type C plugged. \n");
			if (chip->pd_active || chip->pd_hard_reset_done){
				pr_err("PD active or hard reset done. Set PRESENT by force \n");
				val->intval = 1;
				return 0;
			}
		}
#endif

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
		val->intval = psy_usbid_get(chip);
		return 0;

	case POWER_SUPPLY_PROP_USB_HC :
		val->intval = fake_hvdcp_effected(chip);
		return 0;

        case POWER_SUPPLY_PROP_CURRENT_MAX :
                if(extension_usb_get_current_max(chip, val))
                    return 0;
                break;

	default:
		break;
	}

	return smbchg_usb_get_property(psy, psp, val);
}

int extension_usb_set_property(struct power_supply* psy,
	enum power_supply_property psp, const union power_supply_propval* val) {
	struct smbchg_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_USB_HC :
		fake_hvdcp_enable(chip, !!val->intval);
		return 0;

	case POWER_SUPPLY_PROP_REAL_TYPE :
		/* Recheck charger type when floated charger is connected */
		if(!chip->pd_active)
			recheck_charger_type(chip, val);
		return 0;

	case POWER_SUPPLY_PROP_CURRENT_MAX :
		if(extension_usb_set_current_max(chip, val->intval))
			return 0;
		break;

        case POWER_SUPPLY_PROP_SDP_CURRENT_MAX :
		return extension_usb_set_sdp_current_max(psy, val);

	case POWER_SUPPLY_PROP_RESISTANCE :
		psy_usbid_update(chip->dev);
		return 0;

        default:
		break;
	}

	return smbchg_usb_set_property(psy, psp, val);
}

int extension_usb_property_is_writeable(struct power_supply *psy,
	enum power_supply_property psp) {
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_RESISTANCE :
	case POWER_SUPPLY_PROP_REAL_TYPE :
		rc = 1;
		break;

	default:
		rc = smbchg_usb_is_writeable(psy, psp);
		break;
	}

	return rc;
}

////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Helper functions
////////////////////////////////////////////////////////////////////////////
static struct smbchg_chip* wa_helper_smbchg(void) {
	// getting smb_charger from air
	struct power_supply*    psy
			= power_supply_get_by_name("battery");
	struct smbchg_chip*     chip
			= psy ? power_supply_get_drvdata(psy) : NULL;
	if (psy)
			power_supply_put(psy);

	return chip;
}

////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Support for weak supply
////////////////////////////////////////////////////////////////////////////
#define WEAK_SUPPLY_VOTER "WEAK_SUPPLY_VOTER"
#define WEAK_DELAY_MS           500
#define WEAK_DETECTION_COUNT    3
#define DEFAULT_WEAK_ICL_MA 1000
#define MAX_UV_IRQ_COUNT    10

static int  wa_support_weak_supply_count = 0;
static bool wa_support_weak_supply_running = false;
static int wa_uv_irq_count = 0;

static void wa_weak_usb_limit_current_main(struct smbchg_chip *chip)
{
	union power_supply_propval prop = {0, };
	int rc;

	wa_uv_irq_count++;
	pr_info("wa_uv_irq_count[%d]\n", wa_uv_irq_count);

	if (wa_uv_irq_count >= MAX_UV_IRQ_COUNT) {
		if (chip->usb_psy) {
			prop.intval = CURRENT_100_MA * 1000;
			rc = power_supply_set_property(chip->usb_psy,
					POWER_SUPPLY_PROP_CURRENT_MAX, &prop);
			if (rc < 0) {
				dev_err(chip->dev, "could not set current_max:%d\n",
						rc);
				return;
			}
			wa_uv_irq_count = 0;
		}
	} else if (chip->usb_max_current_ma <= CURRENT_150_MA){
		pr_info("usb suspend by weak charger\n");
		rc = vote(chip->usb_suspend_votable,
				WEAK_CHARGER_EN_VOTER, true, 0);
		if (rc < 0)
			pr_err("could not disable charger: %d", rc);
	}
}

static void wa_support_weak_supply_func(struct work_struct *unused) {
	struct smbchg_chip* chip = wa_helper_smbchg();

	if (!wa_support_weak_supply_running)
		return;

	wa_support_weak_supply_count++;
	pr_info("wa_support_weak_supply_count = %d\n",
			wa_support_weak_supply_count);
	if (wa_support_weak_supply_count >= WEAK_DETECTION_COUNT) {
		pr_info("Weak battery is detected, set ICL to 1A\n");
		vote(chip->usb_icl_votable, WEAK_SUPPLY_VOTER,
			true, DEFAULT_WEAK_ICL_MA);
	}

	wa_support_weak_supply_running = false;
}
static DECLARE_DELAYED_WORK(wa_support_weak_supply_dwork, wa_support_weak_supply_func);

void wa_support_weak_supply_trigger(struct smbchg_chip *chip, u8 reg) {
	bool trigger = !!((reg & USBIN_UV_BIT) && (reg & USBIN_SRC_DET_BIT));

	if(trigger) {
		enum power_supply_type usb_supply_type;
		char *usb_type_name = "null";
		int rc, usbin_vol = 0;

		rc = smbchg_read(chip, &reg,
				chip->usb_chgpth_base + ICL_STS_2_REG, 1);
		if(rc)
			return;

		read_usb_type(chip, &usb_type_name, &usb_supply_type);
		usbin_vol = get_usb_adc(chip) / 1000;

		trigger = !!(((reg & ICL_MODE_MASK) == ICL_MODE_HIGH_CURRENT) &&
				(usb_supply_type == POWER_SUPPLY_TYPE_USB_DCP) && (usbin_vol >= 3000));

		if (trigger) {
			if (!delayed_work_pending(&wa_support_weak_supply_dwork))
				schedule_delayed_work(&wa_support_weak_supply_dwork,
					round_jiffies_relative(msecs_to_jiffies(WEAK_DELAY_MS)));
		} else if(!!wa_support_weak_supply_count) {
			pr_info("Clear wa_support_weak_supply_count\n");
			wa_support_weak_supply_count = 0;
			vote(chip->usb_icl_votable, WEAK_SUPPLY_VOTER, false, 0);
		} else
			; /* Do Nothing */

		trigger = !!(((reg & ICL_MODE_MASK) != ICL_MODE_HIGH_CURRENT) &&
			((usb_supply_type == POWER_SUPPLY_TYPE_USB) || (usb_supply_type == POWER_SUPPLY_TYPE_USB_CDP)));

		if (trigger) {
			wa_weak_usb_limit_current_main(chip);
		} else {
			pr_info("skip usb_suspend votable\n");
		}
	} else if((reg & USBIN_UV_BIT)) {
		pr_info("Charger Removed. Clear wa_support_weak_supply_count\n");
		wa_support_weak_supply_count = 0;
		wa_uv_irq_count = 0;
		vote(chip->usb_icl_votable, WEAK_SUPPLY_VOTER, false, 0);
	} else
		; /* Do Nothing */
}

void wa_support_weak_supply_check(void) {
	if (delayed_work_pending(&wa_support_weak_supply_dwork))
		wa_support_weak_supply_running = true;
	wa_uv_irq_count = 0;
	pr_err("wa_support_weak_supply_running = %d\n", wa_support_weak_supply_running);
}
////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Vfloat Trim Check
////////////////////////////////////////////////////////////////////////////
#define BATTCHG_VFLT_TRIM_EN_VOTER      "BATTCHG_VFLT_TRIM_EN_VOTER"
#define VFLOAT_TRIM_RECHECK_DELAY_MS    1000
#define FULL_CHARGED_CAPACITY           100
#define FULL_BATTERY_MIN_VOLTAGE        4350

static bool vfloat_trim_restore_status = false;
static u8 initial_vfloat_trim_reg;

static void wa_vfloat_trim_check_main(struct work_struct *unused);
static DECLARE_DELAYED_WORK(wa_vfloat_trim_check_dwork, wa_vfloat_trim_check_main);
static void wa_vfloat_trim_check_main(struct work_struct *unused)
{
	struct smbchg_chip* chip = wa_helper_smbchg();
	int vbat_uv, vbat_mv, capacity, rc;
	u8 vfloat_trim_reg;

	rc = get_property_from_fg(chip,
			POWER_SUPPLY_PROP_CAPACITY, &capacity);
	if (rc) {
		pr_smb(PR_STATUS,
			"bms psy does not support capacity rc = %d\n", rc);
		goto reschedule;
	}

	if (capacity == FULL_CHARGED_CAPACITY ) {
		pr_smb(PR_STATUS, "Skip, SOC (%d) is Full. \n", capacity);
		goto out;
	}

	rc = get_property_from_fg(chip,
		POWER_SUPPLY_PROP_VOLTAGE_NOW, &vbat_uv);
	if (rc) {
		pr_smb(PR_STATUS,
				"bms psy does not support voltage rc = %d\n", rc);
		goto reschedule;
	}
	vbat_mv = vbat_uv / 1000;

	/* compare with initail VFLOAT_TRIM register */
	rc = smbchg_read(chip, &vfloat_trim_reg, chip->misc_base + TRIM_14, 1);
	if (rc < 0){
		dev_err(chip->dev, "Unable to read trim 14: %d\n", rc);
		goto reschedule;
	}

	if ((capacity < FULL_CHARGED_CAPACITY) &&
			(vbat_mv < FULL_BATTERY_MIN_VOLTAGE ) &&
			(vfloat_trim_reg != initial_vfloat_trim_reg)){
		pr_smb(PR_STATUS, "Restore VFLOAT_TRIM register.\n");
		pr_smb(PR_STATUS, "SOC = %d VBAT = %d mV vfloat_trim_reg %x\n",
				capacity, vbat_mv, vfloat_trim_reg);
		goto restore;
	} else {
		pr_smb(PR_STATUS, "Not restored. SOC %d VBAT %d trim_reg %x\n",
				capacity, vbat_mv, vfloat_trim_reg);
		goto out;
	}

restore:
	pr_smb(PR_STATUS, "start recover VFLOAT_TRIM & recharging \n");

	rc = smbchg_sec_masked_write(chip, chip->misc_base + TRIM_14,
			VF_TRIM_MASK, initial_vfloat_trim_reg);
	if (rc < 0) {
		dev_err(chip->dev,
				"Couldn't change vfloat trim rc=%d\n", rc);
		goto reschedule;
	}
	pr_smb(PR_STATUS, "VFlt trim restored. 0x%02x \n",
			initial_vfloat_trim_reg);

	rc = vote(chip->battchg_suspend_votable, BATTCHG_VFLT_TRIM_EN_VOTER, true, 0);
	msleep(500);
	rc = vote(chip->battchg_suspend_votable, BATTCHG_VFLT_TRIM_EN_VOTER, false, 0);

	if (rc < 0) {
		pr_smb(PR_STATUS, "error during restart charging.\n");
		goto reschedule;
	}

	smbchg_stay_awake(chip, PM_VFLOAT_TRIM_RECHARGE);
	vfloat_trim_restore_status = true;

	if (chip->batt_psy) {
		if (!chip->enable_aicl_wake) {
			pr_smb(PR_STATUS, "enable aicl_done_irq\n");
			enable_irq_wake(chip->aicl_done_irq);
			chip->enable_aicl_wake = true;
		}
	} else {
		pr_smb(PR_STATUS, "smbchg irqs are not registered\n");
	}
	smbchg_parallel_usb_check_ok(chip);
	if (chip->batt_psy)
		power_supply_changed(chip->batt_psy);
	smbchg_charging_status_change(chip);

out:
	pr_smb(PR_STATUS, "End of vfloat trim check. \n");
	smbchg_relax(chip, PM_VFLOAT_TRIM_RESTORE);
	return;

reschedule:
	pr_smb(PR_STATUS, "Rescheduled vfloat trim check. \n");
	if (delayed_work_pending(&wa_vfloat_trim_check_dwork)) {
		pr_smb(PR_STATUS, "Cancel pended work...\n");
		cancel_delayed_work(&wa_vfloat_trim_check_dwork);
	}

	schedule_delayed_work(&wa_vfloat_trim_check_dwork,
			msecs_to_jiffies(VFLOAT_TRIM_RECHECK_DELAY_MS));
}

void wa_vfloat_trim_check_trigger(struct smbchg_chip *chip) {
	enum charger_usbid usbid = psy_usbid_get(chip);

	if (!chip->usb_present)
		return;

	if ((usbid == CHARGER_USBID_56KOHM || usbid == CHARGER_USBID_130KOHM || usbid == CHARGER_USBID_910KOHM) &&
			(chip->usb_supply_type == POWER_SUPPLY_TYPE_USB))
		return;

	if (vfloat_trim_restore_status) {
		pr_info("VFLOAT_TRIM restored recharge finish. \n");
		vfloat_trim_restore_status = false;
		smbchg_relax(chip, PM_VFLOAT_TRIM_RECHARGE);
		return;
	}

	smbchg_stay_awake(chip, PM_VFLOAT_TRIM_RESTORE);
	schedule_delayed_work(&wa_vfloat_trim_check_dwork, 0);
}

void wa_vfloat_trim_check_init(struct smbchg_chip *chip) {
	int rc;
	u8 reg;

	/* save initial VFLOAT_TRIM register */
	rc = smbchg_read(chip, &reg, chip->misc_base + TRIM_14, 1);
	if (rc < 0){
		dev_err(chip->dev, "Unable to read trim 14: %d\n", rc);
		initial_vfloat_trim_reg = 0xE0; //default TRIM_14 value
	}
	initial_vfloat_trim_reg = reg;
	vfloat_trim_restore_status = false;
	pr_smb(PR_STATUS, "Initial VFLOAT_TRIM reg = 0x%x\n",
			initial_vfloat_trim_reg);
}

void wa_vfloat_trim_check_clear(struct smbchg_chip *chip) {
	if (vfloat_trim_restore_status){
		pr_smb(PR_STATUS, "VFLOAT_TRIM wakelock released. \n");
		vfloat_trim_restore_status = false;
		smbchg_relax(chip, PM_VFLOAT_TRIM_RECHARGE);
	}
}

/*************************************************************
 * extension for smbcharger probe.
 */

int extension_smbcharger_probe(struct smbchg_chip *chip) {
	struct device_node *node = chip->dev->of_node;
        int rc;

	if(!vadc_dev) {
		if (of_find_property(node, "qcom,usb_id-vadc", NULL)) {
			vadc_dev = qpnp_get_vadc(chip->dev, "usb_id");
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

error:
	return rc;
}
