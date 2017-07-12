/******************************************************************************
* Copyright (C) 2012 Broadcom
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, you may obtain a copy at
* http://www.broadcom.com/licenses/LGPLv2.1.php or by writing to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*
******************************************************************************/

/**
*
*   @file   simplefilter.c
*
*   @brief  This file demostrate how to weite a filter type ALSA plugin
*
****************************************************************************/
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <dlfcn.h>
#include <sys/time.h>

#define LOG_TAG "HPF_FILTER"
#include <utils/Log.h>

//#define PERFORMANCE 1

struct bcm_filter_functions_t {

    void *  (*FilterInit)(int  order);
    void    (*FilterClose)(void *pFilter);
    int	    (*FilterProcessLoop)(void *p, void *in, void *out, int in_bytes);
};

#define	BCM_FILTER_TAG	'BMFT'
#define	ID_BCM_DLL_HP_FILTER "HPFT"

/**
* Please change version number if you change the data structure
*/
struct bcm_dll_filter_t {
    /** tag must be initialized to BCM_FILTER_TAG */
    unsigned int tag;

    /** version number for the filter */
    unsigned int version;

    /** Filter functions */
    struct bcm_filter_functions_t methods;

    void *dso;

    /** Identifier of the filter */
    const char *id;
    int pad;
};


/**
 * DSP filter parameters
 */
struct bcmfilter_parms {
	int int_parm;
	float float_parm;
	int enable_filter;
	char *pFilterPath;
	int mute_op; /* No of bytes to be muted*/

};

/**
 * Module data structure
 */
typedef struct {
	snd_pcm_extplug_t ext;
	struct bcmfilter_parms parms;
	/* Add more data member here, such as instance, intermedate buffer */
	/* and running states, etc...                                      */
	struct bcm_dll_filter_t	*pDllFilter;
	void	*pFilterPrivate;
#ifdef PERFORMANCE
	int sTotalTime;
	int sTotalFrames;
	int sTotalSize;
#endif	//PERFORMANCE
} snd_pcm_bcmfilter_t;


#ifdef PERFORMANCE
static int GetMSecs() {
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return (int) (tv.tv_sec * 1000000 + tv.tv_usec );
}
#endif

//load filter
int load_filter(const char *id, const char *file_path, struct bcm_dll_filter_t **ppDllFilter)
{
	int status = 0;
	void *handle;
	struct bcm_dll_filter_t *bcmf;

	handle = dlopen(file_path, RTLD_NOW);
	if (handle == NULL) {
	    char const *err_str = dlerror();
	    ALOGI("load: module=%s\n%s", file_path, err_str?err_str:"unknown");
	    status = -EINVAL;
	    goto load_filter_done;
	}


	/* Get the address of the struct hal_module_info. */
	const char *sym = "BCMF";
	bcmf = (struct bcm_dll_filter_t *)dlsym(handle, sym);
	if (bcmf == NULL) {
	    ALOGI("load: couldn't find symbol %s", sym);
	    status = -EINVAL;
	    goto load_filter_done;
	}

	/* Check that the id matches */
	if (strcmp(id, bcmf->id) != 0) {
	    ALOGI("load: id=%s != hmi->id=%s", id, bcmf->id);
	    status = -EINVAL;
	    goto load_filter_done;
	}

load_filter_done:
	if (status != 0) {
	    bcmf = NULL;
	    if (handle != NULL) {
		dlclose(handle);
		handle = NULL;
	    }
	} else {
		ALOGI("loaded filter id=%s path=%s bcmf=%p handle=%p",
			id, file_path, bcmf, handle);
		bcmf->dso = handle;
		*ppDllFilter = bcmf;
	}

	return status;
}


static inline void *byte_addr(const snd_pcm_channel_area_t *area,
			      snd_pcm_uframes_t offset)
{
	unsigned int bit_offset = area->first + area->step * offset;
	return (char *) area->addr + bit_offset / 8;
}

/**
 * The filter transfer function
 * Hookup your filter in this function
 */
static snd_pcm_sframes_t
bcmfilter_transfer(snd_pcm_extplug_t *ext,
	     const snd_pcm_channel_area_t *dst_areas,
	     snd_pcm_uframes_t dst_offset,
	     const snd_pcm_channel_area_t *src_areas,
	     snd_pcm_uframes_t src_offset,
	     snd_pcm_uframes_t size)
{
	snd_pcm_bcmfilter_t *pBcmfilter = (snd_pcm_bcmfilter_t *)ext;
	short *src = byte_addr(src_areas, src_offset);
	short *dst = byte_addr(dst_areas, dst_offset);
	unsigned int count = size;
	short *databuf;
#ifdef PERFORMANCE
	int startTime;
	int endTime;
	int diffTime;
#endif

	if (!pBcmfilter->parms.enable_filter || pBcmfilter->pDllFilter==NULL) {
		/* no DSP processing */
		memcpy(dst, src, snd_pcm_frames_to_bytes(ext->pcm, size));
		return size;
	}

#ifdef PERFORMANCE
	   startTime = GetMSecs();
#endif

	if(pBcmfilter->pDllFilter)
		pBcmfilter->pDllFilter->methods.FilterProcessLoop(pBcmfilter->pFilterPrivate, src, dst, snd_pcm_frames_to_bytes(ext->pcm, size));

        /* Muting for glitch*/
	if(pBcmfilter->parms.mute_op > 0)
	{
	        int size_bytes = snd_pcm_frames_to_bytes(ext->pcm, size);
		int mute_size = pBcmfilter->parms.mute_op > size_bytes ? size_bytes:pBcmfilter->parms.mute_op;
		memset(dst,0,mute_size);
		pBcmfilter->parms.mute_op -= mute_size;

	}

#ifdef PERFORMANCE
	   endTime = GetMSecs();
	   diffTime = endTime - startTime;
	   pBcmfilter->sTotalTime += diffTime;
	   pBcmfilter->sTotalFrames++;
	   pBcmfilter->sTotalSize += (snd_pcm_frames_to_bytes(ext->pcm, size));
#endif

	return size;
}

/**
 * Clean up
 */
static int bcmfilter_close(snd_pcm_extplug_t *ext)
{
	snd_pcm_bcmfilter_t *pBcmfilter = (snd_pcm_bcmfilter_t *)ext;
	ALOGI("bcmfilter_close\n");
#ifdef PERFORMANCE
      	ALOGI("bcmfilter_close TimeTaken = %d, frames = %d, size = %d\n", pBcmfilter->sTotalTime, pBcmfilter->sTotalFrames, pBcmfilter->sTotalSize);
#endif
	pBcmfilter->parms.mute_op = 0;
	if(pBcmfilter->pDllFilter) {
		if(pBcmfilter->pFilterPrivate) {
			pBcmfilter->pDllFilter->methods.FilterClose(pBcmfilter->pFilterPrivate);
			pBcmfilter->pFilterPrivate = NULL;
		}
		dlclose(pBcmfilter->pDllFilter->dso);
	}

	return 0;
}

/**
 * Filter initialization
 */
static int bcmfilter_init(snd_pcm_extplug_t *ext)
{
	snd_pcm_bcmfilter_t *pBcmfilter = (snd_pcm_bcmfilter_t *)ext;
	ALOGI("bcmfilter_init\n");
        /* Muting 16384 bytes to avoid the initial glitch */
	pBcmfilter->parms.mute_op = 16384;

	if(pBcmfilter->pDllFilter) {
		if(pBcmfilter->pFilterPrivate != NULL) {
			pBcmfilter->pDllFilter->methods.FilterClose(pBcmfilter->pFilterPrivate);
			pBcmfilter->pFilterPrivate = NULL;
        }
		pBcmfilter->pFilterPrivate = pBcmfilter->pDllFilter->methods.FilterInit(pBcmfilter->parms.int_parm);
	}
#ifdef PERFORMANCE
   	pBcmfilter->sTotalFrames = 0;
   	pBcmfilter->sTotalTime = 0;
   	pBcmfilter->sTotalSize = 0;
#endif
	return 0;
}

static const snd_pcm_extplug_callback_t bcmfilter_callback = {
	.transfer = bcmfilter_transfer,
	.init = bcmfilter_init,
	.close = bcmfilter_close,
};


/**
 * Get string type parameter from configuration
 */
static int get_string(snd_config_t *n, const char *id, const char *str,
			 const char  **ppStrResult)
{
	int val;
	if (strcmp(id, str))
		return 0;

	val = snd_config_get_string(n, ppStrResult);
	if (val < 0) {
		SNDERR("Invalid value for %s", id);
		return val;
	} else
		return 1;

}


/**
 * Get bool type parameter from configuration
 */
static int get_boolean(snd_config_t *n, const char *id, const char *str,
			 int *result)
{
	int val;
	if (strcmp(id, str))
		return 0;

	val = snd_config_get_bool(n);
	if (val < 0) {
		SNDERR("Invalid value for %s", id);
		return val;
	}
	*result = val;
	return 1;
}

/**
 * Get int parameter from configuration
 */
static int get_integer(snd_config_t *n, const char *id, const char *str,
			int *result)
{
	long val;
	int err;

	if (strcmp(id, str))
		return 0;
	err = snd_config_get_integer(n, &val);
	if (err < 0) {
		SNDERR("Invalid value for %s parameter", id);
		return err;
	}
	*result = val;
	return 1;
}

/**
 * Get float parameter from configuration
 */
static int get_real(snd_config_t *n, const char *id, const char *str,
			  float *result)
{
	double val;
	int err;

	if (strcmp(id, str))
		return 0;
	err = snd_config_get_ireal(n, &val);
	if (err < 0) {
		SNDERR("Invalid value for %s", id);
		return err;
	}
	*result = val;
	return 1;
}



SND_PCM_PLUGIN_DEFINE_FUNC(bcmfilter)
{
	snd_config_iterator_t c, next;
	snd_pcm_bcmfilter_t *pBcmfilter;
	snd_config_t *sconf = NULL;
	int err;
	struct bcmfilter_parms parms = {
		.int_parm = 64,
		.float_parm = 0.95,
		.enable_filter = 0,
	};

	snd_config_for_each(c, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(c);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 ||
		    strcmp(id, "hint") == 0)
			continue;
		if (strcmp(id, "slave") == 0) {
			sconf = n;
			continue;
		}
		err = get_integer(n, id, "int_parm", &parms.int_parm);
		if (err)
			goto error_parm;
		err = get_real(n, id, "float_parm", &parms.float_parm);
		if (err)
			goto error_parm;
		err = get_boolean(n, id, "enable_filter", &parms.enable_filter);
		if (err)
			goto error_parm;
		err = get_string(n, id, "filter_path", (const char **)&parms.pFilterPath);
		if (err)
			goto error_parm;
		SNDERR("Unknown field %s", id);
		err = -EINVAL;
	error_parm:
		if (err < 0)
			return err;
	}

	if (!sconf) {
		SNDERR("No slave configuration for bcmfilter pcm\n");
		return -EINVAL;
	}

	pBcmfilter = calloc(1, sizeof(*pBcmfilter));
	if (!pBcmfilter)
		return -ENOMEM;

	pBcmfilter->ext.version = SND_PCM_EXTPLUG_VERSION;
	pBcmfilter->ext.name = "BCM DSP Plugin";
	pBcmfilter->ext.callback = &bcmfilter_callback;
	pBcmfilter->ext.private_data = pBcmfilter;
	pBcmfilter->parms = parms;
	pBcmfilter->pDllFilter = NULL;
	pBcmfilter->pFilterPrivate = NULL;
	ALOGI("Filter path=%s enable=%d float=%f int=%d \n", pBcmfilter->parms.pFilterPath, pBcmfilter->parms.enable_filter, pBcmfilter->parms.float_parm, pBcmfilter->parms.int_parm);

	err = snd_pcm_extplug_create(&pBcmfilter->ext, name, root, sconf,
				     stream, mode);
	if (err < 0) {
		free(pBcmfilter);
		return err;
	}

	if(parms.pFilterPath && parms.enable_filter) {
		int tt = load_filter(ID_BCM_DLL_HP_FILTER, parms.pFilterPath, &pBcmfilter->pDllFilter);
		if(0 == tt){
			ALOGI("DLL filter loaded %s version %x\n", parms.pFilterPath, pBcmfilter->pDllFilter->version);

		} else
			ALOGE("Error to load DLL filter %s error returned %d\n", parms.pFilterPath, tt);
	}


	snd_pcm_extplug_set_param(&pBcmfilter->ext, SND_PCM_EXTPLUG_HW_FORMAT,
				  SND_PCM_FORMAT_S16);
	snd_pcm_extplug_set_slave_param(&pBcmfilter->ext, SND_PCM_EXTPLUG_HW_FORMAT,
					SND_PCM_FORMAT_S16);

	*pcmp = pBcmfilter->ext.pcm;
	return 0;
}

SND_PCM_PLUGIN_SYMBOL(bcmfilter);

