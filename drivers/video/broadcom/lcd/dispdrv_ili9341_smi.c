/************************************************************************************************/
/*                                                                                              */
/*  Copyright 2011  Broadcom Corporation                                                        */
/*                                                                                              */
/*     Unless you and Broadcom execute a separate written software license agreement governing  */
/*     use of this software, this software is licensed to you under the terms of the GNU        */
/*     General Public License version 2 (the GPL), available at                                 */
/*                                                                                              */
/*          http://www.broadcom.com/licenses/GPLv2.php                                          */
/*                                                                                              */
/*     with the following added to such license:                                                */
/*                                                                                              */
/*     As a special exception, the copyright holders of this software give you permission to    */
/*     link this software with independent modules, and to copy and distribute the resulting    */
/*     executable under terms of your choice, provided that you also meet, for each linked      */
/*     independent module, the terms and conditions of the license of that module.              */
/*     An independent module is a module which is not derived from this software.  The special  */
/*     exception does not apply to any modifications of the software.                           */
/*                                                                                              */
/*     Notwithstanding the above, under no circumstances may you combine this software in any   */
/*     way with any other Broadcom software provided under a license other than the GPL,        */
/*     without Broadcom's express prior written consent.                                        */
/*                                                                                              */
/************************************************************************************************/

/**
 *
 * LCD Module:   ILI9341 Display Driver for HERA/RHEA
 *               Supports Only 8/16/18 bit SMI bus.
 *
 * DISPLAY:      RHEA Display Board
 *
 */

/**
 *
 *  REV    Date         Comment
 *  --------------------------------------------------------------------------
 *  0.10   13th JULY 2011  - Initial HERA Release
 */

#ifndef __KERNEL__
#include <stdio.h>
#include "mobcom_types.h"
#include "chip_version.h"
#include "gpio.h"
#include "gpio_drv.h"
#include "platform_config.h"
#include "irqctrl.h"
#include "osinterrupt.h"
#include "ostask.h"
#include "osheap.h"
#include "dma_drv.h"

#include "dbg.h"
#include "logapi.h"
#include "csl_lcd.h"                // CSL LCD Layer commons
#include "dispdrv_common.h"         // DISPLAY DRIVER Commons
#include "csl_smi.h"                // Combined CSL for MM SMI&SPI(LOSSI)
#include "dispdrv_mipi_dcs.h"       // DISPLAY DRIVER Commons

#include "csl_tectl_vc4lite.h"      // TE Input Control


#include "display_drv.h"            // DISPLAY DRIVER Interface

#else  /* __KERNEL__ */

#include <linux/string.h>
#include <linux/broadcom/mobcom_types.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#include <plat/osabstract/osinterrupt.h>
#include <plat/osabstract/ostask.h>
#include <plat/csl/csl_dma_vc4lite.h>

#include <plat/dma_drv.h>
#include <plat/csl/csl_lcd.h>
#include <plat/csl/csl_smi.h>
#include <plat/csl/csl_tectl_vc4lite.h>
#include <plat/pi_mgr.h>
#include <video/kona_fb_boot.h>	    // LCD DRV init parms API

#include "dispdrv_mipi_dcs.h"
#include "dispdrv_common.h"
#include "display_drv.h"
#include "lcd_clock.h"

#endif /*  __KERNEL__ */

#define __QVGA_MODE_666__
#define     DISP_CM_IN_RGB565  	0x55   
#define     DISP_CM_IN_RGB666  	0x66   
#define     DISP_CM_IN_RGB888  	0x77   

// output color mdoe must be defined before including EC .H
#include "lcd_ili9341.h"

//#define LCD_DBG(id, fmt, args...) printk(KERN_ERR fmt, ##args);

typedef struct
{
	CSL_LCD_HANDLE       	cslH;
	DISPDRV_WIN_t	 	win_cur;
	DISPDRV_WIN_t	 	win_dim;
	void*                	frameBuffer;
	DISP_DRV_STATE       	drvState;
	DISP_PWR_STATE       	pwrState;
	struct pi_mgr_dfs_node 	dfs_node;
	volatile int	 	is_clock_gated;
	/* new */
	UInt32              	teIn;
	UInt32                  cm_out;
	UInt32                  bus_ch;
	Boolean             	boot_mode;
	UInt32              	rst;
	CSL_SMI_CTRL_T		*smi_cfg;
	DISPDRV_INFO_T		*disp_info;
} DISP_SMI_PANEL_T;

static void DispSmi_WrCmndP0  ( DISPDRV_HANDLE_T dispH, UInt32 cmnd );
static void DispSmi_WrCmndP1  ( DISPDRV_HANDLE_T dispH,  UInt32 cmnd, UInt32 data );
static void DispSmi_WrCmndP2  ( DISPDRV_HANDLE_T dispH,  UInt32 data1);



//--- GEN DRIVER --------------------------------------------------------------
Int32 DISP_SMI_Init(
	struct dispdrv_init_parms 	*parms,
	DISPDRV_HANDLE_T	*handle);
		
Int32 DISP_SMI_Exit( DISPDRV_HANDLE_T dispH );
Int32 DISP_SMI_Open( DISPDRV_HANDLE_T dispH );
Int32 DISP_SMI_Close( DISPDRV_HANDLE_T dispH );

const DISPDRV_INFO_T* DISP_SMI_GetDispDrvData( DISPDRV_HANDLE_T dispH );

Int32 DISP_SMI_GetDispDrvFeatures(
	DISPDRV_HANDLE_T 		dispH,
	const char			**driver_name,
	UInt32				*version_major,
	UInt32				*version_minor,
	DISPDRV_SUPPORT_FEATURES_T	*flags );

Int32 DISP_SMI_Start(
	DISPDRV_HANDLE_T dispH,
	struct pi_mgr_dfs_node* dfs_node );

Int32 DISP_SMI_Stop(
	DISPDRV_HANDLE_T dispH,
	struct pi_mgr_dfs_node* dfs_node );

Int32 DISP_SMI_PowerControl (
	DISPDRV_HANDLE_T        dispH,
	DISPLAY_POWER_STATE_T   state );

Int32 DISP_SMI_SetWindow( DISPDRV_HANDLE_T dispH );

Int32 DISP_SMI_Update(
          	DISPDRV_HANDLE_T    	dispH,
		void			*buff,
		DISPDRV_WIN_t*		p_win,
		DISPDRV_CB_T        	apiCb);

Int32 DISP_SMI_WinReset( DISPDRV_HANDLE_T drvH ); 


static DISPDRV_T DISP_SMI_Drv = {
	&DISP_SMI_Init,                  // init
	&DISP_SMI_Exit,                  // exit
	&DISP_SMI_GetDispDrvFeatures,    // info
	&DISP_SMI_Open,                  // open
	&DISP_SMI_Close,                 // close
	&DISP_SMI_GetDispDrvData,        // get_info
	&DISP_SMI_Start,                 // start
	&DISP_SMI_Stop,                  // stop
	&DISP_SMI_PowerControl,          // power_control
	NULL,                           	// update_no_os
	&DISP_SMI_Update,                // update
	NULL, 		                        // set_brightness
	DISP_SMI_WinReset,               // reset_win
};


static DISPDRV_INFO_T DISP_SMI_Info =
{
    DISPLAY_TYPE_LCD_STD,       // DISPLAY_TYPE_T          type;
    240,                        // UInt32                  width;
    320,                        // UInt32                  height;

    DISPDRV_FB_FORMAT_RGB888_U,   // DISPDRV_FB_FORMAT_T     input_format;

    DISPLAY_BUS_SMI,            // DISPLAY_BUS_T           bus_type;
    4,                    // UInt32                  Bpp;
    46,
    61
};


static CSL_SMI_CTRL_T  DISP_SMI_SmiCtrlCfg =
{
    16,                      //  UInt8             busWidth;
    {SMI_PLL_500MHz, 2  },  //  div range 1-16 (1 unusable), 2=4ns timing step
    0,                      //  UInt8             addr_c, init by open
    0,                      //  UInt8             addr_d, init by open
    FALSE,                  //  Boolean           m68;
    FALSE,                  //  Boolean           swap;
    FALSE,                  //  Boolean           setupFirstTrOnly;
#if defined(__HVGA_MODE_888__)
    LCD_IF_CM_I_RGB888U,    //  CSL_LCD_CM_IN     colModeIn;    xRGB
    LCD_IF_CM_O_RGB888,     //  CSL_LCD_CM_OUT    colModeOut;
#elif defined(__QVGA_MODE_666__)
    LCD_IF_CM_I_RGB888U,    //  CSL_LCD_CM_IN     colModeIn;    xRGB
    LCD_IF_CM_O_RGB888,     //  CSL_LCD_CM_OUT    colModeOut;
#else
    LCD_IF_CM_I_RGB565P,    //  CSL_LCD_CM_IN     colModeIn;
    LCD_IF_CM_O_RGB565,     //  CSL_LCD_CM_OUT    colModeOut;
#endif
    // setup_ns, hold_ns, pace_ns, strobe_ns
    // TODO: Plug-In Real Timing For The Display
    { 0, 90, 10, 360, },    //  CSL_SMI_TIMIMG_T  rdTiming;
    {16, 30, 10, 30 , },    //  CSL_SMI_TIMIMG_T  wrTiming;       //  1-8-3-8 => BB 68ns
//   { 0,  15, 10, 15 , },    //  CSL_SMI_TIMIMG_T  wrTiming_m;     //  1-4-3-4 => BB 36ns
 // Original   { 0, 8, 4, 8, },
	{ 16, 15, 10, 30 , }, //@HW, this value needs to be tuned
//  { 0,  8, 10,  8 , },    //  CSL_SMI_TIMIMG_T  wrTiming_m;     //  1-3-3-3 => BB 28ns
//  { 0,  4, 10,  4 , },    //  CSL_SMI_TIMIMG_T  wrTiming_m;     //  1-2-3-2 => BB 20ns
    TRUE,                   //  usesTE
};

typedef enum {
       LCD_PANEL_ID_NOT_SET,	
       LCD_PANEL_ID_ONE,
       LCD_PANEL_ID_TWO,
} LCD_PANEL_ID;

static DISP_SMI_PANEL_T panel[1];

static int isFirstboot = 1;
static int panelID = LCD_PANEL_ID_NOT_SET;   /* it's determined in DispSmi_GetID() */
//*****************************************************************************
//
// Function Name:  DispSmi_WrCmndP2
//
// Description:    Write To LCD register, 0 Parms
//
//*****************************************************************************
void DispSmi_WrCmndP2( DISPDRV_HANDLE_T dispH, UInt32 data1)
{
    DISP_SMI_PANEL_T* lcdDrv = (DISP_SMI_PANEL_T*) dispH;

    CSL_SMI_WrDirect( lcdDrv->cslH, FALSE,  data1 );
}

//*****************************************************************************
//
// Function Name: DispSmi_WrCmndP1
//
// Description:   Write To LCD register, 1 Parms
//
//*****************************************************************************
void DispSmi_WrCmndP1(
	DISPDRV_HANDLE_T    dispH,
	UInt32              cmnd,
	UInt32              data )
{
    DISP_SMI_PANEL_T* lcdDrv = (DISP_SMI_PANEL_T*) dispH;

    CSL_SMI_WrDirect( lcdDrv->cslH, TRUE,  cmnd );
    CSL_SMI_WrDirect( lcdDrv->cslH, FALSE, data );
}

//*****************************************************************************
//
// Function Name:  DispSmi_WrCmndP0
//
// Description:    Write To LCD register, 0 Parms
//
//*****************************************************************************
void DispSmi_WrCmndP0(
	DISPDRV_HANDLE_T 	dispH,
	UInt32 			cmnd )
{
    DISP_SMI_PANEL_T* lcdDrv = (DISP_SMI_PANEL_T*) dispH;

    CSL_SMI_WrDirect( lcdDrv->cslH, TRUE,  cmnd );
}



//*****************************************************************************
//
// Function Name:   DispSmi_ExecCmndList
//
// Description:
//
//*****************************************************************************
void DispSmi_ExecCmndList(
	DISPDRV_HANDLE_T     dispH,
	pDISPCTRL_REC_T      cmnd_lst )
{
UInt32  i = 0;

	while (cmnd_lst[i].type != DISPCTRL_LIST_END) {
		if (cmnd_lst[i].type == DISPCTRL_WR_CMND_DATA){
			DispSmi_WrCmndP1 (dispH, cmnd_lst[i].cmnd,
				cmnd_lst[i].data);
		} else if (cmnd_lst[i].type == DISPCTRL_WR_CMND) {		
			
			DispSmi_WrCmndP0 (dispH, cmnd_lst[i].cmnd);
		} else if (cmnd_lst[i].type == DISPCTRL_WR_DATA) {			
			
			DispSmi_WrCmndP2 (dispH, cmnd_lst[i].data);
		} else if (cmnd_lst[i].type == DISPCTRL_SLEEP_MS)
		{
			OSTASK_Sleep( TICKS_IN_MILLISECONDS(cmnd_lst[i].data));
		}
		i++;
	}
} // DispSmi_ExecCmndList

//*****************************************************************************
//
// Function Name: DispSmi_SetWindow
//
// Description:   Set Window
//
//*****************************************************************************
Int32 DispSmi_SetWindow (
    DISPDRV_HANDLE_T 	dispH,
    Boolean 		update,
    DISPDRV_WIN_t* 	p_win )
{
    	Int32                     res = 0;
    	DISP_SMI_PANEL_T*  lcdDrv = (DISP_SMI_PANEL_T*) dispH;

    	if(    (lcdDrv->win_cur.l != p_win->l)
    	    || (lcdDrv->win_cur.r != p_win->r)
    	    || (lcdDrv->win_cur.t != p_win->t)
    	    || (lcdDrv->win_cur.b != p_win->b) )
    	{
    		lcdDrv->win_cur = *p_win;

		if (update) {
    			CSL_SMI_WrDirect( lcdDrv->cslH, TRUE,  
				MIPI_DCS_SET_COLUMN_ADDRESS );
    			CSL_SMI_WrDirect( lcdDrv->cslH, FALSE, 
				(p_win->l & 0x100) >> 8 );
    			CSL_SMI_WrDirect( lcdDrv->cslH, FALSE, 
				(p_win->l & 0x0FF)      );
    			CSL_SMI_WrDirect( lcdDrv->cslH, FALSE, 
				(p_win->r & 0x100) >> 8 );
    			CSL_SMI_WrDirect( lcdDrv->cslH, FALSE, 
				(p_win->r & 0x0FF)      );
    			CSL_SMI_WrDirect( lcdDrv->cslH, TRUE,  
				MIPI_DCS_SET_PAGE_ADDRESS );
    			CSL_SMI_WrDirect( lcdDrv->cslH, FALSE, 
				(p_win->t & 0x100) >> 8 );
    			CSL_SMI_WrDirect( lcdDrv->cslH, FALSE, 
				(p_win->t & 0x0FF)      );
    			CSL_SMI_WrDirect( lcdDrv->cslH, FALSE, 
				(p_win->b & 0x100) >> 8 );
    			CSL_SMI_WrDirect( lcdDrv->cslH, FALSE, 
				(p_win->b & 0x0FF)      );
   		}
    	}

    	return ( res );
} // DispSmi_SetWindow

//*****************************************************************************
//
// Function Name: DISP_SMI_WinReset
// 
// Description:   Reset windowing to full screen size. 
//                Typically, only used in boot code environment
//
//*****************************************************************************
Int32 DISP_SMI_WinReset( DISPDRV_HANDLE_T dispH ) 
{
    	Int32 res = 0;
    	DISP_SMI_PANEL_T*  pPanel = (DISP_SMI_PANEL_T*) dispH;

	//res = DispSmi_WinSet( dispH, TRUE, &pPanel->win_dim );
	return (res);
}


//*****************************************************************************
//
// Function Name: LCD_DRV_DISP_SMI_GetDrvInfo
//
// Description:   Get Driver Funtion Table
//
//*****************************************************************************
DISPDRV_T* DISPDRV_GetFuncTable ( void )
{
    return ( &DISP_SMI_Drv );
}


//*****************************************************************************
//
// Function Name: DISP_SMI_GetDispDrvFeatures
//
// Description:
//
//*****************************************************************************
Int32 DISP_SMI_GetDispDrvFeatures(
	DISPDRV_HANDLE_T 		dispH,
	const char			**driver_name,
	UInt32				*version_major,
	UInt32				*version_minor,
	DISPDRV_SUPPORT_FEATURES_T	* flags )
{
	Int32 res = -1;

	if (   ( NULL != driver_name )   && ( NULL != version_major )
	    && ( NULL != version_minor ) && (NULL != flags) )
	{
		*driver_name   = "DispSmi";
		*version_major = 0;
		*version_minor = 10;
		*flags         = DISPDRV_SUPPORT_NONE;
		res = 0;
	}
	return ( res );
}

//*****************************************************************************
//
// Function Name: DispSmi_TeOn
//
// Description:   Configure TE Input Pin & Route it to SMI module
//
//*****************************************************************************
#define TE_VC4L_IN_PIN_SELECT TE_VC4L_IN_0_LCD

static int DispSmi_TeOn (void)
{
	Int32       res;
	TECTL_CFG_t teCfg;

	teCfg.te_mode     = TE_VC4L_MODE_VSYNC;
	teCfg.sync_pol    = TE_VC4L_ACT_POL_LO;
	teCfg.vsync_width = 0;
	teCfg.hsync_line  = 0;

	res = CSL_TECTL_VC4L_OpenInput( TE_VC4L_IN_PIN_SELECT, 
		TE_VC4L_OUT_SMI, &teCfg );
	return (res);
}

//*****************************************************************************
//
// Function Name: DispSmi_TeOff
//
// Description:   Release TE Input Pin Used
//
//*****************************************************************************
static int DispSmi_TeOff (void)
{
	return (CSL_TECTL_VC4L_CloseInput(TE_VC4L_IN_PIN_SELECT));
}


//*****************************************************************************
//
// Function Name: DispSmi_reset
// 
// Description:   (De)Assert dsiplay reset  
//
//*****************************************************************************
static void DispSmi_reset(DISPDRV_HANDLE_T dispH, Boolean on)
{	
	DISP_SMI_PANEL_T  *pPanel;
	u32 gpio; 

	pPanel = (DISP_SMI_PANEL_T*) dispH;
	gpio = pPanel->rst;
	
	if (gpio != 0) {
		if (!on) {
			gpio_request(gpio, "LCD_RST1");
			gpio_direction_output(gpio, 0);
			gpio_set_value_cansleep(gpio, 1);
			msleep(1);
			gpio_set_value_cansleep(gpio, 0);
			msleep(1);
			gpio_set_value_cansleep(gpio, 1);
			msleep(20);
		} else {
			gpio_set_value_cansleep(gpio, 0);
			msleep(1);
		}
	}
}

//*****************************************************************************
//
// Function Name: DISP_SMI_Init
//
// Description:   Setup / Verify display driver init interface 
//
//*****************************************************************************
Int32 DISP_SMI_Init(
	struct dispdrv_init_parms	*parms, 
	DISPDRV_HANDLE_T	*handle)
{
	Int32   res = 0;
	DISP_SMI_PANEL_T	*pPanel;

	pPanel = &panel[0];

	if (pPanel->drvState == DRV_STATE_OFF )  {

		pPanel->is_clock_gated = 1;
		pPanel->dfs_node.name = NULL;

		LCD_DBG( LCD_DBG_INIT_ID, "[DISPDRV] %s: Bus        %d \n", 
			__func__, parms->w0.bits.bus_type);
		LCD_DBG( LCD_DBG_INIT_ID, "[DISPDRV] %s: BootMode   %d \n", 
			__func__, parms->w0.bits.boot_mode);
		LCD_DBG( LCD_DBG_INIT_ID, "[DISPDRV] %s: BusNo      %d \n", 
			__func__, parms->w0.bits.bus_no);
		LCD_DBG( LCD_DBG_INIT_ID, "[DISPDRV] %s: col_mode_i %d \n", 
			__func__, parms->w0.bits.col_mode_i);
		LCD_DBG( LCD_DBG_INIT_ID, "[DISPDRV] %s: col_mode_o %d \n", 
			__func__, parms->w0.bits.col_mode_o);
		LCD_DBG( LCD_DBG_INIT_ID, "[DISPDRV] %s: te_input   %d \n", 
			__func__, parms->w0.bits.te_input);
		
		LCD_DBG( LCD_DBG_INIT_ID, "[DISPDRV] %s: API Rev    %d \n", 
			__func__, parms->w1.bits.api_rev);
		LCD_DBG( LCD_DBG_INIT_ID, "[DISPDRV] %s: Rst 0      %d \n", 
			__func__, parms->w1.bits.lcd_rst0);
		LCD_DBG( LCD_DBG_INIT_ID, "[DISPDRV] %s: Rst 1      %d \n", 
			__func__, parms->w1.bits.lcd_rst1);
		LCD_DBG( LCD_DBG_INIT_ID, "[DISPDRV] %s: Rst 2      %d \n", 
			__func__, parms->w1.bits.lcd_rst2);

		if( (u8)parms->w1.bits.api_rev != RHEA_LCD_BOOT_API_REV ) {
			LCD_DBG( LCD_DBG_ERR_ID, "[DISPDRV] %s: "
				"Boot Init API Rev Mismatch(%d.%d vs %d.%d)\n",
				__func__, 
				(parms->w1.bits.api_rev & 0xF0) >> 8,
				(parms->w1.bits.api_rev & 0x0F)     ,
				(RHEA_LCD_BOOT_API_REV  & 0xF0) >> 8,
				(RHEA_LCD_BOOT_API_REV  & 0x0F)       );
				
			return(-1);	
		}

		pPanel->boot_mode = parms->w0.bits.boot_mode;

		pPanel->smi_cfg   = &DISP_SMI_SmiCtrlCfg; 
		pPanel->disp_info = &DISP_SMI_Info;

		pPanel->bus_ch = dispdrv2busCh(parms->w0.bits.bus_ch);
		pPanel->smi_cfg->busWidth   = 
			dispdrv2busW(parms->w0.bits.bus_width);
		pPanel->smi_cfg->colModeIn  = 
			dispdrv2cmIn(parms->w0.bits.col_mode_i);
		pPanel->smi_cfg->colModeOut = 
			dispdrv2cmOut(parms->w0.bits.col_mode_o);

		/* we support both input color modes */
		switch(pPanel->smi_cfg->colModeIn){
		case LCD_IF_CM_I_RGB565P:
			pPanel->disp_info->input_format 
				= DISPDRV_FB_FORMAT_RGB565;	
			pPanel->disp_info->Bpp = 2;
			switch(pPanel->smi_cfg->colModeOut){
			case LCD_IF_CM_O_RGB565:
				pPanel->cm_out = DISP_CM_IN_RGB565;
				break;
			case LCD_IF_CM_O_RGB666:
				pPanel->cm_out = DISP_CM_IN_RGB666;
				break;
			default:
				
				return(-1);	
			}
			break;
		case LCD_IF_CM_I_RGB888U:
			pPanel->disp_info->input_format 
				= DISPDRV_FB_FORMAT_RGB888_U;	
			pPanel->disp_info->Bpp = 4;
			switch(pPanel->smi_cfg->colModeOut){
			case LCD_IF_CM_O_RGB888:
				pPanel->cm_out = DISP_CM_IN_RGB888;
				break;
			default:
				
				return(-1);	
			}
			break;
		default: 
			
			return(-1);	
		}

		/* get reset pin */
		pPanel->rst = parms->w1.bits.lcd_rst0;
		
		pPanel->teIn = dispdrv2busTE(parms->w0.bits.te_input);
		pPanel->smi_cfg->usesTE = pPanel->teIn != TE_VC4L_IN_INV;
			
		pPanel->drvState = DRV_STATE_INIT;
		pPanel->pwrState = STATE_PWR_OFF;

		*handle = (DISPDRV_HANDLE_T)pPanel;
		
		LCD_DBG(LCD_DBG_INIT_ID, "[DISPDRV] %s: OK\n\r", __FUNCTION__ );
	} else {
		LCD_DBG( LCD_DBG_ERR_ID, "[DISPDRV] %s: Not in OFF state\n",
			__FUNCTION__ );
		res = -1;		
	} 
	return(res);  
}

//*****************************************************************************
//
// Function Name: DISP_SMI_Exit
//
// Description:
//
//*****************************************************************************
Int32 DISP_SMI_Exit (DISPDRV_HANDLE_T dispH)
{
	DISP_SMI_PANEL_T  *pPanel;

	pPanel = (DISP_SMI_PANEL_T*) dispH;
        pPanel->drvState = DRV_STATE_OFF;
	
	return (0);
}


//*****************************************************************************
//
// Function Name: DISP_SMI_Close
//
// Description:   Close The Driver
//
//*****************************************************************************
Int32 DISP_SMI_Close ( DISPDRV_HANDLE_T dispH )
{
	Int32           res;
	CSL_LCD_RES_T   cslRes;

	DISP_SMI_PANEL_T*  lcdDrv = (DISP_SMI_PANEL_T*) dispH;

	cslRes = CSL_SMI_Close( lcdDrv->cslH );
	if (cslRes == CSL_LCD_OK) {
		lcdDrv->drvState = DRV_STATE_INIT;
		lcdDrv->pwrState = STATE_PWR_OFF;
		lcdDrv->cslH = NULL;
		res = 0;
	} else {
		LCD_DBG( LCD_DBG_ERR_ID, "[DISPDRV] %s: ERR CLosing SMI Handle\n\r",
			__FUNCTION__ );
		res = -1;
	}

	if (DISP_SMI_SmiCtrlCfg.usesTE) {
		if ( DispSmi_TeOff() == -1 ) {
			LCD_DBG( LCD_DBG_ERR_ID, "[DISPDRV] %s: ERR Failed To Close "
				"TE Input\n\r", __FUNCTION__ );
			res = -1;
		}
	}

#if 0
	if (DISP_SMI_Stop (dispH)) {
		LCD_DBG( LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR to disable the clock\n",
			__FUNCTION__  );
		return ( -1 );
	}
#endif

	if (res != -1) {
		LCD_DBG( LCD_DBG_ID, "[DISPDRV] %s: OK\n\r", __FUNCTION__ );
	}

	return (res);
}



//*****************************************************************************
//
// Function Name: BCM92416_QVGA_Open
//
// Description:   Open Drivers
//
//*****************************************************************************
Int32 DISP_SMI_Open( DISPDRV_HANDLE_T dispH )
{
	Int32                           res = 0;
	CSL_LCD_RES_T                   cslRes;
	CSL_SMI_CTRL_T*                 pSmiCfg;
	UInt32                          busCh;
	DISP_SMI_PANEL_T*        pPanel;

	pPanel = (DISP_SMI_PANEL_T*) dispH;
    
	/* busId NOT RELEVANT TO SMI */
	/* busCh CS Bank, since B0 we have 2, need to cfg SMI address bus on open*/
	busCh = pPanel->bus_ch;

	if (pPanel->drvState != DRV_STATE_INIT)
	{
	    LCD_DBG ( LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR Not Initialized\r\n",
	        __FUNCTION__  );
	    return ( -1 );
	}

#if 0
	if (DISP_SMI_Start((DISPDRV_HANDLE_T)pPanel))
	{
	    LCD_DBG ( LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR to enable the clock\n",
	        __FUNCTION__  );
	    return ( -1 );
	}
#endif

	pSmiCfg = pPanel->smi_cfg;

	if( pSmiCfg->usesTE ) {
	    res = DispSmi_TeOn ();
	    if ( res == -1 )
	    {
	        LCD_DBG ( LCD_DBG_ERR_ID, "[DISPDRV] %s: Failed To Configure "
	            "TE Input\n\r", __FUNCTION__ );
	        return ( res );
	    }
	}

	/* RHEA HAS HARDCODED SMI ADDRESS LINES A1=SMI_CS(LCD_CS1) A0=SMI_nCD */

		pSmiCfg->addr_c = 0xFA;	   /* 1010  */
		pSmiCfg->addr_d = 0xFB;	   /* 1011 */


	LCD_DBG ( LCD_DBG_INIT_ID, "[DISPDRV] DISP_SMI_Open: "
		"BUSCH[0x%04X] => ADDR_CMND[0x%02X] ADDR_DATA[0x%02X]\n",
		(unsigned int)busCh, pSmiCfg->addr_c, pSmiCfg->addr_d );

       if(isFirstboot){
         /* Firstboot skip RESET */
       }else
	DispSmi_reset( dispH, FALSE );
	
	pPanel->win_dim.l = 0;  
	pPanel->win_dim.r = pPanel->disp_info->width-1; 
	pPanel->win_dim.t = 0;  
	pPanel->win_dim.b = pPanel->disp_info->height-1;
	pPanel->win_dim.w = pPanel->disp_info->width; 
	pPanel->win_dim.h = pPanel->disp_info->height;
    
	if( CSL_SMI_Init() != CSL_LCD_OK ) {
	 	LCD_DBG ( LCD_DBG_ERR_ID, "[DISPDRV] %s: CSL_SMI_Init "
			"Failed\n\r", __FUNCTION__);
		return(-1);
	}    

	if( (cslRes = CSL_SMI_Open ( pSmiCfg, &pPanel->cslH ))
	    != CSL_LCD_OK )
	{
	    LCD_DBG ( LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR Failed To Open CSL "
	        "SMI Bank, CslRes[%d]\n", __FUNCTION__, cslRes );
	    return ( -1 );
	}

#ifdef __KERNEL__
	if (csl_dma_vc4lite_init() != DMA_VC4LITE_STATUS_SUCCESS)
	{
	    LCD_DBG ( LCD_DBG_ERR_ID, "[DISPDRV] %s: csl_dma_vc4lite_init Failed\n\r",
	        __FUNCTION__);
	    return ( -1 );
	}
#endif

	pPanel->drvState = DRV_STATE_OPEN;

	return ( res );
}

static Int32 DispSmi_GetID(DISPDRV_HANDLE_T     dispH,UInt32 reg )
{
	   DISP_SMI_PANEL_T* lcdDrv = (DISP_SMI_PANEL_T*) dispH;
	   UInt32 i;
	   UInt32 id1, id2, id3;
 
         CSL_SMI_WrDirect( lcdDrv->cslH, TRUE,  reg);
         CSL_SMI_RdDirect( lcdDrv->cslH, FALSE, &id1 );
         LCD_DBG( LCD_DBG_INIT_ID,"LCD ID id1 = 0x%x\n",id1);
         CSL_SMI_RdDirect( lcdDrv->cslH, FALSE, &id1 );
         LCD_DBG( LCD_DBG_INIT_ID,"LCD ID id1 = 0x%x\n",id1);
         CSL_SMI_RdDirect( lcdDrv->cslH, FALSE, &id2 );
         LCD_DBG( LCD_DBG_INIT_ID,"LCD ID id2 = 0x%x\n",id2);
         CSL_SMI_RdDirect( lcdDrv->cslH, FALSE, &id3 );
         LCD_DBG( LCD_DBG_INIT_ID,"LCD ID id3 = 0x%x\n",id3);

         if((id1 == 0x61) && (id2 == 0xBC) && (id3 == 0xD1)){
            return LCD_PANEL_ID_ONE;
         }else{
            return LCD_PANEL_ID_NOT_SET;
         }
           
}


#define BACKLIGHT_GPIO 95
//*****************************************************************************
//
// Function Name: DISP_SMI_PowerControl
//
// Description:   Display Module Power Control
//
//*****************************************************************************
Int32 DISP_SMI_PowerControl (
    DISPDRV_HANDLE_T        dispH,
    DISPLAY_POWER_STATE_T   state )
{
    Int32  res = 0;
    DISP_SMI_PANEL_T* pPanel = (DISP_SMI_PANEL_T*)dispH;

    #define INIT_MSG_ID  LCD_DBG_ERR_ID
switch (state) {
	case CTRL_PWR_ON:
		switch (pPanel->pwrState) {
		case STATE_PWR_OFF:
				
                   if(isFirstboot){
                       isFirstboot = 0;
                       LCD_DBG( LCD_DBG_INIT_ID, "[DISPDRV] %s: isFirstboot\n\r",
			     __FUNCTION__ );
                       
                       if(LCD_PANEL_ID_NOT_SET == panelID){
                         
                           panelID = DispSmi_GetID(dispH,0x04);
                  
                       }
                   }else{

                       if(LCD_PANEL_ID_NOT_SET == panelID)
                           panelID = DispSmi_GetID(dispH,0x04);
                       
                       if(LCD_PANEL_ID_ONE == panelID){
       
                           DispSmi_ExecCmndList ( dispH, &ili9341_panel01_Init[0]);
                       }else{

                           DispSmi_ExecCmndList ( dispH, &ili9341_panel02_Init[0]);
                       }
                   }

			pPanel->pwrState = STATE_SCREEN_OFF;
			LCD_DBG( LCD_DBG_INIT_ID, "[DISPDRV] %s: INIT-SEQ\n\r",
			    __FUNCTION__ );
			break; 
		default:
			LCD_DBG( LCD_DBG_ERR_ID, "[DISPDRV] %s: POWER ON "
				"Requested While Not In POWER DOWN State\n",
				__FUNCTION__ );
			break;    
		}        
		break;
		
        case CTRL_PWR_OFF:
		if (pPanel->pwrState != STATE_PWR_OFF) {
			DispSmi_WrCmndP0(dispH, MIPI_DCS_SET_DISPLAY_OFF);
			DispSmi_reset(dispH, TRUE);
			
			pPanel->pwrState = STATE_PWR_OFF;
			
			LCD_DBG( INIT_MSG_ID, "[DISPDRV] %s: PWR DOWN\n\r",
				__FUNCTION__ );
		}
		break;


        case CTRL_SLEEP_IN:
		switch (pPanel->pwrState) {
		case STATE_SCREEN_ON:
			DispSmi_WrCmndP0(dispH, MIPI_DCS_SET_DISPLAY_OFF);
                   break;
		case STATE_SCREEN_OFF:
			DispSmi_WrCmndP0(dispH, MIPI_DCS_ENTER_SLEEP_MODE);
			OSTASK_Sleep( TICKS_IN_MILLISECONDS(120) );
			
			pPanel->pwrState = STATE_SLEEP;
			LCD_DBG( INIT_MSG_ID, "[DISPDRV] %s: SLEEP\n\r",
			    __FUNCTION__ );
			break;	    
		default:		
			LCD_DBG( LCD_DBG_ERR_ID, "[DISPDRV] %s: "
				"SLEEP Requested, But Not In "
				"DISP ON|OFF State\n\r", __FUNCTION__ );
			break;
		} 
		break;
        case CTRL_SLEEP_OUT:
		switch (pPanel->pwrState) {
                case STATE_SLEEP:
			DispSmi_ExecCmndList ( dispH, &ili9341_panel01_Sleep[0]);
			OSTASK_Sleep( TICKS_IN_MILLISECONDS(120) );
			pPanel->pwrState = STATE_SCREEN_OFF;
			LCD_DBG( LCD_DBG_ID, "[DISPDRV] %s: SLEEP-OUT\n",
				__FUNCTION__ );
			break;
                default:
			LCD_DBG( LCD_DBG_ERR_ID, "[DISPDRV] %s: "
				"SLEEP-OUT Req While Not In SLEEP State\n",
				__FUNCTION__ );
			break;    
		}
		break;
	       
        case CTRL_SCREEN_ON:
		switch (pPanel->pwrState) {
		case STATE_SCREEN_OFF:
			DispSmi_WrCmndP0(dispH, MIPI_DCS_SET_DISPLAY_ON);
			pPanel->pwrState = STATE_SCREEN_ON;
			LCD_DBG( INIT_MSG_ID, "[DISPDRV] %s: SCREEN ON\n",
				__FUNCTION__ );
			break;
                default:
			LCD_DBG( LCD_DBG_ERR_ID, "[DISPDRV] %s: "
				"SCREEN ON Req While Not In SCREEN OFF State\n",
				__FUNCTION__ );
			break;	
		}
		break;        
        case CTRL_SCREEN_OFF:
		switch (pPanel->pwrState) {
		case STATE_SCREEN_ON:
			DispSmi_WrCmndP0(dispH, MIPI_DCS_SET_DISPLAY_OFF);
			OSTASK_Sleep(TICKS_IN_MILLISECONDS(10));
			pPanel->pwrState = STATE_SCREEN_OFF;
			LCD_DBG( INIT_MSG_ID, "[DISPDRV] %s: SCREEN OFF\n",
				__FUNCTION__ );
			break;
                default:
			LCD_DBG( LCD_DBG_ERR_ID, "[DISPDRV] %s: "
				"SCREEN OFF Req While Not In SCREEN ON State\n",
				__FUNCTION__ );
			break;	
		}
		break;        
	default:
		LCD_DBG(LCD_DBG_ERR_ID, "[DISPDRV] %s: Invalid Power "
			"State[%d] Requested\n", __FUNCTION__, state );
		res = -1;
		break;
	}
	return (res);

}

//*****************************************************************************
//
// Function Name: DISP_SMI_Start
//
// Description:   Configure For Updates
//
//*****************************************************************************
Int32 DISP_SMI_Start (
	DISPDRV_HANDLE_T dispH,
	struct pi_mgr_dfs_node* dfs_node)
{

#if 0
    if (0 == lcdDrv->is_clock_gated)
	return 0;
#endif

    if (brcm_enable_smi_lcd_clocks(dfs_node))
    {
        LCD_DBG ( LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR to enable the clock\n",
            __FUNCTION__  );
        return ( -1 );
    } else {
#if 0
	lcdDrv->is_clock_gated = 0;
#endif
	return ( 0 );
    }
}

//*****************************************************************************
//
// Function Name: DISP_SMI_Stop
//
// Description:
//
//*****************************************************************************
Int32 DISP_SMI_Stop (
	DISPDRV_HANDLE_T dispH,
	struct pi_mgr_dfs_node* dfs_node )
{

#if 0
    if (1 == lcdDrv->is_clock_gated)
	return 0;
#endif

    if (brcm_disable_smi_lcd_clocks(dfs_node))
    {
        LCD_DBG ( LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR to enable the clock\n",
            __FUNCTION__  );
        return ( -1 );
    } else {
#if 0
	lcdDrv->is_clock_gated = 1;
#endif
        return 0;
    }

}

//*****************************************************************************
//
// Function Name: DISP_SMI_GetInfo
//
// Description:
//
//*****************************************************************************
const DISPDRV_INFO_T* DISP_SMI_GetDispDrvData ( DISPDRV_HANDLE_T dispH )
{
   DISP_SMI_PANEL_T* lcdDrv = (DISP_SMI_PANEL_T*) dispH;

   return (lcdDrv->disp_info );
}


//*****************************************************************************
//
// Function Name: DispSmi_Cb
//
// Description:   CSL callback
//
//*****************************************************************************
static void DispSmi_Cb ( CSL_LCD_RES_T cslRes, pCSL_LCD_CB_REC pCbRec )
{
    DISPDRV_CB_RES_T apiRes;

    LCD_DBG ( LCD_DBG_ID, "[DISPDRV] +%s\r\n", __FUNCTION__ );

    if ( pCbRec->dispDrvApiCb != NULL )
    {
        switch ( cslRes )
        {
            case CSL_LCD_OK:
              apiRes = DISPDRV_CB_RES_OK;
              break;
            default:
              apiRes = DISPDRV_CB_RES_ERR;
              break;
        }

#if 0
	if (DISP_SMI_Stop((DISPDRV_HANDLE_T)&panel[0]))
	{
        	LCD_DBG ( LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR to disable "
			"the clock\n", __FUNCTION__  );
    	}
#endif

        ((DISPDRV_CB_T)pCbRec->dispDrvApiCb)( apiRes );
    }

    LCD_DBG ( LCD_DBG_ID, "[DISPDRV] -%s\r\n", __FUNCTION__ );
}


//*****************************************************************************
//
// Function Name: DISP_SMI_Update
//
// Description:   DMA/OS Update using INT frame buffer
//
//*****************************************************************************
Int32 DISP_SMI_Update(
	DISPDRV_HANDLE_T	dispH,
	void			*buff,
	DISPDRV_WIN_t		*p_win,
	DISPDRV_CB_T    	apiCb )
{
	CSL_LCD_UPD_REQ_T       req;
	CSL_LCD_RES_T           cslRes;
	Int32                   res    = 0;
	DISP_SMI_PANEL_T *pPanel = (DISP_SMI_PANEL_T*) dispH;

	LCD_DBG( LCD_DBG_ID, "[DISPDRV] +%s\n", __FUNCTION__ );

	if (pPanel->pwrState == STATE_PWR_OFF) {
		LCD_DBG( LCD_DBG_ERR_ID, "[DISPDRV] +%s: Skip Due To Power "
			"State\n", __FUNCTION__ );
		return (-1);
	}
#if 0
	if (DISP_SMI_Start(dispH))
	{
		LCD_DBG ( LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR to enable "
			"the clock\n", __FUNCTION__  );
		return (-1);
	}
#endif

	LCD_DBG( LCD_DBG_ID, "[DISPDRV] -%s fb phys = 0x%08x\n", __FUNCTION__,(unsigned int)req.buff );

	/* update the whole screen */
	if (p_win == NULL)
		p_win = &pPanel->win_dim;

	//DispSmi_WinSet( dispH, TRUE, p_win );
	DispSmi_WrCmndP0( dispH, MIPI_DCS_WRITE_MEMORY_START );

	req.buff     	 = buff;
	req.timeOut_ms 	 = 100;
	req.buffBpp    	 = pPanel->disp_info->Bpp;
	req.lineLenP 	 = p_win->w;
	req.lineCount	 = p_win->h;
		
	req.xStrideB 	 = (pPanel->disp_info->width - req.lineLenP ) * 
    				pPanel->disp_info->Bpp;

	req.buff     	+= (pPanel->disp_info->width * p_win->t + p_win->l) * 
    				pPanel->disp_info->Bpp;

	req.cslLcdCbRec.cslH            = pPanel->cslH;
	req.cslLcdCbRec.dispDrvApiCbRev = DISP_DRV_CB_API_REV_1_0;
	req.cslLcdCbRec.dispDrvApiCb    = (void*) apiCb;
	req.cslLcdCbRec.dispDrvApiCbP1  = NULL;

	if( apiCb != NULL )
		req.cslLcdCb = DispSmi_Cb;
	else
		req.cslLcdCb = NULL;
	if(panelID==LCD_PANEL_ID_NOT_SET)
	{
		//printk("[LCD] %s There are no LCD module\n", __func__);
		
		if (req.cslLcdCb != NULL)
				DispSmi_Cb(CSL_LCD_OK, &req.cslLcdCbRec);
		return 0;
	}

	if ((cslRes = CSL_SMI_Update(pPanel->cslH, &req) ) != CSL_LCD_OK) {
		LCD_DBG( LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR[%d] returned by "
			"CSL SMI Layer\n\r", __FUNCTION__, cslRes );
		res = -1;
	}

	if ((res==-1) || (apiCb == NULL)) {
#if 0
		if (DISP_SMI_Stop(dispH)) {
			LCD_DBG ( LCD_DBG_ERR_ID, "[DISPDRV] %s: ERROR to "
				"disable the clock\n", __FUNCTION__  );
		}
#endif
	}

	LCD_DBG( LCD_DBG_ID, "[DISPDRV] -%s\n", __FUNCTION__ );

	return ( res );
}

