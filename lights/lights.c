/*
 * Copyright (C) 2016 The CyanogenMod Project
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

#define LOG_TAG "lights"

#include <cutils/log.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/types.h>

#include <hardware/lights.h>

#define UNUSED __attribute__((unused))

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static struct light_state_t g_notification;
static struct light_state_t g_attention;
static struct light_state_t g_battery;

static const char* const LED_BLINK_TIME_FMT = "/sys/class/leds/%s/led_time";
static const char* const LED_BLINK_FMT = "/sys/class/leds/%s/blink";
static const char* const LED_BRIGHTNESS_FMT = "/sys/class/leds/%s/brightness";

static const char* const COLOR_STR[] = {
    "red", "green", "blue"
};

static const char* const lcd_backlight_filename =
    "/sys/class/leds/lcd-backlight/brightness";

static void
write_file(const char *path, const char *str)
{
    int fd;
    ssize_t written = -1;

    ALOGI("%s: path=%s str=%s\n", __func__, path, str);
    fd = open(path, O_RDWR);
    if (fd < 0) {
        ALOGE("%s: Failed to open %s: %s\n",
                __func__, path, strerror(errno));
        return;
    }
    written = write(fd, str, strlen(str));
    if (written < 0) {
        ALOGE("%s: Failed to write to %s: %s\n",
                __func__, path, strerror(errno));
    }
    close(fd);
}

static uint32_t
rgb_to_brightness(const struct light_state_t *state)
{
    uint32_t color = state->color & 0x00ffffff;
    return ((77 * ((color >> 16) & 0xff)) +
            (150 * ((color >> 8) & 0xff)) +
            (29 * (color & 0xff))) >> 8;
}

static bool
is_lit(const struct light_state_t *state)
{
    return (state->color & 0x00ffffff);
}

static void
init_globals(void)
{
    pthread_mutex_init(&g_lock, NULL);
}

/*
 * The AW2013 driver reads 4 values from color/led_time:
 *   rise_time, hold_time, fall_time, off_time
 * Units for rise_time and fall_time are roughly 1/2 second.
 * Units for hold_time and off_time are roughly 1/4 second.
 * Note we hard code rise_time and fall_time to 1.
 *
 * Only one color may be active at a time, and only one mode (solid
 * or blinking).  Blinking is always at full brightness.
 */
static void
set_speaker_light_locked(UNUSED struct light_device_t *dev,
        struct light_state_t *state)
{
    int color_idx = -1;
    uint32_t color_val = 0;
    bool flash = false;
    uint32_t hold_time = 0;
    uint32_t off_time = 0;
    int i;

    if (state) {
        for (i = 0; i < 3; ++i) {
            uint32_t val = (state->color >> ((2-i)*8)) & 0xff;
            if (val > color_val) {
                color_idx = i;
                color_val = val;
            }
        }
        flash = (state->flashMode == LIGHT_FLASH_TIMED);
        hold_time = state->flashOnMS / 250;
        off_time = state->flashOffMS / 250;
    }

    if (color_idx < 0 || color_val == 0) {
        for (i = 0; i < 3; ++i) {
            char pathname[80];
            snprintf(pathname, sizeof(pathname),
                    LED_BRIGHTNESS_FMT, COLOR_STR[i]);
            write_file(pathname, "0");
        }
    }
    else {
        char pathname[80];
        char val_str[40];
        if (flash) {
            snprintf(pathname, sizeof(pathname),
                    LED_BLINK_TIME_FMT, COLOR_STR[color_idx]);
            snprintf(val_str, sizeof(val_str),
                    "2 %u 2 %u", hold_time, off_time);
            write_file(pathname, val_str);
            snprintf(pathname, sizeof(pathname),
                    LED_BLINK_FMT, COLOR_STR[color_idx]);
            write_file(pathname, "1");
        }
        else {
            snprintf(pathname, sizeof(pathname),
                    LED_BRIGHTNESS_FMT, COLOR_STR[color_idx]);
            snprintf(val_str, sizeof(val_str),
                    "%u", color_val);
            write_file(pathname, val_str);
        }
    }
}

static void
handle_speaker_battery_locked(struct light_device_t *dev)
{
    if (is_lit(&g_notification)) {
        set_speaker_light_locked(dev, &g_notification);
    } else if (is_lit(&g_attention)) {
        set_speaker_light_locked(dev, &g_attention);
    } else if (is_lit(&g_battery)) {
        set_speaker_light_locked(dev, &g_battery);
    } else {
        /* No lights or blink */
        set_speaker_light_locked(dev, NULL);
    }
}

static int
set_light_backlight(UNUSED struct light_device_t *dev,
        const struct light_state_t *state)
{
    char val[12+1];

    snprintf(val, sizeof(val), "%u", rgb_to_brightness(state));

    pthread_mutex_lock(&g_lock);

    write_file(lcd_backlight_filename, val);

    pthread_mutex_unlock(&g_lock);

    return 0;
}

static int
set_light_attention(struct light_device_t *dev,
        const struct light_state_t *state)
{
    pthread_mutex_lock(&g_lock);

    g_attention = *state;
    handle_speaker_battery_locked(dev);

    pthread_mutex_unlock(&g_lock);

    return 0;
}

static int
set_light_notifications(struct light_device_t *dev,
        const struct light_state_t *state)
{
    pthread_mutex_lock(&g_lock);

    g_notification = *state;
    handle_speaker_battery_locked(dev);

    pthread_mutex_unlock(&g_lock);

    return 0;
}

static int
set_light_battery(struct light_device_t *dev,
        const struct light_state_t *state)
{
    pthread_mutex_lock(&g_lock);

    g_battery = *state;
    handle_speaker_battery_locked(dev);

    pthread_mutex_unlock(&g_lock);

    return 0;
}

static int
close_lights(struct light_device_t *dev)
{
    free(dev);
    return 0;
}

static int
open_lights(const struct hw_module_t *module, const char *name,
        struct hw_device_t **device)
{
    int (*set_light)(struct light_device_t *dev,
            const struct light_state_t *state);
    struct light_device_t *dev;

    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name)) {
        set_light = set_light_backlight;
    } else if (0 == strcmp(LIGHT_ID_ATTENTION, name)) {
        set_light = set_light_attention;
    } else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name))  {
        set_light = set_light_notifications;
    } else if (0 == strcmp(LIGHT_ID_BATTERY, name)) {
        set_light = set_light_battery;
    } else {
        return -EINVAL;
    }

    pthread_once(&g_init, init_globals);
    dev = malloc(sizeof(struct light_device_t));
    memset(dev, 0, sizeof(struct light_device_t));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t *) module;
    dev->common.close = (int (*)(struct hw_device_t *)) close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t *)dev;
    return 0;

}

static struct hw_module_methods_t lights_module_methods = {
    .open = open_lights,
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "Lights module",
    .author = "The CyanogenMod Project",
    .methods = &lights_module_methods,
};
