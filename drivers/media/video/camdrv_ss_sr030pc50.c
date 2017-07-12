

#include <linux/i2c.h>


#include <linux/delay.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/completion.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>
#include <linux/videodev2_brcm.h>
#include <camdrv_ss.h>
#include <camdrv_ss_sr030pc50.h>


#define SR030PC50_NAME	"sr030pc50"
#define SR030PC50_DEFAULT_PIX_FMT		V4L2_PIX_FMT_UYVY	/* YUV422 */
#define SR030PC50_DEFAULT_MBUS_PIX_FMT    V4L2_MBUS_FMT_UYVY8_2X8
#define SR030PC50_REGISTER_SIZE 2
#define SR030PC50_DELAY_DURATION 0xFF

extern inline struct camdrv_ss_state *to_state(struct v4l2_subdev *sd);

/*  GPIO numbers  needed for power on sequence.
  * If the same sensor is used for different variants/targets. please define those numbers here
  */
#if defined(CONFIG_MACH_RHEA_SS_LUCAS) || defined(CONFIG_MACH_RHEA_IVORY)  || defined(CONFIG_MACH_RHEA_IVORYSS) || defined(CONFIG_MACH_RHEA_CORSICA)
	static struct regulator *VCAM_IO_1_8_V;  /* LDO_HV9 */
	static struct regulator *VCAM_A_2_8_V;   /* LDO_CAM12/12/2011 */
#ifdef CONFIG_SOC_CAMERA_POWER_USE_ASR   //for hw rev 0.3
    static struct regulator *VCAM_CORE_1_2_V;   //ASR_SW
#else
	#define CAM_CORE_EN	   42
#endif
	#define CAM0_RESET    33
	#define CAM0_STNBY    111
	#define CAM1_RESET    23
	#define CAM1_STNBY    34
	#define SENSOR_0_CLK			"dig_ch0_clk"    //(common)
	#define SENSOR_0_CLK_FREQ		(26000000) //@HW, need to check how fast this meaning.
#else  //ZANIN and others
	static struct regulator *VCAM_IO_1_8_V;  //LDO_HV9
	static struct regulator *VCAM_A_2_8_V;   //LDO_CAM12/12/2011
#ifdef CONFIG_SOC_CAMERA_POWER_USE_ASR   //for hw rev 0.3
    static struct regulator *VCAM_CORE_1_2_V;   //ASR_SW
#else
	#define CAM_CORE_EN	   36
#endif
	//#define CAM_AF_EN     121
	#define CAM0_RESET    33
	#define CAM0_STNBY    111
	#define CAM1_RESET    119
	#define CAM1_STNBY    113
	#define SENSOR_0_CLK			"dig_ch0_clk"    /* (common) */
	#define SENSOR_0_CLK_FREQ		(26000000) /* @HW, need to check how fast this meaning. */
#endif

#if defined(CONFIG_MACH_RHEA_SS_LUCAS)
#define EXIF_SOFTWARE		""
#define EXIF_MAKE		"SAMSUNG"
#define EXIF_MODEL		"GT-B7810"
#else
	#define EXIF_SOFTWARE		""
	#define EXIF_MAKE		"Samsung"
	#define EXIF_MODEL		"GT-1234"
#endif


static const struct camdrv_ss_framesize sr030pc50_supported_preview_framesize_list[] = {
	{ PREVIEW_SIZE_VGA,	640,  480 },
};

static const struct camdrv_ss_framesize  sr030pc50_supported_capture_framesize_list[] = {
	{ CAPTURE_SIZE_VGA, 640,  480 }
};

const static struct v4l2_fmtdesc sr030pc50_fmts[] = {
	{
	.index		= 0,
	.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
	.flags		= 0,
	.description	= "UYVY",
	.pixelformat	= V4L2_MBUS_FMT_UYVY8_2X8,
	},
};

static const struct v4l2_queryctrl sr030pc50_controls[] = {
	{
		.id			= V4L2_CID_CAMERA_FLASH_MODE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Flash",
		.minimum	= FLASH_MODE_OFF,
		 .maximum	= (1 << FLASH_MODE_OFF),
		.step		= 1,
		.default_value	= FLASH_MODE_OFF,
	},	
	{
		.id			= V4L2_CID_CAMERA_BRIGHTNESS,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Brightness",
		.minimum	= EV_MINUS_4,
		.maximum	= EV_PLUS_4,
		.step		= 1,
		.default_value	= EV_DEFAULT,
	},

	
	{
		.id			= V4L2_CID_CAMERA_FOCUS_MODE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Focus",
		.minimum	      = FOCUS_MODE_INFINITY,
		.maximum	=  (1 << FOCUS_MODE_INFINITY),
		.step		= 1,
		.default_value	= FOCUS_MODE_INFINITY,
	},	

	{
		.id			= V4L2_CID_CAMERA_SCENE_MODE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Scene Mode",
		.minimum	= SCENE_MODE_NONE,
		.maximum	= (1 << SCENE_MODE_NONE),
		.step		= 1,
		.default_value	= SCENE_MODE_NONE,
	},	
	
	{
		.id			= V4L2_CID_CAMERA_WHITE_BALANCE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Whilte Balance",
		.minimum	= WHITE_BALANCE_AUTO ,
		.maximum	= (1 << WHITE_BALANCE_AUTO | 1 << WHITE_BALANCE_DAYLIGHT/*WHITE_BALANCE_SUNNY*/ | 1 << WHITE_BALANCE_CLOUDY
			           | 1 << WHITE_BALANCE_INCANDESCENT/*WHITE_BALANCE_TUNGSTEN*/ | 1 << WHITE_BALANCE_FLUORESCENT ),
		.step		= 1,
		.default_value	= WHITE_BALANCE_AUTO,
	},
	
	{
		.id			= V4L2_CID_CAMERA_EFFECT,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Color Effects",
		.minimum	= IMAGE_EFFECT_NONE,
		.maximum	= (1 << IMAGE_EFFECT_NONE | 1 << IMAGE_EFFECT_MONO/*IMAGE_EFFECT_BNW*/
						| 1 << IMAGE_EFFECT_SEPIA | 1 << IMAGE_EFFECT_NEGATIVE), /* this should be replace by querymenu */
		.step		= 1,
		.default_value	= IMAGE_EFFECT_NONE,
	},
	
	{
		.id			= V4L2_CID_CAMERA_METERING,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Metering",
		.minimum	= METERING_MATRIX,
		.maximum	= (1 << METERING_MATRIX | 1 << METERING_CENTER | 1 << METERING_SPOT),
		.step		= 1,
		.default_value	= METERING_CENTER,
	},
	
	{
		.id			= V4L2_CID_CAMERA_CONTRAST,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Contrast",
		.minimum	= CONTRAST_MINUS_2,
		.maximum	= CONTRAST_PLUS_2,
		.step		= 1,
		.default_value	= CONTRAST_DEFAULT,
	},
	
	{
		.id			= V4L2_CID_CAM_JPEG_QUALITY,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "JPEG Quality",
		.minimum	= 0,
		.maximum	= 100,
		.step		= 1,
		.default_value	= 100,
	},
	
	{
		.id			= V4L2_CID_CAMERA_FRAME_RATE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Framerate control",
		.minimum	= FRAME_RATE_AUTO,
		.maximum	= (1 << FRAME_RATE_AUTO |/* 1 << FRAME_RATE_5 | 1 << FRAME_RATE_7 |*/ 1 << FRAME_RATE_10  | 1 << FRAME_RATE_15
						/*| 1 << FRAME_RATE_20*/ | 1 << FRAME_RATE_25 | 1 << FRAME_RATE_30),
		.step		= 1,
		.default_value	= FRAME_RATE_AUTO,

	},
#if 0
	{
		.id			= V4L2_CID_CAMERA_CAPTURE,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Capture",
		.minimum	= 0,
		.maximum	= 0,
		.step		= 0,
		.default_value	= 0,
	},
	
	{
		.id			= V4L2_CID_CAM_PREVIEW_ONOFF,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Preview control",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	},
	
	{
		.id			= V4L2_CID_CAMERA_CHECK_DATALINE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Check Dataline",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	},

	{
		.id			= V4L2_CID_CAMERA_CHECK_DATALINE_STOP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Check Dataline Stop",
		.minimum	= 0,
		.maximum	= 0,
		.step		= 0,
		.default_value	= 0,
	},
#endif
	{
		.id			= V4L2_CID_CAMERA_ANTI_BANDING,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Anti Banding",
		.minimum    = ANTI_BANDING_AUTO,
		.maximum	= 	 (1 << ANTI_BANDING_AUTO | 1<< ANTI_BANDING_50HZ | 1 << ANTI_BANDING_60HZ
                              |       1 << ANTI_BANDING_OFF),
		.step		= 1,
		.default_value	= ANTI_BANDING_AUTO,
	},
	
	{
		.id			= V4L2_CID_CAMERA_VT_MODE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Vtmode",
		.minimum	      = CAM_VT_MODE_NONE,
		.maximum	= CAM_VT_MODE_VOIP,
		.step		= 1,
		.default_value	= CAM_VT_MODE_3G,
	},
			
	{
		.id			= V4L2_CID_CAMERA_SENSOR_MODE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Cam mode",
		.minimum	      = SENSOR_CAMERA,
		.maximum	= SENSOR_MOVIE,
		.step		= 1,
		.default_value	= SENSOR_CAMERA,
	},	
};


static int camdrv_ss_sr030pc50_enum_frameintervals(struct v4l2_subdev *sd, struct v4l2_frmivalenum *fival)
{
	int err = 0;
	int size, i;

	if (fival->index >= 1)
		return -EINVAL;

	CAM_INFO_PRINTK(" %s :  E\n", __func__);

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;

	for (i = 0; i < ARRAY_SIZE(sr030pc50_supported_preview_framesize_list); i++) {
		if ((sr030pc50_supported_preview_framesize_list[i].width == fival->width) &&
		    (sr030pc50_supported_preview_framesize_list[i].height == fival->height)) {
			size = sr030pc50_supported_preview_framesize_list[i].index;
			break;
		}
	}
	if (i == ARRAY_SIZE(sr030pc50_supported_preview_framesize_list)) {
		CAM_ERROR_PRINTK("%s unsupported width = %d and height = %d\n",
			__func__, fival->width, fival->height);
		return -EINVAL;
	}

	switch (size) {
	case PREVIEW_SIZE_VGA:
		fival->discrete.numerator = 1;
		fival->discrete.denominator = 30;
		break;
	default:
		fival->discrete.numerator = 1;
		fival->discrete.denominator = 30;
		break;
	}

	return err;
}



static long camdrv_ss_sr030pc50_ss_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
/*s	struct i2c_client *client = v4l2_get_subdevdata(sd); */
/*	struct camdrv_ss_state *state = */
/*	container_of(sd, struct camdrv_ss_states, sd); */
	int ret = 0;

	switch (cmd) {

	case VIDIOC_THUMB_SUPPORTED:
	{
		int *p = arg;
		*p = 0; /* NO */

		break;
	}

	case VIDIOC_THUMB_G_FMT:
	{
		struct v4l2_format *p = arg;
		struct v4l2_pix_format *pix = &p->fmt.pix;
		p->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		/* fixed thumbnail resolution and format */
		pix->width = 640;
		pix->height = 480;
		pix->bytesperline = 640 * 2;
		pix->sizeimage = 640 * 480 * 2;
		pix->field = V4L2_FIELD_ANY;
		pix->colorspace = V4L2_COLORSPACE_JPEG,
		pix->pixelformat = V4L2_PIX_FMT_UYVY;
		break;
	}

	case VIDIOC_THUMB_S_FMT:
	{
		/*	struct v4l2_format *p = arg; */
		/* for now we don't support setting thumbnail fmt and res */
		ret = -EINVAL;
		break;
	}

	case VIDIOC_JPEG_G_PACKET_INFO:
	{
		struct v4l2_jpeg_packet_info *p = arg;
		p->padded = 0;
		p->packet_size = 0;
		break;
	}
	case VIDIOC_SENSOR_G_OPTICAL_INFO:
		{
			 struct v4l2_sensor_optical_info *p= arg;
       p->hor_angle.numerator = 512;
       p->hor_angle.denominator = 10;
       p->ver_angle.numerator = 394;
       p->ver_angle.denominator = 10;
       p->focus_distance[0] = 10;
       p->focus_distance[1] = 120;
       p->focus_distance[2] = -1;
       p->focal_length.numerator = 130;
       p->focal_length.denominator = 100;
			break;	
		}

	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

int camdrv_ss_sr030pc50_set_preview_start(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camdrv_ss_state *state = to_state(sd);
	int err = 0;

	CAM_INFO_PRINTK( "%s :\n", __func__);

	if (!state->pix.width || !state->pix.height) {
		CAM_ERROR_PRINTK( "%s : width or height is NULL!!!\n",__func__);
		return -EINVAL;
	}

	if (state->mode_switch == PICTURE_CAPTURE_TO_CAMERA_PREVIEW_RETURN) {
        // do nothing
	}

	if(state->mode_switch == CAMERA_PREVIEW_TO_CAMCORDER_PREVIEW)
	{
        // do nothing
	}
	else if(state->mode_switch == INIT_DONE_TO_CAMCORDER_PREVIEW)
	{
        // do nothing
	}
	else if(state->mode_switch == CAMCORDER_PREVIEW_TO_CAMERA_PREVIEW)
	{
		err = camdrv_ss_i2c_set_config_register(client, sr030pc50_init_regs, ARRAY_SIZE(sr030pc50_init_regs), "init_regs");
		if (err < 0) {
			CAM_ERROR_PRINTK( "%s :sr030pc50_init_regs IS FAILED\n",__func__);
			return -EIO;
		}
	}

/*
	err = camdrv_ss_set_preview_size(sd);
	if (err < 0) {
		CAM_ERROR_PRINTK( "%s : camdrv_ss_set_preview_size is FAILED !!\n", __func__);
		return -EIO;
	}
	
*/
	state->camera_flash_fire = 0;
	state->camera_af_flash_checked = 0;

	if (state->check_dataline) { /* Output Test Pattern */
		err = camdrv_ss_set_dataline_onoff(sd, 1);
		if (err < 0) {
			CAM_ERROR_PRINTK( "%s : check_dataline is FAILED !!\n", __func__);
			return -EIO;
		}
	}

	return 0;
}

static float camdrv_ss_sr030pc50_get_exposureTime(struct v4l2_subdev *sd)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    unsigned int read_value1=0,read_value2=0,read_value3=0;
    int exposureTime = 0;

    camdrv_ss_i2c_write_2_bytes(client, 0x03, 0x20);

    camdrv_ss_i2c_read_1_byte(client, 0x80, &read_value1);
    camdrv_ss_i2c_read_1_byte(client, 0x81, &read_value2);
    camdrv_ss_i2c_read_1_byte(client, 0x82, &read_value3);

    exposureTime = (read_value1 << 19 | read_value2 << 11 | read_value3<<3);
    CAM_INFO_PRINTK("%s, exposureTime =%d \n",__func__,exposureTime);
    return exposureTime;
		}



static int camdrv_ss_sr030pc50_get_iso_speed_rate(struct v4l2_subdev *sd)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    unsigned short read_value = 0;
    int GainValue = 0;
    int isospeedrating = 100;
    int rows_num_=0;

    camdrv_ss_i2c_write_2_bytes(client, 0x03, 0x20);
    camdrv_ss_i2c_read_1_byte(client, 0xb0, &read_value);

    CAM_INFO_PRINTK("%s, read_value =%x \n",__func__,read_value);

    GainValue = read_value / 16;

    CAM_INFO_PRINTK("%s, read_value =%d, GainValue = %d \n",__func__,read_value, GainValue);

    if(GainValue < 0.875)
    {
        isospeedrating = 50;
    }
    else if(GainValue < 1.75)
    {
        isospeedrating = 100;
    }
    else if(GainValue < 4.625)
    {
        isospeedrating = 200;
    }
    else if(GainValue < 6.9375)
    {
        isospeedrating = 400;
    }
    else if(GainValue < 13.875)
    {
        isospeedrating = 800;
    }
    else
    {
        isospeedrating = 1600;
    }

    CAM_INFO_PRINTK("camdrv_ss_sr030pc50_get_iso_speed_rate, GainValue =%d, isospeedrating =%d\n", GainValue, isospeedrating );       

    return isospeedrating;
		}



static int camdrv_ss_sr030pc50_sensor_power(int on)
{
unsigned int value;
	int ret = -1;
	struct clk *clock;
	struct clk *axi_clk;
	static struct pi_mgr_dfs_node unicam_dfs_node; 

	CAM_INFO_PRINTK("%s:camera power %s\n", __func__, (on ? "on" : "off"));

	if (!unicam_dfs_node.valid) {
		ret = pi_mgr_dfs_add_request(&unicam_dfs_node, "unicam", PI_MGR_PI_ID_MM,
					   PI_MGR_DFS_MIN_VALUE);
		if (ret) {
			CAM_ERROR_PRINTK("%s: failed to register PI DFS request\n",__func__);
			return -1;
		}
     }
	clock = clk_get(NULL, SENSOR_0_CLK);
	if (!clock) {
		CAM_ERROR_PRINTK("%s: unable to get clock %s\n", __func__, SENSOR_0_CLK);
		return -1;
	}
	axi_clk = clk_get(NULL, "csi0_axi_clk");
	if (!axi_clk) {
		CAM_ERROR_PRINTK("%s:unable to get clock csi0_axi_clk\n", __func__);
		return -1;
	}
	VCAM_A_2_8_V = regulator_get(NULL,"cam");
	if(IS_ERR(VCAM_A_2_8_V))
	{
		CAM_ERROR_PRINTK("can not get VCAM_A_2_8_V.8V\n");
		return -1;
	}

	VCAM_IO_1_8_V = regulator_get(NULL,"hv9");
	if(IS_ERR(VCAM_IO_1_8_V))
	{
		CAM_ERROR_PRINTK("can not get VCAM_IO_1.8V\n");
		return -1;
	}	
	
#ifdef CONFIG_SOC_CAMERA_POWER_USE_ASR   //for hw rev 0.3
	VCAM_CORE_1_2_V = regulator_get(NULL,"asr_nm_uc");
	if(IS_ERR(VCAM_CORE_1_2_V))
	{
		CAM_ERROR_PRINTK("can not get VCAM_CORE_1_2_V\n");
		return -1;
	}	
#else		
	gpio_request(CAM_CORE_EN, "cam_1_2v");
	gpio_direction_output(CAM_CORE_EN,0); 
#endif
	
	CAM_INFO_PRINTK("set cam_rst cam_stnby  to low\n");
	gpio_request(CAM0_RESET, "cam0_rst");
	gpio_direction_output(CAM0_RESET,0);
	
	gpio_request(CAM0_STNBY, "cam0_stnby");
	gpio_direction_output(CAM0_STNBY,0);
		
	gpio_request(CAM1_RESET, "cam1_rst");
	gpio_direction_output(CAM1_RESET,0);

	gpio_request(CAM1_STNBY, "cam1_stnby");
	gpio_direction_output(CAM1_STNBY,0);


	if(on)
	{
		CAM_INFO_PRINTK("power on the sensor \n"); //@HW

        regulator_set_voltage(VCAM_A_2_8_V,2800000,2800000);
        regulator_set_voltage(VCAM_IO_1_8_V,1800000,1800000);   
#ifdef CONFIG_SOC_CAMERA_POWER_USE_ASR   //for hw rev 0.3
		regulator_set_voltage(VCAM_CORE_1_2_V,1200000,1200000);
#endif	

		value = clk_enable(axi_clk);
		if (value) {
			CAM_ERROR_PRINTK("%s:failed to enable csi2 axi clock\n", __func__);
			return -1;
		}

		msleep(100);
		CAM_INFO_PRINTK("power on the sensor's power supply\n"); //@HW

		
		regulator_enable(VCAM_A_2_8_V);
		

		regulator_enable(VCAM_IO_1_8_V);
	
		//msleep(5);	
	
#ifdef CONFIG_SOC_CAMERA_POWER_USE_ASR   //for hw rev 0.3		
		regulator_enable(VCAM_CORE_1_2_V);
#else		
		gpio_set_value(CAM_CORE_EN,1); 
#endif
		msleep(2);

#ifdef CONFIG_SOC_CAMERA_POWER_USE_ASR   //for hw rev 0.3		
        regulator_disable(VCAM_CORE_1_2_V);
#else		
		gpio_set_value(CAM_CORE_EN,0); 
#endif
		msleep(12); //changed by aska for delay MCLK on time
	
		value = clk_enable(clock);
		if (value) {
			CAM_ERROR_PRINTK("%s: failed to enable clock %s\n", __func__,SENSOR_0_CLK);
			return -1;
		}
		CAM_INFO_PRINTK("enable camera clock\n");
		value = clk_set_rate(clock, SENSOR_0_CLK_FREQ);
		if (value) {
			CAM_ERROR_PRINTK("%s: failed to set the clock %s to freq %d\n",__func__, SENSOR_0_CLK, SENSOR_0_CLK_FREQ);
			return -1;
		}
		CAM_INFO_PRINTK("set rate\n");
		msleep(5);

		gpio_set_value(CAM1_STNBY,1);
		msleep(5);

		gpio_set_value(CAM1_RESET,1);
		msleep(50);
	}
	else
	{
		printk("power off the sensor \n"); //@HW

		/* enable reset gpio */
		gpio_set_value(CAM1_RESET,0);
		msleep(1);
		
		gpio_set_value(CAM1_STNBY,0);
		msleep(1);

		clk_disable(clock);
		clk_disable(axi_clk);
		msleep(1);

		/* enable power down gpio */

		regulator_disable(VCAM_IO_1_8_V);
		regulator_disable(VCAM_A_2_8_V);
	}	
	
	return 0;
}

 
int camdrv_ss_sr030pc50_get_sensor_param_for_exif(
	struct v4l2_subdev *sd,
	struct v4l2_exif_sensor_info *exif_param)
{
	char str[20];
	int num = -1;
	int ret = -1;
	float exposureTime = 0.0f;

	strcpy(exif_param->strSoftware,		EXIF_SOFTWARE);
	strcpy(exif_param->strMake,		EXIF_MAKE);
	strcpy(exif_param->strModel,		EXIF_MODEL);

	exposureTime = camdrv_ss_sr030pc50_get_exposureTime(sd);

	CAM_INFO_PRINTK("%s : exposureTime =  %d \n",__func__,exposureTime);

	num = (int)exposureTime;///1300;
	if (num > 0) 
	{
		snprintf(str, 19, "%d/13000000", num);
		strcpy(exif_param->exposureTime, str);
	} 
	else 
	{
		strcpy(exif_param->exposureTime, "");
	}
	CAM_INFO_PRINTK("%s : exposure time =  %s \n",__func__,exif_param->exposureTime);


	num = camdrv_ss_sr030pc50_get_iso_speed_rate(sd);
	if (num > 0) {
		sprintf(str, "%d,", num);
		strcpy(exif_param->isoSpeedRating, str);
	} else {
		strcpy(exif_param->isoSpeedRating, "");
	}

	CAM_INFO_PRINTK("%s :num=%d and isoSpeedRating =  %s \n",__func__,num, exif_param->isoSpeedRating);

	/* sRGB mandatory field! */
	strcpy(exif_param->colorSpaceInfo,	"1");

	strcpy(exif_param->contrast,		"");
	strcpy(exif_param->saturation,		"");
	strcpy(exif_param->sharpness,		"");

	strcpy(exif_param->FNumber,		(char *)"28/10");
	strcpy(exif_param->exposureProgram,	"");
	strcpy(exif_param->shutterSpeed,	"");
	strcpy(exif_param->aperture,		"");
	strcpy(exif_param->brightness,		"");
	strcpy(exif_param->exposureBias,	"");
	strcpy(exif_param->maxLensAperture,	"");
	strcpy(exif_param->flash,		"");
	strcpy(exif_param->lensFocalLength,	(char*)"1300/1000");
	strcpy(exif_param->userComments,	"");
	ret = 0;

	return ret;
}

bool camdrv_ss_sr030pc50_get_esd_status(struct v4l2_subdev *sd)
{
	return false;
}

void camdrv_ss_sr030pc50_smartStayChangeInitSetting(struct camdrv_ss_sensor_cap *sensor)
{
	sensor->init_regs						  = sr030pc50_init_regs_smart_stay;
	sensor->rows_num_init_regs				  = ARRAY_SIZE(sr030pc50_init_regs_smart_stay);
	sensor->skip_frames 		= 0;
	CAM_INFO_PRINTK("%s : skip_frames  =  %d \n",__func__,sensor->skip_frames);
	
}

//NIKHIL
void camdrv_ss_sensor_sub_name( struct camdrv_ss_sensor_cap *sensor)
{
 strcpy(sensor->name, SR030PC50_NAME);
}

bool camdrv_ss_sensor_init_sub(bool bOn, struct camdrv_ss_sensor_cap *sensor)
{
	strcpy(sensor->name, SR030PC50_NAME);
	sensor->supported_preview_framesize_list  = sr030pc50_supported_preview_framesize_list;
	sensor->supported_number_of_preview_sizes = ARRAY_SIZE(sr030pc50_supported_preview_framesize_list);

	sensor->supported_capture_framesize_list  =  sr030pc50_supported_capture_framesize_list;
	sensor->supported_number_of_capture_sizes = ARRAY_SIZE(sr030pc50_supported_capture_framesize_list);

	sensor->preview_size_640x480_regs	          =	sr030pc50_preview_size_640x480_regs; 
	sensor->rows_num_preview_size_640x480_regs	  = ARRAY_SIZE(sr030pc50_preview_size_640x480_regs);
	
	sensor->fmts			= sr030pc50_fmts;
	sensor->rows_num_fmts		= ARRAY_SIZE(sr030pc50_fmts);


	sensor->controls		= sr030pc50_controls;
	sensor->rows_num_controls	= ARRAY_SIZE(sr030pc50_controls);

	sensor->default_pix_fmt		= SR030PC50_DEFAULT_PIX_FMT;
	sensor->default_mbus_pix_fmt	= SR030PC50_DEFAULT_MBUS_PIX_FMT;
	sensor->register_size		= SR030PC50_REGISTER_SIZE;
	sensor->skip_frames 		= 0;

  	sensor->delay_duration				= SR030PC50_DELAY_DURATION;

	/* sensor dependent functions , Mandatory*/
	sensor->thumbnail_ioctl			       = camdrv_ss_sr030pc50_ss_ioctl;
	sensor->enum_frameintervals	= camdrv_ss_sr030pc50_enum_frameintervals;

	sensor->set_preview_start      = camdrv_ss_sr030pc50_set_preview_start;//aska



	sensor->get_exif_sensor_info =	  camdrv_ss_sr030pc50_get_sensor_param_for_exif;
	sensor->getEsdStatus 		=     camdrv_ss_sr030pc50_get_esd_status;


	/*REGS and their sizes*/
	/* List all the capabilities of sensor . List all the supported register setting tables */

	//Normal CAM Preview
	sensor->init_regs						  = sr030pc50_init_regs;
	sensor->rows_num_init_regs				  = ARRAY_SIZE(sr030pc50_init_regs);

	sensor->smartStayChangeInitSetting = camdrv_ss_sr030pc50_smartStayChangeInitSetting;

	sensor->sensor_power = camdrv_ss_sr030pc50_sensor_power;

	/*snapshot mode*/

	/*effect*/
	sensor->effect_normal_regs			      =	sr030pc50_effect_normal_regs;
	sensor->rows_num_effect_normal_regs      = ARRAY_SIZE(sr030pc50_effect_normal_regs);
	
	sensor->effect_mono_regs			      =	sr030pc50_effect_mono_regs;
	sensor->rows_num_effect_mono_regs	  	  = ARRAY_SIZE(sr030pc50_effect_mono_regs);

	
	sensor->effect_negative_regs		      =	sr030pc50_effect_negative_regs;
	sensor->rows_num_effect_negative_regs	 = ARRAY_SIZE(sr030pc50_effect_negative_regs);
	
	sensor->effect_sepia_regs			      =	sr030pc50_effect_sepia_regs;
	sensor->rows_num_effect_sepia_regs	  	  = ARRAY_SIZE(sr030pc50_effect_sepia_regs);

	/*wb*/
	sensor->wb_auto_regs				  =	sr030pc50_wb_auto_regs;
	sensor->rows_num_wb_auto_regs	  	  = ARRAY_SIZE(sr030pc50_wb_auto_regs);

	sensor->wb_daylight_regs				 =	sr030pc50_wb_daylight_regs;
	sensor->rows_num_wb_daylight_regs	  	 = ARRAY_SIZE(sr030pc50_wb_daylight_regs);
	
	sensor->wb_cloudy_regs				 =	sr030pc50_wb_cloudy_regs;
	sensor->rows_num_wb_cloudy_regs	 = ARRAY_SIZE(sr030pc50_wb_cloudy_regs);
	
	sensor->wb_incandescent_regs			 =	sr030pc50_wb_incandescent_regs;
	sensor->rows_num_wb_incandescent_regs	 = ARRAY_SIZE(sr030pc50_wb_incandescent_regs);

	sensor->wb_fluorescent_regs 		  =	sr030pc50_wb_fluorescent_regs;
	sensor->rows_num_wb_fluorescent_regs  = ARRAY_SIZE(sr030pc50_wb_fluorescent_regs);

	/*metering*/
	sensor->metering_matrix_regs		  =	sr030pc50_metering_matrix_regs;
	sensor->rows_num_metering_matrix_regs	= ARRAY_SIZE(sr030pc50_metering_matrix_regs);

	sensor->metering_center_regs		  =	sr030pc50_metering_center_regs;
	sensor->rows_num_metering_center_regs	= ARRAY_SIZE(sr030pc50_metering_center_regs);

	sensor->metering_spot_regs			  =	  sr030pc50_metering_spot_regs;
	sensor->rows_num_metering_spot_regs	= ARRAY_SIZE(sr030pc50_metering_spot_regs);
	
	/*EV*/
	sensor->ev_minus_4_regs 			 =	sr030pc50_ev_minus_4_regs;
	sensor->rows_num_ev_minus_4_regs	 = ARRAY_SIZE(sr030pc50_ev_minus_4_regs);

	sensor->ev_minus_3_regs 			 =	sr030pc50_ev_minus_3_regs;
	sensor->rows_num_ev_minus_3_regs	 = ARRAY_SIZE(sr030pc50_ev_minus_3_regs);

	sensor->ev_minus_2_regs 			 =	sr030pc50_ev_minus_2_regs;
	sensor->rows_num_ev_minus_2_regs	  = ARRAY_SIZE(sr030pc50_ev_minus_2_regs);

	sensor->ev_minus_1_regs 			 =	sr030pc50_ev_minus_1_regs;
	sensor->rows_num_ev_minus_1_regs	 = ARRAY_SIZE(sr030pc50_ev_minus_1_regs);

	sensor->ev_default_regs 			 =	sr030pc50_ev_default_regs;
	sensor->rows_num_ev_default_regs	 = ARRAY_SIZE(sr030pc50_ev_default_regs);

	sensor->ev_plus_1_regs				 =	sr030pc50_ev_plus_1_regs;
	sensor->rows_num_ev_plus_1_regs	 = ARRAY_SIZE(sr030pc50_ev_plus_1_regs);

	sensor->ev_plus_2_regs				 =	sr030pc50_ev_plus_2_regs;
	sensor->rows_num_ev_plus_2_regs	 = ARRAY_SIZE(sr030pc50_ev_plus_2_regs);

	sensor->ev_plus_3_regs				 =	sr030pc50_ev_plus_3_regs;
	sensor->rows_num_ev_plus_3_regs	 = ARRAY_SIZE(sr030pc50_ev_plus_3_regs);

	sensor->ev_plus_4_regs				 =	sr030pc50_ev_plus_4_regs;
	sensor->rows_num_ev_plus_4_regs	 = ARRAY_SIZE(sr030pc50_ev_plus_4_regs);

	
	/*contrast*/
	sensor->contrast_minus_2_regs		 	 =	sr030pc50_contrast_minus_2_regs;
	sensor->rows_num_contrast_minus_2_regs	 = ARRAY_SIZE(sr030pc50_contrast_minus_2_regs);

	sensor->contrast_minus_1_regs		     =	sr030pc50_contrast_minus_1_regs;
	sensor->rows_num_contrast_minus_1_regs	 = ARRAY_SIZE(sr030pc50_contrast_minus_1_regs);
  
	sensor->contrast_default_regs			 =	sr030pc50_contrast_default_regs;
	sensor->rows_num_contrast_default_regs  = ARRAY_SIZE(sr030pc50_contrast_default_regs);

	sensor->contrast_plus_1_regs			 =	sr030pc50_contrast_plus_1_regs;
	sensor->rows_num_contrast_plus_1_regs	 = ARRAY_SIZE(sr030pc50_contrast_plus_1_regs);

	sensor->contrast_plus_2_regs			 =	sr030pc50_contrast_plus_2_regs;
	sensor->rows_num_contrast_plus_2_regs	 = ARRAY_SIZE(sr030pc50_contrast_plus_2_regs);
	
	/*sharpness*/ 
		
	/*saturation*/
	
	/*zoom*/
	
	/*scene mode*/
		
	/*fps*/
#if 1
	sensor->fps_auto_regs				 =	sr030pc50_fps_auto_regs;
	sensor->rows_num_fps_auto_regs	  		  = ARRAY_SIZE(sr030pc50_fps_auto_regs);

	sensor->fps_5_regs					 =	sr030pc50_fps_5_regs;
	sensor->rows_num_fps_5_regs	  		  = ARRAY_SIZE(sr030pc50_fps_5_regs);

	sensor->fps_7_regs					 =	sr030pc50_fps_7_regs;
	sensor->rows_num_fps_7_regs	  		  = ARRAY_SIZE(sr030pc50_fps_7_regs);

	sensor->fps_10_regs 				 =	sr030pc50_fps_10_regs;
	sensor->rows_num_fps_10_regs	  		  = ARRAY_SIZE(sr030pc50_fps_10_regs);

	sensor->fps_15_regs 				 =	sr030pc50_fps_15_regs;
	sensor->rows_num_fps_15_regs	  		  = ARRAY_SIZE(sr030pc50_fps_15_regs);

	sensor->fps_20_regs 				 =	sr030pc50_fps_20_regs;
	sensor->rows_num_fps_20_regs	  		  = ARRAY_SIZE(sr030pc50_fps_20_regs);

	sensor->fps_25_regs 				 =	sr030pc50_fps_25_regs;
	sensor->rows_num_fps_25_regs	  		  = ARRAY_SIZE(sr030pc50_fps_25_regs);

	sensor->fps_30_regs 				 =	sr030pc50_fps_30_regs;
	sensor->rows_num_fps_30_regs 		  = ARRAY_SIZE(sr030pc50_fps_30_regs);
#endif	
	/*quality*/
	
	/*preview size */
	sensor->preview_size_640x480_regs	          =	sr030pc50_preview_size_640x480_regs; 
	sensor->rows_num_preview_size_640x480_regs	  = ARRAY_SIZE(sr030pc50_preview_size_640x480_regs);
	
	/*Capture size */
	
	/*pattern*/
	sensor->pattern_on_regs 			  = sr030pc50_pattern_on_regs;
	sensor->rows_num_pattern_on_regs	  = ARRAY_SIZE(sr030pc50_pattern_on_regs);
	
	sensor->pattern_off_regs			  = sr030pc50_pattern_off_regs;
	sensor->rows_num_pattern_off_regs	  = ARRAY_SIZE(sr030pc50_pattern_off_regs);

	/*AE*/

	/*AWB*/

	 // To Do for VT
	sensor->vt_mode_regs						  = sr030pc50_vt_mode_regs;
	sensor->rows_num_vt_mode_regs				  = ARRAY_SIZE(sr030pc50_vt_mode_regs);

	return true;
}
