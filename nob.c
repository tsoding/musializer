#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#define NOB_IMPLEMENTATION
#include "./nob.h"
#include "./src_build/configurer.c"

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *program = nob_shift_args(&argc, &argv);

    const char *old_build_conf_path = "./build/build.conf";
    int build_conf_exists = nob_file_exists(old_build_conf_path);
    if (build_conf_exists < 0) return 1;
    if (build_conf_exists) {
        // @backcomp
        nob_log(NOB_ERROR, "We found %s. That means your build folder has an old schema.", old_build_conf_path);
        nob_log(NOB_ERROR, "Instead of %s you are suppose to use %s to configure the build now.", old_build_conf_path, CONFIG_PATH);
        nob_log(NOB_ERROR, "Remove your ./build/ folder and run %s again to regenerate the folder with the new schema.", program);
        return 1;
    }

    nob_log(NOB_INFO, "--- STAGE 1 ---");

    if (!nob_mkdir_if_not_exists("build")) return 1;

    int config_exists = nob_file_exists(CONFIG_PATH);
    if (config_exists < 0) return 1;
    if (config_exists == 0) {
        if (!generate_default_config(CONFIG_PATH)) return 1;
    } else {
        nob_log(NOB_INFO, "file `%s` already exists", CONFIG_PATH);
    }

    if (!generate_config_logger("build/config_logger.c")) return 1;

    Nob_Cmd cmd = {0};
    const char *stage2_binary = "build/nob_stage2";
    nob_cmd_append(&cmd, NOB_REBUILD_URSELF(stage2_binary, "./src_build/nob_stage2.c"));
    if (!nob_cmd_run_sync(cmd)) return 1;

    cmd.count = 0;
    nob_cmd_append(&cmd, stage2_binary);
    nob_da_append_many(&cmd, argv, argc);
    if (!nob_cmd_run_sync(cmd)) return 1;

    return 0;
}
