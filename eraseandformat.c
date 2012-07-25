/*
 * Copyright (C) 2012 Drew Walton & Nathan Bass
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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/limits.h>
#include <dirent.h>
#include <sys/stat.h>

#include <signal.h>
#include <sys/wait.h>

#include "common.h"
#include "cutils/properties.h"
#include "roots.h"
#include "recovery_ui.h"

#include "extendedcommands.h"
#include "nandroid.h"
#include "mounts.h"
#include "flashutils/flashutils.h"
#include "edify/expr.h"
#include <libgen.h>
#include "mtdutils/mtdutils.h"
#include "bmlutils/bmlutils.h"

#include "settings.h"
#include "settingshandler.h"
#include "settingshandler_lang.h"

int erase_volume(const char *volume) {
    ui_set_background(BACKGROUND_ICON_INSTALLING);
    ui_show_indeterminate_progress();
    ui_print("%s %s...\n", edifyformatting, volume);

    if (strcmp(volume, "/cache") == 0) {
        // Any part of the log we'd copied to cache is now gone.
        // Reset the pointer so we copy from the beginning of the temp
        // log.
        tmplog_offset = 0;
    }
    // Finally run format_volume from within roots
    return format_volume(volume);
}

void wipe_data(int confirm) {
    if (confirm) {
        static char** title_headers = NULL;

        if (title_headers == NULL) {
            char* headers[] = { "Confirm wipe of all user data?",
                                "  THIS CAN NOT BE UNDONE.",
                                "",
                                NULL };
            title_headers = prepend_title((const char**)headers);
        }

        char* items[] = { " No",
#if TARGET_BOOTLOADER_BOARD_NAME == otter
                          " Yes -- delete all user data",   // [1]
#else
                          " No",
                          " No",
                          " No",
                          " No",
                          " No",
                          " No",
                          " Yes -- delete all user data",   // [7]
                          " No",
                          " No",
                          " No",
#endif
                          NULL };

        int chosen_item = get_menu_selection(title_headers, items, 1, 0);
#if TARGET_BOOTLOADER_BOARD_NAME == otter
        if (chosen_item != 1) {
#else
        if (chosen_itme != 7) {
#endif
            return;
        }
    }

    ui_print("\n-- Wiping data...\n");
    device_wipe_data();
    erase_volume("/data");
    erase_volume("/cache");
    if (has_datadata()) {
        erase_volume("/datadata");
    }
#if TARGET_BOOTLOADER_BOARD_NAME != otter	// ToDo: make this check for the partition rather then the device
    erase_volume("/sd-ext");
#endif
    erase_volume("/sdcard/.android_secure");
    ui_print("%s\n", datawipecomplete);
}

void erase_cache(int orscallback) {
    if(orscallback) {
	if(orswipeprompt || !confirm_selection("Confirm wipe?","Yes - Wipe Cache")) {
	    ui_print("Skipping cache wipe...\n");
	    return;
	}
    } else if (!confirm_selection("Confirm wipe?", "Yes - Wipe Cache")) {
	return;
    }
    ui_print("\n-- Wiping cache...\n");
    erase_volume("/cache");
    ui_print("%s\n", cachewipecomplete);
    if (!ui_text_visible()) return;
}

void erase_dalvik_cache(int orscallback) {
    if(orscallback) {
	if(orswipeprompt || !confirm_selection("Confirm wipe?", "Yes - Wipe Dalvik Cache")) {
	    ui_print("Skipping dalvik cache wipe...\n");
	    return;
	}
    } else if (!confirm_selection( "Confirm wipe?", "Yes - Wipe Dalvik Cache")) {
	return;
    }
    if (0 != ensure_path_mounted("/data")) {
	return;
    }
    ensure_path_mounted("/sd-ext");
    ensure_path_mounted("/cache");
    
    __system("rm -r /data/dalvik-cache");
    __system("rm -r /cache/dalvik-cache");
    __system("rm -r /sd-ext/dalvik-cache");
    ui_print("Dalvik Cache wiped.\n");
    
    ensure_path_unmounted("/data");
}

void wipe_all(int orscallback) {
    if(orscallback) {
	if(orswipeprompt || !confirm_selection("Confirm wipe all?", "Yes - Wipe All")) {
	    ui_print("Skipping full wipe...\n");
	    return;
	}
    } else if (!confirm_selection("Confirm wipe all?", "Yes - Wipe All")) {
	return;
    }
    ui_print("\n-- Wiping system, data, cache...\n");
    erase_volume("/system");
    erase_volume("/data");
    erase_volume("/cache");
    ui_print("\nFull wipe complete!\n");
    if (!ui_text_visible()) return;
}

/* This isn't present in newer CWM versions, are we using it or is it deprecated
 * by one of the following. */
int format_non_mtd_device(const char* root)
{
    // if this is SDEXT:, don't worry about it.
    if (0 == strcmp(root, "SDEXT:"))
    {
        struct stat st;
        if (0 != stat("/dev/block/mmcblk0p2", &st))
        {
            ui_print("%s\n", a2sdnotfound);
            return 0;
        }
    }

    char path[PATH_MAX];
    translate_root_path(root, path, PATH_MAX);
    if (0 != ensure_root_path_mounted(root))
    {
        ui_print("%s %s!\n", mounterror, path);
        ui_print("%s\n", skipformat);
        return 0;
    }

    static char tmp[PATH_MAX];
    sprintf(tmp, "rm -rf %s/*", path);
    __system(tmp);
    sprintf(tmp, "rm -rf %s/.*", path);
    __system(tmp);

    ensure_root_path_unmounted(root);
    return 0;
}

int format_device(const char *device, const char *path, const char *fs_type) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        // no /sdcard? let's assume /data/media
        if (strstr(path, "/sdcard") == path && is_data_media()) {
            return format_unknown_device(NULL, path, NULL);
        }
        // silent failure for sd-ext
        if (strcmp(path, "/sd-ext") == 0)
            return -1;
        LOGE("unknown volume \"%s\"\n", path);
        return -1;
    }
    if (strcmp(fs_type, "ramdisk") == 0) {
        // you can't format the ramdisk.
        LOGE("can't format_volume \"%s\"", path);
        return -1;
    }

    if (strcmp(fs_type, "rfs") == 0) {
        if (ensure_path_unmounted(path) != 0) {
            LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
            return -1;
        }
        if (0 != format_rfs_device(device, path)) {
            LOGE("format_volume: format_rfs_device failed on %s\n", device);
            return -1;
        }
        return 0;
    }

    if (strcmp(v->mount_point, path) != 0) {
        return format_unknown_device(v->device, path, NULL);
    }

    if (ensure_path_unmounted(path) != 0) {
        LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
        return -1;
    }

    if (strcmp(fs_type, "yaffs2") == 0 || strcmp(fs_type, "mtd") == 0) {
        mtd_scan_partitions();
        const MtdPartition* partition = mtd_find_partition_by_name(device);
        if (partition == NULL) {
            LOGE("format_volume: no MTD partition \"%s\"\n", device);
            return -1;
        }

        MtdWriteContext *write = mtd_write_partition(partition);
        if (write == NULL) {
            LOGW("format_volume: can't open MTD \"%s\"\n", device);
            return -1;
        } else if (mtd_erase_blocks(write, -1) == (off_t) -1) {
            LOGW("format_volume: can't erase MTD \"%s\"\n", device);
            mtd_write_close(write);
            return -1;
        } else if (mtd_write_close(write)) {
            LOGW("format_volume: can't close MTD \"%s\"\n",device);
            return -1;
        }
        return 0;
    }

    if (strcmp(fs_type, "ext4") == 0) {
        reset_ext4fs_info();
        int result = make_ext4fs(device, NULL, NULL, 0, 0, 0);
        if (result != 0) {
            LOGE("format_volume: make_extf4fs failed on %s\n", device);
            return -1;
        }
        return 0;
    }

    return format_unknown_device(device, path, fs_type);
}

int format_unknown_device(const char *device, const char* path, const char *fs_type)
{
    LOGI("Formatting unknown device.\n");

    if (fs_type != NULL && get_flash_type(fs_type) != UNSUPPORTED)
        return erase_raw_partition(device);

    // if this is SDEXT:, don't worry about it if it does not exist.
    if (0 == strcmp(path, "/sd-ext"))
    {
        struct stat st;
        Volume *vol = volume_for_path("/sd-ext");
        if (vol == NULL || 0 != stat(vol->device, &st))
        {
            ui_print("No app2sd partition found. Skipping format of /sd-ext.\n");
            return 0;
        }
    }

    if (NULL != fs_type) {
        if (strcmp("ext3", fs_type) == 0) {
            LOGI("Formatting ext3 device.\n");
            if (0 != ensure_path_unmounted(path)) {
                LOGE("Error while unmounting %s.\n", path);
                return -12;
            }
            return format_ext3_device(device);
        }

        if (strcmp("ext2", fs_type) == 0) {
            LOGI("Formatting ext2 device.\n");
            if (0 != ensure_path_unmounted(path)) {
                LOGE("Error while unmounting %s.\n", path);
                return -12;
            }
            return format_ext2_device(device);
        }
    }

    if (0 != ensure_path_mounted(path))
    {
        ui_print("%s %s!\n", mounterror, path);
        ui_print("%s\n", skipformat);
        return 0;
    }

    static char tmp[PATH_MAX];
    if (strcmp(path, "/data") == 0) {
        sprintf(tmp, "cd /data ; for f in $(ls -a | grep -v ^media$); do rm -rf $f; done");
        __system(tmp);
    }
    else {
        sprintf(tmp, "rm -rf %s/*", path);
        __system(tmp);
        sprintf(tmp, "rm -rf %s/.*", path);
        __system(tmp);
    }

    ensure_path_unmounted(path);
    return 0;
}

#define MKE2FS_BIN      "/sbin/mke2fs"
#define TUNE2FS_BIN     "/sbin/tune2fs"
#define E2FSCK_BIN      "/sbin/e2fsck"

typedef struct {
    char mount[255];
    char unmount[255];
    Volume* v;
} MountMenuEntry;

typedef struct {
    char txt[255];
    Volume* v;
} FormatMenuEntry;

int is_safe_to_format(char* name)
{
    char str[255];
    char* partition;
    property_get("ro.cwm.forbid_format", str, "/misc,/radio,/bootloader,/recovery,/efs");

    partition = strtok(str, ", ");
    while (partition != NULL) {
        if (strcmp(name, partition) == 0) {
            return 0;
        }
        partition = strtok(NULL, ", ");
    }

    return 1;
}

#define MOUNTABLE_COUNT 5
#define MTD_COUNT 4
#define MMC_COUNT 2

void show_partition_menu()
{
    static char* headers[3];
	headers[0] = showpartitionheader;
	headers[1] = "\n";
	headers[2] = NULL;

    typedef char* string;
    string mounts[MOUNTABLE_COUNT][3] = {
        { "Mount /system", "Unmount /system", "SYSTEM:" },
        { "Mount /data", "Unmount /data", "DATA:" },
        { "Mount /cache", "Unmount /cache", "CACHE:" },
        { "Mount /sdcard", "Unmount /sdcard", "SDCARD:" },
        { "Mount /sd-ext", "Unmount /sd-ext", "SDEXT:" }
        };
	

    string mtds[MTD_COUNT][2] = {
        { "Erase boot", "BOOT:" },
        { "Erase system", "SYSTEM:" },
        { "Erase data", "DATA:" },
        { "Erase cache", "CACHE:" },
    };

    string mmcs[MMC_COUNT][3] = {
      { "Erase sdcard", "SDCARD:" },
      { "Erase sd-ext", "SDEXT:" }
    };

    static char* confirm_format  = "Confirm format?";
    static char* confirm = "Yes - Format";

    for (;;)
    {
        int ismounted[MOUNTABLE_COUNT];
        int i;
        static string options[MOUNTABLE_COUNT + MTD_COUNT + MMC_COUNT + 1 + 1 + 1 + 1]; // mountables, format mtds, format mmcs, wipe dalvik, usb storage, sd part, null
        for (i = 0; i < MOUNTABLE_COUNT; i++)
        {
            ismounted[i] = is_root_path_mounted(mounts[i][2]);
            options[i] = ismounted[i] ? mounts[i][1] : mounts[i][0];
        }

        for (i = 0; i < MTD_COUNT; i++)
        {
            options[MOUNTABLE_COUNT + i] = mtds[i][0];
        }

        for (i = 0; i < MMC_COUNT; i++)
        {
            options[MOUNTABLE_COUNT + MTD_COUNT + i] = mmcs[i][0];
        }

        options[MOUNTABLE_COUNT + MTD_COUNT + MMC_COUNT] = "Erase dalvik-cache";
		options[MOUNTABLE_COUNT + MTD_COUNT + MMC_COUNT + 1] = usbmsmount;
		options[MOUNTABLE_COUNT + MTD_COUNT + MMC_COUNT + 1 + 1] = "Partition SD Card";
        options[MOUNTABLE_COUNT + MTD_COUNT + MMC_COUNT + 1 + 1 + 1] = NULL;

        int chosen_item = get_menu_selection(headers, &options, 0, 0);
        if (chosen_item == GO_BACK)
            break;
		if (chosen_item == MOUNTABLE_COUNT + MTD_COUNT + MMC_COUNT)
		{
			erase_dalvik_cache(0);
		}
        else if (chosen_item == MOUNTABLE_COUNT + MTD_COUNT + MMC_COUNT + 1)
        {
            show_mount_usb_storage_menu();
        }
		else if (chosen_item == MOUNTABLE_COUNT + MTD_COUNT + MMC_COUNT + 1 + 1)
        {
                static char* ext_sizes[] = { "128M",
                                             "256M",
                                             "512M",
                                             "1024M",
                                             "2048M",
                                             "4096M",
                                             NULL };

                static char* swap_sizes[] = { "0M",
                                              "32M",
                                              "64M",
                                              "128M",
                                              "256M",
                                              NULL };

                static char* ext_headers[] = { "Ext Size", "", NULL };
                static char* swap_headers[] = { "Swap Size", "", NULL };

                int ext_size = get_menu_selection(ext_headers, ext_sizes, 0, 0);
                if (ext_size == GO_BACK)
                    continue;

                int swap_size = get_menu_selection(swap_headers, swap_sizes, 0, 0);
                if (swap_size == GO_BACK)
                    continue;

                char sddevice[256];
                Volume *vol = volume_for_path("/sdcard");
                strcpy(sddevice, vol->device);
                // we only want the mmcblk, not the partition
                sddevice[strlen("/dev/block/mmcblkX")] = NULL;
                char cmd[PATH_MAX];
                setenv("SDPATH", sddevice, 1);
                sprintf(cmd, "sdparted -es %s -ss %s -efs ext3 -s", ext_sizes[ext_size], swap_sizes[swap_size]);
                ui_print("Partitioning SD Card... please wait...\n");
                if (0 == __system(cmd))
                    ui_print("Done!\n");
                else
                    ui_print("An error occured while partitioning your SD Card. Please see /tmp/recovery.log for more details.\n");
                break;
        }
        else if (chosen_item < MOUNTABLE_COUNT)
        {
            if (ismounted[chosen_item])
            {
                if (0 != ensure_root_path_unmounted(mounts[chosen_item][2]))
                    ui_print("%s %s!\n", unmounterror, mounts[chosen_item][2]);
            }
            else
            {
                if (0 != ensure_root_path_mounted(mounts[chosen_item][2]))
                    ui_print("%s %s!\n", mounterror, mounts[chosen_item][2]);
            }
        }
        else if (chosen_item < MOUNTABLE_COUNT + MTD_COUNT)
        {
            chosen_item = chosen_item - MOUNTABLE_COUNT;
            if (!confirm_selection(confirmformat, confirm))
                continue;
            ui_print("%s %s...\n", formatting, mtds[chosen_item][1]);
            if (0 != format_root_device(mtds[chosen_item][1]))
                ui_print("%s %s!\n", formaterror, mtds[chosen_item][1]);
            else
                ui_print("%s\n", done);
        }
        else if (chosen_item < MOUNTABLE_COUNT + MTD_COUNT + MMC_COUNT)
        {
            chosen_item = chosen_item - MOUNTABLE_COUNT - MTD_COUNT;
            if (!confirm_selection(confirmformat, confirm))
                continue;
            ui_print("%s %s...\n", formatting, mmcs[chosen_item][1]);
            if (0 != format_non_mtd_device(mmcs[chosen_item][1]))
                ui_print("%s %s!\n", formaterror, mmcs[chosen_item][1]);
            else
                ui_print("%s\n", done);
        }
    }
}
