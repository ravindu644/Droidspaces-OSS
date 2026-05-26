/*
 * Droidspaces v6 — Root-mode dispatch runner
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Minimal dispatch binary for root-mode container operations.
 * Called via `su -c droidspaces-runner <cmd> <args>`.
 *
 * Uses dlopen at runtime to load libdroidspaces.so from
 * /data/local/Droidspaces/lib/. No link-time dependency on
 * the shared library — the runner can be compiled standalone.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

/* Type signature of ds_runner_main */
typedef int (*ds_runner_main_t)(int argc, char **argv, const char *workspace);

/* Well-known path where BinaryInstaller places libdroidspaces.so */
#define DEFAULT_LIB_PATH  "/data/local/Droidspaces/lib/libdroidspaces.so"
#define FALLBACK_LIB_PATH "libdroidspaces.so"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: droidspaces-runner <start|stop|restart|pid|usage|enter|version> [args...]\n");
        return 1;
    }

    /* Load the shared library */
    void *handle = dlopen(DEFAULT_LIB_PATH, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        handle = dlopen(FALLBACK_LIB_PATH, RTLD_NOW | RTLD_GLOBAL);
    }
    if (!handle) {
        fprintf(stderr, "ERROR: cannot load libdroidspaces.so: %s\n", dlerror());
        return 1;
    }

    /* Resolve the dispatch entry point */
    ds_runner_main_t ds_runner_main = (ds_runner_main_t)dlsym(handle, "ds_runner_main");
    if (!ds_runner_main) {
        fprintf(stderr, "ERROR: ds_runner_main not found in libdroidspaces.so: %s\n", dlerror());
        dlclose(handle);
        return 1;
    }

    const char *workspace = getenv("DS_WORKSPACE");
    if (!workspace) workspace = "/data/local/Droidspaces";

    int ret = ds_runner_main(argc, argv, workspace);
    dlclose(handle);
    return ret;
}
