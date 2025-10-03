# Copyright 2023 Morse Micro
# SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial

import subprocess
import os
from SCons.Script import *

def update_targets(target, source, env):
    split = os.path.splitext(str(target[0]))
    target = [
        File(split[0] + '-stderr' + split[1]),
        File(split[0] + '-stdout' + split[1]),
    ]
    return target, source


def stylecheck(source, target, env):
    cmd = ["cpplint.py"]

    for filename in source:
        if not filename.get_abspath().endswith('.c'):
            continue
        cmd += [str(filename)]

    print(' '.join(cmd))
    proc = subprocess.run(cmd, capture_output=True, text=True)
    print(proc.stdout)
    print(proc.stderr)

    with open(str(target[0]), 'w') as f:
        f.write(proc.stderr)

    with open(str(target[1]), 'w') as f:
        f.write(proc.stdout)

    return proc.returncode


def generate(env, **kwargs):
    env.Append(BUILDERS={
        'Style': Builder(action=stylecheck,
                         emitter=update_targets)
    })


def exists(env):
    return True
