/****************************************************************************
*
*	Copyright (c) 1999-2008 Broadcom Corporation
*
*   Unless you and Broadcom execute a separate written software license
*   agreement governing use of this software, this software is licensed to you
*   under the terms of the GNU General Public License version 2, available
*   at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
*
*   Notwithstanding the above, under no circumstances may you combine this
*   software in any way with any other Broadcom software provided under a
*   license other than the GPL, without Broadcom's express prior written
*   consent.
*
****************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/acct.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/ipc/ipc.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/vt_kern.h>
#include <linux/gpio.h>
#include <video/kona_fb_boot.h>
#include <video/kona_fb.h>
#include <mach/io.h>
#ifdef CONFIG_FRAMEBUFFER_FPS
#include <linux/fb_fps.h>
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/clk.h>
#include <plat/pi_mgr.h>
#include <mach/pi_mgr.h>
#include <plat/kona_cpufreq_drv.h>
#include <linux/broadcom/mobcom_types.h>
#include <mach/pm.h>
#include <mach/sec_debug.h>

#include "rhea_fb.h"
#include "lcd/display_drv.h"
#include <plat/pi_mgr.h>

/*#define RHEA_FB_DEBUG*/
//#define PARTIAL_UPDATE_SUPPORT
#define RHEA_FB_ENABLE_DYNAMIC_CLOCK	1

#define RHEA_IOCTL_SET_BUFFER_AND_UPDATE	_IO('F', 0x80)

static struct pi_mgr_qos_node g_mm_qos_node;
#ifdef CONFIG_BACKLIGHT_PWM
static struct pi_mgr_qos_node g_arm_qos_node;
#endif
#ifdef CONFIG_CPU_AT_TURBO_WHILE_LCD_ON
	struct cpufreq_lmt_node cpufreq_min_lmt_node;
#endif

struct rhea_fb {
	dma_addr_t phys_fbbase;
	spinlock_t lock;
	struct task_struct *thread;
	struct completion thread_sem;
	struct mutex update_sem;
	struct completion prev_buf_done_sem;
	atomic_t buff_idx;
	atomic_t is_fb_registered;
	atomic_t is_graphics_started;
	int base_update_count;
	int rotation;
	int is_display_found;
#ifdef CONFIG_FRAMEBUFFER_FPS
	struct fb_fps_info *fps_info;
#endif
	struct fb_info fb;
	u32 cmap[16];
	DISPDRV_T *display_ops;
	const DISPDRV_INFO_T *display_info;
	DISPDRV_HANDLE_T display_hdl;
	struct pi_mgr_dfs_node dfs_node;
	int g_stop_drawing;
	u32 gpio;
	u32 bus_width;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend_level1;
	struct early_suspend early_suspend_level2;
	struct early_suspend early_suspend_level3;
#endif
	void *buff0;
	void *buff1;
	struct dispdrv_init_parms lcd_drv_parms;
};


/* sys fs  */
struct class *lcd_class;
EXPORT_SYMBOL(lcd_class);
struct device *lcd_dev;
EXPORT_SYMBOL(lcd_dev);

static ssize_t show_lcd_info(struct device *dev, struct device_attribute *attr, char *buf);
static DEVICE_ATTR(lcd_type, S_IRUGO, show_lcd_info, NULL);

#ifdef CONFIG_LCD_ILI9341_C_SUPPORT
extern char * DispSmi_panelID(void);
#endif

static ssize_t show_lcd_info(struct device *dev, struct device_attribute *attr, char *buf)
{
#if CONFIG_LCD_ILI9486_SUPPORT // amazing
    return sprintf(buf, "%s","BOE_BT035HVMG003-A301\n" );
#elif CONFIG_LCD_ILI9341_SUPPORT  //zanin
    return sprintf(buf, "%s","BOE_BT030QVMG001-A301\n" );
#elif CONFIG_LCD_ILI9341_I_SUPPORT //ivory
    return sprintf(buf, "%s","BOE_BT030QVMG001-A301\n" );
#elif CONFIG_LCD_ILI9341_C_SUPPORT //coriplus
    return sprintf(buf, "%s",DispSmi_panelID() );
#elif CONFIG_LCD_ILI9486_SMI_SUPPORT //nevis
    return sprintf(buf, "%s","INH_BT035HVMG003-A301\n" );
#else // lucas
    return sprintf(buf, "%s","SMD_L3260HVF11\n" );
#endif
}



static struct rhea_fb *g_rhea_fb = NULL;

struct pi_mgr_qos_node qos_node;
static inline u32 convert_bitfield(int val, struct fb_bitfield *bf)
{
	unsigned int mask = (1 << bf->length) - 1;

	return (val >> (16 - bf->length) & mask) << bf->offset;
}

static int
rhea_fb_setcolreg(unsigned int regno, unsigned int red, unsigned int green,
		  unsigned int blue, unsigned int transp, struct fb_info *info)
{
	struct rhea_fb *fb = container_of(info, struct rhea_fb, fb);

	rheafb_debug("RHEA regno = %d r=%d g=%d b=%d\n", regno, red, green,
		     blue);

	if (regno < 16) {
		fb->cmap[regno] = convert_bitfield(transp, &fb->fb.var.transp) |
		    convert_bitfield(blue, &fb->fb.var.blue) |
		    convert_bitfield(green, &fb->fb.var.green) |
		    convert_bitfield(red, &fb->fb.var.red);
		return 0;
	} else {
		return 1;
	}
}

static int rhea_fb_check_var(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	rheafb_debug("RHEA %s\n", __func__);

	if ((var->rotate & 1) != (info->var.rotate & 1)) {
		if ((var->xres != info->var.yres) ||
		    (var->yres != info->var.xres) ||
		    (var->xres_virtual != info->var.yres) ||
		    (var->yres_virtual > info->var.xres * 2) ||
		    (var->yres_virtual < info->var.xres)) {
			rheafb_error("fb_check_var_failed\n");
			return -EINVAL;
		}
	} else {
		if ((var->xres != info->var.xres) ||
		    (var->yres != info->var.yres) ||
		    (var->xres_virtual != info->var.xres) ||
		    (var->yres_virtual > info->var.yres * 2) ||
		    (var->yres_virtual < info->var.yres)) {
			rheafb_error("fb_check_var_failed\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int rhea_fb_set_par(struct fb_info *info)
{
	struct rhea_fb *fb = container_of(info, struct rhea_fb, fb);

	rheafb_debug("RHEA %s\n", __func__);

	if (fb->rotation != fb->fb.var.rotate) {
		rheafb_warning("Rotation is not supported yet !\n");
		return -EINVAL;
	}

	return 0;
}

static inline void rhea_clock_start(struct rhea_fb *fb)
{
#if (RHEA_FB_ENABLE_DYNAMIC_CLOCK == 1)
	fb->display_ops->start(fb->display_hdl, &fb->dfs_node);
#endif
}

static inline void rhea_clock_stop(struct rhea_fb *fb)
{
#if (RHEA_FB_ENABLE_DYNAMIC_CLOCK == 1)
	fb->display_ops->stop(fb->display_hdl, &fb->dfs_node);
#endif
}

static void rhea_display_done_cb(int status)
{
	(void)status;
	rhea_clock_stop(g_rhea_fb);
	complete(&g_rhea_fb->prev_buf_done_sem);
}

static int rhea_fb_pan_display(struct fb_var_screeninfo *var,
			       struct fb_info *info)
{
	int ret = 0;
	struct rhea_fb *fb = container_of(info, struct rhea_fb, fb);
	uint32_t buff_idx;
#ifdef CONFIG_FRAMEBUFFER_FPS
	void *dst;
#endif
	DISPDRV_WIN_t region, *p_region;

	buff_idx = var->yoffset ? 1 : 0;

	rheafb_debug("RHEA %s with buff_idx =%d \n", __func__, buff_idx);

	if (mutex_lock_killable(&fb->update_sem))
		return -EINTR;

	if (1 == fb->g_stop_drawing) {
		rheafb_debug
		    ("RHEA FB/LCd is in the early suspend state and stops drawing now!");
		goto skip_drawing;
	}

	atomic_set(&fb->buff_idx, buff_idx);

#ifdef CONFIG_FRAMEBUFFER_FPS
	dst = (fb->fb.screen_base) +
	    (buff_idx * fb->fb.var.xres * fb->fb.var.yres *
	     (fb->fb.var.bits_per_pixel / 8));
	fb_fps_display(fb->fps_info, dst, 5, 2, 0);
#endif

	if (!atomic_read(&fb->is_fb_registered)) {
		rhea_clock_start(fb);
		ret =
		    fb->display_ops->update(fb->display_hdl,
					    buff_idx ? fb->buff1 : fb->buff0,
					    NULL, NULL);
		rhea_clock_stop(fb);
	} else {
		atomic_set(&fb->is_graphics_started, 1);
		if (var->reserved[0] == 0x54445055) {
			region.t = var->reserved[1] >> 16;
			region.l = (u16) var->reserved[1];
			region.b = (var->reserved[2] >> 16) - 1;
			region.r = (u16) var->reserved[2] - 1;
			region.w = region.r - region.l + 1;
			region.h = region.b - region.t + 1;
			region.mode = 0;
			p_region = &region;
		} else {
			p_region = NULL;
		}
		wait_for_completion(&fb->prev_buf_done_sem);
		rhea_clock_start(fb);
		ret =
		    fb->display_ops->update(fb->display_hdl,
					buff_idx ? fb->buff1 : fb->buff0,
					0,
					(DISPDRV_CB_T)rhea_display_done_cb);
	}
skip_drawing:
	mutex_unlock(&fb->update_sem);

	rheafb_debug
	    ("RHEA Display is updated once at %d time with yoffset=%d\n",
	     fb->base_update_count, var->yoffset);
	return ret;
}

static int enable_display(struct rhea_fb *fb, struct dispdrv_init_parms *parms)
{
	int ret = 0;

	ret = fb->display_ops->init(parms, &fb->display_hdl);
	if (ret != 0) {
		rheafb_error("Failed to init this display device!\n");
		goto fail_to_init;
	}

	fb->display_info = fb->display_ops->get_info(fb->display_hdl);

	rhea_clock_start(fb);
	ret = fb->display_ops->open(fb->display_hdl);
	if (ret != 0) {
		rheafb_error("Failed to open this display device!\n");
		goto fail_to_open;
	}

	ret = fb->display_ops->power_control(fb->display_hdl, CTRL_PWR_ON);
	if (ret != 0) {
		rheafb_error("Failed to power on this display device!\n");
		goto fail_to_power_control;
	}

	rhea_clock_stop(fb);
	rheafb_info("RHEA display is enabled successfully\n");
	return 0;

fail_to_power_control:
	fb->display_ops->close(fb->display_hdl);
fail_to_open:
	rhea_clock_stop(fb);
	fb->display_ops->exit(fb->display_hdl);
fail_to_init:
	return ret;

}

static int disable_display(struct rhea_fb *fb)
{
	int ret = 0;

	fb->display_ops->power_control(fb->display_hdl, CTRL_PWR_OFF);
	fb->display_ops->close(fb->display_hdl);
	fb->display_ops->exit(fb->display_hdl);

	rheafb_info("RHEA display is disabled successfully\n");
	return ret;
}

static int rhea_fb_ioctl(struct fb_info *info, unsigned int cmd,
			 unsigned long arg)
{
	void *ptr = NULL;
	int ret = 0;
	struct rhea_fb *fb = container_of(info, struct rhea_fb, fb);

	rheafb_debug("RHEA ioctl called! Cmd %x, Arg %lx\n", cmd, arg);
	switch (cmd) {

	case RHEA_IOCTL_SET_BUFFER_AND_UPDATE:

		if (mutex_lock_killable(&fb->update_sem)) {
			return -EINTR;
		}
		ptr = (void *)arg;

		if (ptr == NULL) {
			mutex_unlock(&fb->update_sem);
			return -EFAULT;
		}

		wait_for_completion(&fb->prev_buf_done_sem);
		rhea_clock_start(fb);
		ret = fb->display_ops->update(fb->display_hdl, ptr, NULL, NULL);
		rhea_clock_stop(fb);
		complete(&g_rhea_fb->prev_buf_done_sem);
		mutex_unlock(&fb->update_sem);
		break;

	default:

		rheafb_error("Wrong ioctl cmd\n");
		break;
	}

	return ret;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void rhea_fb_early_suspend(struct early_suspend *h)
{
	struct rhea_fb *fb;
#ifdef CONFIG_CPU_AT_TURBO_WHILE_LCD_ON
		u32 freq_eco = 0;
#endif

	rheafb_error("BRCM fb early suspend with level = %d\n", h->level);

	switch (h->level) {

	case EARLY_SUSPEND_LEVEL_BLANK_SCREEN:

#ifdef CONFIG_BACKLIGHT_PWM
		/*
		LCD supply LDO Opmode is set to 0x11(LPM when PC1 is low)
		For both TCL and Rheastone HW, PWM is used to control LCD
		backlight. PWM clock will be always active when PWM back
		light is ON. PWM clock will make sure that fabric CCU won't
		enter retention and PC1 won't go LOW when the backlight
		is ON. During early suspend, back light is turned off (and
		thereby PWM clock) before LCD is powered down and during late
		resume, LCD is powered ON before back light is turned ON.

		In this small window during early suspend/ late resume
		where LCD is not powered down but back light is OFF,
		all CCUs can enter low power mode (PC1 will go LOW) and LCD
		supply LDO opmode will be set to LPM even though LCD is
		not powered down. To resolve this issue A9 dormant is
		disabled using QoS while processing early suspend/late resume
		requests in FB driver*/

		pi_mgr_qos_request_update(&g_arm_qos_node, 0);
#endif

#ifdef CONFIG_CPU_AT_TURBO_WHILE_LCD_ON
				freq_eco = get_cpu_freq_from_opp(PI_OPP_ECONOMY);
		if (freq_eco != 0)
			cpufreq_update_lmt_req(&cpufreq_min_lmt_node,
						freq_eco);
#endif

		/* Turn off the backlight */
		fb = container_of(h, struct rhea_fb, early_suspend_level1);
		mutex_lock(&fb->update_sem);
		wait_for_completion(&fb->prev_buf_done_sem);
		rhea_clock_start(fb);
		if (fb->display_ops->power_control(fb->display_hdl,
					       CTRL_SCREEN_OFF))
			rheafb_error("Failed to blank this display device!\n");
		rhea_clock_stop(fb);
		complete(&g_rhea_fb->prev_buf_done_sem);
		mutex_unlock(&fb->update_sem);

		break;

	case EARLY_SUSPEND_LEVEL_STOP_DRAWING:
		fb = container_of(h, struct rhea_fb, early_suspend_level2);
		mutex_lock(&fb->update_sem);
		wait_for_completion(&fb->prev_buf_done_sem);
		fb->g_stop_drawing = 1;
		complete(&g_rhea_fb->prev_buf_done_sem);
		mutex_unlock(&fb->update_sem);
		break;

	case EARLY_SUSPEND_LEVEL_DISABLE_FB:
		fb = container_of(h, struct rhea_fb, early_suspend_level3);
		/* screen goes to sleep mode */
		mutex_lock(&fb->update_sem);
		rhea_clock_start(fb);
		disable_display(fb);
		rhea_clock_stop(fb);
		mutex_unlock(&fb->update_sem);
		pi_mgr_qos_request_update(&qos_node, PI_MGR_QOS_DEFAULT_VALUE);
		/* Ok for MM going to shutdown state */
		pi_mgr_qos_request_update(&g_mm_qos_node,
					  PI_MGR_QOS_DEFAULT_VALUE);
#ifdef CONFIG_BACKLIGHT_PWM
		pi_mgr_qos_request_update(&g_arm_qos_node,
				PI_MGR_QOS_DEFAULT_VALUE);
#endif
		break;

	default:
		rheafb_error("Early suspend with the wrong level!\n");
		break;
	}
}
extern unsigned int lp_boot_mode;
static void rhea_fb_late_resume(struct early_suspend *h)
{
	struct rhea_fb *fb;
#ifdef CONFIG_CPU_AT_TURBO_WHILE_LCD_ON
		u32 freq_turbo;
#endif

	rheafb_error("BRCM fb late resume with level = %d\n", h->level);

	switch (h->level) {

	case EARLY_SUSPEND_LEVEL_BLANK_SCREEN:
		/* Turn on the backlight */
		fb = container_of(h, struct rhea_fb, early_suspend_level1);
		rhea_clock_start(fb);
		if (fb->display_ops->
		    power_control(fb->display_hdl, CTRL_SCREEN_ON))
			rheafb_error
			    ("Failed to unblank this display device!\n");
		rhea_clock_stop(fb);
#ifdef CONFIG_BACKLIGHT_PWM
		 pi_mgr_qos_request_update(&g_arm_qos_node,
					PI_MGR_QOS_DEFAULT_VALUE);
#endif
#ifdef CONFIG_CPU_AT_TURBO_WHILE_LCD_ON
		 freq_turbo = get_cpu_freq_from_opp(PI_OPP_TURBO);
					 if (freq_turbo != 0)
						 cpufreq_update_lmt_req(&cpufreq_min_lmt_node,
							 freq_turbo);
#endif

             /* to handel power off charging animation */
             if(1 == lp_boot_mode){
                 rhea_fb_pan_display(&fb->fb.var, &fb->fb);
                 mdelay(200);
             }

		break;

	case EARLY_SUSPEND_LEVEL_STOP_DRAWING:
		fb = container_of(h, struct rhea_fb, early_suspend_level2);
		mutex_lock(&fb->update_sem);
		fb->g_stop_drawing = 0;
		mutex_unlock(&fb->update_sem);
		break;

	case EARLY_SUSPEND_LEVEL_DISABLE_FB:
#ifdef CONFIG_BACKLIGHT_PWM
		/*
		LCD supply LDO Opmode is set to 0x11(LPM when PC1 is low)
		For both TCL and Rheastone HW, PWM is used to control LCD
		backlight. PWM clock will be always active when PWM back
		light is ON. PWM clock will make sure that fabric CCU won't
		enter retention and PC1 won't go LOW when the backlight
		is ON. During early suspend, back light is turned off (and
		thereby PWM clock) before LCD is powered down and during late
		resume, LCD is powered ON before back light is turned ON.

		In this small window during early suspend/ late resume
		where LCD is not powered down but back light is OFF,
		all CCUs can enter low power mode (PC1 will go LOW) and LCD
		supply LDO opmode will be set to LPM even though LCD is
		not powered down. To resolve this issue A9 dormant is
		disabled using QoS while processing early suspend/late resume
		requests in FB driver
		*/
		 pi_mgr_qos_request_update(&g_arm_qos_node, 0);
#endif
		fb = container_of(h, struct rhea_fb, early_suspend_level3);
		/* Ok for MM going to retention but not shutdown state */
		pi_mgr_qos_request_update(&g_mm_qos_node, 10);
		pi_mgr_qos_request_update(&qos_node, DEEP_SLEEP_LATENCY-1);
		/* screen comes out of sleep */
		if (enable_display(fb, &fb->lcd_drv_parms))
			rheafb_error("Failed to enable this display device\n");
		break;

	default:
		rheafb_error("Early suspend with the wrong level!\n");
		break;
	}

}
#endif

static struct fb_ops rhea_fb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = rhea_fb_check_var,
	.fb_set_par = rhea_fb_set_par,
	.fb_setcolreg = rhea_fb_setcolreg,
	.fb_pan_display = rhea_fb_pan_display,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_ioctl = rhea_fb_ioctl,
};

static int rhea_fb_probe(struct platform_device *pdev)
{
	int ret = -ENXIO;
	struct rhea_fb *fb;
	size_t framesize;
	uint32_t width, height;
	int ret_val = -1;
	struct kona_fb_platform_data *fb_data;

	if (g_rhea_fb && (g_rhea_fb->is_display_found == 1)) {
		rheafb_info("A right display device is already found!\n");
		return -EINVAL;
	}

	fb = kzalloc(sizeof(struct rhea_fb), GFP_KERNEL);
	if (fb == NULL) {
		rheafb_error("Unable to allocate framebuffer structure\n");
		ret = -ENOMEM;
		goto err_fb_alloc_failed;
	}
	fb->g_stop_drawing = 0;

	g_rhea_fb = fb;
	ret_val =
	    pi_mgr_dfs_add_request(&g_rhea_fb->dfs_node, "lcd", PI_MGR_PI_ID_MM,
				   PI_MGR_DFS_MIN_VALUE);
	if (ret_val) {
		printk(KERN_ERR "Failed to add dfs request for LCD\n");
		ret = -EIO;
		goto fb_data_failed;
	}

	fb_data = pdev->dev.platform_data;
	if (!fb_data) {
		ret = -EINVAL;
		goto fb_data_failed;
	}
	fb->display_ops = (DISPDRV_T *) fb_data->dispdrv_entry();

	spin_lock_init(&fb->lock);
	platform_set_drvdata(pdev, fb);

	mutex_init(&fb->update_sem);
	atomic_set(&fb->buff_idx, 0);
	atomic_set(&fb->is_fb_registered, 0);
	init_completion(&fb->prev_buf_done_sem);
	complete(&fb->prev_buf_done_sem);
	atomic_set(&fb->is_graphics_started, 0);
	init_completion(&fb->thread_sem);

#if (RHEA_FB_ENABLE_DYNAMIC_CLOCK != 1)
	fb->display_ops->start(&fb->dfs_node);
#endif
	/* Enable_display will start/stop clocks on its own if dynamic */
	fb->lcd_drv_parms = *(struct dispdrv_init_parms *)&fb_data->parms;
	ret = enable_display(fb, &fb->lcd_drv_parms);
	if (ret) {
		rheafb_error("Failed to enable this display device\n");
		goto err_enable_display_failed;
	} else {
		fb->is_display_found = 1;
	}

	framesize = fb->display_info->width * fb->display_info->height *
	    fb->display_info->Bpp * 2;
	fb->fb.screen_base = dma_alloc_writecombine(&pdev->dev,
						    framesize, &fb->phys_fbbase,
						    GFP_KERNEL);
	if (fb->fb.screen_base == NULL) {
		ret = -ENOMEM;
		rheafb_error("Unable to allocate fb memory\n");
		goto err_fbmem_alloc_failed;
	}

	/* Now we should get correct width and height for this display .. */
	width = fb->display_info->width;
	height = fb->display_info->height;
	fb->buff0 = (void *)fb->phys_fbbase;
	fb->buff1 =
	    (void *)fb->phys_fbbase + width * height * fb->display_info->Bpp;

	fb->fb.fbops = &rhea_fb_ops;
	fb->fb.flags = FBINFO_FLAG_DEFAULT;
	fb->fb.pseudo_palette = fb->cmap;
	fb->fb.fix.type = FB_TYPE_PACKED_PIXELS;
	fb->fb.fix.visual = FB_VISUAL_TRUECOLOR;
	fb->fb.fix.line_length = width * fb->display_info->Bpp;
	fb->fb.fix.accel = FB_ACCEL_NONE;
	fb->fb.fix.ypanstep = 1;
	fb->fb.fix.xpanstep = 4;
#ifdef PARTIAL_UPDATE_SUPPORT
	fb->fb.fix.reserved[0] = 0x5444;
	fb->fb.fix.reserved[1] = 0x5055;
#endif
	fb->fb.var.xres = width;
	fb->fb.var.yres = height;
	fb->fb.var.xres_virtual = width;
	fb->fb.var.yres_virtual = height * 2;
	fb->fb.var.bits_per_pixel = fb->display_info->Bpp << 3;
	fb->fb.var.activate = FB_ACTIVATE_NOW;
	fb->fb.var.height = fb->display_info->phys_height;
	fb->fb.var.width = fb->display_info->phys_width;

#ifdef CONFIG_CDEBUGGER
	/* it has dependency on h/w */
	frame_buf_mark.p_fb = (void *)(fb->phys_fbbase - PHYS_OFFSET);
	frame_buf_mark.resX = fb->fb.var.xres;
	frame_buf_mark.resY = fb->fb.var.yres;
	frame_buf_mark.bpp = fb->fb.var.bits_per_pixel;
#endif

	switch (fb->display_info->input_format) {
	case DISPDRV_FB_FORMAT_RGB565:
		fb->fb.var.red.offset = 11;
		fb->fb.var.red.length = 5;
		fb->fb.var.green.offset = 5;
		fb->fb.var.green.length = 6;
		fb->fb.var.blue.offset = 0;
		fb->fb.var.blue.length = 5;
		framesize = width * height * 2 * 2;
		break;

	case DISPDRV_FB_FORMAT_RGB888_U:
		fb->fb.var.red.offset = 16;
		fb->fb.var.red.length = 8;
		fb->fb.var.green.offset = 8;
		fb->fb.var.green.length = 8;
		fb->fb.var.blue.offset = 0;
		fb->fb.var.blue.length = 8;
		fb->fb.var.transp.offset = 24;
		fb->fb.var.transp.length = 8;
		framesize = width * height * 4 * 2;
		break;

	default:
		rheafb_error("Wrong format!\n");
		break;
	}

	fb->fb.fix.smem_start = fb->phys_fbbase;
	fb->fb.fix.smem_len = framesize;

	//{{ Mark for GetLog
	sec_getlog_supply_fbinfo(fb->fb.screen_base, fb->fb.var.xres, fb->fb.var.yres, fb->fb.var.bits_per_pixel,2);

	rheafb_debug
	    ("Framebuffer starts at phys[0x%08x], and virt[0x%08x] with frame size[0x%08x]\n",
	     fb->phys_fbbase, (uint32_t) fb->fb.screen_base, framesize);

	ret = fb_set_var(&fb->fb, &fb->fb.var);
	if (ret) {
		rheafb_error("fb_set_var failed\n");
		goto err_set_var_failed;
	}
    
	/* Paint it black (assuming default fb contents are all zero) 
	ret = rhea_fb_pan_display(&fb->fb.var, &fb->fb);
	if (ret) {
		rheafb_error("Can not enable the LCD!\n");
		goto err_fb_register_failed;
	}*/

	if (fb->display_ops->set_brightness) {
		rhea_clock_start(fb);
		fb->display_ops->set_brightness(fb->display_hdl, 100);
		rhea_clock_stop(fb);
	}

	/* Display on after painted blank */
	rhea_clock_start(fb);
	fb->display_ops->power_control(fb->display_hdl, CTRL_SCREEN_ON);
	rhea_clock_stop(fb);

	ret = register_framebuffer(&fb->fb);
	if (ret) {
		rheafb_error("Framebuffer registration failed\n");
		goto err_fb_register_failed;
	}
#ifdef CONFIG_FRAMEBUFFER_FPS
	fb->fps_info = fb_fps_register(&fb->fb);
	if (NULL == fb->fps_info)
		printk(KERN_ERR "No fps display");
#endif
	complete(&fb->thread_sem);

	atomic_set(&fb->is_fb_registered, 1);
	rheafb_info("RHEA Framebuffer probe successfull\n");

#ifdef CONFIG_LOGO
	fb_prepare_logo(&fb->fb, 0);
	fb_show_logo(&fb->fb, 0);

	mutex_lock(&fb->update_sem);
	rhea_clock_start(fb);
	fb->display_ops->update(fb->display_hdl, fb->buff0, NULL, NULL);
	rhea_clock_stop(fb);
	mutex_unlock(&fb->update_sem);
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	fb->early_suspend_level1.suspend = rhea_fb_early_suspend;
	fb->early_suspend_level1.resume = rhea_fb_late_resume;
	fb->early_suspend_level1.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	register_early_suspend(&fb->early_suspend_level1);

	fb->early_suspend_level2.suspend = rhea_fb_early_suspend;
	fb->early_suspend_level2.resume = rhea_fb_late_resume;
	fb->early_suspend_level2.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING;
	register_early_suspend(&fb->early_suspend_level2);

	fb->early_suspend_level3.suspend = rhea_fb_early_suspend;
	fb->early_suspend_level3.resume = rhea_fb_late_resume;
	fb->early_suspend_level3.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	register_early_suspend(&fb->early_suspend_level3);
#endif

    /* sys fs */
	lcd_class = class_create(THIS_MODULE, "lcd");
	if (IS_ERR(lcd_class))
		pr_err("Failed to create class(lcd)!\n");

	lcd_dev = device_create(lcd_class, NULL, 0, NULL, "panel");
	if (IS_ERR(lcd_dev))
		pr_err("Failed to create device(lcd)!\n");

	if (device_create_file(lcd_dev, &dev_attr_lcd_type) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_lcd_type.attr.name); 
	/* sys fs */

	return 0;

err_fb_register_failed:
err_set_var_failed:
	dma_free_writecombine(&pdev->dev, fb->fb.fix.smem_len,
			      fb->fb.screen_base, fb->fb.fix.smem_start);

	rhea_clock_start(fb);
	disable_display(fb);
	rhea_clock_stop(fb);

err_enable_display_failed:
#if (RHEA_FB_ENABLE_DYNAMIC_CLOCK != 1)
	fb->display_ops->stop(&fb->dfs_node);
#endif
err_fbmem_alloc_failed:
	if (pi_mgr_dfs_request_remove(&fb->dfs_node)) {
		printk(KERN_ERR "Failed to remove dfs request for LCD\n");
	}
fb_data_failed:
	kfree(fb);
	g_rhea_fb = NULL;
err_fb_alloc_failed:
	return ret;
}

static int __devexit rhea_fb_remove(struct platform_device *pdev)
{
	size_t framesize;
	struct rhea_fb *fb = platform_get_drvdata(pdev);

	framesize = fb->fb.var.xres_virtual * fb->fb.var.yres_virtual * 2;

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&fb->early_suspend_level1);
	unregister_early_suspend(&fb->early_suspend_level2);
	unregister_early_suspend(&fb->early_suspend_level3);
#endif

#ifdef CONFIG_FRAMEBUFFER_FPS
	fb_fps_unregister(fb->fps_info);
#endif
	unregister_framebuffer(&fb->fb);
	disable_display(fb);
	kfree(fb);
	rheafb_info("RHEA FB removed !!\n");
	return 0;
}

static struct platform_driver rhea_fb_driver = {
	.probe = rhea_fb_probe,
	.remove = __devexit_p(rhea_fb_remove),
	.driver = {
		   .name = "rhea_fb"}
};

static int __init rhea_fb_init(void)
{
	int ret;
#ifdef CONFIG_CPU_AT_TURBO_WHILE_LCD_ON
	u32 freq_turbo = 0;
#endif

	ret =
	    pi_mgr_qos_add_request(&g_mm_qos_node, "lcd", PI_MGR_PI_ID_MM, 10);
#ifdef CONFIG_BACKLIGHT_PWM
	ret |= pi_mgr_qos_add_request(&g_arm_qos_node, "lcd",
		PI_MGR_PI_ID_ARM_CORE, PI_MGR_QOS_DEFAULT_VALUE);
#endif
	if (ret)
		printk(KERN_ERR "failed to register qos client for lcd\n");

#ifdef CONFIG_CPU_AT_TURBO_WHILE_LCD_ON
	freq_turbo = get_cpu_freq_from_opp(PI_OPP_TURBO);
	if (freq_turbo != 0)
			cpufreq_add_lmt_req(&cpufreq_min_lmt_node, "rhea_fb",
				freq_turbo, MIN_LIMIT);
#endif

	ret = platform_driver_register(&rhea_fb_driver);
	if (ret) {
		printk(KERN_ERR
		       "%s : Unable to register Rhea framebuffer driver\n",
		       __func__);
		goto fail_to_register;
	}
	pi_mgr_qos_add_request(&qos_node, "lcd",
			   PI_MGR_PI_ID_ARM_CORE, DEEP_SLEEP_LATENCY-1);

fail_to_register:
	printk(KERN_INFO "BRCM Framebuffer Init %s !\n", ret ? "FAILED" : "OK");

	return ret;
}

static void __exit rhea_fb_exit(void)
{
	platform_driver_unregister(&rhea_fb_driver);
	printk(KERN_INFO "BRCM Framebuffer exit OK\n");
}

late_initcall(rhea_fb_init);
module_exit(rhea_fb_exit);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("RHEA FB Driver");
