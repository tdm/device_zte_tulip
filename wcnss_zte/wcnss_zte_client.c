/*
 * Copyright (C) 2014, The CyanogenMod Project
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

//#define LOG_NDEBUG 0

#define LOG_TAG "wcnss_zte"

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cutils/log.h>

#define SUCCESS 0
#define FAILED -1

#define WIFIMAC_PATH "/persist/wifimac.dat"

int wcnss_init_qmi(void)
{
    /* empty */
    return SUCCESS;
}

int wcnss_qmi_get_wlan_address(unsigned char *pBdAddr)
{
    int fd, ret;
    char buf[9+5*6+1];

    fd = open(WIFIMAC_PATH, O_RDONLY);
    if (fd < 0) {
        ALOGE("Failure opening wifimac path: %d\n", errno);
        return FAILED;
    }

    memset(buf, 0, sizeof(buf));
    ret = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (ret < 0) {
        ALOGE("Failure to read MAC addr\n");
        return FAILED;
    }

    ret = sscanf(buf, "wifiaddr:0x%02hhx 0x%02hhx 0x%02hhx 0x%02hhx 0x%02hhx 0x%02hhx",
            &pBdAddr[0],
            &pBdAddr[1],
            &pBdAddr[2],
            &pBdAddr[3],
            &pBdAddr[4],
            &pBdAddr[5]);
    if (ret != 6) {
        ALOGE("Failure to parse MAC addr\n");
        return FAILED;
    }

    ALOGI("Found MAC address: %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx\n",
            pBdAddr[0],
            pBdAddr[1],
            pBdAddr[2],
            pBdAddr[3],
            pBdAddr[4],
            pBdAddr[5]);

    return SUCCESS;
}

void wcnss_qmi_deinit(void)
{
    /* empty */
}
