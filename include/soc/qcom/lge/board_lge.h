#ifndef __ASM_ARCH_MSM_BOARD_LGE_H
#define __ASM_ARCH_MSM_BOARD_LGE_H

#ifdef CONFIG_LGE_PM_LGE_POWER_CLASS_BOARD_REVISION
#else
#if defined(CONFIG_MACH_MSM8996_LUCYE) || defined (CONFIG_MACH_MSM8996_FALCON) || defined (CONFIG_MACH_MSM8996_TF10)
enum hw_rev_type {
	HW_REV_EVB1 = 0,
	HW_REV_EVB2,
	HW_REV_EVB3,
	HW_REV_0,
	HW_REV_0_1,
	HW_REV_0_2,
	HW_REV_0_3,
	HW_REV_0_4,
	HW_REV_A,
	HW_REV_B,
	HW_REV_C,
	HW_REV_D,
	HW_REV_1_0,
	HW_REV_1_1,
	HW_REV_1_2,
	HW_REV_1_3,
	HW_REV_1_4,
	HW_REV_1_5,
	HW_REV_1_6,
	HW_REV_MAX
};
#elif defined(CONFIG_MACH_MSM8996_ELSA) || defined(CONFIG_MACH_MSM8996_ANNA)
enum hw_rev_type {
	HW_REV_EVB1 = 0,
	HW_REV_EVB2,
	HW_REV_EVB3,
	HW_REV_0,
	HW_REV_0_1,
	HW_REV_0_2,
	HW_REV_A,
	HW_REV_B,
	HW_REV_C,
	HW_REV_D,
	HW_REV_E,
	HW_REV_F,
	HW_REV_1_0,
	HW_REV_1_1,
	HW_REV_1_2,
	HW_REV_1_3,
	HW_REV_MAX
};
#else
enum hw_rev_type {
	HW_REV_EVB1 = 0,
	HW_REV_EVB2,
	HW_REV_EVB3,
	HW_REV_0,
	HW_REV_0_1,
	HW_REV_A,
	HW_REV_B,
	HW_REV_C,
	HW_REV_D,
	HW_REV_E,
	HW_REV_F,
	HW_REV_G,
	HW_REV_1_0,
	HW_REV_1_1,
	HW_REV_1_2,
	HW_REV_1_3,
	HW_REV_MAX
};
#endif
#endif

extern char *rev_str[];

#ifdef CONFIG_LGE_PM_LGE_POWER_CLASS_BOARD_REVISION
#else
enum hw_rev_type lge_get_board_revno(void);
#endif

#if defined(CONFIG_PRE_SELF_DIAGNOSIS)
int lge_pre_self_diagnosis(char *drv_bus_code, int func_code, char *dev_code, char *drv_code, int errno);
#endif
#if defined(CONFIG_PRE_SELF_DIAGNOSIS)
struct pre_selfd_platform_data {
	int (*set_values) (int r, int g, int b);
	int (*get_values) (int *r, int *g, int *b);
};
#endif
#ifdef CONFIG_LGE_USB_FACTORY
enum lge_boot_mode_type {
	LGE_BOOT_MODE_NORMAL = 0,
	LGE_BOOT_MODE_CHARGER,
	LGE_BOOT_MODE_CHARGERLOGO,
	LGE_BOOT_MODE_QEM_56K,
	LGE_BOOT_MODE_QEM_130K,
	LGE_BOOT_MODE_QEM_910K,
	LGE_BOOT_MODE_PIF_56K,
	LGE_BOOT_MODE_PIF_130K,
	LGE_BOOT_MODE_PIF_910K,
	LGE_BOOT_MODE_MINIOS    /* LGE_UPDATE for MINIOS2.0 */
};

typedef enum {
	LGE_FACTORY_CABLE_NONE = 0,
	LGE_FACTORY_CABLE_56K,
	LGE_FACTORY_CABLE_130K,
	LGE_FACTORY_CABLE_910K,
} lge_factory_cable_t;

enum lge_laf_mode_type {
	LGE_LAF_MODE_NORMAL = 0,
	LGE_LAF_MODE_LAF,
	LGE_LAF_MODE_MID,
};

typedef enum {
	LT_CABLE_56K = 6,
	LT_CABLE_130K,
	USB_CABLE_400MA,
	USB_CABLE_DTC_500MA,
	ABNORMAL_USB_CABLE_400MA,
	LT_CABLE_910K,
	NONE_INIT_CABLE
} cable_boot_type;

typedef enum {
	CABLE_ADC_NO_INIT = 0,
	CABLE_ADC_MHL_1K,
	CABLE_ADC_U_28P7K,
	CABLE_ADC_28P7K,
	CABLE_ADC_56K,
	CABLE_ADC_100K,
	CABLE_ADC_130K,
	CABLE_ADC_180K,
	CABLE_ADC_200K,
	CABLE_ADC_220K,
	CABLE_ADC_270K,
	CABLE_ADC_330K,
	CABLE_ADC_620K,
	CABLE_ADC_910K,
	CABLE_ADC_NONE,
	CABLE_ADC_MAX,
} cable_adc_type;

typedef enum {
	NORMAL_CABLE,
	FACTORY_CABLE,
	CABLE_TYPE_MAX,
} factory_cable_type;

enum lge_laf_mode_type lge_get_laf_mode(void);
enum lge_laf_mode_type lge_get_laf_mid(void);
enum lge_boot_mode_type lge_get_boot_mode(void);
cable_boot_type lge_get_boot_cable(void);
int lge_get_android_dlcomplete(void);
int lge_get_factory_boot(void);
lge_factory_cable_t lge_get_factory_cable(void);
int get_lge_frst_status(void);
#endif

int lge_get_mfts_mode(void);

extern int lge_get_bootreason(void);
extern bool lge_check_recoveryboot(void);

extern int lge_get_bootreason_with_lcd_dimming(void);

extern int lge_get_fota_mode(void);
extern int lge_get_boot_partition_recovery(void);
extern char* lge_get_boot_partition(void);

#if defined(CONFIG_LGE_EARJACK_DEBUGGER) || defined(CONFIG_LGE_USB_DEBUGGER)
/* config */
#define UART_CONSOLE_ENABLE_ON_EARJACK		BIT(0)
#define UART_CONSOLE_ENABLE_ON_EARJACK_DEBUGGER	BIT(1)
#define UART_CONSOLE_ENABLE_ON_DEFAULT		BIT(2)
/* current status
 * ENABLED | DISABLED : logical enable/disable
 * READY : It means whether device is ready or not.
 *         So even if in ENABLED state, console output will
 *         not be emitted on NOT-ready state.
 */
#define UART_CONSOLE_ENABLED		BIT(3)
#define UART_CONSOLE_DISABLED		!(BIT(3))
#define UART_CONSOLE_READY		BIT(4)
/* filter */
# define UART_CONSOLE_MASK_ENABLE_ON	(BIT(0) | BIT(1) | BIT(2))
# define UART_CONSOLE_MASK_CONFIG	UART_CONSOLE_MASK_ENABLE_ON
# define UART_CONSOLE_MASK_ENABLED	BIT(3)
# define UART_CONSOLE_MASK_READY	BIT(4)

/* util macro */
#define lge_uart_console_should_enable_on_earjack()	\
	(unsigned int)(lge_uart_console_get_config() &	\
			UART_CONSOLE_ENABLE_ON_EARJACK)

#define lge_uart_console_should_enable_on_earjack_debugger()	\
	(unsigned int)(lge_uart_console_get_config() &		\
			UART_CONSOLE_ENABLE_ON_EARJACK_DEBUGGER)

#define lge_uart_console_should_enable_on_default()	\
	(unsigned int)(lge_uart_console_get_config() &	\
			UART_CONSOLE_ENABLE_ON_DEFAULT)

#define lge_uart_console_on_earjack_in()	\
	do {					\
		msm_serial_set_uart_console(	\
			lge_uart_console_should_enable_on_earjack());	\
	} while (0)

#define lge_uart_console_on_earjack_out()	\
	do {					\
		msm_serial_set_uart_console(	\
				lge_uart_console_should_enable_on_default()); \
	} while (0)

#define lge_uart_console_on_earjack_debugger_in()	\
	do {						\
		msm_serial_set_uart_console(		\
			lge_uart_console_should_enable_on_earjack_debugger()); \
	} while (0)

#define lge_uart_console_on_earjack_debugger_out()	\
	do {						\
		msm_serial_set_uart_console(		\
				lge_uart_console_should_enable_on_default()); \
	} while (0)

/* config =  UART_CONSOLE_ENABLE_ON_XXX [| UART_CONSOLE_ENABLE_ON_XXX]* */
extern unsigned int lge_uart_console_get_config(void);
extern void lge_uart_console_set_config(unsigned int config);

/* logical uart console status modifier
 * used as a flag to tell "I want to enable/disable uart console"
 * @RETURN or @PARAM::enabled
 * UART_CONSOLE_ENABLED  (non-zero): enabled
 * !UART_CONSOLE_ENABLED (zero): disabled
 */
extern unsigned int lge_uart_console_get_enabled(void);
extern void lge_uart_console_set_enabled(int enabled);
/* internal uart console device status tracker
 *
 * @RETURN or @PARAM::ready
 * UART_CONSOLE_READY (non-zero): device is ready
 * !UART_CONSOLE_READY (zero): device is not ready
 */
extern unsigned int lge_uart_console_get_ready(void);
extern void lge_uart_console_set_ready(unsigned int ready);

/* real device enabler (or disabler)
 * control uart console device to enable/disable
 * NOTE @PARAM::enable should be selected by uart console enable/disable policy
 * which can be known by lge_uart_console_should_enable_on_xxx.
 * @PARAM::enable
 * zero : disabled
 * non-zero : enable
 */
extern int msm_serial_set_uart_console(int enable);
extern int msm_serial_force_off(void);
#endif

#if defined(CONFIG_LGE_DISPLAY_COMMON)
int lge_get_lk_panel_status(void);
int lge_get_dsv_status(void);
int lge_get_panel(void);
void lge_set_panel(int);
#endif

#if defined(CONFIG_LGE_PANEL_MAKER_ID_SUPPORT)
enum panel_maker_id_type {
	LGD_LG4946 = 0,
	LGD_LG4945,
	LGD_S3320,
	LGD_TD4302,
	PANEL_MAKER_ID_MAX
};

enum panel_maker_id_type lge_get_panel_maker_id(void);
#endif

#if defined(CONFIG_LGE_DISPLAY_COMMON)
enum panel_revision_id_type {
	LGD_LG4946_REV0 = 0,
	LGD_LG4946_REV1,
	LGD_LG4946_REV2,
	LGD_LG4946_REV3,
	PANEL_REVISION_ID_MAX
};

enum panel_revision_id_type lge_get_panel_revision_id(void);
#endif

#ifdef CONFIG_LGE_LCD_TUNING
struct lcd_platform_data {
int (*set_values) (int *tun_lcd_t);
int (*get_values) (int *tun_lcd_t);
};

void __init lge_add_lcd_misc_devices(void);
#endif

#ifdef CONFIG_LGE_ALICE_FRIENDS
enum lge_alice_friends {
	LGE_ALICE_FRIENDS_NONE = 0,
	LGE_ALICE_FRIENDS_CM,
	LGE_ALICE_FRIENDS_HM,
	LGE_ALICE_FRIENDS_HM_B,
};

enum lge_alice_friends lge_get_alice_friends(void);
#endif

/*
 * this SKU, SUB revision, VARI main and VARI sub should be sync with
 * boot_images/QcomPkg/SDM845Pkg/Library/LGELib/boot_lge_hw_rev.h
 */
/*--------------------------------
       SKU type
--------------------------------*/
enum lge_sku_carrier_type {
	LAO = 0,
	TMUS,
	TRF_C,
	TRF_G,
	USC,
	VZW,
	TRF,
	TMUS71,
	NA_GSM,
	TRF71,
	ATT,                   // 10
	CRK,
	NA_ALL,
	NA_CDMA,
	CAN,
	MPCS71,                // 15
	SPR,
	GLOBAL,
	KR_ALL,
	HW_SKU_MAX
};

/*
* this enum and string should be sync with ntcode_op_table at
* android/bootable/bootloader/edk2/QcomModulePkg/Library/LGESharedLib/lge_one_binary.c
*/
enum lge_laop_operator_type {
  OP_OPEN_KR,
  OP_SKT_KR,
  OP_KT_KR,
  OP_LGU_KR,
  OP_ATT_US,
  OP_TMO_US,
  OP_MPCS_US,
  OP_USC_US,
  OP_CCA_LRA_ACG_US, // CCA LRA ACG have same NT code
  OP_TRF_US,
  OP_AMZ_US,  // Amazon
  OP_GFI_US,  // GoogleFi
  OP_VZW_POSTPAID,
  OP_VZW_PREPAID,
  OP_COMCAST_US,
  OP_CRK_US,
  OP_OPEN_US,
  OP_OPEN_CA, // Canada
  OP_SPR_US,
  OP_GLOBAL,  // Global, SCA, Asia, CIS, MEA...
  OP_OPEN_RU,
  OP_CHARTER_US,
  OP_INVALID, // Invalid NT Code
  OP_MAX
};

extern int on_hidden_reset;
#endif
