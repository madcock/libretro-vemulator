LOCAL_PATH := $(call my-dir)

CORE_DIR := $(LOCAL_PATH)/..

include $(CLEAR_VARS)
LOCAL_CFLAGS    :=

LOCAL_MODULE    := retro

include $(CORE_DIR)/Makefile.common

LOCAL_SRC_FILES := $(SOURCES_C) $(SOURCES_CXX)
LOCAL_CFLAGS    += -O3 -std=gnu++99 -ffast-math -funroll-loops
LOCAL_LDFLAGS   := -Wl,-version-script=$(CORE_DIR)/libretro/link.T

ifeq ($(TARGET_ARCH),arm)
LOCAL_CFLAGS += -DANDROID_ARM
endif

ifeq ($(TARGET_ARCH),x86)
LOCAL_CFLAGS +=  -DANDROID_X86
endif

ifeq ($(TARGET_ARCH),mips)
LOCAL_CFLAGS += -DANDROID_MIPS -D__mips__ -D__MIPSEL__
endif

include $(BUILD_SHARED_LIBRARY)
