// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#include "Hanabi.h"

struct CardKnowledge {
    CardKnowledge();

    bool mustBe(Hanabi::Color color) const;
    bool mustBe(Hanabi::Value value) const;
    bool cannotBe(Hanabi::Color color) const;
    bool cannotBe(Hanabi::Value value) const;
    int value() const;

    void setMustBe(Hanabi::Color color);
    void setMustBe(Hanabi::Value value);

    bool isPlayable;

private:
    enum Possibility { NO, MAYBE, YES };
    Possibility colors_[Hanabi::NUMCOLORS];
    Possibility values_[5+1];
};

class SimpleBot final : public Hanabi::Bot {

    int me_;

    /* What does each player know about his own hand? */
    std::vector<std::vector<CardKnowledge> > handKnowledge_;

    void invalidateKnol(int player_index, int card_index);
    void wipeOutPlayables(const Hanabi::Card &played_card);

    bool maybePlayLowestPlayableCard(Hanabi::Server &server);
    bool maybeGiveHelpfulHint(Hanabi::Server &server);

  public:
    SimpleBot(int index, int numPlayers, int handSize);
    void pleaseObserveBeforeMove(const Hanabi::Server &) override;
    void pleaseMakeMove(Hanabi::Server &) override;
      void pleaseObserveBeforeDiscard(const Hanabi::Server &, int from, int card_index) override;
      void pleaseObserveBeforePlay(const Hanabi::Server &, int from, int card_index) override;
      void pleaseObserveColorHint(const Hanabi::Server &, int from, int to, Hanabi::Color color, Hanabi::CardIndices card_indices) override;
      void pleaseObserveValueHint(const Hanabi::Server &, int from, int to, Hanabi::Value value, Hanabi::CardIndices card_indices) override;
    void pleaseObserveAfterMove(const Hanabi::Server &) override;
};
