// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#include "Hanabi.h"

class HolmesBot;

namespace Holmes {

class CardKnowledge {
public:
    CardKnowledge();

    bool mustBe(Hanabi::Color color) const;
    bool mustBe(Hanabi::Value value) const;
    bool cannotBe(Hanabi::Card card) const;
    bool cannotBe(Hanabi::Color color) const;
    bool cannotBe(Hanabi::Value value) const;
    int color() const;
    int value() const;

    void setMustBe(Hanabi::Color color);
    void setMustBe(Hanabi::Value value);
    void setCannotBe(Hanabi::Color color);
    void setCannotBe(Hanabi::Value value);
    void update(const Hanabi::Server &server, const HolmesBot &bot);

    bool isPlayable;
    bool isValuable;
    bool isWorthless;

private:
    bool cantBe_[Hanabi::NUMCOLORS][5+1];
    int color_;
    int value_;
};

struct Hint {
    int information_content;
    int to;
    int color;
    int value;

    Hint();
    void give(Hanabi::Server &);
};

} // namespace Holmes

class HolmesBot final : public Hanabi::Bot {

    friend class Holmes::CardKnowledge;

    int me_;
    int myHandSize_;  /* purely for convenience */

    /* What does each player know about his own hand? */
    std::vector<std::vector<Holmes::CardKnowledge> > handKnowledge_;
    /* What cards have been played so far? */
    int playedCount_[Hanabi::NUMCOLORS][5+1];
    /* What cards in players' hands are definitely identified?
     * This table is recomputed every turn. */
    int locatedCount_[Hanabi::NUMCOLORS][5+1];
    /* What is the lowest-value card currently playable?
     * This value is recomputed every turn. */
    int lowestPlayableValue_;

    bool isValuable(const Hanabi::Server &server, Hanabi::Card card) const;
    bool couldBeValuable(const Hanabi::Server &server, const Holmes::CardKnowledge &knol, int value) const;

    bool updateLocatedCount(const Hanabi::Server& server);
    void invalidateKnol(int player_index, int card_index);
    void seePublicCard(const Hanabi::Card &played_card);
    void wipeOutPlayables(const Hanabi::Card &played_card);

    /* Returns -1 if the named player is planning to play a card on his
     * turn, or if all his cards are known to be valuable. Otherwise,
     * returns the index of his oldest not-known-valuable card. */
    int nextDiscardIndex(const Hanabi::Server& server, int player) const;

    Holmes::Hint bestHintForPlayer(const Hanabi::Server &server, int to) const;

    bool maybePlayLowestPlayableCard(Hanabi::Server &server);
    bool maybeGiveHelpfulHint(Hanabi::Server &server);
    bool maybeGiveValuableWarning(Hanabi::Server &server);
    bool maybePlayMysteryCard(Hanabi::Server &server);
    bool maybeDiscardWorthlessCard(Hanabi::Server &server);
    bool maybeDiscardOldCard(Hanabi::Server &server);

  public:
    HolmesBot(int index, int numPlayers, int handSize);
    void pleaseObserveBeforeMove(const Hanabi::Server &) override;
    void pleaseMakeMove(Hanabi::Server &) override;
      void pleaseObserveBeforeDiscard(const Hanabi::Server &, int from, int card_index) override;
      void pleaseObserveBeforePlay(const Hanabi::Server &, int from, int card_index) override;
      void pleaseObserveColorHint(const Hanabi::Server &, int from, int to, Hanabi::Color color, Hanabi::CardIndices card_indices) override;
      void pleaseObserveValueHint(const Hanabi::Server &, int from, int to, Hanabi::Value value, Hanabi::CardIndices card_indices) override;
    void pleaseObserveAfterMove(const Hanabi::Server &) override;
    HolmesBot *clone() const override;

};
