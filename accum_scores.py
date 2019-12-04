#!/usr/bin/env python3

# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the license found in the
# LICENSE file in the root directory of this source tree.

import os
import sys
import torch
import math

"""
A little utility script to accumulate average scores from logs across multiple runs
"""

def calc_scores(filenames):
    scores = []
    num_moves = []
    expected_delta = []
    expected_delta_win = []

    scores_bomb0 = scores[:]
    num_bombs = 0
    my_expected_delta = 0
    my_expected_delta_win = 0

    for filename in filenames:
        og, eg = 0, 0
        for line in open(filename, 'r'):
            fields = line.split()
            if 'Final score' in line:
                score, bomb = int(fields[4]), int(fields[6])
                scores.append(score)
                scores_bomb0.append(0 if bomb else score)
                num_bombs += 1 if bomb else 0
                expected_delta.append(my_expected_delta)
                expected_delta_win.append(my_expected_delta_win)
                my_expected_delta = 0
                my_expected_delta_win = 0
            if 'changed' in line:
                num_moves.append(float(fields[-1]))
                my_expected_delta += float(fields[5])
                #expected_delta.append(float(fields[5]))
                if len(fields) > 14:
                    my_expected_delta_win += float(fields[11])
                    #expected_delta_win.append(float(fields[11]))

    scores = torch.Tensor(scores)
    scores_bomb0 = torch.Tensor(scores_bomb0)
    num_moves = torch.Tensor(num_moves)
    expected_delta = torch.Tensor(expected_delta)
    expected_delta_win = torch.Tensor(expected_delta_win)

    N = scores.nelement()
    win_frac = scores.eq(25).float().mean()
    print(os.getcwd())
    print("Count: %g    Score: %g +/- %g    Win: %g +/- %g" % (N, scores.mean(), scores.std() / math.sqrt(N), win_frac, math.sqrt(win_frac * (1 - win_frac) / N)))
    print("# moves: %g  Expected delta: %g +/- %g  Win: %g +/- %g" % (num_moves.mean(), expected_delta.mean(), expected_delta.std() / math.sqrt(N), expected_delta_win.mean(), expected_delta_win.std() / math.sqrt(N)))
    print("Bomb0 Score: %g +/- %g    bomb: %g%% (%d / %d)" % (scores_bomb0.mean(), scores_bomb0.std() / math.sqrt(N), num_bombs / N * 100, num_bombs, N))

if __name__ == "__main__":
    paths = sys.argv[1:]
    files = []
    for path in paths:
        if os.path.isdir(path):
            for filename in os.listdir(path):
                if filename.startswith('task') and filename.endswith('.out'):
                    files.append(path + '/' + filename)
        else:
            files.append(path)

    calc_scores(files)
