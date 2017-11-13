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
#include <dlfcn.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>

#include <hardware/audio_amplifier.h>
#include <system/audio.h>

#include <tinyalsa/asoundlib.h>

extern int exTfa98xx_calibration(void);
extern int exTfa98xx_speakeron_both(uint32_t, int);
extern int exTfa98xx_speakeroff();

#define AMP_MIXER_CTL "Tfa98xx Mode"

typedef enum {
    SMART_PA_FOR_AUDIO = 0,
    SMART_PA_FOR_MUSIC = 0,
    SMART_PA_FOR_VOIP = 1,
    SMART_PA_FIND = 1,          /* ??? */
    SMART_PA_FOR_VOICE = 3,
    SMART_PA_MMI = 3,           /* ??? */
} smart_pa_mode_t;

#define TFA9890_DEVICE_MASK \
    (AUDIO_DEVICE_OUT_EARPIECE | \
     AUDIO_DEVICE_OUT_SPEAKER)

#define SPEAKER_BOTTOM 1
#define SPEAKER_TOP 2
#define SPEAKER_BOTH 3

typedef struct tfa9890_device {
    amplifier_device_t amp_dev;
    audio_mode_t mode;
} tfa9890_device_t;

static tfa9890_device_t *tfa_dev;

static int g_tfa_mode;
static int g_tfa_old_mode;

static int
tfa9890_enable_clocks(bool enable)
{
    enum mixer_ctl_type type;
    struct mixer_ctl *ctl;
    struct mixer *mixer;
    int val;

    ALOGI("%s: enable=%s\n", __func__, (enable ? "true" : "false"));

    mixer = mixer_open(0);
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

    val = mixer_ctl_get_value(ctl, 0);
    ALOGI("%s: before: val=%d\n", __func__, val);
    mixer_ctl_set_value(ctl, 0, enable);
    val = mixer_ctl_get_value(ctl, 0);
    ALOGI("%s: after: val=%d\n", __func__, val);

    mixer_close(mixer);

    return 0;
}

static int
tfa9890_enable_device(audio_mode_t audio_mode)
{
    smart_pa_mode_t mode = SMART_PA_FOR_AUDIO;
    int spk_sel = SPEAKER_BOTH;
    int ret;

    switch(audio_mode) {
    case AUDIO_MODE_NORMAL:
        ALOGI("%s: AUDIO_MODE_NORMAL\n", __func__);
        mode = SMART_PA_FOR_AUDIO;
        spk_sel = SPEAKER_BOTH;
        break;
    case AUDIO_MODE_RINGTONE:
        ALOGI("%s: AUDIO_MODE_RINGTONE\n", __func__);
        mode = SMART_PA_FOR_VOICE;
        spk_sel = SPEAKER_BOTH;
        break;
    case AUDIO_MODE_IN_CALL:
        ALOGI("%s: AUDIO_MODE_IN_CALL\n", __func__);
        mode = SMART_PA_FOR_VOICE;
        spk_sel = SPEAKER_TOP;
        break;
    case AUDIO_MODE_IN_COMMUNICATION:
        ALOGI("%s: AUDIO_MODE_IN_COMMUNICATION\n", __func__);
        mode = SMART_PA_FOR_VOIP;
        spk_sel = SPEAKER_TOP;
        break;
    default:
        ALOGI("%s: audio mode default\n", __func__);
    }
    tfa9890_enable_clocks(false);
    tfa9890_enable_clocks(true);
    ret = exTfa98xx_speakeron_both(mode, spk_sel);
    if (ret != 0) {
        ALOGI("exTfa98xx_speakeron_both(%d,%d) failed: %d\n", mode, spk_sel, ret);
    }
    return ret;
}

static int
tfa9890_disable_device()
{
    tfa9890_enable_clocks(false);
    return 0;
}

static int
amp_set_mode(amplifier_device_t *amp, audio_mode_t mode)
{
    int ret = 0;
    tfa9890_device_t *dev = (tfa9890_device_t *)amp;

    ALOGI("%s: mode=0x%08x\n", __func__, mode);

    dev->mode = mode;
    return ret;
}

static int
amp_enable_output_devices(amplifier_device_t *amp, uint32_t devices, bool enable)
{
    tfa9890_device_t *dev = (tfa9890_device_t *)amp;

    ALOGI("%s: devices=0x%08x enable=%s\n", __func__,
          devices, (enable ? "true" : "false"));

    if ((devices & TFA9890_DEVICE_MASK) == 0) {
        ALOGI("%s: No relevant devices", __func__);
        return 0;
    }

    if (enable) {
        tfa9890_enable_device(dev->mode);
    }
    else {
        tfa9890_disable_device();
    }

    return 0;
}

static int
amp_dev_close(hw_device_t *device)
{
    tfa9890_device_t *dev = (tfa9890_device_t*) device;

    free(dev);

    return 0;
}

static int
amp_set_parameters(amplifier_device_t *amp, struct str_parms *parms) {
    ALOGI("%s: %p, %p\n", __func__, amp, parms);

    return 0;
}

static int
amp_module_open(const hw_module_t *module,
                __attribute__((unused)) const char *name,
                hw_device_t **device)
{
    int ret;

    if (tfa_dev) {
        ALOGE("%s: Unable to open second instance of the amplifier\n",
              __func__);
        return -EBUSY;
    }

    tfa_dev = calloc(1, sizeof(tfa9890_device_t));
    if (!tfa_dev) {
        ALOGE("%s: Unable to allocate memory for amplifier device\n",
              __func__);
        return -ENOMEM;
    }

    tfa_dev->amp_dev.common.tag = HARDWARE_DEVICE_TAG;
    tfa_dev->amp_dev.common.module = (hw_module_t *) module;
    tfa_dev->amp_dev.common.version = HARDWARE_DEVICE_API_VERSION(1, 0);
    tfa_dev->amp_dev.common.close = amp_dev_close;

    tfa_dev->amp_dev.enable_output_devices = amp_enable_output_devices;
    tfa_dev->amp_dev.set_mode = amp_set_mode;
    tfa_dev->amp_dev.set_parameters = amp_set_parameters;

    tfa9890_enable_clocks(true);
    ret = exTfa98xx_calibration();
    if (ret != 0) {
        ALOGI("exTfa98xx_calibration failed: %d\n", ret);
    }
    tfa9890_enable_clocks(false);

    *device = (hw_device_t *) tfa_dev;

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
