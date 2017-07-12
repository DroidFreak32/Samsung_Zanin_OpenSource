# Copyright (C) 2008 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# Copyright The Android Open Source Project


 ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)

  LOCAL_PATH := $(call my-dir)

  include $(CLEAR_VARS)

  LOCAL_ARM_MODE := arm
  LOCAL_CFLAGS := -fPIC -DPIC -D_POSIX_SOURCE -fomit-frame-pointer -O3 -mcpu=cortex-a9 -mfpu=neon -mthumb-interwork -fno-builtin -fno-short-enums -mfloat-abi=softfp -ftree-vectorize

#  LOCAL_C_INCLUDES += external/alsa-lib/include  $(TARGET_COMMON_BASE)/include/linux/broadcom/
 LOCAL_C_INCLUDES += $(BRCM_ALSA_LIB_DIR)/include  $(TARGET_COMMON_BASE)/include/linux/broadcom/

  LOCAL_SRC_FILES := \
		simplefilter.c
  LOCAL_PRELINK_MODULE := false
  LOCAL_MODULE := libasound_module_pcm_bcmfilter
  LOCAL_MODULE_TAGS := debug optional
  #LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)
  LOCAL_MODULE_PATH := $(TARGET_OUT)/usr/lib/alsa-lib

  #LOCAL_STATIC_LIBRARIES += libmedia_helper
  #LOCAL_WHOLE_STATIC_LIBRARIES += libaudiohw_legacy

  LOCAL_SHARED_LIBRARIES := \
    libasound \
    libc \
    libdl \
    libcutils

  include $(BUILD_SHARED_LIBRARY)

 ifeq ($(strip $(OPENSOURCE_ALSA_AUDIO)),false)

  include $(CLEAR_VARS)

  LOCAL_ARM_MODE := arm
  LOCAL_CFLAGS := -fPIC -DPIC -D_POSIX_SOURCE

  LOCAL_SRC_FILES := \
		dll_bcm_test_filter.c
  LOCAL_PRELINK_MODULE := false
  LOCAL_MODULE := libbcm_test_filter
  LOCAL_MODULE_TAGS := debug optional
  #LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)
  LOCAL_MODULE_PATH := $(TARGET_OUT)/usr/lib/alsa-lib


  LOCAL_SHARED_LIBRARIES := \
    libc

  include $(BUILD_SHARED_LIBRARY)

  include $(CLEAR_VARS)

  LOCAL_ARM_MODE := arm
  LOCAL_CFLAGS := -fPIC -DPIC -D_POSIX_SOURCE -fomit-frame-pointer -O3 -mcpu=cortex-a9 -mfpu=neon -mthumb-interwork -fno-builtin -fno-short-enums -mfloat-abi=softfp -ftree-vectorize

  LOCAL_SRC_FILES := \
		elliptic_hp.c
  LOCAL_PRELINK_MODULE := false
  LOCAL_MODULE := libbcm_hp_filter
  LOCAL_MODULE_TAGS := debug optional
  #LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)
  LOCAL_MODULE_PATH := $(TARGET_OUT)/usr/lib/alsa-lib


  LOCAL_SHARED_LIBRARIES := \
    libc \
    libcutils


  include $(BUILD_SHARED_LIBRARY)

 endif
 endif

