// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#include <cassert>
#include "Hanabi.h"
#include "SimpleBot.h"
#include "BotFactory.h"

using namespace Hanabi;

static void _registerBots() {
  registerBotFactory("SimpleBot", std::shared_ptr<Hanabi::BotFactory>(new ::BotFactory<SimpleBot>()));
}

static int dummy =  (_registerBots(), 0);


CardKnowledge::CardKnowledge()
{
    for (Color color = RED; color <= BLUE; ++color) {
        colors_[color] = MAYBE;
    }
    for (int value = 1; value <= 5; ++value) {
        values_[value] = MAYBE;
    }
    isPlayable = false;
}

bool CardKnowledge::mustBe(Hanabi::Color color) const { return (colors_[color] == YES); }
bool CardKnowledge::mustBe(Hanabi::Value value) const { return (values_[value] == YES); }
bool CardKnowledge::cannotBe(Hanabi::Color color) const { return (colors_[color] == NO); }
bool CardKnowledge::cannotBe(Hanabi::Value value) const { return (values_[value] == NO); }

int CardKnowledge::value() const
{
    for (int i=1; i <= 5; ++i) {
        if (this->mustBe(Value(i))) return i;
    }
    assert(false);
    return -1;
}

void CardKnowledge::setMustBe(Hanabi::Color color)
{
    assert(colors_[color] != NO);
    for (Color k = RED; k <= BLUE; ++k) {
        colors_[k] = ((k == color) ? YES : NO);
    }
}

void CardKnowledge::setMustBe(Hanabi::Value value)
{
    assert(values_[value] != NO);
    for (int v = 1; v <= 5; ++v) {
        values_[v] = ((v == value) ? YES : NO);
    }
}


SimpleBot::SimpleBot(int index, int numPlayers, int handSize)
{
    me_ = index;
    handKnowledge_.resize(numPlayers);
    for (int i=0; i < numPlayers; ++i) {
        handKnowledge_[i].resize(handSize);
    }
}

void SimpleBot::invalidateKnol(int player_index, int card_index)
{
    /* The other cards are shifted down and a new one drawn at the end. */
    std::vector<CardKnowledge> &vec = handKnowledge_[player_index];
    for (int i = card_index; i+1 < vec.size(); ++i) {
        vec[i] = vec[i+1];
    }
    vec.back() = CardKnowledge();
}

void SimpleBot::pleaseObserveBeforeMove(const Server &server)
{
    assert(server.whoAmI() == me_);
}

void SimpleBot::pleaseObserveBeforeDiscard(const Hanabi::Server &server, int from, int card_index)
{
    assert(server.whoAmI() == me_);
    this->invalidateKnol(from, card_index);
}

void SimpleBot::pleaseObserveBeforePlay(const Hanabi::Server &server, int from, int card_index)
{
    assert(server.whoAmI() == me_);
    this->invalidateKnol(from, card_index);
    this->wipeOutPlayables(server.activeCard());
}

void SimpleBot::pleaseObserveColorHint(const Hanabi::Server &server, int /*from*/, int to, Color color, Hanabi::CardIndices card_indices)
{
    assert(server.whoAmI() == me_);

    /* Someone has given P a color hint. Using SimpleBot's strategy,
     * this means that all the named cards are playable. */

    Pile pile = server.pileOf(color);
    int value = pile.size() + 1;

    assert(1 <= value && value <= 5);

    for (int i=0; i < card_indices.size(); ++i) {
        CardKnowledge &knol = handKnowledge_[to][card_indices[i]];
        knol.setMustBe(color);
        knol.setMustBe(Value(value));
        knol.isPlayable = true;
    }
}

void SimpleBot::pleaseObserveValueHint(const Hanabi::Server &server, int from, int to, Value value, Hanabi::CardIndices card_indices)
{
    assert(server.whoAmI() == me_);

    /* Someone has given P a value hint. Using SimpleBot's strategy,
     * this means that all the named cards are playable...
     * unless the hint looks like it was just an attempt to free up
     * a hint stone for discarding. */

    const bool isHintStoneReclaim =
        (!server.discardingIsAllowed()) &&
        (from == (to+1) % server.numPlayers()) &&
        card_indices.contains(0);

    if (isHintStoneReclaim) {
        return;
    }

    for (int i=0; i < card_indices.size(); ++i) {
        CardKnowledge &knol = handKnowledge_[to][card_indices[i]];
        knol.setMustBe(value);
        knol.isPlayable = true;
    }
}

void SimpleBot::pleaseObserveAfterMove(const Hanabi::Server &server)
{
    assert(server.whoAmI() == me_);
}

void SimpleBot::wipeOutPlayables(const Card &played_card)
{
    const int numPlayers = handKnowledge_.size();
    for (int player = 0; player < numPlayers; ++player) {
        for (int c = 0; c < handKnowledge_[player].size(); ++c) {
            CardKnowledge &knol = handKnowledge_[player][c];
            if (!knol.isPlayable) continue;
            if (knol.mustBe(Value(5))) continue;
            if (knol.cannotBe(played_card.color)) continue;
            if (knol.cannotBe(played_card.value)) continue;
            /* This card might or might not be playable, anymore. */
            knol.isPlayable = false;
        }
    }
}

bool SimpleBot::maybePlayLowestPlayableCard(Server &server)
{
    /* Find the lowest-valued playable card in my hand. */
    int best_index = -1;
    int best_value = 6;
    for (int i=0; i < handKnowledge_[me_].size(); ++i) {
        const CardKnowledge &knol = handKnowledge_[me_][i];
        if (knol.isPlayable && knol.value() < best_value) {
            best_index = i;
            best_value = knol.value();
        }
    }

    /* If I found a card to play, play it. */
    if (best_index != -1) {
        assert(1 <= best_value && best_value <= 5);
        server.pleasePlay(best_index);
        return true;
    }

    return false;
}

bool SimpleBot::maybeGiveHelpfulHint(Server &server)
{
    if (server.hintStonesRemaining() == 0) return false;

    const int numPlayers = handKnowledge_.size();
    int best_so_far = 0;
    int player_to_hint = -1;
    int color_to_hint = -1;
    int value_to_hint = -1;
    for (int i = 1; i < numPlayers; ++i) {
        const int partner = (me_ + i) % numPlayers;
        assert(partner != me_);
        const std::vector<Card> partners_hand = server.handOfPlayer(partner);
        bool is_really_playable[5];
        for (int c=0; c < partners_hand.size(); ++c) {
            is_really_playable[c] =
                server.pileOf(partners_hand[c].color).nextValueIs(partners_hand[c].value);
        }
        /* Can we construct a color hint that gives our partner information
         * about unknown-playable cards, without also including any
         * unplayable cards? */
        for (Color color = RED; color <= BLUE; ++color) {
            int information_content = 0;
            bool misinformative = false;
            for (int c=0; c < partners_hand.size(); ++c) {
                if (partners_hand[c].color != color) continue;
                if (is_really_playable[c] &&
                    !handKnowledge_[partner][c].isPlayable)
                {
                    information_content += 1;
                } else if (!is_really_playable[c]) {
                    misinformative = true;
                    break;
                }
            }
            if (misinformative) continue;
            if (information_content > best_so_far) {
                best_so_far = information_content;
                color_to_hint = color;
                value_to_hint = -1;
                player_to_hint = partner;
            }
        }

        for (int value = 1; value <= 5; ++value) {
            int information_content = 0;
            bool misinformative = false;
            for (int c=0; c < partners_hand.size(); ++c) {
                if (partners_hand[c].value != value) continue;
                if (is_really_playable[c] &&
                    !handKnowledge_[partner][c].isPlayable)
                {
                    information_content += 1;
                } else if (!is_really_playable[c]) {
                    misinformative = true;
                    break;
                }
            }
            if (misinformative) continue;
            if (information_content > best_so_far) {
                best_so_far = information_content;
                color_to_hint = -1;
                value_to_hint = value;
                player_to_hint = partner;
            }
        }
    }

    if (best_so_far == 0) return false;

    /* Give the hint. */
    if (color_to_hint != -1) {
        server.pleaseGiveColorHint(player_to_hint, Color(color_to_hint));
    } else if (value_to_hint != -1) {
        server.pleaseGiveValueHint(player_to_hint, Value(value_to_hint));
    } else {
        assert(false);
    }

    return true;
}

void SimpleBot::pleaseMakeMove(Server &server)
{
    assert(server.whoAmI() == me_);
    assert(server.activePlayer() == me_);

    /* If I have a playable card, play it.
     * Otherwise, if someone else has an unknown-playable card, hint it.
     * Otherwise, just discard my oldest (index-0) card. */

    if (maybePlayLowestPlayableCard(server)) return;
    if (maybeGiveHelpfulHint(server)) return;

    /* We couldn't find a good hint to give, or else we're out of hint-stones.
     * Discard a card. However, discarding is not allowed when we have all
     * the hint stones, so in that case, just hint to the player on our right
     * about his oldest card. */
    if (!server.discardingIsAllowed()) {
        const int numPlayers = server.numPlayers();
        const int right_partner = (me_ + numPlayers - 1) % numPlayers;
        server.pleaseGiveValueHint(right_partner, server.handOfPlayer(right_partner)[0].value);
    } else {
        server.pleaseDiscard(0);
    }
}
