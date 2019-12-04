#!/usr/bin/env python3

# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the license found in the
# LICENSE file in the root directory of this source tree.

import torch  # make sure to dynamically load everything beforee loading hanabi_lib
# torch.ops.load_library("hanabi_lib.so")
from hanabi_lib import *
import sys

def wait_for_my_turn(server, bot):
    while not server.activePlayer() == server.whoAmI():
        bot.wait()

preloaded_moves = []
try:
    preload_url = sys.argv[1]
    with open(preload_url) as f:
        content = f.readlines()
        preloaded_moves = [x.strip() for x in content]
        print(f"Preloaded moves from {preload_url}")
except FileNotFoundError:
    exit()
except IndexError:
    pass


try:
    server, bot, thread = start_game(int(sys.argv[2]))
except IndexError:
    server, bot, thread = start_game()

me = server.whoAmI()
partner = 1 - me
print(f"Observation 1: {bot.obs()} Indices: {bot.obs_indices()}")
print(f"Partner hand: {server.handOfPlayer(partner)}")
print(f"My hand (CHEATING): {server.handOfPlayer(me)}")
print(f"My Card ids: {server.cardIdsOfHandOfPlayer(server.whoAmI())}")
print(f"Partner Card ids: {server.cardIdsOfHandOfPlayer(partner)}")
wait_for_my_turn(server, bot)

while len(preloaded_moves) != 0:
    action = preloaded_moves.pop(0)
    tokens = action.split(" ")
    print(action)

    if tokens[0] == "DISCARD":
        server.makeMove(Move(MoveType.DISCARD_CARD, int(tokens[1])))
    elif tokens[0] == "PLACE":
        server.makeMove(Move(MoveType.PLAY_CARD, int(tokens[1])))
    elif tokens[0] == "HINT_COLOR":
        server.makeMove(Move(MoveType.HINT_COLOR, int(tokens[1]), int(tokens[2])))
    elif tokens[0] == "HINT_VALUE":
        server.makeMove(Move(MoveType.HINT_VALUE, int(tokens[1]), int(tokens[2])))
    else:
        print("Unknown input")
    bot.wait()
    bot.wait()

    # print(f"Obs on partner turn: {bot.obs()}")
    # print(f"Observation 2: {bot.obs()} Indices: {bot.obs_indices()}")
    # print(f"Public card counts: {server.getCurrentDeckComposition(-1)}")
    # print(f"Card knowledge: {bot.getCardKnowledge()}")

    # print(f"Discards: {server.discards()}")
    # print(f"Piles: {server.piles()}")
    # print(f"Stones: {server.hintStonesRemaining()} "
    #       f"Mulligans: {server.mulligansRemaining()} "
    #       f"Score: {server.currentScore()} "
    #       f"Game Over: {server.gameOver()}")
