// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include "Hanabi.h"
#include <cstdint>

class SmartBot;

namespace SmartBotInternal {

enum trivalue : int8_t {
    NO, MAYBE, YES
};

class CardKnowledge {
public:
    explicit CardKnowledge(const SmartBot *bot);

    std::string toString() const;

    bool mustBe(Hanabi::Color color) const;
    bool mustBe(Hanabi::Value value) const;
    bool cannotBe(Hanabi::Card card) const;
    bool cannotBe(Hanabi::Color color) const;
    bool cannotBe(Hanabi::Value value) const;

    void setMustBe(Hanabi::Color color);
    void setMustBe(Hanabi::Value value);
    void setMustBe(Hanabi::Card card);
    void setCannotBe(Hanabi::Color color);
    void setCannotBe(Hanabi::Value value);
    void setIsPlayable(bool knownPlayable);
    void setIsValuable(bool knownValuable);
    void setIsWorthless(bool knownWorthless);
    void befuddleByDiscard();
    void befuddleByPlay(bool success);

    template<bool useMyEyesight>
    void update();

    bool known() const { computeIdentity(); return color_ != -1 && value_ != -1; }
    int color() const { computeIdentity(); return color_; }
    int value() const { computeIdentity(); return value_; }
    Hanabi::Card knownCard() const { assert(known()); return Hanabi::Card(Hanabi::Color(color_), value_); }

    int possibilities() const { computePossibilities(); return possibilities_; }

    trivalue playable() const { computePlayable(); return playable_; }
    trivalue valuable() const { computeValuable(); return valuable_; }
    trivalue worthless() const { computeWorthless(); return worthless_; }

    float probabilityPlayable() const { computePlayable(); return probabilityPlayable_; }
    float probabilityValuable() const { computeValuable(); return probabilityValuable_; }
    float probabilityWorthless() const { computeWorthless(); return probabilityWorthless_; }

    bool couldBePlayableWithValue(int value) const;
    bool couldBeValuableWithValue(int value) const;

    void computeIdentity() const;
    void computePossibilities() const;
    void computePlayable() const;
    void computeValuable() const;
    void computeWorthless() const;

    CardKnowledge transfer(const SmartBot *bot) const;

private:
    const SmartBot *bot_;

    bool cantBe_[Hanabi::NUMCOLORS][5+1];
    mutable int8_t possibilities_;
    mutable int8_t color_;
    mutable int8_t value_;
    mutable trivalue playable_;
    mutable trivalue valuable_;
    mutable trivalue worthless_;
    mutable float probabilityPlayable_;
    mutable float probabilityValuable_;
    mutable float probabilityWorthless_;
};

struct Hint {
    int fitness;
    int to;
    int color;
    int value;

    Hint();
    bool includes(Hanabi::Card) const;
    void give(Hanabi::Server &);
};

} // namespace SmartbotInternal

class SmartBot final : public Hanabi::Bot {
    friend class SmartBotInternal::CardKnowledge;

    const Hanabi::Server *server_;
    int me_;
    int myHandSize_;  /* purely for convenience */

    /* What does each player know about his own hand? */
    std::vector<std::vector<SmartBotInternal::CardKnowledge> > handKnowledge_;
    /* What cards have been played so far? */
    int playedCount_[Hanabi::NUMCOLORS][5+1];
    /* What cards in players' hands are definitely identified?
     * This table is recomputed every turn. */
    int locatedCount_[Hanabi::NUMCOLORS][5+1];
    /* What cards in players' hands are visible to me in particular?
     * This table is recomputed every turn. */
    int eyesightCount_[Hanabi::NUMCOLORS][5+1];

    bool isPlayable(Hanabi::Card card) const;
    bool isValuable(Hanabi::Card card) const;
    bool isWorthless(Hanabi::Card card) const;

    void updateEyesightCount();
    bool updateLocatedCount();
    void invalidateKnol(int player_index, int card_index, bool draw_new_card);
    void seePublicCard(const Hanabi::Card &played_card);

    /* Returns -1 if the named player is planning to play a card on his
     * turn, or if all his cards are known to be valuable. Otherwise,
     * returns the index of his oldest not-known-valuable card. */
    int nextDiscardIndex(int player) const;

    void noValuableWarningWasGiven(int from);

    SmartBotInternal::Hint bestHintForPlayer(int to) const;

    template<class F>
    SmartBotInternal::Hint bestHintForPlayerGivenConstraint(int to, F&& is_okay) const;

    bool maybeDiscardFinesse(Hanabi::Server &server);
    bool maybePlayLowestPlayableCard(Hanabi::Server &server);
    bool maybeGiveHelpfulHint(Hanabi::Server &server);
    bool maybeGiveValuableWarning(Hanabi::Server &server);
    bool maybePlayMysteryCard(Hanabi::Server &server);
    bool maybeDiscardWorthlessCard(Hanabi::Server &server);
    bool maybeDiscardOldCard(Hanabi::Server &server);

  public:
    SmartBot(int index, int numPlayers, int handSize);
    void pleaseObserveBeforeMove(const Hanabi::Server &) override;
    void pleaseMakeMove(Hanabi::Server &) override;
      void pleaseObserveBeforeDiscard(const Hanabi::Server &, int from, int card_index) override;
      void pleaseObserveBeforePlay(const Hanabi::Server &, int from, int card_index) override;
      void pleaseObserveColorHint(const Hanabi::Server &, int from, int to, Hanabi::Color color, Hanabi::CardIndices card_indices) override;
      void pleaseObserveValueHint(const Hanabi::Server &, int from, int to, Hanabi::Value value, Hanabi::CardIndices card_indices) override;
    void pleaseObserveAfterMove(const Hanabi::Server &) override;
    SmartBot *clone() const override;
};
