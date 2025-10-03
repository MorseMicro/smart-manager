# Copyright 2023 Morse Micro
# SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial

import os
import glob

# Use a command-line option to allow override of the build directory
AddOption("--build-dir",
          help="Set the build output directory (defaults to build)",
          default="build")

SConscript('src/SConscript', variant_dir=GetOption('build_dir'), duplicate=0)

# Persuade scons to remove the entire build dir on clean
Clean('.', GetOption('build_dir'))
