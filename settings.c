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
#include <sys/reboot.h>
#include <reboot/reboot.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/limits.h>
#include <dirent.h>
#include <sys/stat.h>

#include <signal.h>
#include <sys/wait.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "firmware.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
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
#include "colorific.h"

#include "settings.h"
#include "settingshandler.h"

void show_settings_menu() {
    static char* headers[] = { "COT Settings",
                                "",
                                NULL
    };

    static char* list[] = { "Theme",
                            "ORS Forced Reboots",
                            "ORS Nandroid Prompt",
                            "ZIP flash Nandroid Prompt",
                            NULL
    };

    for (;;) {
        int chosen_item = get_menu_selection(headers, list, 0, 0);
        switch (chosen_item) {
            case GO_BACK:
                return;
            case 0:
            {
                static char* ui_colors[] = {"Hydro (default)",
                                                    "Blood Red",
                                                    "Key Lime Pie",
                                                    "Citrus Orange",
                                                    "Dooderbutt Blue",
                                                    NULL
                };
                static char* ui_header[] = {"COT Theme", "", NULL};

                int ui_color = get_menu_selection(ui_header, ui_colors, 0, 0);
                if(ui_color == GO_BACK)
                    continue;
                else {
                    set_ui_color(ui_color);
                    ui_reset_icons();
                    break;
                }
            }
            case 1:
                //show_ors_reboot_menu();
                create_default_settings();
                break;
            case 2:
                //show_ors_nandroid_prompt_menu();
                parse_settings();
                break;
            case 3:
                //show_nandroid_prompt_menu();
                break;
            default:
                return;
        }
    }
}
