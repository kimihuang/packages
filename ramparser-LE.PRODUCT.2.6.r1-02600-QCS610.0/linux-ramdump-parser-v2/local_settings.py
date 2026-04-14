'''
Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
'''

import os
import sys
import configparser

path = os.path.abspath(os.path.dirname(__file__))

# Loading local_setting.config
config = configparser.ConfigParser()
config.read(os.path.join(path, "local_setting.config"))

# Getting the tool path
gdb_path = config.get('Paths', 'gdb_path')
nm_path = config.get('Paths', 'nm_path')
objdump_path = config.get('Paths', 'objdump_path')
gdb64_path = config.get('Paths', 'gdb64_path')
nm64_path = config.get('Paths', 'nm64_path')
objdump64_path = config.get('Paths', 'objdump64_path')
