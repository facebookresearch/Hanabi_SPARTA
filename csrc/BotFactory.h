// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#ifndef H_BOT_FACTORY
#define H_BOT_FACTORY

#include "Hanabi.h"

template<class SpecificBot>
struct BotFactory final : public Hanabi::BotFactory
{
    Hanabi::Bot *create(int index, int numPlayers, int handSize) const override { return new SpecificBot(index, numPlayers, handSize); }
    void destroy(Hanabi::Bot *bot) const override { delete bot; }
};

#endif /* H_BOT_FACTORY */
