/*******************************************************************************
* Copyright 2011 Broadcom Corporation.  All rights reserved.
*
*             @file     drivers/video/broadcom/dispdrv_bcm91008_alex_dsi.c12/8/2011
*
* Unless you and Broadcom execute a separate written software license agreement
* governing use of this software, this software is licensed to you under the
* terms of the GNU General Public License version 2, available at
* http://www.gnu.org/copyleft/gpl.html (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a license
* other than the GPL, without Broadcom's express prior written consent.
*******************************************************************************/
#define UNDER_LINUX

#ifndef UNDER_LINUX
#include <stdio.h>
#include <string.h>
#include "dbg.h"
#include "mobcom_types.h"
#include "chip_version.h"
#else
#include <linux/string.h>
#include <linux/broadcom/mobcom_types.h>
#endif

#ifndef UNDER_LINUX
#include "gpio.h"		// needed for GPIO defs 4 platform_config
#include "platform_config.h"
#include "irqctrl.h"
#include "osinterrupt.h"
#include "ostask.h"
#else
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <plat/osabstract/osinterrupt.h>
#include <plat/osabstract/ostask.h>
#include <plat/csl/csl_dma_vc4lite.h>
#endif

#ifndef UNDER_LINUX
#include "dbg.h"
#include "logapi.h"
#include "dma_drv.h"
#include "display_drv.h"	// display driver interface
#include "csl_lcd.h"		// LCD CSL Common Interface
#include "csl_dsi.h"		// DSI CSL
#include "dispdrv_mipi_dcs.h"	// MIPI DCS
#include "dispdrv_mipi_dsi.h"	// MIPI DSI
#else
#include <plat/dma_drv.h>
#include <plat/pi_mgr.h>
#include <video/kona_fb_boot.h>	// LCD DRV init API
#include "display_drv.h"	// display driver interface
#include <plat/csl/csl_lcd.h>	// LCD CSL Common Interface
#include <plat/csl/csl_dsi.h>	// DSI CSL
#include "dispdrv_mipi_dcs.h"	// MIPI DCS
#include "dispdrv_mipi_dsi.h"	// MIPI DSI
#endif

#include "dispdrv_common.h"	// Disp Drv Commons
#include "lcd_clock.h"

#ifndef UNDER_LINUX
#include "csl_tectl_vc4lite.h"	// TE Input Control
#else
#include <plat/csl/csl_tectl_vc4lite.h>
#endif

#undef LCD_DBG
#define LCD_DBG(id, fmt, args...)	//      printk(KERN_ERR fmt, ##args)
#include "lcd_s6d05a1x31.h"
#define VC            (0)
#define DISPDRV_CMND_IS_LP    TRUE	// display init comm LP or HS mode

#define GPIODRV_Set_Bit(pin, val) gpio_set_value_cansleep(pin, val)

typedef struct {
	UInt32 left;
	UInt32 right;
	UInt32 top;
	UInt32 bottom;
	UInt32 width;
	UInt32 height;
} LCD_DRV_RECT_t;

typedef struct {
	CSL_LCD_HANDLE clientH;	// DSI Client Handle
	CSL_LCD_HANDLE dsiCmVcHandle;	// DSI CM VC Handle
	DISP_DRV_STATE drvState;
	DISP_PWR_STATE pwrState;
	UInt32 busNo;
	UInt32 teIn;
	UInt32 teOut;
	Boolean isTE;
	LCD_DRV_RECT_t win;
	struct pi_mgr_dfs_node dfs_node;
	/* --- */
	Boolean boot_mode;
	UInt32 rst;
	CSL_DSI_CM_VC_t *cmnd_mode;
	CSL_DSI_CFG_t *dsi_cfg;
	DISPDRV_INFO_T *disp_info;
} DISPDRV_PANEL_T;

// LOCAL FUNCTIONs
static void DISPDRV_WrCmndP0(DISPDRV_HANDLE_T drvH, UInt32 reg);

#ifdef DEBUG
static void DISPDRV_WrCmndP1(DISPDRV_HANDLE_T drvH, UInt32 reg, UInt32 val);
#endif

// DRV INTERFACE FUNCTIONs
static Int32 DISPDRV_Init(struct dispdrv_init_parms *parms,
			  DISPDRV_HANDLE_T *handle);

static Int32 DISPDRV_Exit(DISPDRV_HANDLE_T drvH);

static Int32 DISPDRV_Open(DISPDRV_HANDLE_T drvH);
static Int32 DISPDRV_Close(DISPDRV_HANDLE_T drvH);

static Int32 DISPDRV_GetDispDrvFeatures(DISPDRV_HANDLE_T drvH,
					const char **driver_name,
					UInt32 *version_major,
					UInt32 *version_minor,
					DISPDRV_SUPPORT_FEATURES_T *flags);

static const DISPDRV_INFO_T *DISPDRV_GetDispDrvData(DISPDRV_HANDLE_T drvH);

static Int32 DISPDRV_Start(DISPDRV_HANDLE_T drvH,
			   struct pi_mgr_dfs_node *dfs_node);
static Int32 DISPDRV_Stop(DISPDRV_HANDLE_T drvH,
			  struct pi_mgr_dfs_node *dfs_node);

static Int32 DISPDRV_PowerControl(DISPDRV_HANDLE_T drvH,
				  DISPLAY_POWER_STATE_T state);

static Int32 DISPDRV_Update(DISPDRV_HANDLE_T drvH,
			    void *buff,
			    DISPDRV_WIN_t *p_win, DISPDRV_CB_T apiCb);

static DISPDRV_T Disp_Drv = {
	&DISPDRV_Init,		// init
	&DISPDRV_Exit,		// exit
	&DISPDRV_GetDispDrvFeatures,	// info
	&DISPDRV_Open,		// open
	&DISPDRV_Close,		// close
	&DISPDRV_GetDispDrvData,	// get_info
	&DISPDRV_Start,		// start
	&DISPDRV_Stop,		// stop
	&DISPDRV_PowerControl,	// power_control
	NULL,			// update_no_os
	&DISPDRV_Update,	// update
	NULL,			// set_brightness
	NULL,			// reset_win
};

// DSI Command Mode VC Configuration
CSL_DSI_CM_VC_t DISPDRV_VcCmCfg = {
	VC,			// VC
	DSI_DT_LG_DCS_WR,	// dsiCmnd       
	MIPI_DCS_WRITE_MEMORY_START,	// dcsCmndStart       
	MIPI_DCS_WRITE_MEMORY_CONTINUE,	// dcsCmndContinue       
	FALSE,			// isLP          
	LCD_IF_CM_I_RGB565P,	//LCD_IF_CM_I_RGB888U, //LCD_IF_CM_I_RGB565P,           // cm_in         //@HW
	LCD_IF_CM_O_RGB565,	//LCD_IF_CM_O_RGB888, //LCD_IF_CM_O_RGB565,             // cm_out   

	// TE configuration
	{
	 DSI_TE_NONE,		//DSI_TE_CTRLR_INPUT_0, //DSI_TE_NONE,         // DSI Te Input Type
	 },
};

// DSI BUS CONFIGURATION
static CSL_DSI_CFG_t DISPDRV_dsiCfg = {
	0,			// bus             set on open
	1,			// dlCount
	DSI_DPHY_0_92,		// DSI_DPHY_SPEC_T
	// ESC CLK Config
	{500, 5},		// escClk          500|312 500[MHz], DIV by 5 = 100[MHz]
	// HS CLK Config, RHEA  VCO range 600-2400
	{1000, 2},		// hsBitClk        PLL    1000[MHz], DIV by 2 = 500[Mbps]     
	// LP Speed
	5,			// lpBitRate_Mbps, Max 10[Mbps]

	FALSE,			// enaContClock            
	TRUE,			// enaRxCrc                
	TRUE,			// enaRxEcc               
	TRUE,			// enaHsTxEotPkt           
	FALSE,			// enaLpTxEotPkt        
	FALSE,			// enaLpRxEotPkt        
};

//#define printk(format, arg...)        do {} while (0)

static DISPDRV_PANEL_T panel[1];

//#############################################################################

//*****************************************************************************
//
// Function Name: bcm91008_AlexTeOn
// 
// Description:   Configure TE Input Pin & Route it to DSI Controller Input
//
//*****************************************************************************
static int DISPDRV_TeOn(DISPDRV_PANEL_T *pPanel)
{
	Int32 res = 0;

	TECTL_CFG_t teCfg;

	teCfg.te_mode = TE_VC4L_MODE_VSYNC;
	teCfg.sync_pol = TE_VC4L_ACT_POL_LO;
	teCfg.vsync_width = 0;
	teCfg.hsync_line = 0;

	res = CSL_TECTL_VC4L_OpenInput(pPanel->teIn, pPanel->teOut, &teCfg);
	return (res);

}

//*****************************************************************************
//
// Function Name: bcm91008_AlexTeOff
// 
// Description:   'Release' TE Input Pin Used
//
//*****************************************************************************
static int DISPDRV_TeOff(DISPDRV_PANEL_T *pPanel)
{
	Int32 res = 0;
	res = CSL_TECTL_VC4L_CloseInput(pPanel->teIn);
	return (res);
}

static void DISPDRV_WrCmndPn(DISPDRV_HANDLE_T drvH, UInt32 Pn, UInt8 *Pdata)
{
	DISPDRV_PANEL_T *pPanel = (DISPDRV_PANEL_T *)drvH;
	CSL_DSI_CMND_t msg;
	//  UInt8               msgData[4];

	if (Pn <= 2) {
		msg.dsiCmnd = DSI_DT_SH_DCS_WR_P1;
		msg.isLong = TRUE;
	} else {
		msg.dsiCmnd = DSI_DT_LG_DCS_WR;
		msg.isLong = FALSE;
	}
	msg.msg = &Pdata[0];
	msg.msgLen = Pn;
	msg.vc = VC;
	msg.isLP = DISPDRV_CMND_IS_LP;
	msg.endWithBta = FALSE;
	CSL_DSI_SendPacket(pPanel->clientH, &msg, FALSE);
}

#ifdef DEBUG
//*****************************************************************************
//
// Function Name:  bcm91008_alex_WrCmndP1
// 
// Parameters:     reg   = 08-bit register address (DCS command)
//                 value = 08-bit register data    (DCS command parm)
//
// Description:    Register Write - DCS command byte, 1 parm
//
//*****************************************************************************
static void DISPDRV_WrCmndP1(DISPDRV_HANDLE_T drvH, UInt32 reg, UInt32 value)
{
	DISPDRV_PANEL_T *pPanel = (DISPDRV_PANEL_T *)drvH;
	CSL_DSI_CMND_t msg;
	UInt8 msgData[4];

	msg.dsiCmnd = DSI_DT_SH_DCS_WR_P1;
	msg.msg = &msgData[0];
	msg.msgLen = 2;
	msg.vc = VC;
	msg.isLP = DISPDRV_CMND_IS_LP;
	msg.isLong = FALSE;
	msg.endWithBta = FALSE;

	msgData[0] = reg;
	msgData[1] = value & 0x000000FF;

	CSL_DSI_SendPacket(pPanel->clientH, &msg, FALSE);
}
#endif

//*****************************************************************************
//
// Function Name:  bcm91008_alex_WrCmndP0
// 
// Parameters:     reg   = 08-bit register address (DCS command)
//
// Description:    Register Write - DCS command byte, 0 parm 
//
//*****************************************************************************
static void DISPDRV_WrCmndP0(DISPDRV_HANDLE_T drvH, UInt32 reg)
{
	DISPDRV_PANEL_T *pPanel = (DISPDRV_PANEL_T *)drvH;
	CSL_DSI_CMND_t msg;
	UInt8 msgData[4];

	msg.dsiCmnd = DSI_DT_SH_DCS_WR_P0;
	msg.msg = &msgData[0];
	msg.msgLen = 1;
	msg.vc = VC;
	msg.isLP = DISPDRV_CMND_IS_LP;
	msg.isLong = FALSE;
	msg.endWithBta = FALSE;

	msgData[0] = reg;
	msgData[1] = 0;

	CSL_DSI_SendPacket(pPanel->clientH, &msg, FALSE);
}

//*****************************************************************************
//
// Function Name:   bcm92416_hvga_ExecCmndList
//
// Description:     
//                   
//*****************************************************************************
static void DISPDRV_ExecCmndList(DISPDRV_HANDLE_T drvH,
				 pNEW_DISPCTRL_REC_T cmnd_lst)
{
	UInt32 i = 0;

	while (cmnd_lst[i].type != DISPCTRL_LIST_END) {
		if (cmnd_lst[i].type == DISPCTRL_WR_CMND_DATA) {

			DISPDRV_WrCmndPn(drvH, cmnd_lst[i].number,
					 cmnd_lst[i].data);
		} else if (cmnd_lst[i].type == DISPCTRL_WR_CMND) {
			DISPDRV_WrCmndP0(drvH, cmnd_lst[i].data[0]);
		} else if (cmnd_lst[i].type == DISPCTRL_SLEEP_MS) {
			OSTASK_Sleep(TICKS_IN_MILLISECONDS
				     (cmnd_lst[i].data[0]));
		}

		i++;
	}
}				// bcm92416_hvga_ExecCmndList

//*****************************************************************************
//
// Function Name: LCD_DRV_BCM91008_ALEX_GetDrvInfo
// 
// Description:   Get Driver Funtion Table
//
//*****************************************************************************
DISPDRV_T *DISPDRV_GetFuncTable(void)
{
	return (&Disp_Drv);
}

static void DISPDRV_Reset(DISPDRV_HANDLE_T drvH)
{
	DISPDRV_PANEL_T *pPanel;
	u32 gpio;

	pPanel = (DISPDRV_PANEL_T *)drvH;
	gpio = pPanel->rst;

	if (gpio != 0) {
		gpio_request(gpio, "LCD_RST");
		gpio_direction_output(gpio, 0);
		gpio_set_value_cansleep(gpio, 1);
		msleep(1);
		gpio_set_value_cansleep(gpio, 0);
		msleep(1);
		gpio_set_value_cansleep(gpio, 1);
		msleep(40);
	}
}

//*****************************************************************************
//
// Function Name: BCM91008_ALEX_Init
// 
// Description:   Reset Driver Info
//
//*****************************************************************************
Int32 DISPDRV_Init(struct dispdrv_init_parms *parms, DISPDRV_HANDLE_T *handle)
{
	Int32 res = 0;
	DISPDRV_PANEL_T *pPanel;

	pPanel = &panel[0];

	if (pPanel->drvState == DRV_STATE_OFF) {

		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: Bus        %d \n",
			__func__, parms->w0.bits.bus_type);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: BootMode   %d \n",
			__func__, parms->w0.bits.boot_mode);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: BusNo      %d \n",
			__func__, parms->w0.bits.bus_no);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: col_mode_i %d \n",
			__func__, parms->w0.bits.col_mode_i);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: col_mode_o %d \n",
			__func__, parms->w0.bits.col_mode_o);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: te_input   %d \n",
			__func__, parms->w0.bits.te_input);

		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: API Rev    %d \n",
			__func__, parms->w1.bits.api_rev);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: Rst 0      %d \n",
			__func__, parms->w1.bits.lcd_rst0);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: Rst 1      %d \n",
			__func__, parms->w1.bits.lcd_rst1);
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: Rst 2      %d \n",
			__func__, parms->w1.bits.lcd_rst2);

		if ((u8)parms->w1.bits.api_rev != RHEA_LCD_BOOT_API_REV) {
			LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: "
				"Boot Init API Rev Mismatch(%d.%d vs %d.%d)\n",
				__func__,
				(parms->w1.bits.api_rev & 0xF0) >> 8,
				(parms->w1.bits.api_rev & 0x0F),
				(RHEA_LCD_BOOT_API_REV & 0xF0) >> 8,
				(RHEA_LCD_BOOT_API_REV & 0x0F));
			return (-1);
		}

		pPanel->boot_mode = parms->w0.bits.boot_mode;

		pPanel->cmnd_mode = &DISPDRV_VcCmCfg;
		pPanel->dsi_cfg = &DISPDRV_dsiCfg;
		pPanel->disp_info = &Disp_Info;

		pPanel->busNo = dispdrv2busNo(parms->w0.bits.bus_no);

		/* check for valid DSI bus no */
		if (pPanel->busNo & 0xFFFFFFFE)
			return (-1);

		pPanel->cmnd_mode->cm_in =
		    dispdrv2cmIn(parms->w0.bits.col_mode_i);
		pPanel->cmnd_mode->cm_out =
		    dispdrv2cmOut(parms->w0.bits.col_mode_o);

		/* we support both input color modes */
		switch (pPanel->cmnd_mode->cm_in) {
		case LCD_IF_CM_I_RGB565P:
			pPanel->disp_info->input_format =
			    DISPDRV_FB_FORMAT_RGB565;
			pPanel->disp_info->Bpp = 2;
			break;
		case LCD_IF_CM_I_RGB888U:
			pPanel->disp_info->input_format =
			    DISPDRV_FB_FORMAT_RGB888_U;
			pPanel->disp_info->Bpp = 4;
			break;
		default:
			return (-1);
		}

		/* get reset pin */
		pPanel->rst = parms->w1.bits.lcd_rst0;

		pPanel->isTE = pPanel->cmnd_mode->teCfg.teInType != DSI_TE_NONE;

		/* get TE pin configuration */
		pPanel->teIn = dispdrv2busTE(parms->w0.bits.te_input);
		pPanel->teOut =
		    pPanel->busNo ? TE_VC4L_OUT_DSI1_TE0 : TE_VC4L_OUT_DSI0_TE0;

		pPanel->drvState = DRV_STATE_INIT;
		pPanel->pwrState = STATE_PWR_OFF;

		*handle = (DISPDRV_HANDLE_T)pPanel;

		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: OK\n\r", __FUNCTION__);
	} else {
		LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: Not in OFF state\n",
			__FUNCTION__);
		res = -1;
	}

	return (res);
}

//*****************************************************************************
//
// Function Name: BCM91008_ALEX_Exit
// 
// Description:   
//
//*****************************************************************************
Int32 DISPDRV_Exit(DISPDRV_HANDLE_T drvH)
{
	DISPDRV_PANEL_T *pPanel = (DISPDRV_PANEL_T *)drvH;

	pPanel->drvState = DRV_STATE_OFF;

	return (0);
}

//*****************************************************************************
//
// Function Name: BCM91008_ALEX_Open
// 
// Description:   Open Sub Drivers
//
//*****************************************************************************
Int32 DISPDRV_Open(DISPDRV_HANDLE_T drvH)
{
	Int32 res = 0;
	DISPDRV_PANEL_T *pPanel;

	pPanel = (DISPDRV_PANEL_T *)drvH;

	if (pPanel->drvState != DRV_STATE_INIT) {
		LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR Not Init\n\r",
			__FUNCTION__);
		return (-1);
	}

	DISPDRV_Reset(drvH);

	if (brcm_enable_dsi_pll_clocks(pPanel->busNo,
				       DISPDRV_dsiCfg.hsBitClk.clkIn_MHz *
				       1000000,
				       DISPDRV_dsiCfg.hsBitClk.clkInDiv,
				       DISPDRV_dsiCfg.escClk.clkIn_MHz *
				       1000000 /
				       DISPDRV_dsiCfg.escClk.clkInDiv)) {
		LCD_DBG(LCD_DBG_ERR_ID,
			"[DISPDRV] %s: ERROR to enable the pll clock\n",
			__FUNCTION__);
		return (-1);
	}

	if (pPanel->isTE && DISPDRV_TeOn(pPanel) == -1) {
		LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: "
			"Failed To Configure TE Input\n", __FUNCTION__);
		return (-1);
	}

	if (CSL_DSI_Init(pPanel->dsi_cfg) != CSL_LCD_OK) {
		LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR, DSI CSL Init "
			"Failed\n\r", __FUNCTION__);
		return (-1);
	}

	if (CSL_DSI_OpenClient(pPanel->busNo, &pPanel->clientH) != CSL_LCD_OK) {
		LCD_DBG(LCD_DBG_ERR_ID,
			"[DISPDRV] %s: ERROR, CSL_DSI_OpenClient " "Failed\n\r",
			__FUNCTION__);
		return (-1);
	}

	if (CSL_DSI_OpenCmVc
	    (pPanel->clientH, pPanel->cmnd_mode, &pPanel->dsiCmVcHandle)
	    != CSL_LCD_OK) {
		LCD_DBG(LCD_DBG_ERR_ID,
			"[DISPDRV] %s: CSL_DSI_OpenCmVc Failed\n\r",
			__FUNCTION__);
		return (-1);
	}
#ifdef UNDER_LINUX
	if (csl_dma_vc4lite_init() != DMA_VC4LITE_STATUS_SUCCESS) {
		LCD_DBG(LCD_DBG_ERR_ID,
			"[DISPDRV] %s: csl_dma_vc4lite_init Failed\n\r",
			__FUNCTION__);
		return (-1);
	}
#endif

	pPanel->win.left = 0;
	pPanel->win.right = pPanel->disp_info->width - 1;
	pPanel->win.top = 0;
	pPanel->win.bottom = pPanel->disp_info->height - 1;
	pPanel->win.width = pPanel->disp_info->width;
	pPanel->win.height = pPanel->disp_info->height;

	pPanel->drvState = DRV_STATE_OPEN;

	LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: OK\n\r", __FUNCTION__);

	return (res);
}

//*****************************************************************************
//
// Function Name: BCM91008_ALEX_Close
// 
// Description:   Close The Driver
//
//*****************************************************************************
Int32 DISPDRV_Close(DISPDRV_HANDLE_T drvH)
{
	Int32 res = 0;
	DISPDRV_PANEL_T *pPanel = (DISPDRV_PANEL_T *)drvH;

	if (CSL_DSI_CloseCmVc(pPanel->dsiCmVcHandle)) {
		LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR, "
			"Closing Command Mode Handle\n\r", __FUNCTION__);
		return (-1);
	}

	if (CSL_DSI_CloseClient(pPanel->clientH) != CSL_LCD_OK) {
		LCD_DBG(LCD_DBG_ERR_ID,
			"[DISPDRV] %s: ERROR, Closing DSI Client\n\r",
			__FUNCTION__);
		return (-1);
	}

	if (CSL_DSI_Close(pPanel->busNo) != CSL_LCD_OK) {
		LCD_DBG(LCD_DBG_ERR_ID,
			"[DISPDRV] %s: ERR Closing DSI Controller\n\r",
			__FUNCTION__);
		return (-1);
	}

	if (pPanel->isTE)
		DISPDRV_TeOff(pPanel);

	if (brcm_disable_dsi_pll_clocks(pPanel->busNo)) {
		LCD_DBG(LCD_DBG_ERR_ID,
			"[DISPDRV] %s: ERROR to disable the pll clock\n",
			__FUNCTION__);
		return (-1);
	}

	pPanel->pwrState = STATE_PWR_OFF;
	pPanel->drvState = DRV_STATE_INIT;
	LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: OK\n\r", __FUNCTION__);

	return (res);
}

//*****************************************************************************
//
// Function Name: BCM91008_ALEX_PowerControl
// 
// Description:   Display Module Control
//
//*****************************************************************************
Int32 DISPDRV_PowerControl(DISPDRV_HANDLE_T drvH, DISPLAY_POWER_STATE_T state)
{
	Int32 res = 0;
	DISPDRV_PANEL_T *pPanel = (DISPDRV_PANEL_T *)drvH;

#ifndef CONFIG_BACKLIGHT_LCD_SUPPORT
	gpio_request(95, "BK_LIGHT");
	gpio_direction_output(95, 0);
#endif

	switch (state) {
	case CTRL_PWR_ON:
		switch (pPanel->pwrState) {
		case STATE_PWR_OFF:
			DISPDRV_ExecCmndList(drvH,
					     &power_on_seq_s5d05a1x31_cooperve_AUO
					     [0]);
#ifndef CONFIG_BACKLIGHT_LCD_SUPPORT
			gpio_set_value_cansleep(95, 1);
#endif
			pPanel->pwrState = STATE_SCREEN_OFF;
			LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: INIT-SEQ\n\r",
				__FUNCTION__);
			break;
		default:
			LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: POWER ON "
				"Requested While Not In POWER DOWN State\n",
				__FUNCTION__);
			break;
		}
		break;

	case CTRL_PWR_OFF:
		if (pPanel->pwrState != STATE_PWR_OFF) {
			// e.g. display off, Stop LDI, reset, power down
			DISPDRV_ExecCmndList(drvH,
					     (pNEW_DISPCTRL_REC_T) &
					     enter_sleep_seq_AUO[0]);
			OSTASK_Sleep(120);

			pPanel->pwrState = STATE_PWR_OFF;
			LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: PWR DOWN\n",
				__FUNCTION__);
		}
		break;

	case CTRL_SLEEP_OUT:
		switch (pPanel->pwrState) {
		case STATE_SLEEP:
			DISPDRV_ExecCmndList(drvH,
					     (pNEW_DISPCTRL_REC_T) &
					     exit_sleep_seq_AUO[0]);
			OSTASK_Sleep(120);
#ifndef CONFIG_BACKLIGHT_LCD_SUPPORT
			gpio_set_value_cansleep(95, 1);
#endif
			pPanel->pwrState = STATE_SCREEN_OFF;
			LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: SLEEP-OUT\n",
				__FUNCTION__);
			break;
		default:
			LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: "
				"SLEEP-OUT Req While Not In SLEEP State\n",
				__FUNCTION__);
			break;
		}
		break;

	case CTRL_SLEEP_IN:
		switch (pPanel->pwrState) {
		case STATE_SCREEN_ON:
			// turn off display 
		case STATE_SCREEN_OFF:
			DISPDRV_ExecCmndList(drvH,
					     (pNEW_DISPCTRL_REC_T) &
					     enter_sleep_seq_AUO[0]);
			OSTASK_Sleep(120);
			pPanel->pwrState = STATE_SLEEP;
			LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: "
				"SLEEP-IN\n", __FUNCTION__);
			break;
		default:
			LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: "
				"SLEEP Requested, But Not In "
				"DISP ON|OFF State\n", __FUNCTION__);
			break;
		}
		break;

	case CTRL_SCREEN_ON:
		switch (pPanel->pwrState) {
		case STATE_SCREEN_OFF:
			/*
			   {
			   #ifndef CONFIG_BACKLIGHT_LCD_SUPPORT
			   //gpio_set_value_cansleep(95, 1);
			   gpio_direction_output(95, 1);
			   #endif
			   LCD_DBG ( LCD_DBG_INIT_ID, "[DISPDRV] %s: Turn on backlight ",
			   __FUNCTION__ );
			   }   
			 */
			pPanel->pwrState = STATE_SCREEN_ON;
			LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: SCREEN ON, "
				"Turn on backlight ", __FUNCTION__);
		default:
			LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: "
				"SCREEN ON Req While Not In SCREEN OFF State\n",
				__FUNCTION__);
			break;
		}
		break;

	case CTRL_SCREEN_OFF:
		switch (pPanel->pwrState) {
		case STATE_SCREEN_ON:
#ifndef CONFIG_BACKLIGHT_LCD_SUPPORT
			gpio_set_value_cansleep(95, 0);
#endif
			pPanel->pwrState = STATE_SCREEN_ON;
			LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: SCREEN OFF "
				"Turn off backlight\n\r", __FUNCTION__);
			break;
		default:
			LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: "
				"SCREEN OFF Req While Not In SCREEN ON State\n",
				__FUNCTION__);
			break;
		}
		break;

	default:
		LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: Invalid Power State[%d] "
			"Requested\n\r", __FUNCTION__, state);
		res = -1;
		break;
	}

#ifndef CONFIG_BACKLIGHT_LCD_SUPPORT
	gpio_free(95);
#endif

	return (res);
}

//*****************************************************************************
//
// Function Name: BCM91008_ALEX_Start
// 
// Description:   Configure For Updates
//
//*****************************************************************************
Int32 DISPDRV_Start(DISPDRV_HANDLE_T drvH, struct pi_mgr_dfs_node *dfs_node)
{
	DISPDRV_PANEL_T *pPanel = (DISPDRV_PANEL_T *)drvH;

	if (brcm_enable_dsi_lcd_clocks(dfs_node, pPanel->busNo,
				       DISPDRV_dsiCfg.hsBitClk.clkIn_MHz *
				       1000000,
				       DISPDRV_dsiCfg.hsBitClk.clkInDiv,
				       DISPDRV_dsiCfg.escClk.clkIn_MHz *
				       1000000 /
				       DISPDRV_dsiCfg.escClk.clkInDiv)) {
		LCD_DBG(LCD_DBG_ERR_ID,
			"[DISPDRV] %s: ERROR to enable the clock\n",
			__FUNCTION__);
		return (-1);
	}

	return (0);
}

//*****************************************************************************
//
// Function Name: BCM91008_ALEX_Stop
// 
// Description:   
//
//*****************************************************************************
Int32 DISPDRV_Stop(DISPDRV_HANDLE_T drvH, struct pi_mgr_dfs_node *dfs_node)
{
	DISPDRV_PANEL_T *pPanel = (DISPDRV_PANEL_T *)drvH;

	if (brcm_disable_dsi_lcd_clocks(dfs_node, pPanel->busNo)) {
		LCD_DBG(LCD_DBG_ERR_ID,
			"[DISPDRV] %s: ERROR to enable the clock\n",
			__FUNCTION__);
		return (-1);
	}

	return (0);
}

//*****************************************************************************
//
// Function Name: BCM91008_ALEX_GetInfo
// 
// Description:   
//
//*****************************************************************************
const DISPDRV_INFO_T *DISPDRV_GetDispDrvData(DISPDRV_HANDLE_T drvH)
{

	return (&Disp_Info);
}

//*****************************************************************************
//
// Function Name: BCM91008_ALEX_GetDispDrvFeatures
// 
// Description:   
//
//*****************************************************************************
Int32 DISPDRV_GetDispDrvFeatures(DISPDRV_HANDLE_T drvH,
				 const char **driver_name,
				 UInt32 *version_major,
				 UInt32 *version_minor,
				 DISPDRV_SUPPORT_FEATURES_T *flags)
{
	Int32 res = -1;

	if ((NULL != driver_name) && (NULL != version_major)
	    && (NULL != version_minor) && (NULL != flags)) {
		*driver_name = "BCM91008_ALEX (IN:RGB888U OUT:RGB888)";
		*version_major = 0;
		*version_minor = 15;
		*flags = DISPDRV_SUPPORT_NONE;
		res = 0;
	}
	return (res);
}

//*****************************************************************************
//
// Function Name: bcm91008_AlexCb
// 
// Description:   CSL callback        
//
//*****************************************************************************
static void DISPDRV_Cb(CSL_LCD_RES_T cslRes, pCSL_LCD_CB_REC pCbRec)
{
	DISPDRV_CB_RES_T apiRes;

	LCD_DBG(LCD_DBG_ID, "[DISPDRV] +%s\r\n", __FUNCTION__);

	if (pCbRec->dispDrvApiCb != NULL) {
		switch (cslRes) {
		case CSL_LCD_OK:
			apiRes = DISPDRV_CB_RES_OK;
			break;
		default:
			apiRes = DISPDRV_CB_RES_ERR;
			break;
		}

		((DISPDRV_CB_T)pCbRec->dispDrvApiCb) (apiRes);
	}

	LCD_DBG(LCD_DBG_ID, "[DISPDRV] -%s\r\n", __FUNCTION__);
}

//*****************************************************************************
//
// Function Name: _Update
// 
// Description:   DMA/OS Update using INT frame buffer
//
//*****************************************************************************
Int32 DISPDRV_Update(DISPDRV_HANDLE_T drvH,
		     void *buff, DISPDRV_WIN_t *p_win, DISPDRV_CB_T apiCb)
{
	DISPDRV_PANEL_T *pPanel = (DISPDRV_PANEL_T *)drvH;
	CSL_LCD_UPD_REQ_T req;
	Int32 res = 0;

	LCD_DBG(LCD_DBG_ID, "[DISPDRV] +%s\r\n", __FUNCTION__);

	if (pPanel->pwrState == STATE_PWR_OFF) {
		LCD_DBG(LCD_DBG_ERR_ID,
			"[DISPDRV] +%s: Skip Due To Power State\r\n",
			__FUNCTION__);
		return (-1);
	}

	req.buff = buff;
	req.lineLenP = pPanel->disp_info->width;
	req.lineCount = pPanel->disp_info->height;
	req.buffBpp = pPanel->disp_info->Bpp;
	req.timeOut_ms = 100;

	req.cslLcdCbRec.cslH = pPanel->clientH;
	req.cslLcdCbRec.dispDrvApiCbRev = DISP_DRV_CB_API_REV_1_0;
	req.cslLcdCbRec.dispDrvApiCb = (void *)apiCb;
	req.cslLcdCbRec.dispDrvApiCbP1 = NULL;

	if (apiCb != NULL)
		req.cslLcdCb = DISPDRV_Cb;
	else
		req.cslLcdCb = NULL;

	if (CSL_DSI_UpdateCmVc(pPanel->dsiCmVcHandle, &req, pPanel->isTE)
	    != CSL_LCD_OK) {
		LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR ret by "
			"CSL_DSI_UpdateCmVc\n\r", __FUNCTION__);
		res = -1;
	}

	LCD_DBG(LCD_DBG_ID, "[DISPDRV] -%s\r\n", __FUNCTION__);

	return (res);
}
