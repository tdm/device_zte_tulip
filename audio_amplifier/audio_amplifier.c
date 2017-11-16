/*
 * Copyright (C) 2013-2016, The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "tulip-tfa98xx"

#include <stdio.h>
#include <stdlib.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>

#include <hardware/audio_amplifier.h>
#include <system/audio.h>

#include <tinyalsa/asoundlib.h>

extern int exTfa98xx_calibration(void);
extern int exTfa98xx_speakeron_both(uint32_t, int);
extern int exTfa98xx_speakeroff();

//#define AMP_MIXER_CTL "Initial external PA"
#define AMP_MIXER_CTL "Tfa98xx Mode"

typedef enum {
    SMART_PA_FOR_AUDIO = 0,
    SMART_PA_FOR_MUSIC = 0,
    SMART_PA_FOR_VOIP = 1,
    SMART_PA_FIND = 1,          /* ??? */
    SMART_PA_FOR_VOICE = 3,
    SMART_PA_MMI = 3,           /* ??? */
} smart_pa_mode_t;

#define SPEAKER_BOTTOM 1
#define SPEAKER_TOP 2
#define SPEAKER_BOTH 3

typedef struct amp_device {
    amplifier_device_t amp_dev;
    audio_mode_t mode;
} amp_device_t;

static amp_device_t *amp_dev;

static int set_clocks_enabled(bool enable)
{
    enum mixer_ctl_type type;
    struct mixer_ctl *ctl;
    struct mixer *mixer = mixer_open(0);

    ALOGI("set_clocks_enabled: ctl=%s enable=%s\n",
          AMP_MIXER_CTL, (enable ? "true" : "false"));

    if (mixer == NULL) {
        ALOGE("Error opening mixer 0");
        return -1;
    }

    ctl = mixer_get_ctl_by_name(mixer, AMP_MIXER_CTL);
    if (ctl == NULL) {
        mixer_close(mixer);
        ALOGE("%s: Could not find %s\n", __func__, AMP_MIXER_CTL);
        return -ENODEV;
    }

    type = mixer_ctl_get_type(ctl);
    if (type != MIXER_CTL_TYPE_ENUM) {
        ALOGE("%s: %s is not supported\n", __func__, AMP_MIXER_CTL);
        mixer_close(mixer);
        return -ENOTTY;
    }

    mixer_ctl_set_value(ctl, 0, enable);
    mixer_close(mixer);
    return 0;
}

static int amp_set_mode(struct amplifier_device *device, audio_mode_t mode)
{
    int ret = 0;
    amp_device_t *dev = (amp_device_t *) device;

    ALOGI("amp_set_mode: mode=0x%08x\n", mode);

    dev->mode = mode;
    return ret;
}

#define SMART_PA_DEVICES_MASK \
    (AUDIO_DEVICE_OUT_EARPIECE | AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_WIRED_HEADSET | \
     AUDIO_DEVICE_OUT_WIRED_HEADPHONE)

static int amp_enable_output_devices(struct amplifier_device *device, uint32_t devices, bool enable)
{
    amp_device_t *dev = (amp_device_t *) device;
    int ret;

    ALOGI("amp_enable_output_devices: devices=0x%08x enable=%s\n",
          devices, (enable ? "true" : "false"));
    if ((devices & SMART_PA_DEVICES_MASK) != 0) {
        if (enable) {
            smart_pa_mode_t mode;
            int spk_sel = SPEAKER_BOTH;

            switch(dev->mode) {
            case AUDIO_MODE_IN_CALL:
                ALOGI("amp_enable_output_devices: AUDIO_MODE_IN_CALL\n");
                mode = SMART_PA_FOR_VOICE;
                spk_sel = SPEAKER_TOP;
                break;
            case AUDIO_MODE_IN_COMMUNICATION:
                ALOGI("amp_enable_output_devices: AUDIO_MODE_IN_COMMUNICATION\n");
                mode = SMART_PA_FOR_VOIP;
                break;
            default:
                ALOGI("amp_enable_output_devices: audio mode default\n");
                mode = SMART_PA_FOR_AUDIO;
            }
            ALOGI("amp_enable_output_devices: speakeronmode=%d\n", (int)mode);
            set_clocks_enabled(true);
            if ((ret = exTfa98xx_speakeroff()) != 0) {
                ALOGI("exTfa98xx_speakeroff failed: %d\n", ret);
            }
            if ((ret = exTfa98xx_speakeron_both(mode, spk_sel)) != 0) {
                ALOGI("exTfa98xx_speakeron_both(%d) failed: %d\n", mode, ret);
            }
        } else {
            set_clocks_enabled(false);
        }
    }

    return 0;
}

static int amp_dev_close(hw_device_t *device)
{
    amp_device_t *dev = (amp_device_t*) device;

    free(dev);

    return 0;
}

int tfa9890_set_parameters(struct amplifier_device *device, struct str_parms *parms) {
    ALOGD("%s: %p\n", __func__, parms);
    amp_device_t *tfa9890 = (amp_device_t*) device;
    // todo, dump parms.  Might be more stuff we need to do here
    return 0;
}

static int
amp_module_open(const hw_module_t *module,
                __attribute__((unused)) const char *name,
                hw_device_t **device)
{
    int ret;

    if (amp_dev) {
        ALOGE("%s:%d: Unable to open second instance of the amplifier\n",
              __func__, __LINE__);
        return -EBUSY;
    }

    amp_dev = calloc(1, sizeof(amp_device_t));
    if (!amp_dev) {
        ALOGE("%s:%d: Unable to allocate memory for amplifier device\n",
              __func__, __LINE__);
        return -ENOMEM;
    }

    amp_dev->amp_dev.common.tag = HARDWARE_DEVICE_TAG;
    amp_dev->amp_dev.common.module = (hw_module_t *) module;
    amp_dev->amp_dev.common.version = HARDWARE_DEVICE_API_VERSION(1, 0);
    amp_dev->amp_dev.common.close = amp_dev_close;

    amp_dev->amp_dev.enable_output_devices = amp_enable_output_devices;
    amp_dev->amp_dev.set_mode = amp_set_mode;

    *device = (hw_device_t *) amp_dev;

    set_clocks_enabled(true);
    ret = exTfa98xx_calibration();
    if (ret != 0) {
        ALOGI("exTfa98xx_calibration failed: %d\n", ret);
    }
    set_clocks_enabled(false);

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = amp_module_open,
};

amplifier_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AMPLIFIER_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AMPLIFIER_HARDWARE_MODULE_ID,
        .name = "Tulip amplifier HAL",
        .author = "The CyanogenMod Open Source Project",
        .methods = &hal_module_methods,
    },
};
