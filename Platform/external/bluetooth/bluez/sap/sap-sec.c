/*
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2010 ST-Ericsson SA
 *  Copyright (C) 2011 Tieto Poland
 *  Copyright (C) 2012 Samsung Electronics Co., Ltd
 *
 *  Author: Waldemar Rymarkiewicz <waldemar.rymarkiewicz@tieto.com>
 *          for ST-Ericsson
 *  Author: C S Bhargava <cs.bhargava@samsung.com>
 *          for Samsung Electronics
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <glib.h>
#include <gdbus.h>

#include "log.h"
#include "sap.h"

#define SAP_SEC_IFACE "org.bluez.SimAccessSec"
#define SAP_SEC_PATH "/org/bluez/sapsec"

enum {
	SIM_DISCONNECTED= 0x00,
	SIM_CONNECTED	= 0x01,
	SIM_POWERED_OFF	= 0x02,
	SIM_MISSING	= 0x03
};

typedef struct SAPDataTag {
	char *dest;
	unsigned short sim_status;
	unsigned short max_size;
	int call_status;
	void *device;
	DBusConnection *conn;
} SAPData;

static SAPData sap = {NULL, SIM_DISCONNECTED, 512, FALSE, NULL, NULL};

void sap_connect_req(void *sap_device, uint16_t maxmsgsize)
{
	DBG("");
	sap.device = sap_device;
//	sap_request_handler(SAP_CONNECT_REQ, 0, NULL);
	sap_request_handler(SAP_CONNECT_REQ, 2, &maxmsgsize);
}

void sap_disconnect_req(void *sap_device, uint8_t linkloss)
{
	DBG("");
	sap.device = sap_device;
	// must be called for SIM RESET
	sap_request_handler(SAP_DISCONNECT_REQ, 0, NULL);

	if (linkloss) {
		DBG("Link Loss!!!");
		return;
	}
}

void sap_transfer_apdu_req(void *sap_device, struct sap_parameter *param)
{
	DBG("");
	sap.device = sap_device;
	sap_request_handler(SAP_TRANSFER_APDU_REQ, param->len, param->val);
}

void sap_transfer_atr_req(void *sap_device)
{
	DBG("");
	sap.device = sap_device;
	sap_request_handler(SAP_TRANSFER_ATR_REQ, 0, NULL);
}

void sap_power_sim_off_req(void *sap_device)
{
	DBG("");
	sap.device = sap_device;
	sap_request_handler(SAP_POWER_SIM_OFF_REQ, 0, NULL);
}

void sap_power_sim_on_req(void *sap_device)
{
	DBG("");
	sap.device = sap_device;
	sap_request_handler(SAP_POWER_SIM_ON_REQ, 0, NULL);
}

void sap_reset_sim_req(void *sap_device)
{
	DBG("");
	sap.device = sap_device;
	sap_request_handler(SAP_RESET_SIM_REQ, 0, NULL);
}

void sap_transfer_card_reader_status_req(void *sap_device)
{
	DBG("");
	sap.device = sap_device;
	sap_request_handler(SAP_TRANSFER_CARD_READER_STATUS_REQ, 0, NULL);
}

void sap_set_transport_protocol_req(void *sap_device,
    struct sap_parameter *param)
{
	DBG("");
	sap.device = sap_device;
	sap_transport_protocol_rsp(sap_device, SAP_RESULT_NOT_SUPPORTED);
}

// Open RIL connection and return success status
gboolean sap_open_ril(void)
{
	DBusMessage *msg, *reply;
	gboolean conn_status = FALSE;

	DBG("conn %0x, dest %s", sap.conn, sap.dest);

	msg = dbus_message_new_method_call(sap.dest, SAP_SEC_PATH,
		SAP_SEC_IFACE, "SapRilOpen");
	if (msg == NULL) {
		error("Failed to Call sril_open");
		return FALSE;
	}

	reply = dbus_connection_send_with_reply_and_block(sap.conn, msg, -1, NULL);
	dbus_message_unref(msg);
	if (reply == NULL) {
		error("Reply NULL");
		return FALSE;
	}
	if (dbus_message_get_args(reply, NULL,
			DBUS_TYPE_BOOLEAN, &conn_status,
			DBUS_TYPE_INVALID) == FALSE) {
		dbus_message_unref(reply);
		error("Failed to Get reply for sril_open");
		return FALSE;
	}
	DBG("conn_status %d", conn_status);

	dbus_message_unref(reply);
	return conn_status;
}

// Calles RIL methods through DBUS
void sap_request_handler(uint8_t msg_id, uint16_t req_len, uint8_t *req)
{
	DBusMessage *msg = NULL;

	DBG(" msg_id %d, msg_len %d", msg_id, req_len);

	switch (msg_id) {
		case SAP_CONNECT_REQ:
		case SAP_DISCONNECT_REQ:
		case SAP_TRANSFER_APDU_REQ:
		case SAP_TRANSFER_ATR_REQ:
		case SAP_POWER_SIM_OFF_REQ:
		case SAP_POWER_SIM_ON_REQ:
		case SAP_RESET_SIM_REQ:
		case SAP_TRANSFER_CARD_READER_STATUS_REQ:
			msg = dbus_message_new_method_call(sap.dest, SAP_SEC_PATH,
				SAP_SEC_IFACE, "SapHandleReq");
			if (msg == NULL) {
				DBG("Failed to Call sril_handle_req");
				return;
			}

			dbus_message_append_args(msg,
				DBUS_TYPE_BYTE, &msg_id,
				DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, &req, (uint32_t) req_len,
				DBUS_TYPE_INVALID);
			dbus_connection_send(sap.conn, msg, NULL);
			dbus_message_unref(msg);
			break;
		case SAP_SET_TRANSPORT_PROTOCOL_REQ:
		default:
			DBG("Unknown message Id %d", msg_id);
			break;
	}
}

// Close RIL connection
void sap_close_ril(void)
{
	DBusMessage *msg = NULL;

	DBG("conn %0x", sap.conn);

	// Send connection request via DBus
	msg = dbus_message_new_method_call(sap.dest, SAP_SEC_PATH,
		SAP_SEC_IFACE, "SapRilClose");
	if (msg == NULL) {
		DBG("Failed to Call sril_close");
		return;
	}
	dbus_connection_send(sap.conn, msg, NULL);
	dbus_message_unref(msg);
	return;
}

static inline DBusMessage *invalid_args(DBusMessage *msg)
{
	return g_dbus_create_error(msg, "org.bluez.Error.InvalidArguments",
		"Invalid arguments in method call");
}

// Save sender name from Framework
static DBusMessage *register_sap(DBusConnection *conn, DBusMessage *msg,
								void *data)
{
	DBG("");
	sap.dest = g_strdup(dbus_message_get_sender(msg));
	return dbus_message_new_method_return(msg);
}

// Release memory for sender name
static DBusMessage *unregister_sap(DBusConnection *conn, DBusMessage *msg,
								void *data)
{
	DBG("");
	if (sap.dest) {
		g_free(sap.dest);
		sap.dest = NULL;
	}
	return dbus_message_new_method_return(msg);
}

// Handle RIL responses received from Dbus
static DBusMessage *sap_response_handler(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	uint8_t result, status, msg_id;
	uint16_t max_msg_size;
	uint8_t *resp;
	uint32_t resp_len;
	int ret;

	if (!dbus_message_get_args(msg, NULL,
				DBUS_TYPE_BYTE, &msg_id,
				DBUS_TYPE_BYTE, &result,
				DBUS_TYPE_BYTE, &status,
				DBUS_TYPE_UINT16, &max_msg_size,
				DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, &resp, &resp_len,
				DBUS_TYPE_INVALID)) {
		return invalid_args(msg);
	}

	DBG(" msg_id %d, result %d, status %d, max_msg_size %d, res_len %d",
			msg_id, result, status, max_msg_size, resp_len);

	switch (msg_id) {
	case SAP_CONNECT_RESP:
		ret = sap_connect_rsp(sap.device, status, max_msg_size);
		/* Send Reset SIM request to RIL as per previous implementation */
		sap_reset_sim_req(sap.device);
//		ret = sap_status_ind(sap.device, SAP_STATUS_CHANGE_CARD_RESET);
		break;
	case SAP_DISCONNECT_IND:
	case SAP_DISCONNECT_RESP:
		// connection status, max message size
		ret = sap_disconnect_rsp(sap.device);
		break;
	case SAP_TRANSFER_APDU_RESP:
		// result code, apdu len, apdu
		DBG("APDU response result %d len %d", result, resp_len);
		ret = sap_transfer_apdu_rsp(sap.device, result, resp, (uint16_t) resp_len);
		break;
	case SAP_TRANSFER_ATR_RESP:
		// result code, atr len, atr
		DBG("ATR response result %d len %d", result, resp_len);
		ret = sap_transfer_atr_rsp(sap.device, result, resp, (uint16_t) resp_len);
		break;
	case SAP_POWER_SIM_OFF_RESP:
		// sim off result code
		ret = sap_power_sim_off_rsp(sap.device, result);
		break;
	case SAP_POWER_SIM_ON_RESP:
		// sim on result code
		ret = sap_power_sim_on_rsp(sap.device, result);
		break;
	case SAP_RESET_SIM_RESP:
		// sim reset result code
		sap_reset_sim_rsp(sap.device, result);
		ret = sap_status_ind(sap.device, SAP_STATUS_CHANGE_CARD_RESET);		
		break;
	case SAP_TRANSFER_CARD_READER_STATUS_RESP:
		// result code, card reader status
		ret = sap_transfer_card_reader_status_rsp(sap.device, result, status);
		break;
	case SAP_STATUS_IND:
		// sp status
		ret = sap_status_ind(sap.device, status);
		break;
	case SAP_ERROR_RESP:
		// DBG("SAP_ERROR_RESP : %d",result);
		ret = sap_error_rsp(sap.device);
		break;
	case SAP_SET_TRANSPORT_PROTOCOL_RESP:
	default:
		DBG("Error : Unhandled msg_id %d", msg_id);
		break;
	}
	return dbus_message_new_method_return(msg);
}

static DBusMessage *ongoing_call(DBusConnection *conn, DBusMessage *msg,
						void *data)
{
	dbus_bool_t ongoing;

	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_BOOLEAN, &ongoing,
		DBUS_TYPE_INVALID))
		return invalid_args(msg);

	if (sap.call_status && !ongoing) {
		/* An ongoing call has finished. Continue connection.*/
		sap_connect_rsp(sap.device, SAP_STATUS_OK, sap.max_size);
		sap_status_ind(sap.device, SAP_STATUS_CHANGE_CARD_RESET);
		sap.call_status = ongoing;
	} else if (!sap.call_status && ongoing) {
		/* An ongoing call has started.*/
		sap.call_status = ongoing;
	}

	DBG("OngoingCall status set to %d", sap.call_status);

	return dbus_message_new_method_return(msg);
}

static DBusMessage *max_msg_size(DBusConnection *conn, DBusMessage *msg,
						void *data)
{
	dbus_uint32_t size;

	if (sap.sim_status == SIM_CONNECTED)
		return g_dbus_create_error(msg, "org.bluez.Error.Failed",
			"Can't change msg size when connected.");

	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_UINT32, &size,
		DBUS_TYPE_INVALID))
		return invalid_args(msg);

	sap.max_size = size;

	DBG("MaxMessageSize set to %d", sap.max_size);

	return dbus_message_new_method_return(msg);
}

static DBusMessage *card_status(DBusConnection *conn, DBusMessage *msg,
								void *data)
{
	dbus_uint32_t status;

	DBG(" status %d", sap.sim_status);

	if (sap.sim_status != SIM_CONNECTED)
		return g_dbus_create_error(msg, "org.bluez.Error.Failed",
			"Can't change msg size when not connected.");

	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_UINT32, &status,
		DBUS_TYPE_INVALID))
		return invalid_args(msg);

	switch (status) {
	case 0: /* card removed */
		sap.sim_status = SIM_MISSING;
		DBG("SAP_STATUS_CHANGE_CARD_REMOVED");
		sap_status_ind(sap.device, SAP_STATUS_CHANGE_CARD_REMOVED);
		break;

	case 1: /* card inserted */
		if (sap.sim_status == SIM_MISSING) {
			sap.sim_status = SIM_CONNECTED;
			sap_status_ind(sap.device, SAP_STATUS_CHANGE_CARD_INSERTED);
		}
		break;

	case 2: /* card not longer available*/
		sap.sim_status = SIM_POWERED_OFF;
		sap_status_ind(sap.device, SAP_STATUS_CHANGE_CARD_NOT_ACCESSIBLE);
		break;

	default:
		return g_dbus_create_error(msg, "org.bluez.Error.Failed",
			"Unknown card status. Use 0, 1 or 2.");
	}

	DBG("Card status changed to %d", status);

	return dbus_message_new_method_return(msg);
}

static GDBusMethodTable sap_methods[] = {
	{ "OngoingCall", "b", "", ongoing_call },
	{ "MaxMessageSize", "u", "", max_msg_size },
	{ "CardStatus", "u", "", card_status },
	{ "RegisterSAP", "", "", register_sap },
	{ "UnregisterSAP", "", "", unregister_sap },
	{ "SapResponseHandler", "yyyqay", "", sap_response_handler},
	{ }
};

int sap_init(void)
{
	DBG("");
	sap.conn = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);

	if (g_dbus_register_interface(sap.conn, SAP_SEC_PATH,
					SAP_SEC_IFACE, sap_methods, NULL, NULL,
					NULL, NULL) == FALSE) {
		error("sap-dummy interface %s init failed on path %s",
			SAP_SEC_IFACE, SAP_SEC_PATH);
		return -1;
	}

	return 0;
}

void sap_exit(void)
{
	DBG("");
	g_dbus_unregister_interface(sap.conn, SAP_SEC_PATH,
							SAP_SEC_IFACE);
	dbus_connection_unref(sap.conn);
	sap.conn = NULL;
}
