// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

namespace HleParams {
  // read in parameters from environment variable
  const int GREEDY_ACTION = Params::getParameterInt("GREEDY_ACTION", 1);
} // namespace HleParams

inline int HandSizeFromRules(int numPlayers) {
  if (numPlayers < 4) {
    return 5;
  }
  return 4;
}

class HleSerializedMove {
 private:
  int MaxDiscardMoves() const {
    return handSize_;
  }

  int MaxPlayMoves() const {
    return handSize_;
  }

  int MaxRevealColorMoves() const {
    return (numPlayers_ - 1) * numColors_;
  }

  int MaxRevealRankMoves() const {
    return (numPlayers_ - 1) * numRanks_;
  }

  int MaxMoves() const {
    return MaxDiscardMoves() + MaxPlayMoves() + MaxRevealColorMoves() + MaxRevealRankMoves();
  }

  int NumberCardInstance(int color, int rank) const {
    if (color < 0 || color >= numColors_ || rank < 0 || rank >= numRanks_) {
      return 0;
    }
    if (rank == 0) {
      return 3;
    } else if (rank == numRanks_ - 1) {
      return 1;
    }
    return 2;
  }

  const int numPlayers_;
  const int handSize_;

  const int numColors_ = 5;
  const int numRanks_ = 5;
  const int numCardsPerColor_ = 10;
  const int maxDeckSize_ = numColors_ * numCardsPerColor_;
  const int maxNumInfoTokens_ = 8;
  const int maxNumLifeTokens_ = 3;

  const int bitsPerCard_ = numColors_ * numRanks_;
  const int bitsPerHand_ = bitsPerCard_ * handSize_;
  const int handSectionLen_ = numPlayers_ * handSize_ * bitsPerCard_ + numPlayers_;

  const int maxRemainingDeckSize_ = maxDeckSize_ - numPlayers_ * handSize_;
  const int boardSectionLen_ = (maxRemainingDeckSize_
                                + numColors_ * numRanks_
                                + maxNumInfoTokens_
                                + maxNumLifeTokens_);

  const int numMoveTypes_ = 4;
  const int lastActionSectionLen_ = (
      numPlayers_     // acting player idx
      + numMoveTypes_ // move type
      + numPlayers_   // target player idx
      + numColors_    // color revealed
      + numRanks_     // rank revealed
      + handSize_     //  reveal outcome, each bit is 1 if card was hinted at
      + handSize_     // postion played/discarded
      + bitsPerCard_  // card played/discarded
      + 2);           // successful, added info token

  const int beliefSectionLen_ = (
      numPlayers_ * handSize_ * (numColors_ * numRanks_ + numColors_ + numRanks_));

  std::vector<float> handSection_;
  std::vector<float> boardSection_;
  std::vector<float> discardSection_;
  std::vector<float> lastActionSection_;
  std::vector<float> beliefSection_;

 public:
  HleSerializedMove(const Hanabi::Server &server,
                    Move lastMove,
                    Hanabi::Card lastCard,  // for play/discard
                    Hanabi::CardIndices lastMoveIndices,  // for hint color/rank
                    int prevScore,
                    int prevNumHint,
                    const std::vector<FactorizedBeliefs> &v0Beliefs)
      : numPlayers_(server.numPlayers())
      , handSize_(HandSizeFromRules(numPlayers_))
  {
    handSection_ = encodeHands(server);
    boardSection_ = encodeBoard(server);
    discardSection_ = encodeDiscard(server);
    lastActionSection_ = encodeLastAction(
        server, lastMove, lastCard, lastMoveIndices, prevScore, prevNumHint);
    beliefSection_ = encodeBelief(server, v0Beliefs);

    // std::cout << "hand section: " << handSection_.size() <<std::endl;
    // std::cout << "board section: " << boardSection_.size() <<std::endl;
    // std::cout << "discard section: " << discardSection_.size() <<std::endl;
    // std::cout << "lastAction section: " << lastActionSection_.size() <<std::endl;
    // std::cout << "belief section: " << beliefSection_.size() <<std::endl;
  }

  int numMoves() const {
    // final +1 for noOp
    return 2 * handSize_ + (numPlayers_ - 1) * numColors_ + (numPlayers_ - 1) * numRanks_ + 1;
  }

  int numPlayers() const {
    return numPlayers_;
  }

  int handSize() const {
    return handSize_;
  }

  int numColors() const {
    return numColors_;
  }

  int numRanks() const {
    return numRanks_;
  }

  std::vector<float> encodeHands(const Hanabi::Server &server) {
    std::vector<float> handSection(handSectionLen_, 0);
    int me = server.whoAmI();

    // self-hand is all zero
    int codeOffset = bitsPerHand_;
    for (int playerOffset = 1; playerOffset < numPlayers_; ++playerOffset) {
      const int pidx = (playerOffset + me) % numPlayers_;
      const auto &hand = server.handOfPlayer(pidx);
      for (int cidx = 0; cidx < (int)hand.size(); ++cidx) {
        int index = cardToIndex(hand[cidx]);
        assert(index < bitsPerCard_);
        handSection[codeOffset + index] = 1;
        codeOffset += bitsPerCard_;
      }
      codeOffset += bitsPerCard_ * (handSize_ - (int)hand.size());
    }
    assert(codeOffset == bitsPerHand_ * numPlayers_);

    for (int playerOffset = 0; playerOffset < numPlayers_; ++playerOffset) {
      const int pidx = (playerOffset + me) % numPlayers_;
      int size = server.sizeOfHandOfPlayer(pidx);
      assert(size <= handSize_);
      if (size < handSize_) {
        handSection[codeOffset] = 1;
      }
      codeOffset += 1;
    }
    assert(codeOffset == handSection.size());
    return handSection;
  }

  std::vector<float> encodeBoard(const Hanabi::Server &server) {
    std::vector<float> boardSection(boardSectionLen_, 0);
    int codeOffset = 0;

    int remainingDeck = server.cardsRemainingInDeck();
    assert(remainingDeck <= maxRemainingDeckSize_);
    std::fill(boardSection.begin(), boardSection.begin() + remainingDeck, 1);
    codeOffset += maxRemainingDeckSize_;

    for (int i = 0; i < numColors_; ++i) {
      auto pile = server.pileOf((Hanabi::Color) i);
      if (pile.empty()) {
        codeOffset += numRanks_;
        continue;
      }
      int index = pile.topCard().value - 1;
      boardSection[codeOffset + index] = 1;
      codeOffset += numRanks_;
    }

    assert(codeOffset == boardSectionLen_ - maxNumInfoTokens_ - maxNumLifeTokens_);
    int remainingInfo = server.hintStonesRemaining();
    auto infoStart  = boardSection.begin() + codeOffset;
    std::fill(infoStart, infoStart + remainingInfo, 1);
    codeOffset += maxNumInfoTokens_;

    int remainingLife = server.mulligansRemaining();
    auto lifeStart = boardSection.begin() + codeOffset;
    std::fill(lifeStart, lifeStart + remainingLife, 1);
    assert(codeOffset + maxNumLifeTokens_ == (int)boardSection.size());
    return boardSection;
  }

  std::vector<float> encodeDiscard(const Hanabi::Server &server) {
    std::vector<float> discardSection(maxDeckSize_, 0);

    std::vector<int> discardCount(numColors_ * numRanks_, 0);
    for (const auto& card : server.discards()) {
      ++discardCount[cardToIndex(card)];
    }

    int offset = 0;
    for (int c = 0; c < numColors_; ++c) {
      for (int r = 0; r < numRanks_; ++r) {
        int numDiscarded = discardCount[c * numRanks_ + r];
        int numCard = NumberCardInstance(c, r);
        assert(numCard > 0);
        for (int i = 0; i < numCard; ++i) {
          discardSection[offset + i] = i < numDiscarded ? 1 : 0;
        }
        offset += numCard;
      }
    }
    assert(offset == (int)discardSection.size());
    return discardSection;
  }

  std::vector<float> encodeLastAction(
      const Hanabi::Server &server,
      Move lastMove,
      Hanabi::Card lastCard,               // for play/discard
      Hanabi::CardIndices lastMoveIndices, // for hint color/rank
      int prevScore,
      int prevNumHint) {
    std::vector<float> lastAction(lastActionSectionLen_, 0);
    // first step, no prev action
    if (lastMove.type == INVALID_MOVE) {
      return lastAction;
    }

    int me = server.whoAmI();
    int offset = 0;
    int lastActivePlayer = (server.activePlayer() + numPlayers_ - 1) % numPlayers_;
    assert(lastActivePlayer < numPlayers_);
    int relativeIdx = (numPlayers_ + lastActivePlayer - me) % numPlayers_;
    lastAction[relativeIdx] = 1;
    offset += numPlayers_;

    // move type
    int typeIdx = (int)lastMove.type;
    assert(typeIdx >= 0 && typeIdx < numMoveTypes_);
    lastAction[offset + typeIdx] = 1;
    offset += numMoveTypes_;

    // target player idx
    // std::cout << "lastMove.to " << lastMove.to << std::endl;
    if (lastMove.type == HINT_COLOR || lastMove.type == HINT_VALUE) {
      // std::cout << "@@@hint" << std::endl;
      assert(lastMove.to >= 0 && lastMove.to < numPlayers_);
      int targetPlayer = (lastMove.to + numPlayers_ - me) % numPlayers_;
      // if (targetPlayer == 1) {
      //   std::cout << "@@@" << lastMove.to << ", " << lastActivePlayer << ", " << me << std::endl;
      // }
      lastAction[offset + targetPlayer] = 1;
    }
    offset += numPlayers_;

    // color revealed
    if (lastMove.type == HINT_COLOR) {
      lastAction[offset + lastMove.value] = 1;
    }
    offset += numColors_;

    // value revealed
    if (lastMove.type == HINT_VALUE) {
      lastAction[offset + lastMove.value - 1] = 1;
    }
    offset += numRanks_;

    // reveal outcome
    if (lastMove.type == HINT_VALUE || lastMove.type == HINT_COLOR) {
      for (int i = 0; i < handSize_; ++i) {
        if (lastMoveIndices.contains(i)) {
          lastAction[offset + i] = 1;
        }
      }
    }
    offset += handSize_;

    // position played/discarded
    if (lastMove.type == PLAY_CARD || lastMove.type == DISCARD_CARD) {
      // std::cout << "last move index: " << lastMoveIndex << std::endl;
      lastAction[offset + lastMove.value] = 1;
    }
    offset += handSize_;

    // card played/discarded
    if (lastMove.type == PLAY_CARD || lastMove.type == DISCARD_CARD) {
      lastAction[offset + cardToIndex(lastCard)] = 1;
    }
    offset += bitsPerCard_;

    // std::cout << offset << ", " << LAST_ACTION_SIZE << std::endl;
    // whether success play
    if (lastMove.type == PLAY_CARD) {
      if (server.currentScore() > prevScore) {
        lastAction[offset] = 1;
      }
      if (server.hintStonesRemaining() > prevNumHint) {
        lastAction[offset + 1] = 1;
      }
    }
    offset += 2;
    assert(offset == lastActionSectionLen_);
    return lastAction;
  }

  std::vector<float> encodeBelief(
      const Hanabi::Server &server,
      const std::vector<FactorizedBeliefs>& v0Beliefs) {
    std::vector<float> beliefSection(beliefSectionLen_, 0);
    assert(v0Beliefs.size() == numPlayers_);

    int offset = 0;
    int me = server.whoAmI();
    for (int playerOffset = 0; playerOffset < numPlayers_; ++playerOffset) {
      int player = (playerOffset + me) % numPlayers_;
      const auto& belief = v0Beliefs[player];
      auto beliefArray = belief.get();
      for (int cardIdx = 0; cardIdx < handSize_; ++cardIdx) {
        for (int i = 0; i < bitsPerCard_; ++i) {
          beliefSection[offset + i] = beliefArray[cardIdx][i];
        }
        offset += bitsPerCard_;

        for (int i = 0; i < numColors_; ++i) {
          beliefSection[offset] = belief.colorRevealed.get(cardIdx * 5 + i);
          ++offset;
        }

        for (int i = 0; i < numRanks_; ++i) {
          beliefSection[offset] = belief.rankRevealed.get(cardIdx * 5 + i);
          ++offset;
        }
      }
    }

    assert(offset == beliefSectionLen_);
    return beliefSection;
  }

  void dumpArray() const {
    assert(false);
    // printf("SerializedMove: ");
    // const float *data = reinterpret_cast<const float *>(this);
    // for (int i = 0; i < sizeof(*this) / sizeof(float); i++) {
    //   printf("%.1f ", data[i]);
    // }
    // printf("\n");
  }

  void write(std::ostream &out) const {
    assert(false);
    // out.write(reinterpret_cast<const char*>(this), sizeof(*this));
    // out.flush();
  }

  void log(std::ostream &out) const {
    auto s = toArray();
    std::cout << "size of feature: " << s.size() << std::endl;
    out << "hands" << std::endl;
    for (auto v : handSection_) {
      out << v << " ";
    }
    out << std::endl;

    out << "board" << std::endl;
    for (auto v : boardSection_) {
      out << v << " ";
    }
    out << std::endl;

    out << "discard" << std::endl;
    for (auto v : discardSection_) {
      out << v << " ";
    }
    out << std::endl;

    out << "last action" << std::endl;
    for (auto v : lastActionSection_) {
      out << v << " ";
    }
    out << std::endl;

    out << "card knowledge" << std::endl;
    for (auto v : beliefSection_) {
      out << v << " ";
    }
    out << std::endl;
  }

  float sum() const {
    auto v = toArray();
    float s = 0;
    for (auto val : v) {
      s += val;
    }
    return s;
  }

  std::vector<float> toArray() const {
    std::vector<float> res;
    res.insert(res.end(), handSection_.begin(), handSection_.end());
    res.insert(res.end(), boardSection_.begin(), boardSection_.end());
    res.insert(res.end(), discardSection_.begin(), discardSection_.end());
    res.insert(res.end(), lastActionSection_.begin(), lastActionSection_.end());
    res.insert(res.end(), beliefSection_.begin(), beliefSection_.end());
    if (HleParams::GREEDY_ACTION) {
      res.insert(res.end(), lastActionSection_.begin(), lastActionSection_.end());
    }
    // std::cout << "size of feature: " << res.size() << std::endl;
    return res;
  }
};
