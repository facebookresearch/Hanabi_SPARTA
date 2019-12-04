#  Copyright (c) Facebook, Inc. and its affiliates.
#  All rights reserved.
#
#  This source code is licensed under the license found in the
#  LICENSE file in the root directory of this source tree.

import torch  # make sure to dynamically load everything beforee loading hanabi_lib
import time
# torch.ops.load_library("hanabi_lib.so")
from hanabi_lib import *

def wait_for_my_turn(server, bot):
    while not server.activePlayer() == server.whoAmI():
        bot.wait()

def run():
    print("Starting game 1...")
    server, bot, thread = start_game("SearchBot", 42)
    print(f"Ending game")
    tic = time.time()
    end_game(server, bot, thread)

    print("Starting game 2...")
    server, bot, thread = start_game("SearchBot", 42)
    print(f"Ending game")
    tic = time.time()
    end_game(server, bot, thread)
    print(f"Exiting {time.time()-tic}")


if __name__ == "__main__":
    run()
