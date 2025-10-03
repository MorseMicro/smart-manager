# Copyright 2023 Morse Micro
# SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial

import os
import glob
import multiprocessing
import importlib

# We set the number of parallel jobs to 1.5 times the number of cores
# in attempt to ensure we keep all these cores busy and get maximum
# performance for the build. The --jobs or -j command line option will
# override this.
SetOption("num_jobs", int(1.5 * multiprocessing.cpu_count()))


def RecursiveGlob(search, pattern):
    result = []

    # We want to search in the given search directory, but return results
    # relative to the current SCons root, which is whereever Dir('.') happens
    # to be at the time. This allows variant dir and related functionality to
    # still work.
    relative = str(Dir('.').srcnode())
    search = str(Dir(search).srcnode())

    for root, dirs, files in os.walk(search):
        found = glob.glob(root + '/' + pattern)
        found = list(map(lambda x: os.path.relpath(x, relative), found))
        result += found

    return result
