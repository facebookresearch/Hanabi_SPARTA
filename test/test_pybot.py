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
    server, bot, thread = start_game("TorchBot", 42)
    print(f"Botname: {get_botname()}")
    me = server.whoAmI()
    partner = 1 - me
    print(f"Observation 1: {bot.obs()} Indices: {bot.obs_indices()}")
    print(f"Partner hand: {server.handOfPlayer(partner)}")
    print(f"My hand (CHEATING): {server.handOfPlayer(me)}")
    print(f"My Card ids: {server.cardIdsOfHandOfPlayer(server.whoAmI())}")
    print(f"Partner Card ids: {server.cardIdsOfHandOfPlayer(partner)}")
    wait_for_my_turn(server, bot)
    bot.makeMove(Move(MoveType.HINT_VALUE, 1, partner))
    bot.wait()
    print(f"Obs on partner turn: {bot.obs()}")
    bot.wait() # always call this after makeMove!
    print(f"Observation 2: {bot.obs()} Indices: {bot.obs_indices()}")
    print(f"Public card counts: {server.getCurrentDeckComposition(-1)}")
    print(f"Card knowledge: {bot.getCardKnowledge()}")

    print(f"Discards: {server.discards()}")
    print(f"Piles: {server.piles()}")
    print(f"Stones: {server.hintStonesRemaining()} "
          f"Mulligans: {server.mulligansRemaining()} "
          f"Score: {server.currentScore()} "
          f"Game Over: {server.gameOver()}")

    bot.makeMove(Move(MoveType.DISCARD_CARD, 1))
    bot.wait()
    bot.wait()
    print(f"Move history: {bot.move_history_}")

    end_game(server, bot, thread) # cleanup on the C side

    print("Starting game 2...")
    server, bot, thread = start_game("SearchBot", 42)
    set_search_thresh(1.5)
    print("Search thresh: " , get_search_thresh())
    wait_for_my_turn(server, bot)
    print(f"Observation 1: {bot.obs()} Indices: {bot.obs_indices()}")
    bot.makeMove(Move(MoveType.HINT_VALUE, 1, partner))
    # print("SLEEPING!")
    # time.sleep(100)
    # print("DONE SLEEPING!")
    bot.wait()
    print(f"Obs on partner turn: {bot.obs()}")
    bot.wait() # always call this after makeMove!
    print(f"Observation 2: {bot.obs()} Indices: {bot.obs_indices()}")
    bot.makeMove(Move(MoveType.HINT_VALUE, 1, partner))
    bot.wait()
    print(f"Obs on partner turn: {bot.obs()}")
    bot.wait() # always call this after makeMove!
    end_game(server, bot, thread)

    print("Exiting\n")


if __name__ == "__main__":
    run()
