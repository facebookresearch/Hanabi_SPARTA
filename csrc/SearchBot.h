// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include "Hanabi.h"
#include "BotFactory.h"
#include "BotUtils.h"

#include <memory>
#include <map>
#include <functional>
#include <fstream>


namespace SearchBotParams {
  // read in parameters from environment variables
  const std::string BPBOT = Params::getParameterString("BPBOT", "SmartBot",
    "The blueprint agent to use for search.");
  const int SEARCH_PLAYER = Params::getParameterInt("SEARCH_PLAYER", -1,
    "For single-agent search, which player performs search (negative numbers count from the end).");
  const int SEARCH_ALL = Params::getParameterInt("SEARCH_ALL", 0,
    "If 1, all agents perform search independently (unsound)");
  // hack: not const because we twiddle it in extension.cc
  extern float SEARCH_THRESH; // score threshold to override the blueprint
  const int SEARCH_N = Params::getParameterInt("SEARCH_N", 10000,
    "Number of MC rollouts to perform for search.");
  const int DOUBLE_SEARCH = Params::getParameterInt("DOUBLE_SEARCH", 0,
    "Perform a second (independent) search to use as an unbiased estimator of the true scores.");
  const float PARTNER_UNIFORM_UNC = Params::getParameterFloat("PARTNER_UNIFORM_UNC", 0.,
    "Add 'uniform' uncertainty to the belief update. Should be 0-1, with 1 corresponding to assuming a uniform policy.");
  const float PARTNER_BOLTZMANN_UNC = Params::getParameterFloat("PARTNER_BOLTZMANN_UNC", 0.,
    "Assume the TorchBot partner plays a Boltzmann distribution of actions proportional to exp(Q_a / T), where T is chosen so the 'best' action is played with probability 1-unc. (TorchBot only).");

  const int OPTIMIZE_WINS = Params::getParameterInt("OPTIMIZE_WINS", 0,
    "Have search ptimize for wins (25 points) rather than max score. This tends to produce worse scores *and* fewer wins, due to bad reward shaping.");
  const int UCB = Params::getParameterInt("UCB", 1,
    "Use UCB for search MC rollouts.");
  const int SEARCH_BASELINE = Params::getParameterInt("SEARCH_BASELINE", 0,
    "If 1, subtract blueprint action EV from EVs for other actions during MC rollouts; reduces the number of MC rollouts required.");
  const int DELAYED_OBS_THRESH = Params::getParameterInt("DELAYED_OBS_THRESH", 100000,
    "Only apply observations to belief bots if the range is below this size. For TorchBot, this trades off time vs space "
    "(higher THRESH uses less memory at the cost of more compute).");
} // namespace SearchBotParams


void logSearchResults(const SearchStats &stats, int numPlayers, int me);

void applyDelayedObservations(
  HandDist &handDist,
  const std::vector<BoxedHand> &handDistKeys
);

struct SearchBot : public Hanabi::Bot {
  /* public API */
  SearchBot(int index, int numPlayers, int handSize);
  void pleaseObserveBeforeMove(const Hanabi::Server &server) override;
  void pleaseMakeMove(Hanabi::Server &server) override;
    void pleaseObserveBeforeDiscard(const Hanabi::Server &server, int from, int card_index) override;
    void pleaseObserveBeforePlay(const Hanabi::Server &server, int from, int card_index) override;
    void pleaseObserveColorHint(const Hanabi::Server &server, int from, int to, Hanabi::Color color, Hanabi::CardIndices card_indices) override;
    void pleaseObserveValueHint(const Hanabi::Server &server, int from, int to, Hanabi::Value value, Hanabi::CardIndices card_indices) override;
  void pleaseObserveAfterMove(const Hanabi::Server &server) override;

protected:
  virtual void init_(const Hanabi::Server &server);

  /* == belief update helper == */
  virtual void applyToAll(ObservationFunc f);

  /* Populate hand_distribution_ with all possible hands I may have based on
   * the deck composition (i.e. initial deck minus partner hands) */
  virtual void populateInitialHandDistribution_(Hand &hand, float prob, DeckComposition &deck, int handSize, int who, HandDist &handDist, const BotVec &partners);

  /* Remove all hands from my hand distribution that are inconsistent with the
   * hint given. */
  virtual void filterBeliefsConsistentWithHint_(int from, const Move &move, const Hanabi::CardIndices &card_indices, const Hanabi::Server &server);
  virtual void filterBeliefsConsistentWithHint_(int from, const Move &move, const Hanabi::CardIndices &card_indices, const Hanabi::Server &server, HandDist &handDist, const Hanabi::CardIndices *relevant_indices=0) const;

  /* Remove all hands from my hand distribution for which my partner would not have
   * taken the action she took if I had that hand. */
  virtual void filterBeliefsConsistentWithAction_(const Move &move, int from, const Hanabi::Server &server);
  void filterBeliefsConsistentWithAction_(const Move &move, int from, const Hanabi::Server &server, int who, HandDist &handDist);

  virtual void updateBeliefsFromDraw_(int who, int card_index, Hanabi::Card played_card, const Hanabi::Server &server);
  /* Update my hand distribution based on a random draw from the remaining deck */
  void updateBeliefsFromMyDraw_(int who, int card_index, Hanabi::Card played_card, const Hanabi::Server &server, HandDist &handDist, bool public_beliefs) const;
  /* Update my hand distribution based on the card I observe a partner draw.
   * This means removing or reducing the probability of hands that contain the
   * card the partner just drew */
  void updateBeliefsFromRevealedCard_(int who, Hanabi::Card revealed_card, const Hanabi::Server &server, HandDist &handDist, Hanabi::CardIndices *relevant_indices=0) const;

  /* A sanity check that peeks at my true hand from the server and asserts
   * that my true hand is contained in my hand belief distribution. */
  virtual void checkBeliefs_(const Hanabi::Server &server) const;
  void checkBeliefs_(const Hanabi::Server &server, int who, const HandDist &handDist, const Hand &trueHand) const;

  /* == search helpers == */
  Move doSearch_(int who, Move bp_move, Move frame_move, Bot *me_bot, const HandDist &handDist,
                 const HandDistCDF &cdf, SearchStats &stats, std::mt19937 &gen,
                 const Hanabi::Server &server, bool verbose=true,
                 SearchStats *win_stats=nullptr) const;

  std::mt19937 gen_;
  SimulServer simulserver_;
  bool inited_ = false;
  HandDist hand_distribution_;
  int me_;
  BotVec players_;

  /* keep track when a partner plays/discards, so that I can update
   * the belief distribution based on their new card */
  int player_about_to_draw_ = -1;
  /* keep track of the last card that each player played or discarded
   * to get around server limitations */
  std::vector<Move> last_move_ ;
  Hanabi::Card last_active_card_ = Hanabi::Card(Hanabi::RED, 5);

  // stats
  int changed_moves_ = 0;
  double score_difference_ = 0;
  double unbiased_score_difference_ = 0;
  double unbiased_win_difference_ = 0;
  mutable int total_iters_ = 0;

  std::ofstream dumpFile_;
  int numFrames_ = 0;
};

/* override the bot factory for SearchBot to create a SearchBot only for the last
 * player and SmartBots for everyone else */
template<>
struct BotFactory<SearchBot> final : public Hanabi::BotFactory
{
    Hanabi::Bot *create(int index, int numPlayers, int handSize) const override {
      int searchPlayer = SearchBotParams::SEARCH_PLAYER;
      if (searchPlayer < 0) {
        searchPlayer += numPlayers;
      }
      if (index == searchPlayer || SearchBotParams::SEARCH_ALL) {
        return new SearchBot(index, numPlayers, handSize);
      } else {
        auto bpFactory = Hanabi::getBotFactory(SearchBotParams::BPBOT);
        Hanabi::Bot *bot = bpFactory->create(index, numPlayers, handSize);
        bot->setPermissive(true);
        return bot;
      }
    }
    void destroy(Hanabi::Bot *bot) const override { delete bot; }
};
