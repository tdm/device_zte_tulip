LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := camera.$(TARGET_BOARD_PLATFORM)
LOCAL_SRC_FILES := CameraWrapper.cpp

LOCAL_C_INCLUDES := \
    system/media/camera/include

LOCAL_SHARED_LIBRARIES := \
    libhardware liblog libcamera_client libutils libcutils libdl

#LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR_SHARED_LIBRARIES)/hw
LOCAL_MULTILIB := 32
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_CFLAGS := -Werror
include $(BUILD_SHARED_LIBRARY)
