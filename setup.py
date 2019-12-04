#!/usr/bin/env python3

# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the license found in the
# LICENSE file in the root directory of this source tree.

from setuptools import setup
from torch.utils.cpp_extension import BuildExtension, CppExtension
import sys
import os

OPTIONAL_SRC = []
if int(os.environ.get("INSTALL_TORCHBOT", 0)):
    OPTIONAL_SRC = ["csrc/TorchBot.cc"]

boost_libs = ["boost_fiber", "boost_thread", "boost_context"]
if sys.platform == "darwin":
    boost_libs = [lib + '-mt' for lib in boost_libs]

setup(
    name='hanabi_lib',
    ext_modules=[
        CppExtension('hanabi_lib', [
            "csrc/extension.cc",
            "csrc/SimpleBot.cc",
            "csrc/HolmesBot.cc",
            "csrc/SmartBot.cc",
            "csrc/SearchBot.cc",
            "csrc/JointSearchBot.cc",
            "csrc/HanabiServer.cc",
            "csrc/BotUtils.cc",
        ] + OPTIONAL_SRC,
        extra_compile_args=['-fPIC', '-std=c++1y', '-Wno-deprecated', '-O3', '-Wno-sign-compare', '-D_GLIBCXX_USE_CXX11_ABI=0', '-DCARD_ID=1'],
        libraries = ['z'] + boost_libs,
        library_dirs=['/usr/local/lib'],
        include_dirs=['csrc'],
        undef_macros=['NDEBUG'])
    ],
    cmdclass={"build_ext": BuildExtension},
    )
