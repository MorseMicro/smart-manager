/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <stdio.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>
#include <netlink/genl/genl.h>
#include <linux/nl80211.h>
#include <libconfig.h>

#include "smart_manager.h"
#include "datalog.h"
#include "backend/backend.h"
#include "utils.h"
#include "logging.h"
#include "backend/morsectrl/command.h"

#ifndef MORSE_VERSION
#define MORSE_VERSION "Undefined"
#endif

pthread_mutex_t halt_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t halt_condition = PTHREAD_COND_INITIALIZER;

/* Structure to hold the module handle and function pointers */
typedef struct
{
    const char *module_name;
    void *handle;
    void *context;
    void *(*create_func)(const config_t *);
    void (*destroy_func)(void *);
    const char *(*get_version_func)(void);
} module_info_t;

/* Load a module and find its create/destroy functions */
int load_module(const char *libname, const char *module_name, module_info_t *module)
{
    const char *error;
    char *create_func_name = NULL, *destroy_func_name = NULL, *get_version_func_name = NULL;
    int create_func_len, destroy_func_len, get_version_func_len;
    int ret = 0;

    /* Open the shared library */
    module->handle = dlopen(libname, RTLD_LAZY | RTLD_GLOBAL);
    if (!module->handle)
    {
        LOG_VERBOSE("Error: %s\n", dlerror());
        return 1;
    }

    module->module_name = module_name;

    /* Get the required length for the function names (including the null terminator) */
    create_func_len = snprintf(NULL, 0, "%s_create", module_name) + 1;
    destroy_func_len = snprintf(NULL, 0, "%s_destroy", module_name) + 1;
    get_version_func_len = snprintf(NULL, 0, "%s_get_version", module_name) + 1;

    create_func_name = malloc(create_func_len);
    destroy_func_name = malloc(destroy_func_len);
    get_version_func_name = malloc(get_version_func_len);

    if (!create_func_name || !destroy_func_name || !get_version_func_name)
    {
        LOG_ERROR("Memory allocation error for function names\n");
        ret = 1;
        goto cleanup;
    }

    /* Construct the function names */
    snprintf(create_func_name, create_func_len, "%s_create", module_name);
    snprintf(destroy_func_name, destroy_func_len, "%s_destroy", module_name);
    snprintf(get_version_func_name, get_version_func_len, "%s_get_version", module_name);

    /* Clear any existing error */
    dlerror();

    /* Find the _create function */
    module->create_func = (void *(*)(const config_t *))dlsym(module->handle, create_func_name);
    error = dlerror();
    if (error)
    {
        LOG_ERROR("Error loading function %s: %s\n", create_func_name, error);
        ret = 1;
        goto cleanup;
    }

    /* Find the _destroy function */
    module->destroy_func = (void (*)(void *))dlsym(module->handle, destroy_func_name);
    error = dlerror();
    if (error)
    {
        LOG_ERROR("Error loading function %s: %s\n", destroy_func_name, error);
        ret = 1;
        goto cleanup;
    }

    /* Find the _get_version function */
    module->get_version_func = (const char *(*)(void))dlsym(module->handle, get_version_func_name);
    error = dlerror();
    if (error)
    {
        LOG_ERROR("Error loading function %s: %s\n", get_version_func_name, error);
        ret = 1;
        goto cleanup;
    }

cleanup:
    if (ret)
    {
        dlclose(module->handle);
    }
    free(create_func_name);
    free(destroy_func_name);
    free(get_version_func_name);

    return ret;
}

/* Unload a module */
void unload_module(module_info_t *module)
{
    if (module->handle)
    {
        if (module->destroy_func)
        {
            LOG_VERBOSE("Calling %s_destroy...\n", module->module_name);
            module->destroy_func(module->context); /* Call destroy function */
            module->context = NULL;
        }
        dlclose(module->handle);
        module->handle = NULL;
    }
}

#define MMEXT_STR "%s/%s.mmext"

/* Helper function to create the full library path and load the module */
int load_module_from_directory(const char *dir, const char *module_name, const config_t *cfg,
                               module_info_t *module)
{
    char *libname;
    int libname_len;

    /* Construct the full path of the library (e.g., <dir>/<module_name>.mmext) */
    libname_len = snprintf(NULL, 0, MMEXT_STR, dir, module_name) + 1;
    libname = malloc(libname_len);
    if (!libname)
    {
        LOG_ERROR("Memory allocation error for libname\n");
        return 1;
    }

    snprintf(libname, libname_len, MMEXT_STR, dir, module_name);

    /* Try loading the module */
    if (load_module(libname, module_name, module) == 0)
    {
        LOG_DEBUG("Loaded module: %s from library: %s. Version: %s\n", module_name, libname,
                  module->get_version_func());
        module->context = module->create_func(cfg);
        free(libname);
        return 0;
    }
    else
    {
        LOG_VERBOSE("Failed to load module %s from library %s\n", module_name, libname);
        free(libname);
        return 1;
    }
}

/* Load module list from config file */
module_info_t *load_modules_from_config(const config_t *cfg, int *num_modules)
{
    config_setting_t *setting;
    module_info_t *modules = NULL;
    config_setting_t *module_dirs = NULL;
    char *cwd = NULL;
    const char *module_name;
    int dir_count = 0;
    int found_module = 0;
    int count = 0;

    /* Get the 'module_dirs' section if available (it could be an array) */
    module_dirs = config_lookup(cfg, "module_dirs");

    /* Get the 'modules' section from the config file */
    setting = config_lookup(cfg, "modules");
    if (setting == NULL)
    {
        LOG_ERROR("Error: 'modules' section not found in config file\n");
        return NULL;
    }

    count = config_setting_length(setting);

    /* Dynamically allocate the array of modules */
    modules = (module_info_t *)malloc(count * sizeof(module_info_t));
    if (!modules)
    {
        LOG_ERROR("Error: memory allocation failed\n");
        return NULL;
    }

    for (int i = 0; i < count; i++)
    {
        module_name = config_setting_get_string_elem(setting, i);
        found_module = 0;

        /* Try to find the module in the module_dirs if provided */
        if (module_dirs != NULL && config_setting_is_array(module_dirs))
        {
            dir_count = config_setting_length(module_dirs);
            for (int j = 0; j < dir_count; j++)
            {
                const char *dir = config_setting_get_string_elem(module_dirs, j);
                if (dir == NULL)
                {
                    LOG_ERROR("Invalid directory in module_dirs\n");
                    continue;
                }

                LOG_VERBOSE("Trying to find module %s in directory: %s\n", module_name, dir);

                /* Try to load the module from this directory */
                if (load_module_from_directory(dir, module_name, cfg, &modules[*num_modules]) == 0)
                {
                    (*num_modules)++;
                    found_module = 1;
                    break;
                }
            }
        }

        /* If not found in any provided directory, try the current working directory */
        if (!found_module)
        {
            /* Allocate memory for the current working directory dynamically */
            cwd = getcwd(NULL, 0);

            if (cwd != NULL)
            {
                LOG_VERBOSE("Trying to find module %s in current working directory: %s\n",
                            module_name, cwd);

                /* Try to load the module from the current working directory */
                if (load_module_from_directory(cwd, module_name, cfg, &modules[*num_modules]) == 0)
                {
                    (*num_modules)++;
                }

                free(cwd);
            }
            else
            {
                LOG_ERROR("Error: could not determine current working directory\n");
            }
        }
    }

    return modules;
}

/**
 * @brief Halt the smart manager by unblocking the root thread, and allowing it to return from main.
 *
 * Call this function in any modules to terminate SM.
 */
void mmsm_halt(void)
{
    LOG_WARN("Halting smartmanager\n");
    MMSM_ASSERT(pthread_mutex_lock(&halt_mutex) == 0);
    pthread_cond_signal(&halt_condition);
    MMSM_ASSERT(pthread_mutex_unlock(&halt_mutex) == 0);
}

int main(int argc, char **argv)
{
    config_t config;
    int num_modules = 0;
    module_info_t *modules = NULL;

    if (argc < 2)
    {
        LOG_ERROR("Usage: smart_manager { <config file> | -v }\n");
        return 1;
    }

    if (strcmp(argv[1], "-v") == 0)
    {
        printf("%s\n", MORSE_VERSION);
        return 0;
    }

    config_init(&config);

    if (!config_read_file(&config, argv[1]))
    {
        LOG_ERROR("Error in reading config file %s\n", config_error_file(&config));
        LOG_ERROR("Failed at line %d: %s\n", config_error_line(&config),
                  config_error_text(&config));
        return 1;
    }

    LOG_INFO_ALWAYS("Smart Manager starting... (config file: %s)\n", argv[1]);

    mmsm_set_log_config(config_lookup(&config, "logging"));

    mmsm_init();

    datalog_set_config_settings(config_lookup(&config, "datalog"));

    /* Load modules from the config file */
    modules = load_modules_from_config(&config, &num_modules);
    if (!modules)
    {
        LOG_ERROR("Error loading modules from config file\n");
        return 1;
    }

    LOG_INFO_ALWAYS("Starting monitors\n");
    mmsm_start();

    /* Suspend the main thread until a child thread signals that SM should halt.
     * In normal applications this should not happen, and modules should be self sufficent / error
     * tolerant
     */

    MMSM_ASSERT(pthread_mutex_lock(&halt_mutex) == 0);
    pthread_cond_wait(&halt_condition, &halt_mutex);
    MMSM_ASSERT(pthread_mutex_unlock(&halt_mutex) == 0);

    for (int i = 0; i < num_modules; i++)
    {
        unload_module(&modules[i]);
    }

    free(modules);

    config_destroy(&config);

    return 0;
}
