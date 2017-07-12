ifeq ($(USE_BLUETOOTH_SAP),true)
LOCAL_PATH:= $(call my-dir)

# SAP plugin

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	main.c \
	manager.c \
	server.c \
	sap-sec.c

LOCAL_CFLAGS:= \
	-DVERSION=\"4.93\" \

LOCAL_C_INCLUDES:= \
	$(LOCAL_PATH)/../btio \
	$(LOCAL_PATH)/../lib \
	$(LOCAL_PATH)/../src \
	$(LOCAL_PATH)/../gdbus \
	$(call include-path-for, glib) \
	$(call include-path-for, dbus)

LOCAL_SHARED_LIBRARIES := \
	libbluetoothd \
	libbluetooth \
	libbtio \
	libcutils \
	libdbus \
	libglib

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/bluez-plugin
LOCAL_UNSTRIPPED_PATH := $(TARGET_OUT_SHARED_LIBRARIES_UNSTRIPPED)/bluez-plugin
LOCAL_MODULE := sap
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
endif