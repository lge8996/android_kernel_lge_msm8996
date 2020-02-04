/*
 * CAUTION! :
 * 	This file will be included at the end of "qpnp-fg-gen3.c".
 * 	So "qpnp-fg-gen3.c" should be touched before you start to build.
 * 	If not, your work will not be applied to the built image
 * 	because the build system doesn't care the update time of this file.
 */

#include <linux/thermal.h>
#include <linux/kernel.h>
#include "veneer-primitives.h"

#define LGE_FG_INITVAL -1

struct _fake {
	int temperature;
	int capacity;
	int uvoltage;
};

struct _fginfo {
/* Capacity */
	int capacity_rescaled;
        int capacity_monotonic;
	int capacity_chargecnt;
	int capacity_learned;
/* v/i ADCs */
	int battery_inow;
	int battery_vnow;
	int battery_ocv;
	int input_vusb;
	int input_iusb;
	int input_aicl;
/* Temperature */
	int temp_compensated;
	int temp_thermister;
	int temp_vts;
/* Impedance */
	int impedance_esr;
/* Misc */
//	int misc_cycle;
        int misc_batid;
/* SoCs */
	int misc_battsoc;
	int misc_ccsocsw;
	int misc_ccsoc;
};

#define TCOMP_TABLE_MAX 1
#define TCOMP_COUNT 25
struct tcomp_param {
	bool load_done;
	int load_max;
	bool icoeff_load_done;
	struct tcomp_entry {
		int temp_cell;
		int temp_bias;
	} table[TCOMP_TABLE_MAX][TCOMP_COUNT];
	int icoeff;

	bool rise_cut;
	int rise_cut_trig;
	bool fall_cut;
	int fall_cut_trig;

	bool qnovo_charging;
	bool logging;
};

struct _rescale {
	bool lge_monotonic;
        bool lge_under_five_level;
	int	criteria;	// 0 ~ 255
	int	rawsoc;		// 0 ~ 255
	int	result;		// 0 ~ 100
};

enum tcomp_chg_type {
	TCOMP_CHG_NONE = 0,
	TCOMP_CHG_USB,
	TCOMP_CHG_WLC_LCDOFF,
	TCOMP_CHG_WLC_LCDON
};

/* Gloval variable for extension-qpnp-fg */
static struct _fake fake = {
	.temperature = LGE_FG_INITVAL,
	.capacity = LGE_FG_INITVAL,
	.uvoltage = LGE_FG_INITVAL,
};

static struct _fginfo fginfo = {
/* Capacity  */ LGE_FG_INITVAL, LGE_FG_INITVAL, LGE_FG_INITVAL, LGE_FG_INITVAL,
/* v/i ADCs  */ LGE_FG_INITVAL, LGE_FG_INITVAL, LGE_FG_INITVAL,
                LGE_FG_INITVAL, LGE_FG_INITVAL, LGE_FG_INITVAL,
/* Temp      */ LGE_FG_INITVAL, LGE_FG_INITVAL, -1000,
/* impedance */ LGE_FG_INITVAL,
/* Misc      */ /*LGE_FG_INITVAL,*/LGE_FG_INITVAL,
/* SoCs      */ LGE_FG_INITVAL, LGE_FG_INITVAL, LGE_FG_INITVAL,
};

static struct tcomp_param tcomp = {
	.load_done = false,
	.icoeff_load_done = false,
	.icoeff = 0,

	.rise_cut = false,
	.rise_cut_trig = -999,
	.fall_cut = false,
	.fall_cut_trig = -999,

	.qnovo_charging = false,
	.logging = false,
};

/* For SoC rescaling, .rawsoc(0~255) is updated ONLY ON
 * 'fg_delta_msoc_irq_handler' and it is rescaled to 0~100
 */
static struct _rescale rescale = {
	.lge_monotonic = false,
        .lge_under_five_level = false,
	.criteria = 247,
	.rawsoc = LGE_FG_INITVAL,
	.result = LGE_FG_INITVAL,
};

/* Extension A. Battery temp tuning */
/* calculate_battery_temperature
 *     bias  : 1st compensation by predefined diffs
 *     icomp : 2nd compensation by (i^2 * k)
 */

static int get_charging_type(struct fg_chip *fg)
{
	union power_supply_propval val = { 0, };

	if (fg->batt_psy) {
		if (!power_supply_get_property(fg->batt_psy,
			POWER_SUPPLY_PROP_STATUS, &val))
			if (val.intval != POWER_SUPPLY_STATUS_CHARGING)
				return TCOMP_CHG_NONE;
	}

	if (fg->usb_psy) {
		if (!power_supply_get_property(fg->usb_psy,
			POWER_SUPPLY_PROP_PRESENT, &val))
			if (val.intval)
				return TCOMP_CHG_USB;
	}

	/* if you have wlc, you need to add TCOMP_CHG_WLC_ here */

	return TCOMP_CHG_NONE;
}

static int get_batt_charging_current(struct fg_chip *fg)
{
	static int ichg = 0;
	bool is_cc = false;
	bool is_fast_charging = false;
	union power_supply_propval val = { 0, };

	if (!fg || !fg->batt_psy || !fg->usb_psy)
		return 0;

	if (!power_supply_get_property(fg->batt_psy,
			POWER_SUPPLY_PROP_STATUS, &val)
				&& val.intval == POWER_SUPPLY_STATUS_CHARGING) {
		if (!power_supply_get_property(fg->usb_psy,
				POWER_SUPPLY_PROP_REAL_TYPE, &val)) {

			if (val.intval == POWER_SUPPLY_TYPE_USB_HVDCP ||
				val.intval == POWER_SUPPLY_TYPE_USB_HVDCP_3 ||
				val.intval == POWER_SUPPLY_TYPE_USB_PD)
				is_fast_charging = true;

			if (!power_supply_get_property(fg->batt_psy,
					POWER_SUPPLY_PROP_CHARGE_TYPE, &val) &&
				(val.intval == POWER_SUPPLY_CHARGE_TYPE_TRICKLE ||
				 val.intval == POWER_SUPPLY_CHARGE_TYPE_FAST))
				is_cc = true;

			/*  in case of fast charging, fcc is refered instead of
				real current for avoiding qni negative pulse effect */
			if (is_fast_charging && is_cc ) {
				ichg = !power_supply_get_property(fg->batt_psy,
							POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &val) ?
								val.intval / -1000 : ichg;
				goto out;
			} else {
				/* if charging current is over -25mA,
					batt_therm compensation current keeps the previous value */
				if (!power_supply_get_property(fg->batt_psy,
						POWER_SUPPLY_PROP_CURRENT_NOW, &val) &&
							val.intval < -25000)
					ichg = val.intval / 1000;

				goto out;
			}
		}
	}

	ichg = 0;

out:
	return ichg;
}

static int filtered_batt_therm(bool changed, int comp, int batt)
{
	bool tbl_changed = changed;
	int battemp_cell = batt;
	int battemp_comp = comp;
	static int pre_battemp_cell = -9999;
	static int pre_battemp_comp = -9999;
	static bool is_filtering_rise = false;
	static bool is_filtering_fall = false;
	int battemp_cell_diff = 0;
	int battemp_comp_diff = 0;

	if (!((tbl_changed && tcomp.rise_cut
			&& (battemp_comp > tcomp.rise_cut_trig)) ||
		(tbl_changed && tcomp.fall_cut
			&& (battemp_comp < tcomp.fall_cut_trig))))
		tbl_changed = false;

	if ((tbl_changed || is_filtering_rise || is_filtering_fall)
		&& (pre_battemp_cell > -9999 && pre_battemp_comp > -9999)) {
		battemp_cell_diff = battemp_cell - pre_battemp_cell;
		battemp_comp_diff = battemp_comp - pre_battemp_comp;
		// rise
		if (tcomp.rise_cut && (battemp_comp >= pre_battemp_comp)) {
			if (is_filtering_fall)
				is_filtering_fall = false;

			if (battemp_comp_diff > battemp_cell_diff) {
				is_filtering_rise = true;
				if ( battemp_cell_diff > 0 )
					battemp_comp = pre_battemp_comp + battemp_cell_diff;
				else
					battemp_comp = pre_battemp_comp;
			}
			else {
				is_filtering_rise = false;
			}
		}
		// fall
		else if (tcomp.fall_cut) {
			if (is_filtering_rise)
				is_filtering_rise = false;

			if (battemp_cell_diff > battemp_comp_diff ) {
				is_filtering_fall = true;
				if (battemp_cell_diff < 0)
					battemp_comp = pre_battemp_comp + battemp_cell_diff;
				else
					battemp_comp = pre_battemp_comp;
			}
			else {
				is_filtering_fall = false;
			}
		}
		else if (tcomp.rise_cut) {
			if (is_filtering_rise)
				is_filtering_rise = false;
		}
	}

	pre_battemp_cell = battemp_cell;
	pre_battemp_comp = battemp_comp;
	return battemp_comp;
}

static int calculate_battery_temperature(/* @Nonnull */ struct fg_chip *fg)
{
	int battemp_bias, battemp_icomp, battemp_cell = 0;
	int i, temp, ichg = 0, tbl_pt = 0;
	union power_supply_propval val = { 0, };
	bool tbl_changed = false;
	static int pre_tbl_pt = -1;

        battemp_cell = get_sram_prop_now(fg, FG_DATA_BATT_TEMP);
/*	if (fg_get_battery_temp(fg, &battemp_cell)){
		pr_info("get real batt therm error\n");
		return LGE_FG_INITVAL;
	}*/

//	if (wa_skip_batt_temp_on_bootup_check(battemp_cell, false))
//		return 250;

	if (!tcomp.load_done) {
		pr_info("not ready tcomp table. rerun -> table=%d\n",
			tcomp.load_done);
		return battemp_cell;
	}

	if (!tcomp.icoeff_load_done) {
		pr_info("not ready icoeff. rerun -> icoeff=%d\n",
			tcomp.icoeff_load_done);
		return battemp_cell;
	}

	if (tcomp.load_max > 1) {
		switch (get_charging_type(fg)) {
			case TCOMP_CHG_WLC_LCDOFF: tbl_pt = 1; break;
			case TCOMP_CHG_WLC_LCDON:  tbl_pt = 2; break;
			default: tbl_pt = 0; break;
		}
		if (pre_tbl_pt >= 0 )
			if (pre_tbl_pt != tbl_pt)
				tbl_changed = true;
		pre_tbl_pt = tbl_pt;
	}
	else
		tbl_pt = 0;


	/* Compensating battemp_bias */
	for (i = 0; i < TCOMP_COUNT; i++) {
		if (battemp_cell < tcomp.table[tbl_pt][i].temp_cell)
			break;
	}

	if (i == 0)
		battemp_bias = tcomp.table[tbl_pt][0].temp_bias;
	else if (i == TCOMP_COUNT)
		battemp_bias = tcomp.table[tbl_pt][TCOMP_COUNT-1].temp_bias;
	else
		battemp_bias =
		(	(tcomp.table[tbl_pt][i].temp_bias -
				tcomp.table[tbl_pt][i-1].temp_bias)
			* (battemp_cell - tcomp.table[tbl_pt][i-1].temp_cell)
			/ (tcomp.table[tbl_pt][i].temp_cell -
				tcomp.table[tbl_pt][i-1].temp_cell)
		) + tcomp.table[tbl_pt][i-1].temp_bias;

	/* Compensating battemp_icomp */
	if (fg->batt_psy) {
		if (tcomp.qnovo_charging) {
			ichg = get_batt_charging_current(fg);
		}
		else {
			if (!power_supply_get_property(
				fg->batt_psy, POWER_SUPPLY_PROP_STATUS, &val)
				&& val.intval == POWER_SUPPLY_STATUS_CHARGING
				&& !power_supply_get_property(
					fg->batt_psy, POWER_SUPPLY_PROP_CURRENT_NOW, &val)
				&& val.intval < -25000)
				ichg = ((val.intval) / 1000);
		}
	} else {
		pr_info("Battery is not available, %d(=%d+%d) as batt temp\n",
			battemp_cell + battemp_bias, battemp_cell, battemp_bias);
	}

	battemp_icomp = ichg * ichg * tcomp.icoeff / 10000000;
	temp = battemp_cell + battemp_bias - battemp_icomp;

	if (tcomp.logging)
		pr_info("Battery temperature : "
				"%d = (%d)(cell) + (%d)(bias) - %d(icomp), "
				"icoeff = %d, ichg = %d\n",
			temp, battemp_cell, battemp_bias, battemp_icomp,
			tcomp.icoeff, ichg);

	if (tcomp.rise_cut || tcomp.fall_cut)
		return filtered_batt_therm(tbl_changed, temp, battemp_cell);

	return temp;
}
/* SOC Under 5 Level */
#define BATTERY_SOC_100 10000
#define BATTERY_SOC_Y1 9700 //real battery SOC level
#define BATTERY_SOC_Y2 500  //real battery SOC level
#define BATTERY_SOC_X1 9600 //modified battery SOC level
#define BATTERY_SOC_X2 700  //modified battery SOC level
int soc_rescale_under_five_level(int temp_soc)
{
    if (temp_soc > 0 && temp_soc <= BATTERY_SOC_X2)
    {
        temp_soc = temp_soc*(BATTERY_SOC_Y2)/(BATTERY_SOC_X2);
    }
    if(temp_soc > 100 && temp_soc <= 250)
    {
        temp_soc = 200;
    }
    else if (temp_soc > 250 && temp_soc <= 400)
    {
        temp_soc = 300;
    }
    else if (temp_soc > 400 && temp_soc <= 550)
    {
        temp_soc = 400;
    }
    else if (temp_soc > 550 && temp_soc <= BATTERY_SOC_X2)
    {
        temp_soc = 500;
    }
    else if ((BATTERY_SOC_X2 < temp_soc) &&
            (temp_soc <= BATTERY_SOC_X1))
    {
        temp_soc = BATTERY_SOC_Y2 +
            (temp_soc*(BATTERY_SOC_Y1-BATTERY_SOC_Y2)-
             BATTERY_SOC_X2*(BATTERY_SOC_Y1-BATTERY_SOC_Y2))/
            (BATTERY_SOC_X1-BATTERY_SOC_X2);
    }
    else if ((BATTERY_SOC_X1 < temp_soc) &&
            (temp_soc <= BATTERY_SOC_100))
    {
        temp_soc = BATTERY_SOC_Y1 +
            (temp_soc*(BATTERY_SOC_100-BATTERY_SOC_Y1)-
             BATTERY_SOC_X1*(BATTERY_SOC_100-BATTERY_SOC_Y1))/
            (BATTERY_SOC_100-BATTERY_SOC_X1);
    }
    return temp_soc;
}

void check_update_soc_lge_five_level(struct fg_chip *fg, int msoc_raw,
        int msoc_scale_raw, int msoc_scale,
        int five_soc_raw, int five_soc)
{
    static int pre_msoc_raw = 0;

    if (msoc_raw != pre_msoc_raw) {
        pr_debug("[FG_CHANGE]:before soc=0x%X,current soc=0x%X\n",
                pre_msoc_raw, msoc_raw);

        pre_msoc_raw = msoc_raw;

        pr_debug("[FG_DELTA]:SOC_Raw=0x%X,"
                "SOC_Scale_raw=%d,SOC_scale=%d,SOC_5LEVEL_Raw=%d,"
                "SOC_5LEVEL=%d,Voltage=%d,OCV=%d,current=%d,temp=%d\n"
                ,msoc_raw, msoc_scale_raw, msoc_scale,
                five_soc_raw, five_soc,
                get_sram_prop_now(fg, FG_DATA_VOLTAGE) / 1000,
                get_sram_prop_now(fg, FG_DATA_OCV) / 1000,
                get_sram_prop_now(fg, FG_DATA_CURRENT) / 1000,
                get_sram_prop_now(fg, FG_DATA_BATT_TEMP) / 10);
        if (fg->bms_psy)
            power_supply_changed(fg->bms_psy);
    }
    return;
}
/* Extension B. Battery UI SOC tuning */

#define LGE_FG_CHARGING         1
#define LGE_FG_DISCHARGING      0
static int lge_is_fg_charging(struct fg_chip *fg)
{
	union power_supply_propval val = { 0, };
	bool power_status = false;

	if (!fg || !fg->batt_psy || !fg->usb_psy)
		return -1;

	if (!power_supply_get_property(fg->batt_psy,
					POWER_SUPPLY_PROP_STATUS_RAW, &val))
		power_status = (val.intval == POWER_SUPPLY_STATUS_CHARGING) ? true : false;
	else
		return -1;

	if (!power_status)
		return LGE_FG_DISCHARGING;

	if (!power_supply_get_property(fg->batt_psy,
					POWER_SUPPLY_PROP_CURRENT_NOW, &val)) {
		if (val.intval < -25000 )
			return LGE_FG_CHARGING;
		else
			return LGE_FG_DISCHARGING;
	}

	return -1;
}

#define LGE_SOC_SCALE_UNIT      100
#define LGE_SOC_ROUND_UNIT      100
#ifdef CONFIG_LGE_PM_CCD
#define LGE_SOC_TTF_SCALE_UNIT  10
#endif
bool wa_get_check_ima_error_handling(void);
bool wa_get_wait_for_cc_soc_store(void);

int lge_get_ui_soc(struct fg_chip *fg, int msoc_raw)
{
        int round_soc = (((((msoc_raw - 1) * (FULL_CAPACITY - 2)) * LGE_SOC_SCALE_UNIT)
                                /(rescale.criteria - 2)) + LGE_SOC_ROUND_UNIT);
        int new_result = min(FULL_CAPACITY, (round_soc/LGE_SOC_SCALE_UNIT));
#ifdef CONFIG_LGE_PM_CCD
        char buff[10] = { 0, };
#endif

        rescale.rawsoc = msoc_raw;
        pr_debug("msoc_raw: %d, round_soc = %d\n", msoc_raw, round_soc);

        if (rescale.lge_under_five_level) {
                int five_soc_raw = soc_rescale_under_five_level(round_soc);
                int five_soc = min(FULL_CAPACITY, (five_soc_raw/LGE_SOC_SCALE_UNIT));
                static int pre_soc;
#ifdef CONFIG_LGE_PM_CCD
                static int pre_soc_raw;
#endif
                if(wa_get_check_ima_error_handling() || wa_get_wait_for_cc_soc_store()) {   //check_ima_error_handling or vint_error
				    pr_info("[FG_INFO] capacity return pre_soc = %d\n", pre_soc);
                    return pre_soc;
                }

                pr_debug("five_soc_raw: %d, five_soc = %d\n", five_soc_raw, five_soc);
                check_update_soc_lge_five_level(fg, msoc_raw, round_soc,
                                (round_soc/LGE_SOC_SCALE_UNIT), five_soc_raw, (five_soc_raw/LGE_SOC_SCALE_UNIT));

                if(pre_soc < five_soc && (get_sram_prop_now(fg, FG_DATA_CURRENT) > 0) && !is_usb_present(fg) &&
                        fg->init_done == true && pre_soc != 0) {
                    pr_debug("[FG_INFO] soc did not update (pre soc = %d\n, scaled soc = %d)\n", pre_soc, five_soc);
                    pr_debug("[FG_INFO] use pre soc\n");
                    pr_debug("[FG_INFO] reverse soc when discharging!");
                    rescale.result = pre_soc;
                    return 0;
                }

                if (fg->init_done == true) {
#ifdef CONFIG_LGE_PM_CCD
                    if (!is_battery_missing(fg) && fg->soc_reporting_ready) {
                        if (pre_soc_raw != five_soc_raw) {
                            snprintf(buff, sizeof(buff), "%d", five_soc_raw/LGE_SOC_TTF_SCALE_UNIT);
                            unified_nodes_store("ttf_capacity", buff, strlen(buff));
                        }
                        pre_soc_raw = five_soc_raw;
                    }
#endif
                    pre_soc = five_soc;
                    pr_debug("[FG_INFO] pre soc update (pre soc = %d\n, scaled soc = %d)\n", pre_soc, five_soc);
                }
                pr_debug("[FG_INFO] soc scaling done\n");
                rescale.result = five_soc;
                return 0;
        }

	if (!rescale.lge_monotonic) {
		rescale.result = new_result;
		return 0;
	}

	if (rescale.result <= 0 ||
		max(rescale.result, new_result) - min(rescale.result, new_result) > 5 )
		rescale.result = new_result;

	switch (lge_is_fg_charging(fg)) {
		case LGE_FG_CHARGING:
			pr_info("fg_rescale: charging: %d = max(old=%d, new=%d)\n",
				max(rescale.result, new_result), rescale.result, new_result);
			rescale.result = max(rescale.result, new_result);
			break;
		case LGE_FG_DISCHARGING:
			pr_info("fg_rescale: discharging: %d = min(old=%d, new=%d)\n",
				min(rescale.result, new_result), rescale.result, new_result);
			rescale.result = min(rescale.result, new_result);
			break;
		default:
			pr_info("fg_rescale: error: old=%d, new=%d\n", rescale.result, new_result);
			rescale.result = new_result;
			break;
	}

	return 0;
}

/* Extension C. Battery information update & logging*/
static void fginfo_snapshot_print(void)
{
	printk("PMINFO: [CSV] "
/* Capacity  */ "cSYS:%d, cMNT:%d, cCHG:%d, cLRN:%d, "
/* v/i ADCs  */ "iBAT:%d, vBAT:%d, vOCV:%d, vUSB:%d, iUSB:%d, AiCL:%d\n"
				"PMINFO: [CSV] "
/* Temp      */ "tSYS:%d, tORI:%d, tVTS:%d, "
/* Impedance */ "rESR:%d, "
/* Misc      */ /*"CYCLE:%d,*/"BATID:%d, "
/* SoCs      */ "sBATT:%d, sCCSW:%d, sCC:%d\n",

/* Capacity  */ fginfo.capacity_rescaled*10, fginfo.capacity_monotonic, fginfo.capacity_chargecnt, fginfo.capacity_learned,
/* Battery   */ fginfo.battery_inow, fginfo.battery_vnow, fginfo.battery_ocv,
/* Input     */ fginfo.input_vusb, fginfo.input_iusb, fginfo.input_aicl,
/* Temp      */ fginfo.temp_compensated, fginfo.temp_thermister, fginfo.temp_vts,
/* Impedance */ fginfo.impedance_esr,
/* Misc      */ /*fginfo.misc_cycle,*/fginfo.misc_batid,
/* SoCs      */ fginfo.misc_battsoc, fginfo.misc_ccsocsw, fginfo.misc_ccsoc);
}

static void//need to check
fginfo_snapshot_inputnow(int* vusb, int* iusb, int* aicl)
{
	struct power_supply* psy_battery = power_supply_get_by_name("battery");
	struct power_supply* psy_usb = power_supply_get_by_name("usb");
	union power_supply_propval val = { 0, };

	*aicl = (psy_battery && !power_supply_get_property(
			psy_battery, POWER_SUPPLY_PROP_INPUT_CURRENT_MAX, &val))
				? val.intval/1000 : LGE_FG_INITVAL;
	*vusb = (psy_usb && !power_supply_get_property(
			psy_usb, POWER_SUPPLY_PROP_VOLTAGE_NOW, &val))
				? val.intval : LGE_FG_INITVAL;
	*iusb = (psy_battery && !power_supply_get_property(
			psy_battery, POWER_SUPPLY_PROP_INPUT_CURRENT_NOW, &val))
				? val.intval/1000 : LGE_FG_INITVAL;

	if (psy_battery)
		power_supply_put(psy_battery);
	if (psy_usb)
		power_supply_put(psy_usb);
}

static void fginfo_snapshot_update(struct power_supply *psy)
{
	int buf = 0;
#if 0
	int64_t temp64 = 0;
#endif
	struct fg_chip*	fg = power_supply_get_drvdata(psy);
	struct thermal_zone_device*	tzd = thermal_zone_get_zone_by_name("vts-virt-therm");
	union power_supply_propval val = { 0, };

	if (!tzd || !fg)
		return;

/* Capacity */
	fginfo.capacity_rescaled = rescale.result < 0
		? LGE_FG_INITVAL : rescale.result;
        fginfo.capacity_monotonic = get_monotonic_soc_raw(fg);
	fginfo.capacity_chargecnt = !power_supply_get_property(fg->bms_psy,
                                    POWER_SUPPLY_PROP_CHARGE_COUNTER, &val)
		? val.intval/1000 : LGE_FG_INITVAL;
	fginfo.capacity_learned = !power_supply_get_property(fg->bms_psy,
                                    POWER_SUPPLY_PROP_CHARGE_FULL, &val)
		? val.intval/1000 : LGE_FG_INITVAL;
/* Battery */
	fginfo.battery_inow = get_sram_prop_now(fg, FG_DATA_CURRENT);
	fginfo.battery_vnow = get_sram_prop_now(fg, FG_DATA_VOLTAGE);
	fginfo.battery_ocv = !power_supply_get_property(fg->bms_psy,
                                    POWER_SUPPLY_PROP_VOLTAGE_OCV, &val)
		? val.intval / 1000 : LGE_FG_INITVAL;
/* Input */
	fginfo_snapshot_inputnow(&fginfo.input_vusb, &fginfo.input_iusb, &fginfo.input_aicl);
/* Temperature */
	fginfo.temp_compensated
		= calculate_battery_temperature(fg);
	fginfo.temp_thermister = get_sram_prop_now(fg, FG_DATA_BATT_TEMP);
	fginfo.temp_vts = !thermal_zone_get_temp(tzd, &buf)
		? buf / 100 : -1000;
/* Impedance */
	fginfo.impedance_esr = get_sram_prop_now(fg, FG_DATA_BATT_ESR);
/* Misc */
#if 0
	if (kstrtoint(qg_get_cycle_count(qg), 10, &(qpnpqg.misc_cycle)))
		pr_err("Error in getting cycle counts\n");
#endif
	fginfo.misc_batid
		= get_sram_prop_now(fg, FG_DATA_BATT_ID)/1000;
/* SoCs */
	fginfo.misc_battsoc = get_sram_prop_now(fg, FG_DATA_BATT_SOC);
	fginfo.misc_ccsocsw = fg_get_current_cc(fg);
	fginfo.misc_ccsoc = get_sram_prop_now(fg, FG_DATA_CC_CHARGE);
	/* logging finally */
	fginfo_snapshot_print();
}

///////////////////////////////////////////////////////////////////////////////
#define PROPERTY_CONSUMED_WITH_SUCCESS	0
#define PROPERTY_CONSUMED_WITH_FAIL	EINVAL
#define PROPERTY_BYPASS_REASON_NOENTRY	ENOENT
#define PROPERTY_BYPASS_REASON_ONEMORE	EAGAIN

static enum power_supply_property extension_bms_appended [] = {
	POWER_SUPPLY_PROP_UPDATE_NOW,
};

static int
extension_bms_get_property_pre(struct power_supply *psy,
	enum power_supply_property prop, union power_supply_propval *val)
{
	int rc = PROPERTY_CONSUMED_WITH_SUCCESS;
	struct fg_chip*	fg = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_RESISTANCE:
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
			rc = -PROPERTY_BYPASS_REASON_ONEMORE;
		break;

	case POWER_SUPPLY_PROP_CAPACITY :
		// Battery fake setting has top priority
		if (fake.capacity != LGE_FG_INITVAL)
			val->intval = fake.capacity;
		else if (rescale.result != LGE_FG_INITVAL)
			val->intval = rescale.result;
		else
			rc = -PROPERTY_BYPASS_REASON_ONEMORE;
		break;

	case POWER_SUPPLY_PROP_TEMP :
		if (fake.temperature == LGE_FG_INITVAL) {
			val->intval = calculate_battery_temperature(fg); // Use compensated temperature
		} else
			val->intval = fake.temperature;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW :
		if (fake.uvoltage == LGE_FG_INITVAL)
			rc = -PROPERTY_BYPASS_REASON_ONEMORE;
		else
			val->intval = fake.uvoltage;
		break;

	case POWER_SUPPLY_PROP_UPDATE_NOW :
		/* Do nothing and just consume getting */
		val->intval = -1;
		break;

	default:
		rc = -PROPERTY_BYPASS_REASON_NOENTRY;
		break;
	}

	return rc;
}

static int
extension_bms_get_property_post(struct power_supply *psy,
	enum power_supply_property prop, union power_supply_propval *val, int rc)
{
        switch (prop) {
        default:
                break;
        }
	return rc;
}

static int
extension_bms_set_property_pre(struct power_supply *psy,
	enum power_supply_property prop, const union power_supply_propval *val)
{
	int* fakeset = NULL;
	int  rc = PROPERTY_CONSUMED_WITH_SUCCESS;
	struct fg_chip* fg = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_UPDATE_NOW :
		if (val->intval)
			fginfo_snapshot_update(psy);
		break;

	case POWER_SUPPLY_PROP_TEMP :
		fakeset = &fake.temperature;
		break;
	case POWER_SUPPLY_PROP_CAPACITY :
		fakeset = &fake.capacity;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW :
		fakeset = &fake.uvoltage;
		break;

	default:
		rc = -PROPERTY_BYPASS_REASON_NOENTRY;
	}

	if (fakeset && *fakeset != val->intval) {
		*fakeset = val->intval;
		power_supply_changed(fg->batt_psy);
	}

	return rc;
}

static int
extension_bms_set_property_post(struct power_supply *psy,
	enum power_supply_property prop, const union power_supply_propval *val, int rc)
{
	return rc;
}

///////////////////////////////////////////////////////////////////////////////
enum power_supply_property* extension_bms_properties(void)
{
	static enum power_supply_property
		extended_properties[ARRAY_SIZE(fg_power_props)
		+ ARRAY_SIZE(extension_bms_appended)];
	int size_original = ARRAY_SIZE(fg_power_props);
	int size_appended = ARRAY_SIZE(extension_bms_appended);

	memcpy(extended_properties, fg_power_props,
		size_original * sizeof(enum power_supply_property));
	memcpy(&extended_properties[size_original], extension_bms_appended,
		size_appended * sizeof(enum power_supply_property));

	veneer_extension_pspoverlap(fg_power_props, size_original,
		extension_bms_appended, size_appended);

        return extended_properties;
}

size_t extension_bms_num_properties(void)
{
	return ARRAY_SIZE(fg_power_props) + ARRAY_SIZE(extension_bms_appended);
}

int
extension_bms_get_property(struct power_supply *psy,
	enum power_supply_property prop, union power_supply_propval *val)
{
	int rc = extension_bms_get_property_pre(psy, prop, val);
	if (rc == -PROPERTY_BYPASS_REASON_NOENTRY
		|| rc == -PROPERTY_BYPASS_REASON_ONEMORE)
		rc = fg_power_get_property(psy, prop, val);
	rc = extension_bms_get_property_post(psy, prop, val, rc);

	return rc;
}

int
extension_bms_set_property(struct power_supply *psy,
	enum power_supply_property prop, const union power_supply_propval *val)
{
	int rc = extension_bms_set_property_pre(psy, prop, val);
	if (rc == -PROPERTY_BYPASS_REASON_NOENTRY
		|| rc == -PROPERTY_BYPASS_REASON_ONEMORE)
		rc = fg_power_set_property(psy, prop, val);
	rc = extension_bms_set_property_post(psy, prop, val, rc);

	return rc;
}

int
extension_bms_property_is_writeable(
	struct power_supply *psy, enum power_supply_property prop)
{
	int rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_TEMP:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_UPDATE_NOW:
        case POWER_SUPPLY_PROP_VOLTAGE_NOW:
#ifndef CONFIG_LGE_PM_CCD
	case POWER_SUPPLY_PROP_CAPACITY_RAW:
#endif
		rc = 1;
		break;
	default:
		rc = fg_property_is_writeable(psy, prop);
		break;
	}
	return rc;
}
////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Helper functions
////////////////////////////////////////////////////////////////////////////
static struct fg_chip* wa_helper_fg(void) {
	// getting smb_charger from air
	struct power_supply*    psy
		= power_supply_get_by_name("bms");
	struct fg_chip* chip
		= psy ? power_supply_get_drvdata(psy) : NULL;
	if (psy)
		power_supply_put(psy);

	return chip;
}
////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Check IMA Error Handling
////////////////////////////////////////////////////////////////////////////
#define GUARANTEE_INTERVAL_MS 1470
static bool wa_check_ima_error_handling = false;

void wa_set_check_ima_error_handling(bool value) {
	wa_check_ima_error_handling = value;
}

bool wa_get_check_ima_error_handling(void) {
	return wa_check_ima_error_handling;
}

static void wa_guarantee_soc_interval_work_main(struct work_struct *unused) {
	struct fg_chip* chip = wa_helper_fg();

	chip->use_last_soc = false;
	wa_check_ima_error_handling = false;
	pr_info("[FG_INFO]clear use_last_soc\n");
}
static DECLARE_DELAYED_WORK(wa_guarantee_soc_interval_dwork, wa_guarantee_soc_interval_work_main);

void wa_check_ima_error_handling_trigger(struct fg_chip *chip) {
	if (wa_check_ima_error_handling) {
		pr_info("[FG_INFO]ima_error_handling before\n");
		schedule_delayed_work(&wa_guarantee_soc_interval_dwork,
			msecs_to_jiffies(GUARANTEE_INTERVAL_MS));
	} else {
		chip->use_last_soc = false;
	}
}
////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Check Vint Error Handling
////////////////////////////////////////////////////////////////////////////
#define OFFSET_FOR_THRESHOLD_MV 100
#define CHECK_VOLTAGE_COUNT 12
#define VINT_ERR_FOR_FG_RESET 12
#define CHECK_VINT_ERR_COUNT 12
static int vint_err_pct;
static bool wait_for_cc_soc_store = false;

void wa_set_wait_for_cc_soc_store(bool value) {
	wait_for_cc_soc_store = value;
}

bool wa_get_wait_for_cc_soc_store(void) {
	return wait_for_cc_soc_store;
}

void wa_vint_error_check_percent(int64_t temp) {
	temp = twos_compliment_extend(temp, 3);
	vint_err_pct = div64_s64(temp * 10000, FULL_PERCENT_3B);
	pr_info("vint err raw %d\n", vint_err_pct);
}

bool wa_vint_error_check_trigger(struct fg_chip *chip) {
	static int vint_err_count = 0;
	static int vbat_count = 0;
	int batt_vol;

	batt_vol = get_sram_prop_now(chip, FG_DATA_VOLTAGE) / 1000;
	if (batt_vol > (chip->batt_max_voltage_uv / 1000) + OFFSET_FOR_THRESHOLD_MV) {
		vbat_count++;

		pr_info("vbatt %d, count %d\n", batt_vol, vbat_count);

		if (vbat_count >= CHECK_VOLTAGE_COUNT) {
			vbat_count = 0;
			pr_err("abnormal vbatt\n");
			fg_check_ima_error_handling(chip);
			return true;            
		}
	} else {
		vbat_count = 0;
	}

	if ((vint_err_pct/100 > VINT_ERR_FOR_FG_RESET) ||
			(vint_err_pct/100 < VINT_ERR_FOR_FG_RESET * (-1))){
		vint_err_count++;

		pr_info("[FG_INFO] vint_err %d, count %d\n",
				vint_err_pct / 100, vint_err_count);

		if (vint_err_count >= CHECK_VINT_ERR_COUNT) {
			//wait_for_cc_soc_store = true;
			vint_err_count = 0;
			pr_err("[FG_INFO] abnormal vint_err\n");
			fg_check_ima_error_handling(chip);
			return true;
		}
	} else {
		vint_err_count = 0;
	}

	return false;
}
////////////////////////////////////////////////////////////////////////////
// LGE Workaround
////////////////////////////////////////////////////////////////////////////

struct device_node *
extension_get_batt_profile(struct device_node* container, int resistance_id)
{
	/* Search with resistance_id and
	 * Hold the result to an unified node(sysfs) for the fab process
	 */
	struct device_node* node;
	const char* name;
#ifndef CONFIG_MACH_MSM8996_TF10
	char buffer [8] = { '\0', };
#endif
	int kohm = 0;
	struct device_node* profile =
			of_batterydata_get_best_profile(container, resistance_id, fg_batt_type);

	/* If no matching, set it as default */
	if (IS_ERR_OR_NULL(profile)) {
		pr_err("no profile with batt id resistor.\n");
		node = of_find_node_by_name(NULL, "lge-battery-supplement");
		name = of_property_read_string(node, "default-battery-name", &name)
				? NULL : name;
		kohm = of_property_read_u32(node, "default-battery-kohm", &kohm)
				? 0 : kohm;
		profile = of_batterydata_get_best_profile(container, kohm, name);
		pr_err("Getting default battery profile(%s): %s\n",
			name, profile ? "success" : "fail");
	}

	// At this time, 'battery_valid' may be true always for the embedded battery model
	pr_err("Getting battery profile: %s\n", profile ? "success" : "fail");
#ifdef CONFIG_MACH_MSM8996_TF10
	unified_nodes_store("battery_valid", "1", 1);
#else
	snprintf(buffer, sizeof(buffer), "%d", !!profile);
	unified_nodes_store("battery_valid", buffer, sizeof(buffer));
#endif

	return profile;
}

int extension_fg_load_icoeff_dt(struct fg_chip *fg)
{
	struct device_node* tcomp_dtroot;
	struct device_node* tcomp_override;
	int dt_icomp = 0;

	if (tcomp.icoeff_load_done) {
		pr_info("icoeff had already been loaded.\n");
		return 0;
	}

	if (!fg->soc_reporting_ready) {
		pr_info("FG profile is not ready.\n");
		return LGE_FG_INITVAL;
	}

	tcomp_dtroot = of_find_node_by_name(NULL, "lge-battery-supplement");
	if (!tcomp_dtroot) {
		pr_info("failed to find lge-battery-supplement\n");
		return LGE_FG_INITVAL;
	}

	if (fg->batt_type) {
		tcomp_override = of_find_node_by_name(
				tcomp_dtroot, fg->batt_type);
		if (tcomp_override &&
				of_property_read_u32(
					tcomp_override, "tempcomp-icoeff", &dt_icomp) >= 0)
			pr_info("ICOEFF is overridden to %d for %s\n", dt_icomp, fg->batt_type);
	}

	if (!dt_icomp) {
		if (of_property_read_u32(tcomp_dtroot, "tempcomp-icoeff", &dt_icomp) >= 0) {
			pr_info("ICOEFF is set to %d by default\n", dt_icomp);
		} else {
			pr_info("ICOEFF isn't set. error.\n");
			return -1;
		}
	}

	tcomp.icoeff = dt_icomp;
	tcomp.icoeff_load_done = true;
	return 0;
}

int extension_fg_load_dt(void)
{
	const char str_tempcomp[TCOMP_TABLE_MAX][30] = {
		"tempcomp-offset"
	};

	struct device_node* tcomp_dtroot;
	int dtarray_count = TCOMP_COUNT * 2;
	u32 dtarray_data [TCOMP_COUNT * 2];
	int i = 0, j = 0;

	if (tcomp.load_done) {
		pr_info("tcomp table had already been loaded.\n");
		return 0;
	}

	tcomp_dtroot = of_find_node_by_name(NULL, "lge-battery-supplement");
	if (!tcomp_dtroot) {
		pr_info("failed to find lge-battery-supplement\n");
		return -1;
	}

	if (of_property_read_bool(tcomp_dtroot, "tempcomp-offset-wlc-enable"))
		tcomp.load_max = 3;
	else

		tcomp.load_max = 1;

	for (j = 0; j < tcomp.load_max; j++ ) {
		/* Finding tcomp_table and tcomp_icoeff */
		if (of_property_read_u32_array(tcomp_dtroot, str_tempcomp[j],
				dtarray_data, dtarray_count) >= 0) {
			for (i = 0; i < dtarray_count; i += 2) {
				tcomp.table[j][i/2].temp_cell = dtarray_data[i];
				tcomp.table[j][i/2].temp_bias = dtarray_data[i+1];
				pr_debug("Index = %02d : %4d - %4d\n",
					i/2,
					tcomp.table[j][i/2].temp_cell,
					tcomp.table[j][i/2].temp_bias);
			}
		} else {
			pr_info("%s is not found, error\n", str_tempcomp[j]);
			tcomp.table[j][0].temp_cell = INT_MAX;
			tcomp.table[j][i/2].temp_bias = 0;
			return -1;
		}
	}

	tcomp.rise_cut = of_property_read_bool(tcomp_dtroot,
		"tempcomp-offset-wlc-rise-filter-enable");
	if (tcomp.rise_cut)
		of_property_read_u32(tcomp_dtroot,
			"tempcomp-offset-wlc-rise-filter-trigger", &tcomp.rise_cut_trig);
	tcomp.fall_cut = of_property_read_bool(tcomp_dtroot,
		"tempcomp-offset-wlc-fall-filter-enable");
	if (tcomp.fall_cut)
		of_property_read_u32(tcomp_dtroot,
			"tempcomp-offset-wlc-fall-filter-trigger", &tcomp.fall_cut_trig);

	of_property_read_u32(tcomp_dtroot,
		"capacity-raw-full", &rescale.criteria);
	rescale.lge_monotonic = of_property_read_bool(tcomp_dtroot,
		"lg-monotonic-soc-enable");
        rescale.lge_under_five_level = of_property_read_bool(tcomp_dtroot,
                                "lg-under-five-level-soc");
	tcomp.logging = of_property_read_bool(tcomp_dtroot,
		"tempcomp-logging");
	tcomp.qnovo_charging = of_property_read_bool(tcomp_dtroot,
		"tempcomp-qnovo-charging");

	if (j == tcomp.load_max) {
		tcomp.load_done = true;

		pr_info("[tempcomp config] table count: %s (%d/%d), "
			"rise cut filter: %s (trigger = %d degree), "
			"fall cut filter: %s (trigger = %d degree)\n",
			(j == tcomp.load_max) ? "done" : "error", j, tcomp.load_max,
			tcomp.rise_cut ? "enabled" : "disabled", tcomp.rise_cut_trig,
			tcomp.fall_cut ? "enabled" : "disabled", tcomp.fall_cut_trig);
	}

	return 0;
}

#ifdef CONFIG_LGE_PM_BATTID_REDETECTION
#define REDO_BATID_DURING_FIRST_EST BIT(4)
#ifdef CONFIG_MACH_MSM8996_TF10
#define BATT_ID_OPEN_KOHM	100
#endif
static int battery_id_redetection(struct fg_chip *chip)
{
	u8 reg = 0;
	int rc,id = 0;
	int unused;

	rc = fg_mem_read(chip, &reg, PROFILE_INTEGRITY_REG, 1, 0, 1);
	if (rc) {
		pr_err("failed to read profile integrity\n");
		return rc;
	}

	if (reg & PROFILE_INTEGRITY_BIT) {
		pr_err("profile has been loaded, no need to redetect id\n");
		return rc;
	}

	id = get_sram_prop_now(chip, FG_DATA_BATT_ID);
	pr_err("batt_id (boot-up): %d \n",id);

#ifdef CONFIG_MACH_MSM8996_TF10
	if (id/1000 < BATT_ID_OPEN_KOHM)
		return rc;
#endif

	fg_masked_write(chip, 0x4150,0x80, 0x80, 1); // set 0x80 to 0x4150
	fg_masked_write(chip, chip->soc_base + SOC_RESTART,0xFF, 0, 1); //clear 0x4051
	reg = REDO_BATID_DURING_FIRST_EST|REDO_FIRST_ESTIMATE;
	fg_masked_write(chip, chip->soc_base + SOC_RESTART,reg, reg, 1); //set 0x18 to 0x4051
	reg = REDO_BATID_DURING_FIRST_EST |REDO_FIRST_ESTIMATE| RESTART_GO;
	fg_masked_write(chip, chip->soc_base + SOC_RESTART,reg, reg, 1); //set 0x19 to 0x4051
	msleep(1000);
	fg_masked_write(chip, chip->soc_base + SOC_RESTART,0xFF, 0, 1); //clear 0x4051
	fg_masked_write(chip, 0x4150,0x80, 0, 1); // clear 0x4150

	update_sram_data(chip, &unused);

	id = get_sram_prop_now(chip, FG_DATA_BATT_ID);
	pr_err("batt_id (redetected): %d \n",id);

	return rc;
}
#endif