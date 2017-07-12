/*****************************************************************************
*  Copyright 2012 Broadcom Corporation.  All rights reserved.
*
*  Unless you and Broadcom execute a separate written software license
*  agreement governing use of this software, this software is licensed to you
*  under the terms of the GNU General Public License version 2, available at
*  http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
*  Notwithstanding the above, under no circumstances may you combine this
*  software in any way with any other Broadcom software provided under a
*  license other than the GPL, without Broadcom's express prior written
*  consent.
*
*****************************************************************************/

#if !defined(WFD_IOCTL_H)
#define WFD_IOCTL_H

/* ---- Include Files ---------------------------------------------------- */

#if defined(__KERNEL__)
#include <linux/types.h>	/* Needed for standard types */
#else
#include <stdint.h>
#endif

#include <linux/ioctl.h>

/* ---- Constants and Types ---------------------------------------------- */

#define WFDSCRAPER_METADATA(i, j, k, l) (((i)<<24)|((j)<<16)|((k)<<8)|l)

#define WFDSCRAPER_METADATA_START \
	WFDSCRAPER_METADATA('W', 'D', 'S', 'T')
#define WFDSCRAPER_METADATA_STOP \
	WFDSCRAPER_METADATA('W', 'D', 'E', 'N')
#define WFDSCRAPER_METADATA_RATE_CTRL \
	WFDSCRAPER_METADATA('W', 'D', 'R', 'C')

/* Type define used to create unique IOCTL number */
#define WFD_MAGIC_TYPE                  'W'

/* IOCTL commands */
enum wfd_cmd_e {
	WFD_CMD_SET_METADATA,
	WFD_CMD_GET_METADATA,
	WFD_CMD_SET_NEG_CONFIG,
	WFD_CMD_GET_NEG_CONFIG,
	WFD_CMD_SET_CON_STATE,
	WFD_CMD_GET_CON_STATE,
	WFD_CMD_SET_AUDIO_DATA,
	WFD_CMD_GET_AUDIO_DATA,
	WFD_CMD_FLUSH_AUDIO_DATA,
	WFD_CMD_SET_PROFILING_TS,
	WFD_CMD_LAST		/* Do no delete */
};

typedef enum {
	WFD_AUDIO_NONE,
	WFD_AUDIO_AAC_LC2,
	WFD_AUDIO_LPCM_48,

} wfd_audio_e;

/* IOCTL Data structures */
struct wfd_ioctl_metadata {
	unsigned int action;

};

struct wfd_ioctl_neg_config {
	/* H.264 video resolution data. */
	unsigned int width;
	unsigned int height;
	unsigned int fps;
	char scan_mode;

	/* Audio resolution data. */
	wfd_audio_e audio_fmt;
	unsigned int audio_hz;
	unsigned int audio_ch;
};

struct wfd_ioctl_con_state {
	unsigned int status;
	unsigned int peer;
};

#define WFD_AUDIO_SIZE 8192
struct wfd_ioctl_audio_data {
	void *data;
	unsigned int size;
};

/* WFD Profiling timestamp types */
enum wfd_profiling_ts_type_e {
	WFD_PROFILING_TS_HW_COMPOSER = 0,
	WFD_PROFILING_TS_CODEC,
	WFD_PROFILING_TS_MPEG2TS_WRITER,
	WFD_PROFILING_TS_DRV_WRITE,
	WFD_PROFILING_TS_DRV_READ,
	WFD_PROFILING_TS_NETWORK_SEND,
	WFD_PROFILING_TS_LAST /* must be last */
};

/* WFD Profiling timestamp structure */
struct wfd_profiling_ts {
	unsigned int type;
	unsigned long timestamp;
};

/* WFD Profiling structure passed in ioctl */
struct wfd_ioctl_profiling_ts {
	unsigned int num_ts;
	struct wfd_profiling_ts *first;
};

/* IOCTL numbers */
#define WFD_IOCTL_SET_METADATA \
	_IOR(WFD_MAGIC_TYPE, WFD_CMD_SET_METADATA,\
	struct wfd_ioctl_metadata)
#define WFD_IOCTL_GET_METADATA \
	_IOR(WFD_MAGIC_TYPE, WFD_CMD_GET_METADATA,\
	struct wfd_ioctl_metadata)
#define WFD_IOCTL_SET_NEG_CONFIG \
	_IOR(WFD_MAGIC_TYPE, WFD_CMD_SET_NEG_CONFIG,\
	struct wfd_ioctl_neg_config)
#define WFD_IOCTL_GET_NEG_CONFIG \
	_IOR(WFD_MAGIC_TYPE, WFD_CMD_GET_NEG_CONFIG,\
	struct wfd_ioctl_neg_config)
#define WFD_IOCTL_SET_CON_STATE \
	_IOR(WFD_MAGIC_TYPE, WFD_CMD_SET_CON_STATE,\
	struct wfd_ioctl_con_state)
#define WFD_IOCTL_GET_CON_STATE \
	_IOR(WFD_MAGIC_TYPE, WFD_CMD_GET_CON_STATE,\
	struct wfd_ioctl_con_state)
#define WFD_IOCTL_SET_AUDIO_DATA \
	_IOR(WFD_MAGIC_TYPE, WFD_CMD_SET_AUDIO_DATA,\
	struct wfd_ioctl_audio_data)
#define WFD_IOCTL_GET_AUDIO_DATA \
	_IOR(WFD_MAGIC_TYPE, WFD_CMD_GET_AUDIO_DATA,\
	struct wfd_ioctl_audio_data)
#define WFD_IOCTL_FLUSH_AUDIO_DATA \
	_IO(WFD_MAGIC_TYPE, WFD_CMD_FLUSH_AUDIO_DATA)
#define WFD_IOCTL_SET_PROFILING_TS \
	_IOW(WFD_MAGIC_TYPE, WFD_CMD_SET_PROFILING_TS,\
	struct wfd_ioctl_profiling_ts)

/* ---- Variable Externs ------------------------------------------ */

/* ---- Function Prototypes --------------------------------------- */

#endif /* WFD_IOCTL_H */
