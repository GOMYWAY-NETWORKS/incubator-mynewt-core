/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <string.h>
#include <assert.h>
#include "sysinit/sysinit.h"
#include "syscfg/syscfg.h"
#include "sysflash/sysflash.h"
#include "os/os.h"
#include "log/log.h"
#include "bootutil/image.h"
#include "bootutil/bootutil.h"
#include "imgmgr/imgmgr.h"
#include "config/config.h"
#include "config/config_file.h"
#include "reboot/log_reboot.h"
#include "bsp/bsp.h"
#include "flash_map/flash_map.h"

#if MYNEWT_VAL(REBOOT_LOG_FCB)
#include "fcb/fcb.h"
#endif

static struct log_handler *reboot_log_handler;
static struct log reboot_log;
static uint16_t reboot_cnt;
static uint16_t soft_reboot;
static char reboot_cnt_str[12];
static char soft_reboot_str[12];
static char *reboot_cnt_get(int argc, char **argv, char *buf, int max_len);
static int reboot_cnt_set(int argc, char **argv, char *val);
static int reboot_cnt_export(void (*export_func)(char *name, char *val),
                             enum conf_export_tgt tgt);

struct conf_handler reboot_conf_handler = {
    .ch_name = "reboot",
    .ch_get = reboot_cnt_get,
    .ch_set = reboot_cnt_set,
    .ch_commit = NULL,
    .ch_export = reboot_cnt_export
};

#if MYNEWT_VAL(REBOOT_LOG_FCB)
static struct fcb_log reboot_log_fcb;
static struct flash_area sector;
#endif

/**
 * Reboot log initilization
 * @param type of log(console or storage); number of entries to restore
 * @return 0 on success; non-zero on failure
 */
int
reboot_init_handler(int log_store_type, uint8_t entries)
{
#if MYNEWT_VAL(REBOOT_LOG_FCB)
    const struct flash_area *ptr;
    struct fcb *fcbp = &reboot_log_fcb.fl_fcb;
#endif
    void *arg;
    int rc;

    rc = conf_register(&reboot_conf_handler);
    if (rc != 0) {
        return rc;
    }

    switch (log_store_type) {
#if MYNEWT_VAL(REBOOT_LOG_FCB)
        case LOG_STORE_FCB:
            if (flash_area_open(MYNEWT_VAL(REBOOT_LOG_FLASH_AREA), &ptr)) {
                return rc;
            }
            fcbp = &reboot_log_fcb.fl_fcb;
            sector = *ptr;
            fcbp->f_sectors = &sector;
            fcbp->f_sector_cnt = 1;
            fcbp->f_magic = 0x7EADBADF;
            fcbp->f_version = g_log_info.li_version;

            reboot_log_fcb.fl_entries = entries;

            rc = fcb_init(fcbp);
            if (rc) {
                return rc;
            }
            reboot_log_handler = (struct log_handler *)&log_fcb_handler;
            if (rc) {
                return rc;
            }
            arg = &reboot_log_fcb;
            break;
#endif
#if MYNEWT_VAL(REBOOT_LOG_CONSOLE)
       case LOG_STORE_CONSOLE:
            reboot_log_handler = (struct log_handler *)&log_console_handler;
            arg = NULL;
            break;
#endif
       default:
            assert(0);
    }

    rc = log_register("reboot_log", &reboot_log,
                      (struct log_handler *)reboot_log_handler,
                      arg, LOG_SYSLEVEL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Logs reboot with the specified reason
 * @param reason for reboot
 * @return 0 on success; non-zero on failure
 */
int
log_reboot(enum hal_reset_reason reason)
{
    int rc;
    char str[12] = {0};
    struct image_version ver;
    int16_t reboot_tmp_cnt;

    rc = 0;
#if MYNEWT_VAL(REBOOT_LOG_FCB)
    {
        const struct flash_area *ptr;
        if (flash_area_open(MYNEWT_VAL(REBOOT_LOG_FLASH_AREA), &ptr)) {
            goto err;
        }
    }
#endif

    reboot_tmp_cnt = reboot_cnt;

    if (reason == HAL_RESET_REQUESTED) {
        /*
         * Save soft_reboot as 1 if user is requesting restart.
         */
        reboot_tmp_cnt = 1;
        conf_save_one("reboot/soft_reboot",
                      conf_str_from_value(CONF_INT16, &reboot_tmp_cnt,
                                          str, sizeof(str)));
        reboot_tmp_cnt = reboot_cnt + 1;
    } else {
        conf_save_one("reboot/soft_reboot", "0");
        if (soft_reboot && reason == HAL_RESET_SOFT) {
            /* No need to log as it's not a hard reboot */
            goto err;
        } else {
            reboot_cnt++;
            reboot_tmp_cnt = reboot_cnt;
        }
    }

    /*
     * Only care for this return code as it will tell whether the config is
     * full, the caller of the function might not care about the return code
     * Saving the reboot cnt
     */
    rc = conf_save_one("reboot/reboot_cnt",
                       conf_str_from_value(CONF_INT16, &reboot_tmp_cnt,
                                           str, sizeof(str)));

    imgr_my_version(&ver);

    /* Log a reboot */
    LOG_CRITICAL(&reboot_log, LOG_MODULE_REBOOT, "rsn:%s, cnt:%u,"
                 " img:%u.%u.%u.%u", REBOOT_REASON_STR(reason), reboot_tmp_cnt,
                 ver.iv_major, ver.iv_minor, ver.iv_revision,
                 (unsigned int)ver.iv_build_num);
err:
    return (rc);
}

static char *
reboot_cnt_get(int argc, char **argv, char *buf, int max_len)
{
    if (argc == 1) {
        if (!strcmp(argv[0], "reboot_cnt")) {
            return reboot_cnt_str;
        } else if (!strcmp(argv[0], "soft_reboot")) {
            return soft_reboot_str;
        }
    }
    return NULL;
}

static int
reboot_cnt_set(int argc, char **argv, char *val)
{
    int rc;

    if (argc == 1) {
        if (!strcmp(argv[0], "reboot_cnt")) {
            rc = CONF_VALUE_SET(val, CONF_STRING, reboot_cnt_str);
            if (rc) {
                goto err;
            }

            return CONF_VALUE_SET(reboot_cnt_str, CONF_INT16, reboot_cnt);
        } else if (!strcmp(argv[0], "soft_reboot")) {
            rc = CONF_VALUE_SET(val, CONF_STRING, soft_reboot_str);
            if (rc) {
                goto err;
            }

            return CONF_VALUE_SET(soft_reboot_str, CONF_INT16, soft_reboot);
        }
    }

err:
    return OS_ENOENT;
}

static int
reboot_cnt_export(void (*func)(char *name, char *val), enum conf_export_tgt tgt)
{
    if (tgt == CONF_EXPORT_SHOW) {
        func("reboot/reboot_cnt", reboot_cnt_str);
        func("reboot/soft_reboot", soft_reboot_str);
    }
    return 0;
}

void
log_reboot_pkg_init(void)
{
    int type;
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    (void)rc;
    (void)type;

#if MYNEWT_VAL(REBOOT_LOG_ENTRY_COUNT)
#if MYNEWT_VAL(REBOOT_LOG_FCB)
    type = LOG_STORE_FCB;
#elif MYNEWT_VAL(REBOOT_LOG_CONSOLE)
    type = LOG_STORE_CONSOLE;
#else
#error "sys/reboot included, but no log target"
#endif
    rc = reboot_init_handler(type, MYNEWT_VAL(REBOOT_LOG_ENTRY_COUNT));
    SYSINIT_PANIC_ASSERT(rc == 0);
#endif
}
