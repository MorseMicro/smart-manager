# Copyright 2023 Morse Micro
# SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial

from SCons.Script import *

def generate(env, **kwargs):

    # This is the top-level configuration for the debug logging subsystem
    # ("log"), but it *IS NOT* the one you should modify if you want to
    # enable some logging. Instead you should create yourself a
    # config/local/log.sc file (perhaps from the example/template
    # config/local/log.sc.example) and apply your settings there. That
    # file will be pulled in below.

    # Default to disabling file-specific log levels
    env["LOG_LEVELS"] = {}

    # Default global log options (which only apply if any logging is
    # enabled)
    env["LOG_PRINT_LEVEL"] = True
    env["LOG_PRINT_TIME"] = True
    env["LOG_PRINT_FILENAME"] = True

    # Default to LOG_LEVEL_INFO, unless overridden on command line or by
    # our local log configuration
    env["LOG_LEVEL_DEFAULT"] = "LOG_LEVEL_INFO"


    # Get any command-line log level override
    AddOption("--log-level",
            help=("Force log level (options: NONE, ERROR, "
                    "WARN, INFO (default), DEBUG, VERBOSE)"))
    if GetOption("log_level"):
        env["LOG_LEVEL_DEFAULT"] = "LOG_LEVEL_" + GetOption("log_level").upper()

    # Create the flags to convey the logging config into the build
    env.Append(CCFLAGS=[
        # Source file name
        "-DLOG_FILENAME=\\\"${SOURCE.file}\\\"",
        # Source file log level
        "-DLOG_LEVEL=${__env__['LOG_LEVEL_DEFAULT']}",
    ])

    if env["LOG_PRINT_LEVEL"]:
        env.Append(CCFLAGS=["-DLOG_PRINT_LEVEL"])

    if env["LOG_PRINT_TIME"]:
        env.Append(CCFLAGS=["-DLOG_PRINT_TIME"])

    if env["LOG_PRINT_FILENAME"]:
        env.Append(CCFLAGS=["-DLOG_PRINT_FILENAME"])
    return env


def exists(env):
    return True
