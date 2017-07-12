/*
 * File Name : multi_config.c
 *
 * Virtual multi configuration utilities for composite USB gadgets.
 * This utilitie can support variable interface for variable Host PC.
 *
 * Copyright (C) 2011 Samsung Electronics
 * Author: SoonYong, Cho <soonyong.cho@samsung.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "multi_config.h"



static int stringMode = OTHER_REQUEST;


/* Description  : Change configuration using virtual multi configuration.
 * Parameter    : struct usb_funciton f (to be changed function interface)
		  void *next (next means usb req->buf)
		  int len (length for to fill buffer)
		  struct usb_configuration *config
		  (To reference interface array of current config)
		  enum usb_device_speed speed (usb speed)
 * Return value : "ret < 0" means fillbuffer function is failed.
 */
int change_conf(struct usb_function *f,
		void *next, int len,
		struct usb_configuration *config,
		enum usb_device_speed speed)
{
	int status = 0;


	if (f->set_config_desc)
		f->set_config_desc(stringMode);

	return status;
}
