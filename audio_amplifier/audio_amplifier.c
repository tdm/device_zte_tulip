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

extern int exTfa98xx_calibration(int);
extern int exTfa98xx_speakeron_both(uint32_t, int);
extern int exTfa98xx_speakeroff();

#define AMP_MIXER_CTL "Tfa98xx Mode"
#define SPK_MIXER_CTL "Speaker Select"

#define TFA_MODE_DISABLE        0
#define TFA_MODE_INCALL_SPEAKER 3
#define TFA_MODE_INCALL_HANDSET 4
#define TFA_MODE_HANDSET        4
#define TFA_MODE_SPEAKER        1

#define SPEAKER_NONE    0
#define SPEAKER_BOTTOM  1 /* Left */
#define SPEAKER_TOP     2 /* Right */
#define SPEAKER_BOTH    3

typedef struct amp_device {
    amplifier_device_t amp_dev;
    int tfa_mode;
} amp_device_t;

static amp_device_t *amp_dev;

static int get_amp_mixer_values(int *tfa_mode, int *spk_sel)
{
    struct mixer *mixer;
    struct mixer_ctl *amp_ctl;
    struct mixer_ctl *spk_ctl;

    mixer = mixer_open(0);
    if (mixer == NULL) {
        ALOGE("Error opening mixer 0");
        return -1;
    }

    amp_ctl = mixer_get_ctl_by_name(mixer, AMP_MIXER_CTL);
    if (amp_ctl == NULL) {
        mixer_close(mixer);
        ALOGE("%s: Could not find %s\n", __func__, AMP_MIXER_CTL);
        return -ENODEV;
    }
    spk_ctl = mixer_get_ctl_by_name(mixer, SPK_MIXER_CTL);
    if (spk_ctl == NULL) {
        mixer_close(mixer);
        ALOGE("%s: Could not find %s\n", __func__, SPK_MIXER_CTL);
        return -ENODEV;
    }

    *tfa_mode = mixer_ctl_get_value(amp_ctl, 0);
    *spk_sel = mixer_ctl_get_value(spk_ctl, 0);
    ALOGI("%s: tfa_mode=%d spk_sel=%d\n", __func__, *tfa_mode, *spk_sel);

    mixer_close(mixer);
    return 0;
}

static int set_tfa_mode(int mode)
{
    struct mixer *mixer;
    struct mixer_ctl *amp_ctl;
    int old_mode;

    ALOGI("%s: mode=%d", __func__, mode);

    mixer = mixer_open(0);
    if (mixer == NULL) {
        ALOGE("Error opening mixer 0");
        return -1;
    }

    amp_ctl = mixer_get_ctl_by_name(mixer, AMP_MIXER_CTL);
    if (amp_ctl == NULL) {
        mixer_close(mixer);
        ALOGE("%s: Could not find %s\n", __func__, AMP_MIXER_CTL);
        return -ENODEV;
    }

    old_mode = mixer_ctl_get_value(amp_ctl, 0);
    ALOGI("set_tfa_mode: mode=%d old_mode=%d\n", mode, old_mode);
    if (mode != old_mode) {
        mixer_ctl_set_value(amp_ctl, 0, mode);
    }

    mixer_close(mixer);
    return 0;
}

/* This is called after the mixer controls are set */
static int amp_set_output_devices(struct amplifier_device *device, uint32_t devices)
{
    amp_device_t *dev = (amp_device_t *)device;
    int tfa_mode = TFA_MODE_DISABLE;
    int spk_sel = SPEAKER_NONE;

    ALOGI("%s: devices=0x%08x\n", __func__, devices);

    if (!devices) {
        return 0;
    }

    get_amp_mixer_values(&tfa_mode, &spk_sel);
    ALOGI("%s: dev->tfa_mode=%d tfa_mode=%d spk_sel=%d\n", __func__, dev->tfa_mode, tfa_mode, spk_sel);
    if (tfa_mode != dev->tfa_mode) {
        exTfa98xx_speakeron_both(tfa_mode - 1, spk_sel);
        dev->tfa_mode = tfa_mode;
    }

    return 0;
}

/* This is called before the mixer controls are set */
static int amp_enable_output_devices(struct amplifier_device *device, uint32_t devices, bool enable)
{
    amp_device_t *dev = (amp_device_t*) device;
    ALOGI("%s: devices=0x%08x enable=%s\n",
          __func__, devices, (enable ? "true" : "false"));

    if (!enable) {
        exTfa98xx_speakeroff();
        dev->tfa_mode = 0;
        return 0;
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
    amp_device_t *dev = (amp_device_t*) device;
    ALOGD("%s: %p\n", __func__, parms);
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

    amp_dev->amp_dev.set_output_devices = amp_set_output_devices;
    amp_dev->amp_dev.enable_output_devices = amp_enable_output_devices;

    *device = (hw_device_t *) amp_dev;

    set_tfa_mode(1);
    ret = exTfa98xx_calibration(0);
    if (ret != 0) {
        ALOGI("exTfa98xx_calibration failed: %d\n", ret);
    }
    set_tfa_mode(0);

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
