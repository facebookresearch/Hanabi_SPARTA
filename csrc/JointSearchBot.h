// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#include "Hanabi.h"
#include "SearchBot.h"

#include <memory>
#include <map>
#include <functional>
struct BeliefFrame;


namespace JointSearchBotParams {

  // read in parameters from environment variables
  const int RANGE_MAX = Params::getParameterInt("RANGE_MAX", 2000,
    "For JointSearchBot, the max range to perform search. Higher allows more search, at a higher computational cost.");
  const int JOINT_SEARCH_SEED = Params::getParameterInt("JOINT_SEARCH_SEED", 12345,
    "For JointSearchBot, the shared seed to use to select MC samples for search.");
  const int MEMOIZE_RANGE_SEARCH = Params::getParameterInt("MEMOIZE_RANGE_SEARCH", 0,
    "For JointSearchBot, if 1 then speed up play by only performing common-knowledge belief updates once and copying it to the other agent.");

} // namespace SearchBotParams


struct JointSearchBot : public SearchBot {
  friend class BeliefFrame;

  JointSearchBot(int index, int numPlayers, int handSize);

protected:
  void init_(const Hanabi::Server &server) override;
  void applyToAll(ObservationFunc f) override;
  void pleaseObserveBeforeMove(const Hanabi::Server &server) override;
  void updateBeliefsFromDraw_(int who, int card_index, Hanabi::Card played_card, const Hanabi::Server &server) override;
  void filterBeliefsConsistentWithHint_(int from, const Move &move, const Hanabi::CardIndices &card_indices, const Hanabi::Server &server) override;
  void filterBeliefsConsistentWithAction_(const Move &move, int from, const Hanabi::Server &server) override;
  void checkBeliefs_(const Hanabi::Server &server) const override;
  void pleaseMakeMove(Hanabi::Server &server) override;

  // void constructPrivateBeliefs_(int who, const Hand &partnerHand, HandDist &newHandDist, const Hanabi::Server &server);

  void insertBeliefFrame_(int who, const Hanabi::Server &server);
  void updateFrames_(int who, const Hanabi::Server &server);
  void propagatePrunedHand_(int who, int frame_idx, const Hand &hand);
  std::vector<HandDist> hand_dists_;
  // std::vector<int> cached_num_beliefs_;

  std::vector<std::vector<BeliefFrame>> history_;
};

/**
 * Each BeliefFrame represents one deferred belief update from a partner's
 * move. Eventually the frame's hand_dist_ gets small enough (from being updated
 * by played cards or hints) that the agent can efficiently compute the belief
 * update from the partner's move, whixch gets 'pushed' forward to the present.
 */
struct BeliefFrame {
  int frame_idx_;
  Move move_;
  // BotVec players_;
  std::vector<int> hand_map_;
  Move last_move_; // *my* last move, to know how to relate draws
  Hand cheat_hand_; // for beliefCheck only!
  SimulServer simulserver_;
  HandDist hand_dist_, partner_hand_dist_;

  BeliefFrame(const JointSearchBot &bot, int who, const Move &move, const Hanabi::Server &server);

};
