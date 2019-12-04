#!/usr/bin/env python3

# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the license found in the
# LICENSE file in the root directory of this source tree.

import torch  # make sure to dynamically load everything beforee loading hanabi_lib
import argparse
# torch.ops.load_library("hanabi_lib.so")
import hanabi_lib

"""
This is a very thing wrapper that just runs a C++ executable,
which runs a bunch of simulations on a set of bots, and returns
statistics for them.

It's wrapped in python just to have a single build process using
setup.py.
"""

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='deepregret')
    # data
    parser.add_argument('botname', default="SmartBot")
    parser.add_argument('--players', type=int, default=2)
    parser.add_argument('--games', type=int, default=1000)
    parser.add_argument('--log_every', type=int, default=100)
    parser.add_argument('--seed', type=int, default=-1,
                        help="-1 means to pick a random seed")

    opt = parser.parse_args()
    hanabi_lib.eval_bot(
        opt.botname,
        players=opt.players,
        games=opt.games,
        log_every=opt.log_every,
        seed=opt.seed
    )
