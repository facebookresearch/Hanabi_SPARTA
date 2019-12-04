#!/usr/bin/env bash

# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the license found in the
# LICENSE file in the root directory of this source tree.

mkdir -p models
echo "downloading models"
wget -P models https://dl.fbaipublicfiles.com/hanabi_sparta/pytorch1.2/sad_player2.pth
wget -P models https://dl.fbaipublicfiles.com/hanabi_sparta/pytorch1.2/sad_player3.pth
wget -P models https://dl.fbaipublicfiles.com/hanabi_sparta/pytorch1.2/sad_player4.pth
wget -P models https://dl.fbaipublicfiles.com/hanabi_sparta/pytorch1.2/sad_player5.pth
echo "done"
