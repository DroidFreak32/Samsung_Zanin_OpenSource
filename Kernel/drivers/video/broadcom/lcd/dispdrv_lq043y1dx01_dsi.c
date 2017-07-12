/*******************************************************************************
* Copyright 2011 Broadcom Corporation.	All rights reserved.
*
* @file	drivers/video/broadcom/dispdrv_lq043y1dx01_dsi.c
*
* Unless you and Broadcom execute a separate written software license agreement
* governing use	of this	software, this software	is licensed to you under the
* terms	of the GNU General Public License version 2, available at
* http://www.gnu.org/copyleft/gpl.html (the "GPL").
*
* Notwithstanding the above, under no circumstances may	you combine this
* software in any way with any other Broadcom software provided	under a	license
* other	than the GPL, without Broadcom's express prior written consent.
*******************************************************************************/
#ifdef __KERNEL__

#include <linux/string.h>
#include <linux/broadcom/mobcom_types.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <plat/osabstract/osinterrupt.h>
#include <plat/osabstract/ostask.h>
#include <plat/csl/csl_dma_vc4lite.h>
#include <plat/dma_drv.h>
#include <plat/pi_mgr.h>
#include <video/kona_fb_boot.h>	   /* dispdrv init interface */
#include "display_drv.h"	   /* dispdrv interface	*/
#include <plat/csl/csl_lcd.h>	   /* dispdrv CSL Common Interface */
#include <plat/csl/csl_dsi.h>	   /* DSI CSL API */
#include "dispdrv_mipi_dcs.h"	   /* MIPI DCS */
#include "dispdrv_mipi_dsi.h"	   /* MIPI DSI */

#include <linux/spi/spi.h>
#include "dispdrv_common.h"	   /* dispdrv common */
#include "lcd_clock.h"
#include <plat/csl/csl_tectl_vc4lite.h>

#else

#include <stdio.h>
#include <string.h>
#include "dbg.h"
#include "mobcom_types.h"
#include "chip_version.h"
#include "gpio.h"
#include "platform_config.h"
#include "irqctrl.h"
#include "osinterrupt.h"
#include "ostask.h"
#include "dbg.h"
#include "logapi.h"
#include "dma_drv.h"
#include "display_drv.h"	   /* dispdrv interface	*/
#include "csl_lcd.h"		   /* dispdrv CSL Common Interface */
#include "csl_dsi.h"		   /* DSI CSL API */
#include "dispdrv_mipi_dcs.h"	   /* MIPI DCS */
#include "dispdrv_mipi_dsi.h"	   /* MIPI DSI */

#endif




#define	LQ043Y1DX01_VC		(0)
#define	LQ043Y1DX01_CMND_IS_LP	FALSE /* display init comm LP or HS mode */
#define	SHARP_BL		(95)


#if 0
#define	LCD_DBG(id, fmt, args...) printk(KERN_ERR fmt, ##args);
#endif

typedef	enum {
	LQ043Y1DX01_CMD_MIN,

	LQ043Y1DX01_CMD_SLPIN		= 0x10,
	LQ043Y1DX01_CMD_SLPOUT		= 0x11,
	LQ043Y1DX01_CMD_GAMTABLESET	= 0x26,
	LQ043Y1DX01_CMD_DISOFF		= 0x28,
	LQ043Y1DX01_CMD_DISON		= 0x29,
	LQ043Y1DX01_CMD_SOSEQCTL	= 0x36,
	LQ043Y1DX01_CMD_COLMOD		= 0x3A,
	LQ043Y1DX01_CMD_PRIV1		= 0xB9,

	LQ043Y1DX01_CMD_MAX

} LQ043Y1DX01_CMD_T;

typedef	struct {
	struct spi_device	*spi;
	CSL_LCD_HANDLE		clientH;
	CSL_LCD_HANDLE		dsiCmVcHandle;
	DISP_DRV_STATE		drvState;
	DISP_PWR_STATE		pwrState;
	UInt32			busNo;
	UInt32			teIn;
	UInt32			teOut;
	Boolean			isTE;
	DISPDRV_WIN_t		win_dim;
	DISPDRV_WIN_t		win_cur;
	struct pi_mgr_dfs_node	*dfs_node;
	/* --- */
	int			is_sharp;
	Boolean			boot_mode;
	UInt32			rst_bridge_pwr_down;
	UInt32			rst_bridge_reset;
	UInt32			rst_panel_reset;
	CSL_DSI_CM_VC_t		*cmnd_mode;
	CSL_DSI_CFG_t		*dsi_cfg;
	DISPDRV_INFO_T		*disp_info;
} LQ043Y1DX01_PANEL_t;

/* LOCAL FUNCTIONs */
static void lq043y1dx01_WrCmndP0(
	DISPDRV_HANDLE_T	drvH,
	UInt32			reg);

/* DRV INTERFACE FUNCTIONs */
static Int32 LQ043Y1DX01_Init(
	struct dispdrv_init_parms	*parms,
	DISPDRV_HANDLE_T		*handle);

static Int32 LQ043Y1DX01_Exit(DISPDRV_HANDLE_T	drvH);

static Int32 LQ043Y1DX01_Open(DISPDRV_HANDLE_T	drvH);

static Int32 LQ043Y1DX01_Close(DISPDRV_HANDLE_T drvH);

static Int32 LQ043Y1DX01_GetDispDrvFeatures(
	DISPDRV_HANDLE_T		drvH,
	const char			**driver_name,
	UInt32				*version_major,
	UInt32				*version_minor,
	DISPDRV_SUPPORT_FEATURES_T	*flags);

const DISPDRV_INFO_T *LQ043Y1DX01_GetDispDrvData(DISPDRV_HANDLE_T drvH);

static Int32 LQ043Y1DX01_Start(
	DISPDRV_HANDLE_T drvH,
	struct pi_mgr_dfs_node *dfs_node);

static Int32 LQ043Y1DX01_Stop(
	DISPDRV_HANDLE_T drvH,
	struct pi_mgr_dfs_node *dfs_node);

static Int32 LQ043Y1DX01_PowerControl(
	DISPDRV_HANDLE_T	drvH,
	DISPLAY_POWER_STATE_T	state);

static Int32 LQ043Y1DX01_Update(
	 DISPDRV_HANDLE_T	drvH,
	 void			*buff,
	 DISPDRV_WIN_t		*p_win,
	 DISPDRV_CB_T		apiCb);

static Int32 LQ043Y1DX01_WinReset(DISPDRV_HANDLE_T drvH);


static DISPDRV_T LQ043Y1DX01_Drv = {
	&LQ043Y1DX01_Init,		/* init	*/
	&LQ043Y1DX01_Exit,		/* exit	*/
	&LQ043Y1DX01_GetDispDrvFeatures,/* info	*/
	&LQ043Y1DX01_Open,		/* open	*/
	&LQ043Y1DX01_Close,		/* close */
	&LQ043Y1DX01_GetDispDrvData,	/* get_info */
	&LQ043Y1DX01_Start,		/* start */
	&LQ043Y1DX01_Stop,		/* stop	*/
	&LQ043Y1DX01_PowerControl,	/* power_control */
	NULL,				/* update_no_os	*/
	&LQ043Y1DX01_Update,		/* update */
	NULL,				/* set_brightness */
	&LQ043Y1DX01_WinReset,		/* reset_win */
};


/* DISP	DRV API	- Display Info */
static DISPDRV_INFO_T LQ043Y1DX01_Info = {
	DISPLAY_TYPE_LCD_STD,		/* type	*/
	480,				/* width */
	800,				/* height */
	DISPDRV_FB_FORMAT_RGB565,	/* input_format	*/
	DISPLAY_BUS_DSI,		/* bus_type */
	4,				/* Bpp */
};

/* DSI Command Mode VC Configuration */
CSL_DSI_CM_VC_t	lq043y1dx01_VcCmCfg = {
	LQ043Y1DX01_VC,			/* VC */
	DSI_DT_LG_DCS_WR,		/* dsiCmnd */
	MIPI_DCS_WRITE_MEMORY_START,	/* dcsCmndStart	*/
	MIPI_DCS_WRITE_MEMORY_CONTINUE,	/* dcsCmndContinue */
	FALSE,				/* isLP	*/
	LCD_IF_CM_I_RGB565P,		/* cm_in */
	LCD_IF_CM_O_RGB565,		/* cm_out */
	/* TE configuration */
	{
		DSI_TE_CTRLR_INPUT_0,	/* DSI Te Input	Type */
	},
};

/* DSI BUS CONFIGURATION */
CSL_DSI_CFG_t lq043y1dx01_dsiCfg = {
	0,		/* bus */
	1,		/* dlCount */
	DSI_DPHY_0_92,	/* DSI_DPHY_SPEC_T */
	/* ESC CLK Config */
	{500, 5},	/* escClk   500|312 500[MHz], DIV by 5 = 100[MHz] */

	/* HS CLK Config, RHEA VCO range 600-2400 */
	{1000, 2},	/* hsBitClk PLL	1000[MHz], DIV by 2 = 500[Mbps]	*/

	/* LP Speed */
	5,		/* lpBitRate_Mbps, Max 10[Mbps]	*/

	FALSE,		/* enaContClock	*/
	TRUE,		/* enaRxCrc */
	TRUE,		/* enaRxEcc */
	TRUE,		/* enaHsTxEotPkt */
	FALSE,		/* enaLpTxEotPkt */
	FALSE,		/* enaLpRxEotPkt */
};



static LQ043Y1DX01_PANEL_t   panel[1];

static const u16 sharp_sleep_out_seq[] = { LQ043Y1DX01_CMD_SLPOUT };
static const u16 sharp_sleep_in_seq[]  = { LQ043Y1DX01_CMD_SLPIN  };

static const u16 sharp_priv_seq[]	   = {
	LQ043Y1DX01_CMD_PRIV1, 0x01ff, 0x0183, 0x0163};

static const u16 sharp_cm_565_seq[]    = { LQ043Y1DX01_CMD_COLMOD, 0x0150 };
static const u16 sharp_disp_on_seq[]   = { LQ043Y1DX01_CMD_DISON  };
static const u16 sharp_disp_off_seq[]  = { LQ043Y1DX01_CMD_DISOFF };



static DISPCTRL_REC_T NT35510_AUO0430_16RGB_Init[] = {
	/* PAGE	1 */
	{DISPCTRL_WR_CMND_DATA,	0xF000,	0x55},
	{DISPCTRL_WR_CMND_DATA,	0xF001,	0xAA},
	{DISPCTRL_WR_CMND_DATA,	0xF002,	0x52},
	{DISPCTRL_WR_CMND_DATA,	0xF003,	0x08},
	{DISPCTRL_WR_CMND_DATA,	0xF004,	0x01},

	/* AVDD	*/
	{DISPCTRL_WR_CMND_DATA,	0xB000,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xB001,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xB002,	0x00},

	/* AVEE	*/
	{DISPCTRL_WR_CMND_DATA,	0xB100,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xB101,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xB102,	0x00},

	/* VCL */
	{DISPCTRL_WR_CMND_DATA,	0xB800,	0x26},
	{DISPCTRL_WR_CMND_DATA,	0xB801,	0x26},
	{DISPCTRL_WR_CMND_DATA,	0xB802,	0x26},

	/* VGH */
	{DISPCTRL_WR_CMND_DATA,	0xBF00,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xB300,	0x07},
	{DISPCTRL_WR_CMND_DATA,	0xB301,	0x07},
	{DISPCTRL_WR_CMND_DATA,	0xB302,	0x07},
	{DISPCTRL_WR_CMND_DATA,	0xB900,	0x25},
	{DISPCTRL_WR_CMND_DATA,	0xB901,	0x25},
	{DISPCTRL_WR_CMND_DATA,	0xB902,	0x25},

	/* VGLX	*/
	{DISPCTRL_WR_CMND_DATA,	0xBA00,	0x14},
	{DISPCTRL_WR_CMND_DATA,	0xBA01,	0x14},
	{DISPCTRL_WR_CMND_DATA,	0xBA02,	0x14},

	/* VCOM	*/
	{DISPCTRL_WR_CMND_DATA,	0xBE01,	0x43},

	/* Gamma Voltage */
	{DISPCTRL_WR_CMND_DATA,	0xBC01,	0xA0},
	{DISPCTRL_WR_CMND_DATA,	0xBC02,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xBD01,	0xA0},
	{DISPCTRL_WR_CMND_DATA,	0xBD02,	0x00},

	/* RED Positive	*/
	{DISPCTRL_WR_CMND_DATA,	0xD100,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD101,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD102,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD103,	0x2D},
	{DISPCTRL_WR_CMND_DATA,	0xD104,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD105,	0x55},
	{DISPCTRL_WR_CMND_DATA,	0xD106,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD107,	0x77},
	{DISPCTRL_WR_CMND_DATA,	0xD108,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD109,	0x8C},
	{DISPCTRL_WR_CMND_DATA,	0xD10A,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD10B,	0xAE},
	{DISPCTRL_WR_CMND_DATA,	0xD10C,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD10D,	0xCA},
	{DISPCTRL_WR_CMND_DATA,	0xD10E,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD10F,	0xF3},
	{DISPCTRL_WR_CMND_DATA,	0xD110,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD111,	0x14},
	{DISPCTRL_WR_CMND_DATA,	0xD112,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD113,	0x3F},
	{DISPCTRL_WR_CMND_DATA,	0xD114,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD115,	0x61},
	{DISPCTRL_WR_CMND_DATA,	0xD116,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD117,	0x95},
	{DISPCTRL_WR_CMND_DATA,	0xD118,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD119,	0xBD},
	{DISPCTRL_WR_CMND_DATA,	0xD11A,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD11B,	0xBF},
	{DISPCTRL_WR_CMND_DATA,	0xD11C,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD11D,	0xE1},
	{DISPCTRL_WR_CMND_DATA,	0xD11E,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD11F,	0x05},
	{DISPCTRL_WR_CMND_DATA,	0xD120,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD121,	0x15},
	{DISPCTRL_WR_CMND_DATA,	0xD122,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD123,	0x29},
	{DISPCTRL_WR_CMND_DATA,	0xD124,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD125,	0x3F},
	{DISPCTRL_WR_CMND_DATA,	0xD126,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD127,	0x73},
	{DISPCTRL_WR_CMND_DATA,	0xD128,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD129,	0xA3},
	{DISPCTRL_WR_CMND_DATA,	0xD12A,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD12B,	0xE1},
	{DISPCTRL_WR_CMND_DATA,	0xD12C,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD12D,	0x11},
	{DISPCTRL_WR_CMND_DATA,	0xD12E,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD12F,	0x4A},
	{DISPCTRL_WR_CMND_DATA,	0xD130,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD131,	0x66},
	{DISPCTRL_WR_CMND_DATA,	0xD132,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD133,	0x9A},


	/* Green Positive */
	{DISPCTRL_WR_CMND_DATA,	0xD200,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD201,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD202,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD203,	0x2D},
	{DISPCTRL_WR_CMND_DATA,	0xD204,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD205,	0x55},
	{DISPCTRL_WR_CMND_DATA,	0xD206,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD207,	0x77},
	{DISPCTRL_WR_CMND_DATA,	0xD208,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD209,	0x8C},
	{DISPCTRL_WR_CMND_DATA,	0xD20A,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD20B,	0xAE},
	{DISPCTRL_WR_CMND_DATA,	0xD20C,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD20D,	0xCA},
	{DISPCTRL_WR_CMND_DATA,	0xD20E,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD20F,	0xF3},
	{DISPCTRL_WR_CMND_DATA,	0xD210,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD211,	0x14},
	{DISPCTRL_WR_CMND_DATA,	0xD212,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD213,	0x3F},
	{DISPCTRL_WR_CMND_DATA,	0xD214,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD215,	0x61},
	{DISPCTRL_WR_CMND_DATA,	0xD216,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD217,	0x95},
	{DISPCTRL_WR_CMND_DATA,	0xD218,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD219,	0xBD},
	{DISPCTRL_WR_CMND_DATA,	0xD21A,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD21B,	0xBF},
	{DISPCTRL_WR_CMND_DATA,	0xD21C,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD21D,	0xE1},
	{DISPCTRL_WR_CMND_DATA,	0xD21E,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD21F,	0x05},
	{DISPCTRL_WR_CMND_DATA,	0xD220,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD221,	0x15},
	{DISPCTRL_WR_CMND_DATA,	0xD222,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD223,	0x29},
	{DISPCTRL_WR_CMND_DATA,	0xD224,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD225,	0x3F},
	{DISPCTRL_WR_CMND_DATA,	0xD226,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD227,	0x73},
	{DISPCTRL_WR_CMND_DATA,	0xD228,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD229,	0xA3},
	{DISPCTRL_WR_CMND_DATA,	0xD22A,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD22B,	0xE1},
	{DISPCTRL_WR_CMND_DATA,	0xD22C,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD22D,	0x11},
	{DISPCTRL_WR_CMND_DATA,	0xD22E,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD22F,	0x4A},
	{DISPCTRL_WR_CMND_DATA,	0xD230,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD231,	0x66},
	{DISPCTRL_WR_CMND_DATA,	0xD232,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD233,	0x9A},

	/* Blue	Positive */
	{DISPCTRL_WR_CMND_DATA,	0xD300,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD301,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD302,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD303,	0x2D},
	{DISPCTRL_WR_CMND_DATA,	0xD304,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD305,	0x55},
	{DISPCTRL_WR_CMND_DATA,	0xD306,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD307,	0x77},
	{DISPCTRL_WR_CMND_DATA,	0xD308,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD309,	0x8C},
	{DISPCTRL_WR_CMND_DATA,	0xD30A,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD30B,	0xAE},
	{DISPCTRL_WR_CMND_DATA,	0xD30C,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD30D,	0xCA},
	{DISPCTRL_WR_CMND_DATA,	0xD30E,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD30F,	0xF3},
	{DISPCTRL_WR_CMND_DATA,	0xD310,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD311,	0x14},
	{DISPCTRL_WR_CMND_DATA,	0xD312,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD313,	0x3F},
	{DISPCTRL_WR_CMND_DATA,	0xD314,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD315,	0x61},
	{DISPCTRL_WR_CMND_DATA,	0xD316,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD317,	0x95},
	{DISPCTRL_WR_CMND_DATA,	0xD318,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD319,	0xBD},
	{DISPCTRL_WR_CMND_DATA,	0xD31A,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD31B,	0xBF},
	{DISPCTRL_WR_CMND_DATA,	0xD31C,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD31D,	0xE1},
	{DISPCTRL_WR_CMND_DATA,	0xD31E,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD31F,	0x05},
	{DISPCTRL_WR_CMND_DATA,	0xD320,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD321,	0x15},
	{DISPCTRL_WR_CMND_DATA,	0xD322,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD323,	0x29},
	{DISPCTRL_WR_CMND_DATA,	0xD324,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD325,	0x3F},
	{DISPCTRL_WR_CMND_DATA,	0xD326,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD327,	0x73},
	{DISPCTRL_WR_CMND_DATA,	0xD328,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD329,	0xA3},
	{DISPCTRL_WR_CMND_DATA,	0xD32A,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD32B,	0xE1},
	{DISPCTRL_WR_CMND_DATA,	0xD32C,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD32D,	0x11},
	{DISPCTRL_WR_CMND_DATA,	0xD32E,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD32F,	0x4A},
	{DISPCTRL_WR_CMND_DATA,	0xD330,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD331,	0x66},
	{DISPCTRL_WR_CMND_DATA,	0xD332,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD333,	0x9A},

	/* Red NEG */
	{DISPCTRL_WR_CMND_DATA,	0xD400,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD401,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD402,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD403,	0x2D},
	{DISPCTRL_WR_CMND_DATA,	0xD404,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD405,	0x55},
	{DISPCTRL_WR_CMND_DATA,	0xD406,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD407,	0x77},
	{DISPCTRL_WR_CMND_DATA,	0xD408,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD409,	0x8C},
	{DISPCTRL_WR_CMND_DATA,	0xD40A,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD40B,	0xAE},
	{DISPCTRL_WR_CMND_DATA,	0xD40C,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD40D,	0xCA},
	{DISPCTRL_WR_CMND_DATA,	0xD40E,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD40F,	0xF3},
	{DISPCTRL_WR_CMND_DATA,	0xD410,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD411,	0x14},
	{DISPCTRL_WR_CMND_DATA,	0xD412,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD413,	0x3F},
	{DISPCTRL_WR_CMND_DATA,	0xD414,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD415,	0x61},
	{DISPCTRL_WR_CMND_DATA,	0xD416,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD417,	0x95},
	{DISPCTRL_WR_CMND_DATA,	0xD418,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD419,	0xBD},
	{DISPCTRL_WR_CMND_DATA,	0xD41A,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD41B,	0xBF},
	{DISPCTRL_WR_CMND_DATA,	0xD41C,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD41D,	0xE1},
	{DISPCTRL_WR_CMND_DATA,	0xD41E,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD41F,	0x05},
	{DISPCTRL_WR_CMND_DATA,	0xD420,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD421,	0x15},
	{DISPCTRL_WR_CMND_DATA,	0xD422,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD423,	0x29},
	{DISPCTRL_WR_CMND_DATA,	0xD424,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD425,	0x3F},
	{DISPCTRL_WR_CMND_DATA,	0xD426,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD427,	0x73},
	{DISPCTRL_WR_CMND_DATA,	0xD428,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD429,	0xA3},
	{DISPCTRL_WR_CMND_DATA,	0xD42A,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD42B,	0xE1},
	{DISPCTRL_WR_CMND_DATA,	0xD42C,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD42D,	0x11},
	{DISPCTRL_WR_CMND_DATA,	0xD42E,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD42F,	0x4A},
	{DISPCTRL_WR_CMND_DATA,	0xD430,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD431,	0x66},
	{DISPCTRL_WR_CMND_DATA,	0xD432,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD433,	0x9A},

	/* Green NEG */
	{DISPCTRL_WR_CMND_DATA,	0xD500,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD501,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD502,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD503,	0x2D},
	{DISPCTRL_WR_CMND_DATA,	0xD504,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD505,	0x55},
	{DISPCTRL_WR_CMND_DATA,	0xD506,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD507,	0x77},
	{DISPCTRL_WR_CMND_DATA,	0xD508,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD509,	0x8C},
	{DISPCTRL_WR_CMND_DATA,	0xD50A,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD50B,	0xAE},
	{DISPCTRL_WR_CMND_DATA,	0xD50C,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD50D,	0xCA},
	{DISPCTRL_WR_CMND_DATA,	0xD50E,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD50F,	0xF3},
	{DISPCTRL_WR_CMND_DATA,	0xD510,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD511,	0x14},
	{DISPCTRL_WR_CMND_DATA,	0xD512,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD513,	0x3F},
	{DISPCTRL_WR_CMND_DATA,	0xD514,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD515,	0x61},
	{DISPCTRL_WR_CMND_DATA,	0xD516,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD517,	0x95},
	{DISPCTRL_WR_CMND_DATA,	0xD518,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD519,	0xBD},
	{DISPCTRL_WR_CMND_DATA,	0xD51A,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD51B,	0xBF},
	{DISPCTRL_WR_CMND_DATA,	0xD51C,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD51D,	0xE1},
	{DISPCTRL_WR_CMND_DATA,	0xD51E,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD51F,	0x05},
	{DISPCTRL_WR_CMND_DATA,	0xD520,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD521,	0x15},
	{DISPCTRL_WR_CMND_DATA,	0xD522,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD523,	0x29},
	{DISPCTRL_WR_CMND_DATA,	0xD524,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD525,	0x3F},
	{DISPCTRL_WR_CMND_DATA,	0xD526,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD527,	0x73},
	{DISPCTRL_WR_CMND_DATA,	0xD528,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD529,	0xA3},
	{DISPCTRL_WR_CMND_DATA,	0xD52A,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD52B,	0xE1},
	{DISPCTRL_WR_CMND_DATA,	0xD52C,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD52D,	0x11},
	{DISPCTRL_WR_CMND_DATA,	0xD52E,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD52F,	0x4A},
	{DISPCTRL_WR_CMND_DATA,	0xD530,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD531,	0x66},
	{DISPCTRL_WR_CMND_DATA,	0xD532,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD533,	0x9A},

	/* Blue	NEG */
	{DISPCTRL_WR_CMND_DATA,	0xD600,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD601,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD602,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD603,	0x2D},
	{DISPCTRL_WR_CMND_DATA,	0xD604,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD605,	0x55},
	{DISPCTRL_WR_CMND_DATA,	0xD606,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD607,	0x77},
	{DISPCTRL_WR_CMND_DATA,	0xD608,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD609,	0x8C},
	{DISPCTRL_WR_CMND_DATA,	0xD60A,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD60B,	0xAE},
	{DISPCTRL_WR_CMND_DATA,	0xD60C,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD60D,	0xCA},
	{DISPCTRL_WR_CMND_DATA,	0xD60E,	0x00},
	{DISPCTRL_WR_CMND_DATA,	0xD60F,	0xF3},
	{DISPCTRL_WR_CMND_DATA,	0xD610,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD611,	0x14},
	{DISPCTRL_WR_CMND_DATA,	0xD612,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD613,	0x3F},
	{DISPCTRL_WR_CMND_DATA,	0xD614,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD615,	0x61},
	{DISPCTRL_WR_CMND_DATA,	0xD616,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD617,	0x95},
	{DISPCTRL_WR_CMND_DATA,	0xD618,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD619,	0xBD},
	{DISPCTRL_WR_CMND_DATA,	0xD61A,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD61B,	0xBF},
	{DISPCTRL_WR_CMND_DATA,	0xD61C,	0x01},
	{DISPCTRL_WR_CMND_DATA,	0xD61D,	0xE1},
	{DISPCTRL_WR_CMND_DATA,	0xD61E,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD61F,	0x05},
	{DISPCTRL_WR_CMND_DATA,	0xD620,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD621,	0x15},
	{DISPCTRL_WR_CMND_DATA,	0xD622,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD623,	0x29},
	{DISPCTRL_WR_CMND_DATA,	0xD624,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD625,	0x3F},
	{DISPCTRL_WR_CMND_DATA,	0xD626,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD627,	0x73},
	{DISPCTRL_WR_CMND_DATA,	0xD628,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD629,	0xA3},
	{DISPCTRL_WR_CMND_DATA,	0xD62A,	0x02},
	{DISPCTRL_WR_CMND_DATA,	0xD62B,	0xE1},
	{DISPCTRL_WR_CMND_DATA,	0xD62C,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD62D,	0x11},
	{DISPCTRL_WR_CMND_DATA,	0xD62E,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD62F,	0x4A},
	{DISPCTRL_WR_CMND_DATA,	0xD630,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD631,	0x66},
	{DISPCTRL_WR_CMND_DATA,	0xD632,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xD633,	0x9A},

	/* ENABLE PAGE 0 */
	{DISPCTRL_WR_CMND_DATA,	0xF000,	0x55},
	{DISPCTRL_WR_CMND_DATA,	0xF001,	0xAA},
	{DISPCTRL_WR_CMND_DATA,	0xF002,	0x52},
	{DISPCTRL_WR_CMND_DATA,	0xF003,	0x08},
	{DISPCTRL_WR_CMND_DATA,	0xF004,	0x00},

	/* RAM Keep */
	{DISPCTRL_WR_CMND_DATA,	0xB100,	0xCC},

	/* Z-Inversion */
	{DISPCTRL_WR_CMND_DATA,	0xBC00,	0x05},
	{DISPCTRL_WR_CMND_DATA,	0xBC01,	0x05},
	{DISPCTRL_WR_CMND_DATA,	0xBC02,	0x05},

#if 1
	/* Porch Adjust	- orig script */
	{DISPCTRL_WR_CMND_DATA,	0xBD02,	0x07},
	{DISPCTRL_WR_CMND_DATA,	0xBD03,	0x31},
	{DISPCTRL_WR_CMND_DATA,	0xBE02,	0x07},
	{DISPCTRL_WR_CMND_DATA,	0xBE03,	0x31},
	{DISPCTRL_WR_CMND_DATA,	0xBF02,	0x07},
	{DISPCTRL_WR_CMND_DATA,	0xBF03,	0x31},
#else
	/* Porch Adjust	- as per bridge */
	{DISPCTRL_WR_CMND_DATA,	0xBD02,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xBD03,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xBE02,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xBE03,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xBF02,	0x03},
	{DISPCTRL_WR_CMND_DATA,	0xBF03,	0x03},
#endif
	/* ENABLE LV3 */
	{DISPCTRL_WR_CMND_DATA,	0xFF00,	0xAA},
	{DISPCTRL_WR_CMND_DATA,	0xFF01,	0x55},
	{DISPCTRL_WR_CMND_DATA,	0xFF02,	0x25},
	{DISPCTRL_WR_CMND_DATA,	0xFF03,	0x01},

	{DISPCTRL_WR_CMND_DATA,	0xF304,	0x31},

#if 0
	/* TE ON */
	{DISPCTRL_WR_CMND_DATA,	0x3500,	0x00},	/* te on     */
#endif
	{DISPCTRL_WR_CMND_DATA,	0x3A00,	0x55},	/* colmod    */

	/* StartUp */
	{DISPCTRL_WR_CMND     ,	0x1100,	0    },	/* sleep out */
#if 0
	{DISPCTRL_SLEEP_MS    ,	0     ,	120  },
	{DISPCTRL_WR_CMND     ,	0x2900,	0    },	/* disp	on   */
	{DISPCTRL_SLEEP_MS    ,	0     ,	120  },
	{DISPCTRL_WR_CMND     ,	0x2C00,	0    },	/* memwr     */
	{DISPCTRL_SLEEP_MS    ,	0     ,	120  },
#endif
	{DISPCTRL_LIST_END    ,	0     ,	0    },
};

static DISPCTRL_REC_T dsi_bridge_init[]	= {
#if 0
	/* (TEON) VSYNC	MODE */
	{DISPCTRL_WR_CMND_DATA	, 0x35	, 0x00},
#endif
	/* (0x11) SLEEP	OUT */
	{DISPCTRL_WR_CMND	, 0x11	, 0   },
	{DISPCTRL_SLEEP_MS	, 0	, 20  },

	/* (0x40) DSIIFSEL  1= RX CmndMode */
	{DISPCTRL_WR_CMND	, 0x40	, 0   },
	{DISPCTRL_WR_DATA	, 0	, 0x01},
	/*
	 *  (0x37) DPICTL  DEF=4
	 *  B4 666 DATA	OUTPUT PACKING 0(packet	bottom)	1(packed top)
	 *  B3 PCLK  0(N) 1(P)	N=FALL EDGE P=RISING EDGE
	 *  B2 DE    0(N) 1(P)
	 *  B1 HSYNC 0(N) 1(P)
	 *  B0 VSYNC 0(N) 1(P)
	 */
	{DISPCTRL_WR_CMND	, 0x37	, 0   },
	/* V/H active low, DE active hi, PCLK falling edge, */
	/* getting only	LSBs of	colors (242)		    */
#if 0
	{DISPCTRL_WR_DATA	, 0	, 0x14},
#endif
	{DISPCTRL_WR_DATA	, 0	, 0x04},

	/* (0x92) H-TIMING */
	{DISPCTRL_WR_CMND, 0x92, 0   },
	{DISPCTRL_WR_DATA, 0,	 0x06},	/* FP */
	{DISPCTRL_WR_DATA, 0,	 0x06},	/* S  */
	{DISPCTRL_WR_DATA, 0,	 0x0F},	/* BP */
	{DISPCTRL_WR_DATA, 0,	 0x00},
	{DISPCTRL_WR_DATA, 0,	 0x01},
	{DISPCTRL_WR_DATA, 0,	 0xE0},

#if 1
	/* (0x8B) V-TIMING - AUO adjusted */
	{DISPCTRL_WR_CMND, 0x8B, 0   },
	{DISPCTRL_WR_DATA, 0,	 0x31},	/* FP */
	{DISPCTRL_WR_DATA, 0,	 0x03},	/* S  */
	{DISPCTRL_WR_DATA, 0,	 0x07},	/* BP */
	{DISPCTRL_WR_DATA, 0,	 0x00},
	{DISPCTRL_WR_DATA, 0,	 0x03},
	{DISPCTRL_WR_DATA, 0,	 0x20},
#else
	/* (0x8B) V-TIMING - orig SHARP */
	{DISPCTRL_WR_CMND, 0x8B, 0   },
	{DISPCTRL_WR_DATA, 0,	 0x03},	/* FP */
	{DISPCTRL_WR_DATA, 0,	 0x03},	/* S  */
	{DISPCTRL_WR_DATA, 0,	 0x03},	/* BP */
	{DISPCTRL_WR_DATA, 0,	 0x00},
	{DISPCTRL_WR_DATA, 0,	 0x03},
	{DISPCTRL_WR_DATA, 0,	 0x20},
#endif
	/* PCLK	24.02Mhz @ 32KHz */
	{DISPCTRL_WR_CMND, 0xA0, 0   },	/* A0=PLNR */
	{DISPCTRL_WR_DATA, 0,	 0x00},

	{DISPCTRL_WR_CMND, 0xA2, 0   },	/* A2=PLL2NF1(LSB) */
	{DISPCTRL_WR_DATA, 0,	 0xDC},

	{DISPCTRL_WR_CMND, 0xA4, 0   },	/* A4=PLL2NF2(MSB) */
	{DISPCTRL_WR_DATA, 0,	 0x02},

	{DISPCTRL_WR_CMND, 0xA6, 0   },	/* A6=PLL2BWADJ1(LSB) */
	{DISPCTRL_WR_DATA, 0,	 0xDC},

	{DISPCTRL_WR_CMND, 0xA8, 0   },	/* A8=PLL2BWADJ2(MSB) */
	{DISPCTRL_WR_DATA, 0,	 0x02},

	{DISPCTRL_WR_CMND, 0xAA, 0   },	/* AA=PLL2CTL pll2prediv0 */
	{DISPCTRL_WR_DATA, 0,	 0x00},

	/* (0x3A) COLMOD */
	{DISPCTRL_WR_CMND, 0x3A, 0   },
	{DISPCTRL_WR_DATA, 0,	 0x05},

	/* (0x36) MADCTL */
	{DISPCTRL_WR_CMND, 0x36, 0   },
	{DISPCTRL_WR_DATA, 0,	 0x00},

	/* (0x82) DBIOC	*/
	{DISPCTRL_WR_CMND, 0x82, 0   },
	{DISPCTRL_WR_DATA, 0,	 0x00},

	/* CASET */
	{DISPCTRL_WR_CMND, 0x2A, 0   },
	{DISPCTRL_WR_DATA, 0,	 0x00}, /* CA-Start MSB */
	{DISPCTRL_WR_DATA, 0,	 0x00},
	{DISPCTRL_WR_DATA, 0,	 0x00},
	{DISPCTRL_WR_DATA, 0,	 0x00}, /* CA-End   MSB */
	{DISPCTRL_WR_DATA, 0,	 0x01},
	{DISPCTRL_WR_DATA, 0,	 0xDF},


	/* PASET */
	{DISPCTRL_WR_CMND, 0x2B, 0   },
	{DISPCTRL_WR_DATA, 0,	 0x00}, /* PA-Start MSB */
	{DISPCTRL_WR_DATA, 0,	 0x00},
	{DISPCTRL_WR_DATA, 0,	 0x00},
	{DISPCTRL_WR_DATA, 0,	 0x00}, /* PA-End   MSB */
	{DISPCTRL_WR_DATA, 0,	 0x03},
	{DISPCTRL_WR_DATA, 0,	 0x1F},

	/* WRTEVSOT TE pulse config - verify */
	{DISPCTRL_WR_CMND, 0x84, 0   },
	{DISPCTRL_WR_DATA, 0,	 0x00}, /* Falling Edge MSB */
	{DISPCTRL_WR_DATA, 0,	 0x03},
	{DISPCTRL_WR_DATA, 0,	 0x16},
	{DISPCTRL_WR_DATA, 0,	 0x00}, /* Rising Edge  MSB */
	{DISPCTRL_WR_DATA, 0,	 0x03},
	{DISPCTRL_WR_DATA, 0,	 0x19},

	/* DSIRXCTL enable TE output */
	{DISPCTRL_WR_CMND_DATA,	0x41, 1	},
	{DISPCTRL_WR_CMND_DATA,	MIPI_DCS_SET_TEAR_ON, 0	},

	/* DPI Output (TX) ON */
	{DISPCTRL_WR_CMND, 0x81, 0   },
	{DISPCTRL_SLEEP_MS, 0,	 20  },

	{DISPCTRL_LIST_END, 0,	 0   },
};

/*###########################################################################*/



static int lq043y1dx01_send_cmd(
	struct spi_device	*spi,
	const u16		*cmd_buff,
	u32			byte_count)
{
	if (spi	== NULL) {
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	spi EQ NULL\n",
			__func__);
		return -1;
	}
	spi_write(spi, (const u8 *)cmd_buff, byte_count);
	return 0;
}

/*
 * R/nW	 = 0 Write	 0x80 =	READ
 * D/nC	 = 0 Command	 0x40 =	DATA
 * H/nL	 = 0 LO	Byte	 0x20 =	HI BYTE

 * 16-bit register addr	write 1B
 * 0x20,REG_HI,	 0x00,REG_LO, 0x40, Tx DATA_1B
 * 16-bit register addr	read  1B
 * 0x20,REG_HI,	 0x00,REG_LO, 0xC0, Rx DATA_1B
 */

static int spi08_write_reg_data(
	struct spi_device	*spi,
	u32			reg,
	u32			data)
{
	u8 spi_data[6] = {0x20,	0x00, 0x00, 0x00, 0x40,	0x00};

	spi_data[1] = (reg  & 0x0000FF00) >> 8;
	spi_data[3] = (reg  & 0x000000FF);
	spi_data[5] = (data & 0x000000FF);
	spi_write(spi, (u8 *)spi_data, 6);

	return 0;
}

static int spi08_write_reg(
	struct spi_device	*spi,
	u32			reg)
{
	u8 spi_data[] =	{0x20, 0x00, 0x00, 0x00};

	spi_data[1] = (reg  & 0x0000FF00) >> 8;
	spi_data[3] = (reg  & 0x000000FF);
	spi_write(spi, (u8 *)spi_data, 4);

	return 0;
}

static void sharp_init(LQ043Y1DX01_PANEL_t *pPanel)
{
	lq043y1dx01_send_cmd(pPanel->spi, sharp_sleep_out_seq,
		ARRAY_SIZE(sharp_sleep_out_seq)*sizeof(u16));

	lq043y1dx01_send_cmd(pPanel->spi, sharp_cm_565_seq,
		ARRAY_SIZE(sharp_cm_565_seq)*sizeof(u16));
}

static void sharp_on(LQ043Y1DX01_PANEL_t *pPanel)
{
	lq043y1dx01_send_cmd(pPanel->spi, sharp_disp_on_seq,
		ARRAY_SIZE(sharp_disp_on_seq)*sizeof(u16));
}

static void sharp_off(LQ043Y1DX01_PANEL_t *pPanel)
{
	lq043y1dx01_send_cmd(pPanel->spi, sharp_disp_off_seq,
		ARRAY_SIZE(sharp_disp_off_seq)*sizeof(u16));
}

static void sharp_sleep_in(LQ043Y1DX01_PANEL_t *pPanel)
{
	lq043y1dx01_send_cmd(pPanel->spi, sharp_sleep_in_seq,
		ARRAY_SIZE(sharp_sleep_in_seq)*sizeof(u16));
}

static void sharp_sleep_out(LQ043Y1DX01_PANEL_t	*pPanel)
{
	lq043y1dx01_send_cmd(pPanel->spi, sharp_sleep_out_seq,
		ARRAY_SIZE(sharp_sleep_out_seq)*sizeof(u16));

}

static void auo_on(LQ043Y1DX01_PANEL_t *pPanel)
{
	spi08_write_reg(pPanel->spi, 0x2900);		    /* disp on	 */
}
static void auo_off(LQ043Y1DX01_PANEL_t	*pPanel)
{
	spi08_write_reg(pPanel->spi, 0x2800);		    /* disp  off */
}

static void auo_sleep_in(LQ043Y1DX01_PANEL_t *pPanel)
{
	spi08_write_reg(pPanel->spi, 0x1000);		    /* sleep in	 */
}

static void auo_sleep_out(LQ043Y1DX01_PANEL_t *pPanel)
{
	spi08_write_reg(pPanel->spi, 0x1100);		    /* sleep out */
}



/*
 *
 *   Function Name: auo_send_cmnd_seq
 *
 *   Description:
 *
 */
static void auo_send_cmnd_seq(
	DISPDRV_HANDLE_T     drvH,
	pDISPCTRL_REC_T	     cmnd_lst)
{

	LQ043Y1DX01_PANEL_t   *pPanel =	(LQ043Y1DX01_PANEL_t *)drvH;
	UInt32	i = 0;

	while (cmnd_lst[i].type	!= DISPCTRL_LIST_END) {
		if (cmnd_lst[i].type ==	DISPCTRL_WR_CMND) {
			spi08_write_reg(pPanel->spi, cmnd_lst[i].cmnd);
		} else if (cmnd_lst[i].type == DISPCTRL_WR_CMND_DATA) {
			spi08_write_reg_data(pPanel->spi, cmnd_lst[i].cmnd,
				cmnd_lst[i].data);
		} else if (cmnd_lst[i].type == DISPCTRL_SLEEP_MS) {
			OSTASK_Sleep(TICKS_IN_MILLISECONDS(cmnd_lst[i].data));
		}
		i++;
	}
}

static void lq043y1dx01_panel_on(LQ043Y1DX01_PANEL_t *pPanel)
{
	if (pPanel->is_sharp)
		sharp_on(pPanel);
	else
		auo_on(pPanel);
}

static void lq043y1dx01_panel_off(LQ043Y1DX01_PANEL_t *pPanel)
{
	if (pPanel->is_sharp)
		sharp_off(pPanel);
	else
		auo_off(pPanel);
}

static void lq043y1dx01_panel_sleep_in(LQ043Y1DX01_PANEL_t *pPanel)
{
	if (pPanel->is_sharp)
		sharp_sleep_in(pPanel);
	else
		auo_sleep_in(pPanel);
}

static void lq043y1dx01_panel_sleep_out(LQ043Y1DX01_PANEL_t *pPanel)
{
	if (pPanel->is_sharp)
		sharp_sleep_out(pPanel);
	else
		auo_sleep_out(pPanel);
}


static void lq043y1dx01_panel_init(LQ043Y1DX01_PANEL_t *pPanel)
{
	if (pPanel->is_sharp)
		sharp_init(pPanel);
	else
		auo_send_cmnd_seq(pPanel, NT35510_AUO0430_16RGB_Init);
}


/*
 *
 *   Function Name: lq043y1dx01_TeOn
 *
 *   Description:   Configure TE Input Pin & Route it to DSI Controller	Input
 *
 */
static int lq043y1dx01_TeOn(LQ043Y1DX01_PANEL_t	*pPanel)
{
	Int32	    res	= 0;

	TECTL_CFG_t teCfg;

	teCfg.te_mode	  = TE_VC4L_MODE_VSYNC;
	teCfg.sync_pol	  = TE_VC4L_ACT_POL_LO;
	teCfg.vsync_width = 0;
	teCfg.hsync_line  = 0;

	res = CSL_TECTL_VC4L_OpenInput(pPanel->teIn, pPanel->teOut, &teCfg);

	return res;
}

/*
 *
 *   Function Name: lq043y1dx01_TeOff
 *
 *   Description:   'Release' TE Input Pin Used
 *
 */
static int lq043y1dx01_TeOff(LQ043Y1DX01_PANEL_t *pPanel)
{
	Int32  res = 0;

	res = CSL_TECTL_VC4L_CloseInput(pPanel->teIn);

	return res;
}


/*
 *
 *   Function Name: lq043y1dx01_WrCmndP0
 *
 *   Description:
 *
 */
static void lq043y1dx01_WrCmndP0(
	DISPDRV_HANDLE_T    drvH,
	UInt32		    reg)
{
	LQ043Y1DX01_PANEL_t	*pPanel	= (LQ043Y1DX01_PANEL_t *)drvH;
	CSL_DSI_CMND_t		msg;
	UInt8			msgData[4];

	msg.dsiCmnd    = DSI_DT_SH_DCS_WR_P0;
	msg.msg	       = &msgData[0];
	msg.msgLen     = 1;
	msg.vc	       = LQ043Y1DX01_VC;
	msg.isLP       = LQ043Y1DX01_CMND_IS_LP;
	msg.isLong     = FALSE;
	msg.endWithBta = FALSE;

	msgData[0] = reg;
	msgData[1] = 0;

	CSL_DSI_SendPacket(pPanel->clientH, &msg, FALSE);
}

#if 0
/*
 *
 *   Function Name: lq043y1dx01_ReadReg
 *
 *   Description:   DSI	Read Reg
 *
 */
static int lq043y1dx01_ReadReg(DISPDRV_HANDLE_T	drvH, UInt8 reg)
{
	LQ043Y1DX01_PANEL_t		*pPanel	= (LQ043Y1DX01_PANEL_t *)drvH;
	CSL_DSI_CMND_t			msg;
	CSL_DSI_REPLY_t			rxMsg;	    /* DSI RX message */
	UInt8				txData[1];  /* DCS Rd Command */
	UInt8				rxBuff[1];  /* Read Buffer    */
	Int32				res = 0;
	CSL_LCD_RES_T			cslRes;

	msg.dsiCmnd    = DSI_DT_SH_DCS_RD_P0;
	msg.msg	       = &txData[0];
	msg.msgLen     = 1;
	msg.vc	       = LQ043Y1DX01_VC;
	msg.isLP       = FALSE;
	msg.isLong     = FALSE;
	msg.endWithBta = TRUE;

	rxMsg.pReadReply = (UInt8 *)&rxBuff[0];
	msg.reply	 = (CSL_DSI_REPLY_t *)&rxMsg;

	txData[0] = reg;
	cslRes = CSL_DSI_SendPacket(pPanel->clientH, &msg, FALSE);
	if ((cslRes != CSL_LCD_OK) ||
		((rxMsg.type & DSI_RX_TYPE_READ_REPLY) == 0)) {

		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	ERR"
			"Reading From Reg[0x%08X]\n\r",
			__func__, reg);
		res = -1;
	} else {
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	 OK"
			"Reg[0x%08X] Val[0x%08X]\n\r",
			__func__, reg, rxBuff[0]);
	}

	return res;
}
#endif

/*
 *
 *   Function Name: lq043y1dx01_WinSet
 *
 *   Description:   Set	Window
 *
 */
Int32 lq043y1dx01_WinSet(
	DISPDRV_HANDLE_T	drvH,
	Boolean			update,
	DISPDRV_WIN_t		*p_win)
{
	LQ043Y1DX01_PANEL_t *pPanel = (LQ043Y1DX01_PANEL_t *) drvH;
	CSL_DSI_CMND_t	    msg;
	UInt8		    msgData[7];
	CSL_DSI_REPLY_t	    rxMsg;
	UInt8		    rx_buff[8];

	if ((pPanel->win_cur.l != p_win->l) ||
	    (pPanel->win_cur.r != p_win->r) ||
	    (pPanel->win_cur.t != p_win->t) ||
	    (pPanel->win_cur.b != p_win->b)) {

		pPanel->win_cur	= *p_win;

		if (update) {
			rxMsg.pReadReply = rx_buff;

			msg.dsiCmnd    = DSI_DT_LG_DCS_WR;
			msg.msg	       = &msgData[0];
			msg.msgLen     = 7;
			msg.vc	       = LQ043Y1DX01_VC;
			msg.isLP       = LQ043Y1DX01_CMND_IS_LP;
			msg.isLong     = TRUE;
			msg.endWithBta = FALSE;
			msg.reply      = &rxMsg;

			msgData[0] = MIPI_DCS_SET_COLUMN_ADDRESS;
			msgData[1] = 0;
			msgData[2] = (p_win->l & 0xFF00) >> 8;
			msgData[3] = (p_win->l & 0x00FF);
			msgData[4] = 0;
			msgData[5] = (p_win->r & 0xFF00) >> 8;
			msgData[6] = (p_win->r & 0x00FF);

			CSL_DSI_SendPacket(pPanel->clientH, &msg, FALSE);

			msgData[0] = MIPI_DCS_SET_PAGE_ADDRESS;
			msgData[1] = 0;
			msgData[2] = (p_win->t & 0xFF00) >> 8;
			msgData[3] = (p_win->t & 0x00FF);
			msgData[4] = 0;
			msgData[5] = (p_win->b & 0xFF00) >> 8;
			msgData[6] = (p_win->b & 0x00FF);

			CSL_DSI_SendPacket(pPanel->clientH, &msg, FALSE);

			return 0;
		}
	}
	return 0;
}

/*
 *
 *   Function Name: LQ043Y1DX01_WinReset
 *
 *   Description:   Reset	windowing to full screen size.
 *		    Typically, only used in boot code environment
 */
Int32 LQ043Y1DX01_WinReset(DISPDRV_HANDLE_T drvH)
{
	Int32 res;
	LQ043Y1DX01_PANEL_t *pPanel = (LQ043Y1DX01_PANEL_t *) drvH;

	res = lq043y1dx01_WinSet(drvH, TRUE, &pPanel->win_dim);
	return res;
}

/*
 *
 *   Function Name: lq043y1dx01_reset
 *
 *   Description:   (De)Assert display reset
 *
 */
static void lq043y1dx01_reset(DISPDRV_HANDLE_T drvH, Boolean on)
{
	LQ043Y1DX01_PANEL_t *pPanel = (LQ043Y1DX01_PANEL_t *) drvH;


	if (!on) {
		gpio_request(pPanel->rst_panel_reset  ,	"LCD_RST1");
		gpio_request(pPanel->rst_bridge_reset ,	"LCD_RST2");
		gpio_request(pPanel->rst_bridge_pwr_down, "LCD_RST3");

		gpio_direction_output(pPanel->rst_panel_reset  , 1);
		gpio_direction_output(pPanel->rst_bridge_reset , 1);
		gpio_direction_output(pPanel->rst_bridge_pwr_down, 1);
		msleep(1);

		gpio_set_value_cansleep(pPanel->rst_panel_reset,   0);
		gpio_set_value_cansleep(pPanel->rst_bridge_pwr_down, 0);
		msleep(100);
		gpio_set_value_cansleep(pPanel->rst_panel_reset,   1);

		gpio_set_value_cansleep(pPanel->rst_bridge_reset, 0);
		msleep(1);
		gpio_set_value_cansleep(pPanel->rst_bridge_reset, 1);
		msleep(20);
	} else {
		gpio_set_value_cansleep(pPanel->rst_panel_reset,   0);
		gpio_set_value_cansleep(pPanel->rst_bridge_pwr_down, 1);
		gpio_set_value_cansleep(pPanel->rst_bridge_reset, 0);
	}
}

/*
 *
 *   Function Name: LCD_DRV_LQ043Y1DX01_GetDrvInfo
 *
 *   Description:   Get	Driver Funtion Table
 *
 */
DISPDRV_T *DISP_DRV_LQ043Y1DX01_GetFuncTable(void)
{
	return &LQ043Y1DX01_Drv;
}

/*
 *
 *   Function Name: LQ043Y1DX01_Init
 *
 *   Description:   Setup / Verify display driver init interface
 *
 */
Int32 LQ043Y1DX01_Init(
	struct dispdrv_init_parms	*parms,
	DISPDRV_HANDLE_T		*handle)
{
	Int32 res = 0;
	LQ043Y1DX01_PANEL_t	*pPanel;

	pPanel = &panel[0];

	if (pPanel->drvState ==	DRV_STATE_OFF)	{

		if (pPanel->spi == NULL) {
			LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: SPI Drv Not "
				"Init\n", __func__);
			return -1;
		}

		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	Bus	    %d\n",
			__func__, parms->w0.bits.bus_type);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	BootMode   %d\n",
			__func__, parms->w0.bits.boot_mode);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	BusNo	    %d\n",
			__func__, parms->w0.bits.bus_no);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	col_mode_i %d\n",
			__func__, parms->w0.bits.col_mode_i);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	col_mode_o %d\n",
			__func__, parms->w0.bits.col_mode_o);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	te_input   %d\n",
			__func__, parms->w0.bits.te_input);

		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	API Rev	   %d\n",
			__func__, parms->w1.bits.api_rev);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	Rst 0	    %d\n",
			__func__, parms->w1.bits.lcd_rst0);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	Rst 1	    %d\n",
			__func__, parms->w1.bits.lcd_rst1);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	Rst 2	    %d\n",
			__func__, parms->w1.bits.lcd_rst2);

		if ((u8)parms->w1.bits.api_rev != RHEA_LCD_BOOT_API_REV) {
			LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	"
				"Boot Init API Rev Mismatch(%d.%d vs %d.%d)\n",
				__func__,
				(parms->w1.bits.api_rev	& 0xF0)	>> 8,
				(parms->w1.bits.api_rev	& 0x0F)	    ,
				(RHEA_LCD_BOOT_API_REV	& 0xF0)	>> 8,
				(RHEA_LCD_BOOT_API_REV	& 0x0F));
			return -1;
		}

		pPanel->boot_mode = parms->w0.bits.boot_mode;

		pPanel->cmnd_mode = &lq043y1dx01_VcCmCfg;
		pPanel->dsi_cfg	  = &lq043y1dx01_dsiCfg;
		pPanel->disp_info = &LQ043Y1DX01_Info;

		pPanel->busNo =	dispdrv2busNo(parms->w0.bits.bus_no);

		/* check for valid DSI bus no */
		if (pPanel->busNo & 0xFFFFFFFE)	{
			LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	Invlid DSI "
				"BusNo[%lu]\n",	__func__, pPanel->busNo);
			return -1;
		}

		pPanel->cmnd_mode->cm_in  =
			dispdrv2cmIn(parms->w0.bits.col_mode_i);
		pPanel->cmnd_mode->cm_out =
			dispdrv2cmOut(parms->w0.bits.col_mode_o);

		/* as of now, only 565 */
		switch (pPanel->cmnd_mode->cm_in) {
		case LCD_IF_CM_I_RGB565P:
			pPanel->disp_info->input_format
				= DISPDRV_FB_FORMAT_RGB565;
			pPanel->disp_info->Bpp = 2;
			break;
		case LCD_IF_CM_I_RGB888U:
		default:
			LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	Unsupported"
				" Color	Mode\n", __func__);
			return -1;
		}

		/* get reset pins */
		pPanel->rst_bridge_pwr_down = parms->w1.bits.lcd_rst0;
		pPanel->rst_bridge_reset  = parms->w1.bits.lcd_rst1;
		pPanel->rst_panel_reset	  = parms->w1.bits.lcd_rst2;


		pPanel->isTE = pPanel->cmnd_mode->teCfg.teInType != DSI_TE_NONE;

		/* get TE pin configuration */
		pPanel->teIn  =	dispdrv2busTE(parms->w0.bits.te_input);
		pPanel->teOut =	pPanel->busNo
			? TE_VC4L_OUT_DSI1_TE0 : TE_VC4L_OUT_DSI0_TE0;

		pPanel->drvState = DRV_STATE_INIT;
		pPanel->pwrState = STATE_PWR_OFF;

		*handle	= (DISPDRV_HANDLE_T)pPanel;

		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	OK\n",	__func__);
	} else {
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	Not in OFF state\n",
			__func__);
		res = -1;
	}

	return res;
}

/*
 *
 *   Function Name: LQ043Y1DX01_Exit
 *
 *   Description:
 *
 */
Int32 LQ043Y1DX01_Exit(DISPDRV_HANDLE_T	drvH)
{
	LQ043Y1DX01_PANEL_t *pPanel;

	pPanel = (LQ043Y1DX01_PANEL_t *) drvH;
	pPanel->drvState = DRV_STATE_OFF;
	return 0;
}



/*
 *
 *  Function Name: lq043y1dx01_detect_panel
 *
 *  Description:   Detect Panel	Attached
 *
 */
static int lq043y1dx01_detect_panel(DISPDRV_HANDLE_T drvH)
{

	LQ043Y1DX01_PANEL_t *pPanel = (LQ043Y1DX01_PANEL_t *) drvH;
	int err;
	static int to_detect = 1;

	u16 COL_MOD[] =	{0x0C};
	u16 read_buff;
	u8  colmod;

	LCD_DBG(LCD_DBG_ID, "[DISPDRV] %s\n", __func__);

	if (to_detect) {
		to_detect = 0;
		pPanel->spi->bits_per_word = 9;
		pPanel->spi->mode = SPI_MODE_3;
		err = spi_setup(pPanel->spi);
		if (err) {
			LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: SPI Setup Err"
				" (9 B Mode 3)\n", __func__);
			goto fail;
		}

		spi_write_then_read(pPanel->spi, (u8 *)COL_MOD,	2,
			(void *) &read_buff, 2);

		colmod = read_buff >> 1;

		if ((colmod != 0x00) &&	(colmod	!= 0xFF)) {
			LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: "
				"USING SHARP(0x%02X)\n", __func__, colmod);
			pPanel->is_sharp = 1;
		} else {
			gpio_set_value_cansleep(pPanel->rst_panel_reset, 0);
			msleep(1);
			gpio_set_value_cansleep(pPanel->rst_panel_reset, 1);
			msleep(1);

			pPanel->spi->bits_per_word = 8;
			pPanel->spi->mode = SPI_MODE_0;
			err = spi_setup(pPanel->spi);
			if (err) {
				LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: SPI "
					"Setup Err (8B Mode 0)\n", __func__);
				goto fail;
			}
			LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: USING AUO\n",
				__func__);
			pPanel->is_sharp = 0;
		}
	}
	return 0;

fail:
	LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: Failed To Detect	The Panel\n",
		__func__);
	return -1;
}



/*
 *
 *  Function Name: LQ043Y1DX01_Open
 *
 *  Description:   disp	bus ON
 *
 */
Int32 LQ043Y1DX01_Open(DISPDRV_HANDLE_T	drvH)
{
	Int32				res = 0;
	LQ043Y1DX01_PANEL_t		*pPanel;

	pPanel = (LQ043Y1DX01_PANEL_t *) drvH;



	if (pPanel->drvState !=	DRV_STATE_INIT)	{
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: ERROR State != Init\n",
			__func__);
		return -1;
	}

	if (brcm_enable_dsi_pll_clocks(pPanel->busNo,
		pPanel->dsi_cfg->hsBitClk.clkIn_MHz * 1000000,
		pPanel->dsi_cfg->hsBitClk.clkInDiv,
		pPanel->dsi_cfg->escClk.clkIn_MHz   * 1000000
		/ pPanel->dsi_cfg->escClk.clkInDiv)) {

		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	ERROR to enable	the "
			"clock\n", __func__);
	}

	if (pPanel->isTE && lq043y1dx01_TeOn(pPanel) ==	-1) {
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	"
			"Failed	To Configure TE	Input\n", __func__);
		goto err_te_on;
	}

	if (CSL_DSI_Init(pPanel->dsi_cfg) != CSL_LCD_OK) {
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	ERROR, DSI CSL Init "
			"Failed\n", __func__);
		goto err_dsi_init;
	}

	if (CSL_DSI_OpenClient(pPanel->busNo, &pPanel->clientH)	!= CSL_LCD_OK) {
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	ERROR, "
			"CSL_DSI_OpenClient Failed\n", __func__);
		goto err_dsi_open_cl;
	}

	if (CSL_DSI_OpenCmVc(pPanel->clientH,
		pPanel->cmnd_mode, &pPanel->dsiCmVcHandle) != CSL_LCD_OK) {

		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	CSL_DSI_OpenCmVc "
			"Failed\n", __func__);
		goto err_dsi_open_cm;
	}

	if (csl_dma_vc4lite_init() != DMA_VC4LITE_STATUS_SUCCESS) {
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: csl_dma_vc4lite_init "
			"Failed\n", __func__);
		goto err_dma_init;
	}

	lq043y1dx01_reset(drvH,	FALSE);

	if (-1 == lq043y1dx01_detect_panel(drvH))
		goto err_dma_init;

	pPanel->win_dim.l = 0;
	pPanel->win_dim.r = pPanel->disp_info->width-1;
	pPanel->win_dim.t = 0;
	pPanel->win_dim.b = pPanel->disp_info->height-1;
	pPanel->win_dim.w = pPanel->disp_info->width;
	pPanel->win_dim.h = pPanel->disp_info->height;

	pPanel->drvState   = DRV_STATE_OPEN;

	LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	OK\n\r", __func__);

	return res;

err_dma_init:
	CSL_DSI_CloseCmVc(pPanel->dsiCmVcHandle);
err_dsi_open_cm:
	CSL_DSI_CloseClient(pPanel->clientH);
err_dsi_open_cl:
	CSL_DSI_Close(pPanel->busNo);
err_dsi_init:
	if (pPanel->isTE)
		lq043y1dx01_TeOff(pPanel);
err_te_on:
	return -1;
}

/*
 *
 *  Function Name: LQ043Y1DX01_Close
 *
 *  Description:   disp	bus OFF
 *
 */
Int32 LQ043Y1DX01_Close(DISPDRV_HANDLE_T drvH)
{
	Int32			res = 0;
	LQ043Y1DX01_PANEL_t	*pPanel	= (LQ043Y1DX01_PANEL_t *)drvH;

	if (CSL_DSI_CloseCmVc(pPanel->dsiCmVcHandle)) {
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: ERROR,	"
			"Closing Command Mode Handle\n\r", __func__);
		return -1;
	}

	if (CSL_DSI_CloseClient(pPanel->clientH) != CSL_LCD_OK)	{
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	ERROR, Closing "
			"DSI Client\n",	__func__);
		return -1;
	}

	if (CSL_DSI_Close(pPanel->busNo) != CSL_LCD_OK)	{
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: ERR Closing DSI "
			"Controller\n",	__func__);
		return -1;
	}

	if (pPanel->isTE)
		lq043y1dx01_TeOff(pPanel);

	if (brcm_disable_dsi_pll_clocks(pPanel->busNo))	{
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: ERROR to	disable	"
			"the pll clock\n", __func__);
		return -1;
	}

	pPanel->pwrState = STATE_PWR_OFF;
	pPanel->drvState = DRV_STATE_INIT;
	LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	OK\n\r", __func__);

	return res;
}


/*
 *
 * Function Name:  lq043y1dx01_WrSendCmnd
 *
 * Parameters:	   msg_buff   =	TX byte	buffer
 *		   msg_size   =	size of	message	to be sent [bytes]
 *
 * Description:	   Send	commands with variable no of parms
 *		   Assumption: DISPLAY's Controller Accepts DT Used
 */
static int lq043y1dx01_WrSendCmnd(
	DISPDRV_HANDLE_T	drvH,
	UInt8			*msg_buff,
	UInt32			msg_size)
{
	LQ043Y1DX01_PANEL_t	*pPanel	= (LQ043Y1DX01_PANEL_t *)drvH;
	CSL_DSI_CMND_t		msg;
	int			res = 0;

	switch (msg_size) {
	case 0:
		res = -1;
		break;
	case 1:
		msg.dsiCmnd = DSI_DT_SH_DCS_WR_P0;
		break;
	case 2:
		msg.dsiCmnd = DSI_DT_SH_DCS_WR_P1;
		break;
	default:
		if (msg_size <=	CSL_DSI_GetMaxTxMsgSize())
			msg.dsiCmnd = DSI_DT_LG_DCS_WR;
		else
			res = -1;
		break;
	}

	if (res	== 0) {
		msg.msg	       = msg_buff;
		msg.msgLen     = msg_size;
		msg.vc	       = LQ043Y1DX01_VC;
		msg.isLP       = LQ043Y1DX01_CMND_IS_LP;
		msg.isLong     = msg_size > 2;
		msg.endWithBta = FALSE;

		CSL_DSI_SendPacket(pPanel->clientH, &msg, FALSE);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	DT[0x%02lX] "
			"SIZE[%lu]\n",	__func__, msg.dsiCmnd, msg_size);
	}

	return res;
}


/*
 *
 *   Function Name:   lq043y1dx01_ExecCmndList
 *
 *   Description:
 *
 */
static void lq043y1dx01_ExecCmndList(
	DISPDRV_HANDLE_T     drvH,
	pDISPCTRL_REC_T	     cmnd_lst)
{

	#define	TX_MSG_MAX	32
	UInt32	i = 0;

	UInt8	tx_buff[TX_MSG_MAX];
	UInt32	tx_size	= 0;

	while (cmnd_lst[i].type	!= DISPCTRL_LIST_END) {
		if (cmnd_lst[i].type ==	DISPCTRL_WR_CMND) {
			tx_buff[tx_size++] = cmnd_lst[i].cmnd;

			while (cmnd_lst[i+1].type == DISPCTRL_WR_DATA) {
				tx_buff[tx_size++] = cmnd_lst[i+1].data;
				i++;
			}
			lq043y1dx01_WrSendCmnd(drvH, tx_buff, tx_size);
			tx_size	= 0;
		} else if (cmnd_lst[i].type == DISPCTRL_WR_CMND_DATA) {
			tx_buff[tx_size++] = cmnd_lst[i].cmnd;
			tx_buff[tx_size++] = cmnd_lst[i].data;
			lq043y1dx01_WrSendCmnd(drvH, tx_buff, tx_size);
			tx_size	= 0;

		} else if (cmnd_lst[i].type == DISPCTRL_SLEEP_MS) {
			OSTASK_Sleep(TICKS_IN_MILLISECONDS(cmnd_lst[i].data));
		}
		i++;
	}
} /* lq043y1dx01_ExecCmndList */



/*
 *
 *   Function Name: LQ043Y1DX01_PowerControl
 *
 *   Description:	  Display Module Control
 *
 */
Int32 LQ043Y1DX01_PowerControl(
	DISPDRV_HANDLE_T	drvH,
	DISPLAY_POWER_STATE_T	state)
{
	Int32  res = 0;
	LQ043Y1DX01_PANEL_t   *pPanel =	(LQ043Y1DX01_PANEL_t *)drvH;

	#define	 INIT_MSG_ID   LCD_DBG_ERR_ID

	switch (state) {
	case CTRL_PWR_ON:
		switch (pPanel->pwrState) {
		case STATE_PWR_OFF:

			lq043y1dx01_ExecCmndList(drvH, dsi_bridge_init);
			/* DPI Ouptut ON at this point */
			lq043y1dx01_panel_init(pPanel);
			OSTASK_Sleep(TICKS_IN_MILLISECONDS(120));
#if 0
			lq043y1dx01_ReadReg(drvH, 0x0A);
			lq043y1dx01_ReadReg(drvH, 0x0B);
			lq043y1dx01_ReadReg(drvH, 0x0C);
			lq043y1dx01_ReadReg(drvH, 0x0E);
			lq043y1dx01_ReadReg(drvH, 0x85);
#endif
			lq043y1dx01_WinSet(drvH, TRUE, &pPanel->win_dim);

			pPanel->pwrState = STATE_SCREEN_OFF;

			LCD_DBG(INIT_MSG_ID, "[DISPDRV]	%s: INIT-SEQ\n",
				__func__);
			break;
		default:
			LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	POWER ON "
				"Requested While Not In	POWER DOWN State\n",
				__func__);
			break;
		}
		break;

	case CTRL_PWR_OFF:
		if (pPanel->pwrState !=	STATE_PWR_OFF) {
			lq043y1dx01_panel_off(pPanel);
			lq043y1dx01_panel_sleep_in(pPanel);
			lq043y1dx01_WrCmndP0(drvH, MIPI_DCS_ENTER_SLEEP_MODE);
			OSTASK_Sleep(TICKS_IN_MILLISECONDS(20));
			lq043y1dx01_reset(drvH,	TRUE);

			pPanel->pwrState = STATE_PWR_OFF;

			LCD_DBG(INIT_MSG_ID, "[DISPDRV]	%s: PWR	DOWN\n\r",
				__func__);
		}
		break;

	case CTRL_SLEEP_IN:
		switch (pPanel->pwrState) {
		case STATE_SCREEN_ON:
			lq043y1dx01_panel_off(pPanel);
		case STATE_SCREEN_OFF:
			lq043y1dx01_panel_sleep_in(pPanel);
			lq043y1dx01_WrCmndP0(drvH, MIPI_DCS_ENTER_SLEEP_MODE);
			OSTASK_Sleep(TICKS_IN_MILLISECONDS(120));

			pPanel->pwrState = STATE_SLEEP;
			LCD_DBG(INIT_MSG_ID, "[DISPDRV]	%s: SLEEP\n\r",
			    __func__);
			break;
		default:
			LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	"
				"SLEEP Requested, But Not In "
				"DISP ON|OFF State\n\r", __func__);
			break;
		}
		break;

	case CTRL_SLEEP_OUT:
		switch (pPanel->pwrState) {
		case STATE_SLEEP:
			lq043y1dx01_panel_sleep_out(pPanel);
			lq043y1dx01_WrCmndP0(drvH, MIPI_DCS_EXIT_SLEEP_MODE);
			OSTASK_Sleep(TICKS_IN_MILLISECONDS(120));
			pPanel->pwrState = STATE_SCREEN_OFF;
			LCD_DBG(INIT_MSG_ID, "[DISPDRV]	%s: SLEEP-OUT\n\r",
				__func__);
			break;
		default:
			LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	"
				"SLEEP-OUT Req While Not In SLEEP State\n",
				__func__);
			break;
		}
		break;

	case CTRL_SCREEN_ON:
		switch (pPanel->pwrState) {
		case STATE_SCREEN_OFF:
			OSTASK_Sleep(TICKS_IN_MILLISECONDS(100));
			lq043y1dx01_panel_on(pPanel);
			pPanel->pwrState = STATE_SCREEN_ON;
			LCD_DBG(INIT_MSG_ID, "[DISPDRV]	%s: SCREEN ON\n",
				__func__);
			break;
		default:
			LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	"
				"SCREEN	ON Req While Not In SCREEN OFF State\n",
				__func__);
			break;
		}
		break;
	case CTRL_SCREEN_OFF:
		switch (pPanel->pwrState) {
		case STATE_SCREEN_ON:
			lq043y1dx01_panel_off(pPanel);
			pPanel->pwrState = STATE_SCREEN_OFF;
			LCD_DBG(INIT_MSG_ID, "[DISPDRV]	%s: SCREEN OFF\n",
				__func__);
			break;
		default:
			LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	"
				"SCREEN	OFF Req	While Not In SCREEN ON State\n",
				__func__);
			break;
		}
		break;

	default:
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: Invalid Power "
			"State[%d] Requested\n", __func__, state);
		res = -1;
		break;
	}
	return res;
}

/*
 *
 *  Function Name: LQ043Y1DX01_Start
 *
 *  Description:
 *
 */
Int32 LQ043Y1DX01_Start(
	DISPDRV_HANDLE_T drvH,
	struct pi_mgr_dfs_node *dfs_node)
{
	LQ043Y1DX01_PANEL_t   *pPanel =	(LQ043Y1DX01_PANEL_t *)drvH;

	if (brcm_enable_dsi_lcd_clocks(dfs_node, pPanel->busNo,
		lq043y1dx01_dsiCfg.hsBitClk.clkIn_MHz *	1000000,
		lq043y1dx01_dsiCfg.hsBitClk.clkInDiv,
		lq043y1dx01_dsiCfg.escClk.clkIn_MHz   *	1000000
		/ lq043y1dx01_dsiCfg.escClk.clkInDiv)) {

		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: ERROR to	enable "
			"the clock\n", __func__);
		return -1;
	}

	return 0;
}

/*
 *
 *   Function Name: LQ043Y1DX01_Stop
 *
 *   Description:
 *
 */
Int32 LQ043Y1DX01_Stop(DISPDRV_HANDLE_T	drvH, struct pi_mgr_dfs_node *dfs_node)
{
	LQ043Y1DX01_PANEL_t *pPanel = (LQ043Y1DX01_PANEL_t *)drvH;

	if (brcm_disable_dsi_lcd_clocks(dfs_node, pPanel->busNo)) {
		LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR to enable "
			"the clock\n", __func__);
	    return -1;
	}
	return	0;
}

/*
 *
 *   Function Name: LQ043Y1DX01_GetInfo
 *
 *   Description:
 *
 */
const DISPDRV_INFO_T *LQ043Y1DX01_GetDispDrvData(DISPDRV_HANDLE_T drvH)
{
	return &LQ043Y1DX01_Info;
}

/*
 *
 *  Function Name: LQ043Y1DX01_GetDispDrvFeatures
 *
 *  Description:
 *
 */
Int32 LQ043Y1DX01_GetDispDrvFeatures(
	DISPDRV_HANDLE_T		drvH,
	const char			**driver_name,
	UInt32				*version_major,
	UInt32				*version_minor,
	DISPDRV_SUPPORT_FEATURES_T	*flags)
{
	Int32 res = -1;

	if ((NULL != driver_name)   && (NULL !=	version_major) &&
	    (NULL != version_minor) && (NULL !=	flags))	{

		*driver_name   = "UPD60801 (IN:RG565 OUT:RGB565)";
		*version_major = 0;
		*version_minor = 15;
		*flags	       = DISPDRV_SUPPORT_NONE;
		res	       = 0;
	}
	return res;
}


/*
 *
 *  Function Name: lq043y1dx01_Cb
 *
 *  Description:   CSL callback
 *
 */
static void lq043y1dx01_Cb(CSL_LCD_RES_T cslRes, pCSL_LCD_CB_REC pCbRec)
{
	DISPDRV_CB_RES_T apiRes;

	LCD_DBG(LCD_DBG_ID, "[DISPDRV]	+%s\r\n", __func__);

	if (pCbRec->dispDrvApiCb != NULL) {
		switch (cslRes)	{
		case CSL_LCD_OK:
			apiRes = DISPDRV_CB_RES_OK;
			break;
		default:
			apiRes = DISPDRV_CB_RES_ERR;
			break;
		}

		((DISPDRV_CB_T)pCbRec->dispDrvApiCb)(apiRes);
	}

	LCD_DBG(LCD_DBG_ID, "[DISPDRV] -%s\r\n", __func__);
}


/*
 *
 *  Function Name: LQ043Y1DX01_Update
 *
 *  Description:
 *
 */
Int32 LQ043Y1DX01_Update(
	DISPDRV_HANDLE_T	drvH,
	void			*buff,
	DISPDRV_WIN_t		*p_win,
	DISPDRV_CB_T		apiCb)
{
	LQ043Y1DX01_PANEL_t	*pPanel	= (LQ043Y1DX01_PANEL_t *)drvH;
	CSL_LCD_UPD_REQ_T	req;
	Int32			res  = 0;

	LCD_DBG(LCD_DBG_ID, "[DISPDRV]	+%s\r\n", __func__);

	if (pPanel->pwrState ==	STATE_PWR_OFF) {
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: Skip Due	To "
			"Power State\n", __func__);
		return -1;
	}

	if (p_win == NULL)
		p_win =	&pPanel->win_dim;

	lq043y1dx01_WinSet(drvH, TRUE, p_win);

	req.buff	= buff;
	req.lineLenP	= p_win->w;
	req.lineCount	= p_win->h;
	req.buffBpp	= pPanel->disp_info->Bpp;
	req.timeOut_ms	= 100;

	LCD_DBG(LCD_DBG_ID, "%s: buf=%08x, linelenp = %lu, linecnt =%lu\n",
		__func__, (u32)req.buff, req.lineLenP, req.lineCount);

	req.cslLcdCbRec.cslH		= pPanel->clientH;
	req.cslLcdCbRec.dispDrvApiCbRev	= DISP_DRV_CB_API_REV_1_0;
	req.cslLcdCbRec.dispDrvApiCb	= (void	*) apiCb;
	req.cslLcdCbRec.dispDrvApiCbP1	= NULL;

	if (apiCb != NULL)
		req.cslLcdCb = lq043y1dx01_Cb;
	else
		req.cslLcdCb = NULL;

	if (CSL_DSI_UpdateCmVc(pPanel->dsiCmVcHandle, &req, pPanel->isTE)
		!= CSL_LCD_OK)	{
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:	ERROR ret by "
			"CSL_DSI_UpdateCmVc\n\r", __func__);
		res = -1;
	}

	LCD_DBG(LCD_DBG_ID, "[DISPDRV]	-%s\r\n", __func__);

	return res;
}

static int __devinit lq043y1dx01_spi_probe(struct spi_device *spi)
{
	int err;

	LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s\n", __func__);
	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	err = spi_setup(spi);
	if (err)  {
		LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s: ERR\n", __func__);
		panel[0].spi = NULL;
		return err;
	} else {
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s:	OK\n", __func__);
	}


	panel[0].spi = spi;

#if 0
	/* Turn	BL ON */
	gpio_request(SHARP_BL, "sharp_bl");
	gpio_direction_output(SHARP_BL,	0);
	gpio_set_value_cansleep(SHARP_BL, 1);
#endif
	return 0;

}

static int __devexit lq043y1dx01_spi_remove(struct spi_device *spi)
{
	LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:\n", __func__);
	return 0;
}


static void lq043y1dx01_spi_shutdown(struct spi_device *spi)
{
	LCD_DBG(LCD_DBG_ERR_ID,	"[DISPDRV] %s:\n", __func__);
}


static struct spi_driver lq043y1dx01_spi_driver	= {
	.driver	= {
		.name	= "lq043y1dx01_spi",
		.owner	= THIS_MODULE,
	},
	.probe		= lq043y1dx01_spi_probe,
	.remove		= __devexit_p(lq043y1dx01_spi_remove),
	.shutdown	= lq043y1dx01_spi_shutdown,
};


static int __init lq043y1dx01_spi_init(void)
{
	return spi_register_driver(&lq043y1dx01_spi_driver);
}
module_init(lq043y1dx01_spi_init);

static void __exit lq043y1dx01_spi_exit(void)
{
	spi_unregister_driver(&lq043y1dx01_spi_driver);
}
module_exit(lq043y1dx01_spi_exit);
