/*******************************************************************************
* Copyright 2010 Broadcom Corporation.  All rights reserved.
*
* 	@file	drivers/video/broadcom/displays/lcd_s6d05a1x01.h
*
* Unless you and Broadcom execute a separate DISPCTRL_WRitten software license agreement
* governing use of this software, this software is licensed to you under the
* terms of the GNU General Public License version 2, available at
* http://www.gnu.org/copyleft/gpl.html (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a license
* other than the GPL, without Broadcom's express prior DISPCTRL_WRitten consent.
*******************************************************************************/

/****************************************************************************
*
*  lcd_tiama_s6d04h0.h
*
*  PURPOSE:
*    This is the LCD-specific code for a S6d05a1x01 module.
*
*****************************************************************************/

/* ---- Include Files ---------------------------------------------------- */
#ifndef __BCM_LCD_R61531_H
#define __BCM_LCD_R61531_H

//  LCD command definitions
#define PIXEL_FORMAT_RGB565  0x05   // for 16 bits
#define PIXEL_FORMAT_RGB666  0x06   // for 18 bits
#define PIXEL_FORMAT_RGB888  0x07   // for 24 bits

const char *LCD_panel_name = "R61531 LCD";

// DISP DRV API - Display Info
static DISPDRV_INFO_T Disp_Info =
{
    DISPLAY_TYPE_LCD_STD,         // DISPLAY_TYPE_T          type;          
    480,                          // UInt32                  width;         
    360,                          // UInt32                  height;        
    DISPDRV_FB_FORMAT_RGB888_U,   // DISPDRV_FB_FORMAT_T     input_format; //@HW
	//DISPDRV_FB_FORMAT_RGB565,
    DISPLAY_BUS_DSI,              // DISPLAY_BUS_T           bus_type;    
    4,                           // bpp
    53,	//width (mm)
    40,  // height (mm)
};


DISPCTRL_REC_T sleep_out_seq_DCS_Type[] =
{

	/* + Sleep Out*/
    {DISPCTRL_WR_CMND, 0x11,0},
	/* - Sleep Out*/

	/* Wait 120ms */
    {DISPCTRL_SLEEP_MS, 0, 120},
	{DISPCTRL_LIST_END, 0, 0}    

};

DISPCTRL_REC_T sleep_out_seq_DCS_Type_1[] =
{
	/* + Sleep Out*/
    {DISPCTRL_WR_CMND, 0x11,0},
	/* - Sleep Out*/

	/* Wait 120ms */
    {DISPCTRL_SLEEP_MS, 0, 120},
	{DISPCTRL_LIST_END, 0, 0}    
};

DISPCTRL_REC_T sleep_out_seq_DCS_Type_2[] =
{
	/* + Sleep Out*/
    {DISPCTRL_WR_CMND, 0x11,0},
	/* - Sleep Out*/

	/* Wait 120ms */
    {DISPCTRL_SLEEP_MS, 0, 120},
	{DISPCTRL_LIST_END, 0, 0}    
};



DISPCTRL_REC_T power_setting_seq_GEN_Type[] =
{
    /* + Power Setting Sequence */
    {DISPCTRL_WR_CMND, 0xB0,0}, 
	{DISPCTRL_WR_DATA, 0, 0x04},

    {DISPCTRL_WR_CMND, 0xD0,0},
	{DISPCTRL_WR_DATA, 0, 0x29},
	{DISPCTRL_WR_DATA, 0, 0x04},
	{DISPCTRL_WR_DATA, 0, 0x0E},
	{DISPCTRL_WR_DATA, 0, 0x10},
    {DISPCTRL_WR_DATA, 0, 0x2A},
	{DISPCTRL_WR_DATA, 0, 0x04},
	{DISPCTRL_WR_DATA, 0, 0x01},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x08},
	{DISPCTRL_WR_DATA, 0, 0x01},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x06},
	{DISPCTRL_WR_DATA, 0, 0x01},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x20},

    {DISPCTRL_WR_CMND, 0xD1,0}, 
	{DISPCTRL_WR_DATA, 0, 0x02},
    {DISPCTRL_WR_DATA, 0, 0x26},
    {DISPCTRL_WR_DATA, 0, 0x26},
	{DISPCTRL_WR_DATA, 0, 0x4F},	
    /* - Power Setting Sequence */


    /* + Initializing Sequence */
    {DISPCTRL_WR_CMND, 0xB3,0}, 
	{DISPCTRL_WR_DATA, 0, 0x02},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},

    {DISPCTRL_WR_CMND, 0xB6,0}, 
	{DISPCTRL_WR_DATA, 0, 0x51},
	{DISPCTRL_WR_DATA, 0, 0x83},

    {DISPCTRL_WR_CMND, 0xC0,0}, 
	{DISPCTRL_WR_DATA, 0, 0x1F}, /*0x1B*/
	{DISPCTRL_WR_DATA, 0, 0x67},
	{DISPCTRL_WR_DATA, 0, 0x40},
	{DISPCTRL_WR_DATA, 0, 0x10},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},	

    {DISPCTRL_WR_CMND, 0xC1,0}, 
	{DISPCTRL_WR_DATA, 0, 0x07},
	{DISPCTRL_WR_DATA, 0, 0x27},
	{DISPCTRL_WR_DATA, 0, 0x08},
	{DISPCTRL_WR_DATA, 0, 0x08},
	{DISPCTRL_WR_DATA, 0, 0x31},

    {DISPCTRL_WR_CMND, 0xC4,0}, 
	{DISPCTRL_WR_DATA, 0, 0x76},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x03},
	{DISPCTRL_WR_DATA, 0, 0x01},

    /* - Initializing Sequence */

    /* + Gamma Setting */
    {DISPCTRL_WR_CMND, 0xC8,0}, 
    {DISPCTRL_WR_DATA, 0, 0x15},
	{DISPCTRL_WR_DATA, 0, 0x1A},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x46},	
    {DISPCTRL_WR_DATA, 0, 0x4E},
	{DISPCTRL_WR_DATA, 0, 0x60},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x2A},
    {DISPCTRL_WR_DATA, 0, 0x26},
	{DISPCTRL_WR_DATA, 0, 0x23},	
    {DISPCTRL_WR_DATA, 0, 0x20},
	{DISPCTRL_WR_DATA, 0, 0x16},	
    {DISPCTRL_WR_DATA, 0, 0x0B},	
    {DISPCTRL_WR_DATA, 0, 0x12},
	{DISPCTRL_WR_DATA, 0, 0x30},	
	{DISPCTRL_WR_DATA, 0, 0x42},
	{DISPCTRL_WR_DATA, 0, 0x4C},
	{DISPCTRL_WR_DATA, 0, 0x60},
	{DISPCTRL_WR_DATA, 0, 0x30},
	{DISPCTRL_WR_DATA, 0, 0x25},	
	{DISPCTRL_WR_DATA, 0, 0x1F},
	{DISPCTRL_WR_DATA, 0, 0x1B},	
	{DISPCTRL_WR_DATA, 0, 0x17},
	{DISPCTRL_WR_DATA, 0, 0x0A},

    {DISPCTRL_WR_CMND, 0xC9,0}, 
    {DISPCTRL_WR_DATA, 0, 0x15},
	{DISPCTRL_WR_DATA, 0, 0x1A},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x46},	
    {DISPCTRL_WR_DATA, 0, 0x4E},
	{DISPCTRL_WR_DATA, 0, 0x60},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x2A},
    {DISPCTRL_WR_DATA, 0, 0x26},
	{DISPCTRL_WR_DATA, 0, 0x23},	
    {DISPCTRL_WR_DATA, 0, 0x20},
	{DISPCTRL_WR_DATA, 0, 0x16},	
    {DISPCTRL_WR_DATA, 0, 0x0B},	
    {DISPCTRL_WR_DATA, 0, 0x12},
	{DISPCTRL_WR_DATA, 0, 0x30},	
	{DISPCTRL_WR_DATA, 0, 0x42},
	{DISPCTRL_WR_DATA, 0, 0x4C},
	{DISPCTRL_WR_DATA, 0, 0x60},
	{DISPCTRL_WR_DATA, 0, 0x30},
	{DISPCTRL_WR_DATA, 0, 0x25},	
	{DISPCTRL_WR_DATA, 0, 0x1F},
	{DISPCTRL_WR_DATA, 0, 0x1B},	
	{DISPCTRL_WR_DATA, 0, 0x17},
	{DISPCTRL_WR_DATA, 0, 0x0A},
	
    {DISPCTRL_WR_CMND, 0xCA,0}, 
    {DISPCTRL_WR_DATA, 0, 0x15},
	{DISPCTRL_WR_DATA, 0, 0x1A},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x46},	
    {DISPCTRL_WR_DATA, 0, 0x4E},
	{DISPCTRL_WR_DATA, 0, 0x60},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x2A},
    {DISPCTRL_WR_DATA, 0, 0x26},
	{DISPCTRL_WR_DATA, 0, 0x23},	
    {DISPCTRL_WR_DATA, 0, 0x20},
	{DISPCTRL_WR_DATA, 0, 0x16},	
    {DISPCTRL_WR_DATA, 0, 0x0B},	
    {DISPCTRL_WR_DATA, 0, 0x12},
	{DISPCTRL_WR_DATA, 0, 0x30},	
	{DISPCTRL_WR_DATA, 0, 0x42},
	{DISPCTRL_WR_DATA, 0, 0x4C},
	{DISPCTRL_WR_DATA, 0, 0x60},
	{DISPCTRL_WR_DATA, 0, 0x30},
	{DISPCTRL_WR_DATA, 0, 0x25},	
	{DISPCTRL_WR_DATA, 0, 0x1F},
	{DISPCTRL_WR_DATA, 0, 0x1B},	
	{DISPCTRL_WR_DATA, 0, 0x17},
	{DISPCTRL_WR_DATA, 0, 0x0A},
    /* - Gamma Setting */
	{DISPCTRL_LIST_END, 0, 0}    
};

DISPCTRL_REC_T power_setting_seq_GEN_Type_1[] =
{

    /* + Power Setting Sequence */
    {DISPCTRL_WR_CMND, 0xB0,0}, 
	{DISPCTRL_WR_DATA, 0, 0x04},

    {DISPCTRL_WR_CMND, 0xD0,0},
	{DISPCTRL_WR_DATA, 0, 0x29},
	{DISPCTRL_WR_DATA, 0, 0x04},
	{DISPCTRL_WR_DATA, 0, 0x0E},
	{DISPCTRL_WR_DATA, 0, 0x10},
    {DISPCTRL_WR_DATA, 0, 0x2A},
	{DISPCTRL_WR_DATA, 0, 0x04},
	{DISPCTRL_WR_DATA, 0, 0x01},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x08},
	{DISPCTRL_WR_DATA, 0, 0x01},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x06},
	{DISPCTRL_WR_DATA, 0, 0x01},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x20},

    {DISPCTRL_WR_CMND, 0xD1,0}, 
	{DISPCTRL_WR_DATA, 0, 0x02},
    {DISPCTRL_WR_DATA, 0, 0x26},
    {DISPCTRL_WR_DATA, 0, 0x26},
	{DISPCTRL_WR_DATA, 0, 0x4F},	
    /* - Power Setting Sequence */


    /* + Initializing Sequence */
    {DISPCTRL_WR_CMND, 0xB3,0}, 
	{DISPCTRL_WR_DATA, 0, 0x02},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},

    {DISPCTRL_WR_CMND, 0xB6,0}, 
	{DISPCTRL_WR_DATA, 0, 0x51},
	{DISPCTRL_WR_DATA, 0, 0x83},

    {DISPCTRL_WR_CMND, 0xC0,0}, 
	{DISPCTRL_WR_DATA, 0, 0x16}, /*0x1F*/ /*0x1B*/
	{DISPCTRL_WR_DATA, 0, 0x67},
	{DISPCTRL_WR_DATA, 0, 0x40},
	{DISPCTRL_WR_DATA, 0, 0x10},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},	

    {DISPCTRL_WR_CMND, 0xC1,0}, 
	{DISPCTRL_WR_DATA, 0, 0x07},
	{DISPCTRL_WR_DATA, 0, 0x27},
	{DISPCTRL_WR_DATA, 0, 0x08},
	{DISPCTRL_WR_DATA, 0, 0x08},
	{DISPCTRL_WR_DATA, 0, 0x31},

    {DISPCTRL_WR_CMND, 0xC4,0}, 
	{DISPCTRL_WR_DATA, 0, 0x76},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x03},
	{DISPCTRL_WR_DATA, 0, 0x01},

    /* - Initializing Sequence */

    /* + Gamma Setting */
    {DISPCTRL_WR_CMND, 0xC8,0}, 
    {DISPCTRL_WR_DATA, 0, 0x15},
	{DISPCTRL_WR_DATA, 0, 0x1A},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x46},	
    {DISPCTRL_WR_DATA, 0, 0x4E},
	{DISPCTRL_WR_DATA, 0, 0x60},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x2A},
    {DISPCTRL_WR_DATA, 0, 0x26},
	{DISPCTRL_WR_DATA, 0, 0x23},	
    {DISPCTRL_WR_DATA, 0, 0x20},
	{DISPCTRL_WR_DATA, 0, 0x16},	
    {DISPCTRL_WR_DATA, 0, 0x0B},	
    {DISPCTRL_WR_DATA, 0, 0x12},
	{DISPCTRL_WR_DATA, 0, 0x30},	
	{DISPCTRL_WR_DATA, 0, 0x42},
	{DISPCTRL_WR_DATA, 0, 0x4C},
	{DISPCTRL_WR_DATA, 0, 0x60},
	{DISPCTRL_WR_DATA, 0, 0x30},
	{DISPCTRL_WR_DATA, 0, 0x25},	
	{DISPCTRL_WR_DATA, 0, 0x1F},
	{DISPCTRL_WR_DATA, 0, 0x1B},	
	{DISPCTRL_WR_DATA, 0, 0x17},
	{DISPCTRL_WR_DATA, 0, 0x0A},

    {DISPCTRL_WR_CMND, 0xC9,0}, 
    {DISPCTRL_WR_DATA, 0, 0x15},
	{DISPCTRL_WR_DATA, 0, 0x1A},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x46},	
    {DISPCTRL_WR_DATA, 0, 0x4E},
	{DISPCTRL_WR_DATA, 0, 0x60},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x2A},
    {DISPCTRL_WR_DATA, 0, 0x26},
	{DISPCTRL_WR_DATA, 0, 0x23},	
    {DISPCTRL_WR_DATA, 0, 0x20},
	{DISPCTRL_WR_DATA, 0, 0x16},	
    {DISPCTRL_WR_DATA, 0, 0x0B},	
    {DISPCTRL_WR_DATA, 0, 0x12},
	{DISPCTRL_WR_DATA, 0, 0x30},	
	{DISPCTRL_WR_DATA, 0, 0x42},
	{DISPCTRL_WR_DATA, 0, 0x4C},
	{DISPCTRL_WR_DATA, 0, 0x60},
	{DISPCTRL_WR_DATA, 0, 0x30},
	{DISPCTRL_WR_DATA, 0, 0x25},	
	{DISPCTRL_WR_DATA, 0, 0x1F},
	{DISPCTRL_WR_DATA, 0, 0x1B},	
	{DISPCTRL_WR_DATA, 0, 0x17},
	{DISPCTRL_WR_DATA, 0, 0x0A},
	
    {DISPCTRL_WR_CMND, 0xCA,0}, 
    {DISPCTRL_WR_DATA, 0, 0x15},
	{DISPCTRL_WR_DATA, 0, 0x1A},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x46},	
    {DISPCTRL_WR_DATA, 0, 0x4E},
	{DISPCTRL_WR_DATA, 0, 0x60},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x2A},
    {DISPCTRL_WR_DATA, 0, 0x26},
	{DISPCTRL_WR_DATA, 0, 0x23},	
    {DISPCTRL_WR_DATA, 0, 0x20},
	{DISPCTRL_WR_DATA, 0, 0x16},	
    {DISPCTRL_WR_DATA, 0, 0x0B},	
    {DISPCTRL_WR_DATA, 0, 0x12},
	{DISPCTRL_WR_DATA, 0, 0x30},	
	{DISPCTRL_WR_DATA, 0, 0x42},
	{DISPCTRL_WR_DATA, 0, 0x4C},
	{DISPCTRL_WR_DATA, 0, 0x60},
	{DISPCTRL_WR_DATA, 0, 0x30},
	{DISPCTRL_WR_DATA, 0, 0x25},	
	{DISPCTRL_WR_DATA, 0, 0x1F},
	{DISPCTRL_WR_DATA, 0, 0x1B},	
	{DISPCTRL_WR_DATA, 0, 0x17},
	{DISPCTRL_WR_DATA, 0, 0x0A},
    /* - Gamma Setting */
	{DISPCTRL_LIST_END, 0, 0}    
};

DISPCTRL_REC_T power_setting_seq_GEN_Type_2[] =
{

    /* + Power Setting Sequence */
    {DISPCTRL_WR_CMND, 0xB0,0}, 
	{DISPCTRL_WR_DATA, 0, 0x04},

    {DISPCTRL_WR_CMND, 0xD0,0},
	{DISPCTRL_WR_DATA, 0, 0x29},
	{DISPCTRL_WR_DATA, 0, 0x04},
	{DISPCTRL_WR_DATA, 0, 0x0E},
	{DISPCTRL_WR_DATA, 0, 0x10},
    {DISPCTRL_WR_DATA, 0, 0x2A},
	{DISPCTRL_WR_DATA, 0, 0x04},
	{DISPCTRL_WR_DATA, 0, 0x01},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x08},
	{DISPCTRL_WR_DATA, 0, 0x01},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x06},
	{DISPCTRL_WR_DATA, 0, 0x01},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x20},

    /* - Power Setting Sequence */


    /* + Initializing Sequence */
    {DISPCTRL_WR_CMND, 0xB3,0}, 
	{DISPCTRL_WR_DATA, 0, 0x02},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},

    {DISPCTRL_WR_CMND, 0xB6,0}, 
	{DISPCTRL_WR_DATA, 0, 0x51},
	{DISPCTRL_WR_DATA, 0, 0x83},

    {DISPCTRL_WR_CMND, 0xC0,0}, 
	{DISPCTRL_WR_DATA, 0, 0x16}, /*0x1F*/ /*0x1B*/
	{DISPCTRL_WR_DATA, 0, 0x67},
	{DISPCTRL_WR_DATA, 0, 0x40},
	{DISPCTRL_WR_DATA, 0, 0x10},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x00},	

    {DISPCTRL_WR_CMND, 0xC1,0}, 
	{DISPCTRL_WR_DATA, 0, 0x07},
	{DISPCTRL_WR_DATA, 0, 0x27},
	{DISPCTRL_WR_DATA, 0, 0x08},
	{DISPCTRL_WR_DATA, 0, 0x08},
	{DISPCTRL_WR_DATA, 0, 0x10},

    {DISPCTRL_WR_CMND, 0xC4,0}, 
	{DISPCTRL_WR_DATA, 0, 0x76},
	{DISPCTRL_WR_DATA, 0, 0x00},
	{DISPCTRL_WR_DATA, 0, 0x03},
	{DISPCTRL_WR_DATA, 0, 0x01},

    /* - Initializing Sequence */

    /* + Gamma Setting */
    {DISPCTRL_WR_CMND, 0xC8,0}, 
    {DISPCTRL_WR_DATA, 0, 0x15},
	{DISPCTRL_WR_DATA, 0, 0x1A},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x46},	
    {DISPCTRL_WR_DATA, 0, 0x4E},
	{DISPCTRL_WR_DATA, 0, 0x60},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x2A},
	{DISPCTRL_WR_DATA, 0, 0x26},	
    {DISPCTRL_WR_DATA, 0, 0x23},
    {DISPCTRL_WR_DATA, 0, 0x20},
	{DISPCTRL_WR_DATA, 0, 0x16},	
    {DISPCTRL_WR_DATA, 0, 0x0B},	
    {DISPCTRL_WR_DATA, 0, 0x12},
	{DISPCTRL_WR_DATA, 0, 0x30},	
	{DISPCTRL_WR_DATA, 0, 0x42},
	{DISPCTRL_WR_DATA, 0, 0x4C},
	{DISPCTRL_WR_DATA, 0, 0x60},
	{DISPCTRL_WR_DATA, 0, 0x30},
	{DISPCTRL_WR_DATA, 0, 0x25},	
	{DISPCTRL_WR_DATA, 0, 0x1F},
	{DISPCTRL_WR_DATA, 0, 0x1B},	
	{DISPCTRL_WR_DATA, 0, 0x17},
	{DISPCTRL_WR_DATA, 0, 0x0A},

    {DISPCTRL_WR_CMND, 0xC9,0}, 
    {DISPCTRL_WR_DATA, 0, 0x15},
	{DISPCTRL_WR_DATA, 0, 0x1A},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x46},	
    {DISPCTRL_WR_DATA, 0, 0x4E},
	{DISPCTRL_WR_DATA, 0, 0x60},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x2A},
	{DISPCTRL_WR_DATA, 0, 0x26},	
    {DISPCTRL_WR_DATA, 0, 0x23},
    {DISPCTRL_WR_DATA, 0, 0x20},
	{DISPCTRL_WR_DATA, 0, 0x16},	
    {DISPCTRL_WR_DATA, 0, 0x0B},	
    {DISPCTRL_WR_DATA, 0, 0x12},
	{DISPCTRL_WR_DATA, 0, 0x30},	
	{DISPCTRL_WR_DATA, 0, 0x42},
	{DISPCTRL_WR_DATA, 0, 0x4C},
	{DISPCTRL_WR_DATA, 0, 0x60},
	{DISPCTRL_WR_DATA, 0, 0x30},
	{DISPCTRL_WR_DATA, 0, 0x25},	
	{DISPCTRL_WR_DATA, 0, 0x1F},
	{DISPCTRL_WR_DATA, 0, 0x1B},	
	{DISPCTRL_WR_DATA, 0, 0x17},
	{DISPCTRL_WR_DATA, 0, 0x0A},
	
    {DISPCTRL_WR_CMND, 0xCA,0}, 
    {DISPCTRL_WR_DATA, 0, 0x15},
	{DISPCTRL_WR_DATA, 0, 0x1A},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x46},	
    {DISPCTRL_WR_DATA, 0, 0x4E},
	{DISPCTRL_WR_DATA, 0, 0x60},	
	{DISPCTRL_WR_DATA, 0, 0x32},
	{DISPCTRL_WR_DATA, 0, 0x2A},
	{DISPCTRL_WR_DATA, 0, 0x26},	
    {DISPCTRL_WR_DATA, 0, 0x23},
    {DISPCTRL_WR_DATA, 0, 0x20},
	{DISPCTRL_WR_DATA, 0, 0x16},	
    {DISPCTRL_WR_DATA, 0, 0x0B},	
    {DISPCTRL_WR_DATA, 0, 0x12},
	{DISPCTRL_WR_DATA, 0, 0x30},	
	{DISPCTRL_WR_DATA, 0, 0x42},
	{DISPCTRL_WR_DATA, 0, 0x4C},
	{DISPCTRL_WR_DATA, 0, 0x60},
	{DISPCTRL_WR_DATA, 0, 0x30},
	{DISPCTRL_WR_DATA, 0, 0x25},	
	{DISPCTRL_WR_DATA, 0, 0x1F},
	{DISPCTRL_WR_DATA, 0, 0x1B},	
	{DISPCTRL_WR_DATA, 0, 0x17},
	{DISPCTRL_WR_DATA, 0, 0x0A},

    /* - Gamma Setting */
	{DISPCTRL_LIST_END, 0, 0}    
};

DISPCTRL_REC_T writemem_seq_DCS_Type[] =
{
    /* set tear scanline */
    {DISPCTRL_WR_CMND,	0x44,	0x0},
    {DISPCTRL_WR_DATA,	0x0,	0x01},
    {DISPCTRL_WR_DATA,	0x0,	0x4A},
    
    /* + TE ON */
    {DISPCTRL_WR_CMND, 0x35,0}, 
    {DISPCTRL_WR_DATA, 0, 0x00}, /* v, h blinking informagion */
    /* - TE ON */

	{DISPCTRL_WR_CMND, 0x36,0}, /*Set Address Mode*/
	{DISPCTRL_WR_DATA, 0, 0x40 /*0xC0*/},

    {DISPCTRL_WR_CMND, 0x2C,0}, /*Start Write Memory*/

	/* Display On */
	{DISPCTRL_WR_CMND, 0x29,0}, 
	{DISPCTRL_LIST_END, 0, 0}
	
};


DISPCTRL_REC_T writemem_seq_DCS_Type_1[] =
{
   /* set tear scanline */
    {DISPCTRL_WR_CMND,	0x44,	0x0},
    {DISPCTRL_WR_DATA,	0x0,	0x01},
    {DISPCTRL_WR_DATA,	0x0,	0x4A},
    
    /* + TE ON */
    {DISPCTRL_WR_CMND, 0x35,0}, 
    {DISPCTRL_WR_DATA, 0, 0x00}, /* v, h blinking informagion */
    /* - TE ON */

    {DISPCTRL_WR_CMND, 0x2C,0}, /*Start Write Memory*/

	/* Display On */
	{DISPCTRL_WR_CMND, 0x29,0}, 
	{DISPCTRL_LIST_END, 0, 0}
};

DISPCTRL_REC_T writemem_seq_DCS_Type_2[] =
{
    /* + TE ON */
    {DISPCTRL_WR_CMND, 0x35,0}, 
    {DISPCTRL_WR_DATA, 0, 0x00}, /* v, h blinking informagion */
    /* - TE ON */

    {DISPCTRL_WR_CMND, 0x2C,0}, /*Start Write Memory*/

	/* Display On */
	{DISPCTRL_WR_CMND, 0x29,0}, 
	{DISPCTRL_LIST_END, 0, 0}
};

DISPCTRL_REC_T power_off_seq_DCS_Type[] =
{
	{DISPCTRL_WR_CMND, 0x28,0}, 
	{DISPCTRL_WR_CMND, 0x10,0}, 
	{DISPCTRL_SLEEP_MS, 0, 120},
	{DISPCTRL_LIST_END, 0, 0}	
};


DISPCTRL_REC_T power_off_seq_DCS_Type_1[] =
{
	{DISPCTRL_WR_CMND, 0x28,0}, 
	{DISPCTRL_WR_CMND, 0x10,0}, 
	{DISPCTRL_SLEEP_MS, 0, 120},
	{DISPCTRL_LIST_END, 0, 0}	
};

DISPCTRL_REC_T power_off_seq_DCS_Type_2[] =
{
	{DISPCTRL_WR_CMND, 0x28,0}, 
	{DISPCTRL_WR_CMND, 0x10,0}, 
	{DISPCTRL_SLEEP_MS, 0, 120},
	{DISPCTRL_LIST_END, 0, 0}	
};

DISPCTRL_REC_T power_on_seq_DCS_Type[] =
{
	{DISPCTRL_WR_CMND, 0x11,0}, // (SLPOUT)
	{DISPCTRL_SLEEP_MS, 0, 120},
	{DISPCTRL_WR_CMND, 0x29,0}, // Display on
	{DISPCTRL_LIST_END, 0, 0}
};


DISPCTRL_REC_T power_on_seq_DCS_Type_1[] =
{
	{DISPCTRL_WR_CMND, 0x11,0}, // (SLPOUT)
	{DISPCTRL_SLEEP_MS, 0, 120},
	{DISPCTRL_WR_CMND, 0x29,0}, // Display on
	{DISPCTRL_LIST_END, 0, 0}
};

DISPCTRL_REC_T power_on_seq_DCS_Type_2[] =
{
	{DISPCTRL_WR_CMND, 0x11,0}, // (SLPOUT)
	{DISPCTRL_SLEEP_MS, 0, 120},
	{DISPCTRL_WR_CMND, 0x29,0}, // Display on
	{DISPCTRL_LIST_END, 0, 0}
};


#endif /* __BCM_LCD_R61531_H */

