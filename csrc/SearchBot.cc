// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#include <string>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <set>
#include <algorithm>
#include <atomic>
#include <cstring>
#include "SearchBot.h"
#include <thread>
#include <mutex>
#include <shared_mutex>

using namespace Hanabi;
using namespace HanabiParams;
using namespace SearchBotParams;

namespace SearchBotParams {
  float SEARCH_THRESH = Params::getParameterFloat("SEARCH_THRESH", 0.1,
    "Search deviates from the blueprint only if the EV of a move exceeds the blueprint action EV by SEARCH_THRESH.");
}

static void _registerBots() {
  registerBotFactory("SearchBot", std::shared_ptr<Hanabi::BotFactory>(new ::BotFactory<SearchBot>()));
}

static int dummy =  (_registerBots(), 0);


void applyDelayedObservations(HandDist &handDist, const std::vector<BoxedHand> &handDistKeys) {
  if (handDist.size() > DELAYED_OBS_THRESH) {
    // bail to save memory
    return;
  }
  std::vector<boost::fibers::future<void>> futures;
  std::cerr << now() << "Applying "
    << handDist[handDistKeys[0]].delayed_observations.size() << " observations to "
    << handDistKeys.size() << " bots." << std::endl;

  for (int t = 0; t < NUM_THREADS; t++) {
    futures.push_back(getThreadPool().enqueue([&, t]() {
      for (int i = t; i < handDistKeys.size(); i += NUM_THREADS) {
        auto key = handDistKeys[i];
        handDist.at(key).applyObservations();
      }
    }));
  }
  for (auto &f: futures) {
    f.get();
  }
  std::cerr << now() << "Done applying delayed observations." << std::endl;
}


SearchBot::SearchBot(int index, int numPlayers, int handSize) : simulserver_(numPlayers)
{
  std::cerr << now() << "SearchBotParams {" << std::endl; // legacy

  me_ = index;
  last_move_ = std::vector<Move>(numPlayers, Move());
  std::cerr << now() << "Initializing sub-bots..." << std::endl;
  auto botFactory = getBotFactory(SearchBotParams::BPBOT);

  for (int player = 0; player < numPlayers; player++) {
    auto bot = botFactory->create(player, numPlayers, handSize);
    if (PARTNER_BOLTZMANN_UNC > 0 && player != me_) {
      bot->setActionUncertainty(PARTNER_BOLTZMANN_UNC);
    }
    players_.push_back(std::shared_ptr<Bot>(bot));
    players_.back()->setPermissive(true); // because we may not follow the blueprint
  }
  simulserver_.setPlayers(players_);
}

void SearchBot::populateInitialHandDistribution_(Hand &hand, float prob, DeckComposition &deck, int handSize, int who, HandDist &handDist, const BotVec &partners) {
  // This function recursively populates a hand distribution with all possible
  // five-card hands composed of the provided deck composition.
  if (hand.size() == 0) {
    hand.reserve(handSize);
  }
  if (hand.size() == handSize) {
    if (handDist.size() % 1000000 == 0) {
      std::cerr << now() << "Generated " << handDist.size() << " hands." << std::endl;
    }
    // assert(handDist.count(hand) == 0);
    handDist[hand] = HandDistVal(prob, partners);
    return;
  }

  for (auto &kv : deck) {
    const Card &card = kv.first;
    int count = kv.second;
    if (count > 0) {
      deck[card]--;
      hand.push_back(card);
      populateInitialHandDistribution_(hand, prob * count, deck, handSize, who, handDist, partners);
      hand.pop_back();
      deck[card]++;
    }
  }
}

void SearchBot::applyToAll(ObservationFunc f) {
  simulserver_.applyToAll(f, hand_distribution_, me_);
}

void SearchBot::init_(const Server &server) {
  // we have to generate the initial hand distribution here rather than in the constructor
  // because we need access to the server to know what the partner hand is
  assert(hand_distribution_.empty());
  std::cerr << now() << "Generating initial hand distribution..." << std::endl;
  DeckComposition deck = getCurrentDeckComposition(server, me_);
  Hand hand;
  auto partners = cloneBotVec(players_, me_);
  populateInitialHandDistribution_(hand, 1, deck, server.handSize(), me_, hand_distribution_, partners);
  std::cerr << now() << "Hand distribution contains " << hand_distribution_.size() << " hands." << std::endl;
}

void SearchBot::pleaseObserveBeforeMove(const Server &server) {
  simulserver_.sync(server);

  if (!inited_) {
    this->init_(server);
    inited_ = true;
  }

  assert(server.whoAmI() == me_);
  simulserver_.sync(server);
  std::cerr << now() << "applyToAll ObserveBeforeMove start" << std::endl;
  applyToAll(
    [](Bot *bot, const Server &server) { bot->pleaseObserveBeforeMove(server); }
  );
  std::cerr << now() << "applyToAll ObserveBeforeMove end" << std::endl;

}

void SearchBot::pleaseObserveBeforeDiscard(const Server &server, int from, int card_index) {
  simulserver_.sync(server);

  Move move(DISCARD_CARD, card_index);
  last_move_[from] = move;
  last_active_card_ = (from == me_) ? server.activeCard() : server.handOfPlayer(from)[card_index];
  player_about_to_draw_ = from;
  filterBeliefsConsistentWithAction_(move, from, server);
  applyToAll(
    [from, card_index](Bot *bot, const Server &server) {
      bot->pleaseObserveBeforeDiscard(server, from, card_index); }
  );
}

void SearchBot::pleaseObserveBeforePlay(const Server &server, int from, int card_index) {
  simulserver_.sync(server);

  Move move(PLAY_CARD, card_index);
  last_move_[from] = move;
  last_active_card_ = (from == me_) ? server.activeCard() : server.handOfPlayer(from)[card_index];
  player_about_to_draw_ = from;
  filterBeliefsConsistentWithAction_(move, from, server);
  applyToAll(
    [from, card_index](Bot *bot, const Server &server) {
      bot->pleaseObserveBeforePlay(server, from, card_index); }
  );
}

void SearchBot::pleaseObserveColorHint(const Server &server, int from, int to, Color color, CardIndices card_indices) {
  simulserver_.sync(server);

  Move move(HINT_COLOR, (int) color, to);
  last_move_[from] = move;
  filterBeliefsConsistentWithHint_(from, move, card_indices, server);
  filterBeliefsConsistentWithAction_(move, from, server);

  applyToAll(
    [from, to, color, card_indices](Bot *bot, const Server &server) {
      bot->pleaseObserveColorHint(server, from, to, color, card_indices); }
  );
}

void SearchBot::pleaseObserveValueHint(const Server &server, int from, int to, Value value, CardIndices card_indices) {
  simulserver_.sync(server);

  Move move(HINT_VALUE, (int) value, to);
  last_move_[from] = move;
  filterBeliefsConsistentWithHint_(from, move, card_indices, server);
  filterBeliefsConsistentWithAction_(move, from, server);

  applyToAll(
    [from, to, value, card_indices](Bot *bot, const Server &server) {
      bot->pleaseObserveValueHint(server, from, to, value, card_indices); }
  );
}

void SearchBot::pleaseObserveAfterMove(const Server &server) {
  simulserver_.sync(server);
  if (player_about_to_draw_ != -1) {
    updateBeliefsFromDraw_(player_about_to_draw_, last_move_[player_about_to_draw_].value, last_active_card_, server);
    player_about_to_draw_ = -1;
  }
  applyToAll(
    [](Bot *bot, const Server &server){
      bot->pleaseObserveAfterMove(server); }
  );
  if(server.gameOver() || server.finalCountdown() == server.numPlayers()) {
    std::cout << "SearchBot changed " << changed_moves_ << " moves, gaining ";
    if (DOUBLE_SEARCH) {
      std::cout << unbiased_score_difference_ << " (unbiased) " << score_difference_ << " (biased) ";
      std::cout << "Win delta: " << unbiased_win_difference_ << " (unbiased) ";
    } else {
      std::cout << score_difference_;
    }
    std::cout << " points. Total search iters: " << total_iters_ << std::endl;

  }
}

void SearchBot::filterBeliefsConsistentWithHint_(
    int from,
    const Move &move,
    const CardIndices &card_indices,
    const Server &server) {

  if (move.to != me_) {
    return;
  }
  filterBeliefsConsistentWithHint_(from, move, card_indices, server, hand_distribution_);
  checkBeliefs_(server);

}

void SearchBot::filterBeliefsConsistentWithHint_(
    int from,
    const Move &move,
    const CardIndices &card_indices,
    const Server &server,
    HandDist &handDist,
    const CardIndices *relevant_indices) const {

  auto hand_dist_keys = copyKeys(handDist);
  auto old_size = handDist.size();
  for (auto &hand : hand_dist_keys) {
    bool keep = true;
    for (int i = 0; i < hand.size(); i++) {
      if (relevant_indices && !relevant_indices->contains(i)) {
        continue;
      }
      const Card &card = hand[i];
      int card_value = move.type == HINT_COLOR ? (int) card.color : (int) card.value;
      bool consistent = card_indices.contains(i) ?
                        card_value == move.value :  // positive info
                        card_value != move.value;   // negative info
      if (!consistent) {
        keep = false;
        break;
      }
    }
    if (!keep) {
      handDist.erase(hand);
    }
  }
  std::cerr << now() << "Player " << me_ << ": Filtered beliefs consistent with hint " << move.toString()
            << " reduced from " << old_size << " to " <<
            handDist.size() << std::endl;
}

void SearchBot::filterBeliefsConsistentWithAction_(const Move &move, int from, const Server &server) {
  if (from == me_) {
    return;
  }

  {
    // just for logging
    auto cheat_hand = server.cheatGetHand(me_);
    auto cheat_bot = hand_distribution_[cheat_hand].getPartner(from);
    auto cheat_server = SimulServer(server);
    cheat_server.setHand(me_, cheat_hand);
    auto expected_move = cheat_server.simulatePlayerMove(from, cheat_bot->clone());
    std::cerr << "SearchBot expected " << expected_move.toString() << " , observed " << move.toString() << std::endl;
  }

  if (PARTNER_UNIFORM_UNC == 1) {
    return;
  }
  size_t old_size = hand_distribution_.size();
  std::cerr << now() << "filterAction_ with " << old_size << " beliefs." << std::endl;
  auto hand_dist_keys = copyKeys(hand_distribution_);
  applyDelayedObservations(hand_distribution_, hand_dist_keys);
  std::vector<boost::fibers::future<void>> futures;
  for (int t = 0; t < NUM_THREADS; t++) {
    futures.push_back(getThreadPool().enqueue([&, t]() {
      SimulServer simulserver(simulserver_);
      for (int i = t; i < hand_dist_keys.size(); i += NUM_THREADS) {
        auto &hand = hand_dist_keys[i];
        simulserver.setHand(me_, hand);
        auto bot = hand_distribution_[hand].getPartner(from);
        if (PARTNER_BOLTZMANN_UNC > 0) {
          auto action_probs = bot->getActionProbs();
          if (server.cheatGetHand(me_) == hand.get()) {
            for (auto kv : action_probs) std::cerr << "Action " << kv.first << " : " <<kv.second << std::endl;
            std::cerr << "Prob of " << move.toString() << " ( " << moveToIndex(move, server) << ") : " << action_probs[moveToIndex(move, server)] << std::endl;
          }
          hand_distribution_[hand].prob *= (action_probs[moveToIndex(move, server)] + PARTNER_UNIFORM_UNC);
        } else {
          Move cf_move = simulserver.simulatePlayerMove(from, bot.get());

          if (move != cf_move) {
            hand_distribution_[hand].prob *= PARTNER_UNIFORM_UNC;
          }
        }
      }
    }));
  }
  for (auto &f: futures) {
    f.get();
  }
  for (auto &hand: hand_dist_keys) {
    if (hand_distribution_[hand].prob == 0) {
      hand_distribution_.erase(hand);
    }
  }
  std::cerr << now() << "Player " << me_ << ": Filtered beliefs consistent with player " << from << " action '" << move.toString()
            << "' reduced from " << old_size << " to " <<
            hand_distribution_.size() << std::endl;

  checkBeliefs_(server);
}

void SearchBot::updateBeliefsFromDraw_(int who, int card_index, Card played_card, const Server &server) {
  if (who == me_) {
    updateBeliefsFromMyDraw_(who, card_index, played_card, server, hand_distribution_, false);
  } else if (server.sizeOfHandOfPlayer(who) == server.handSize()) {
    Card drawn_card = server.handOfPlayer(who).back();
    updateBeliefsFromRevealedCard_(me_, drawn_card, server, hand_distribution_);
  }
  checkBeliefs_(server);

  DeckComposition deck = getCurrentDeckComposition(server, -1);
}

void SearchBot::updateBeliefsFromMyDraw_(
    int who,
    int card_index,
    Card played_card,
    const Server &server,
    HandDist &handDist,
    bool public_beliefs) const
{
  HandDist new_hand_distribution;
  DeckComposition deck = getCurrentDeckComposition(server, public_beliefs ? -1 : who);
  int hand_size = server.sizeOfHandOfPlayer(who);
  for (auto &kv : handDist) {
    const Hand &hand = kv.first;
    //std::cerr << now() << "MyDraw_: " << handAsString(hand) << std::endl;
    if (hand[card_index] != played_card) {
      //std::cerr << now() << "pruned : (" << card_index << ") " << hand[card_index].toString() << " != " << played_card.toString() << std::endl;
      continue;
    }
    Hand new_hand = hand;
    new_hand.erase(new_hand.begin() + card_index);
    // for (int i = 0; i < hand.size(); i++) {
    //   if (i != card_index) {
    //     new_hand.push_back(hand[i]);
    //   }
    // }
    // std::cerr << now() << "Removing from deck: " << handAsString(new_hand) << std::endl;
    removeFromDeck(new_hand, deck);

    if (server.sizeOfHandOfPlayer(who) == server.handSize()) {
      // update distribution with all possible drawn cards
      for (const auto &kv2 : deck) {
        const Card &card = kv2.first;
        int count = kv2.second;
        //std::cerr << now() << "Added card: " << card.toString() << " with count " << count << std::endl;

        if (count > 0) {
          new_hand.push_back(card);
          assert(new_hand.size() == hand_size);
          assert(new_hand_distribution.count(new_hand) == 0);
          new_hand_distribution[new_hand] = kv.second;

          //std::cerr << now() << "new hand: " << handAsString(new_hand) << std::endl;

          new_hand.pop_back();
        }
      }
    } else {
      // no new card drawn, just keep your old hand
      assert(server.cardsRemainingInDeck() == 0 || server.gameOver());
      assert(new_hand.size() == hand_size);
      new_hand_distribution[new_hand] = kv.second;
    }
    addToDeck(new_hand, deck);

  }
  std::cerr << now() << "Player " << me_ << ": Filtered player " << who << " beliefs consistent with my draw; went from "
            << handDist.size() << " to " <<
            new_hand_distribution.size() << std::endl;
  handDist = new_hand_distribution;
}

void SearchBot::updateBeliefsFromRevealedCard_(
    int who,
    Card revealed_card,
    const Server &server,
    HandDist &handDist,
    CardIndices *relevant_indices) const
{

  DeckComposition deck = getCurrentDeckComposition(server, who);
  int remaining = deck[revealed_card] + 1; // this is what was remaining *before* the draw
  assert(remaining > 0);
  int old_size = handDist.size();
  auto hand_dist_keys = copyKeys(handDist);
  for (auto &hand: hand_dist_keys) {
    int in_hand = 0;
    for (int i = 0; i < hand.size(); i++) {
      const Card &my_card = hand[i];
      if (relevant_indices && !relevant_indices->contains(i)) continue;
      if (my_card == revealed_card) in_hand++;
    }
    if (in_hand > 0) {
      auto &val = handDist[hand];
      float new_prob = val.prob * (remaining - in_hand) / remaining;
      if (new_prob > 0) {
        val.prob = new_prob;
      } else {
        handDist.erase(hand);
      }
    }
  }
  std::cerr << now() << "Player " << me_ << ": Filtered player " << who << " beliefs consistent with revealed card " << revealed_card.toString()
            << " reduced from " << old_size << " to " <<
            handDist.size() << std::endl;
}

void SearchBot::checkBeliefs_(const Server &server) const {
  checkBeliefs_(server, me_, hand_distribution_, server.cheatGetHand(me_));
}

void SearchBot::checkBeliefs_(const Server &server, int who, const HandDist &handDist, const Hand &trueHand) const {

  if (handDist.count(trueHand) == 0) {
    std::cerr << now() << "ERROR: player's true hand not contained in beliefs" << std::endl;
    std::cerr << now() << "Who am I? " << who << std::endl;
    std::cerr << now() << "true hand: " << handAsString(trueHand) << std::endl;
    std::cerr << now() << "Hands: " << server.handsAsString() << std::endl;
    std::cerr << now() << "Discards: " << server.discardsAsString() << std::endl;
    std::cerr << now() << "Piles: " << server.pilesAsString() << std::endl;
    std::cerr << now() << "-------------------------" << std::endl;
    std::cerr << now() << "Hand distribution: (count= " << handDist.size() << ")" << std::endl;
    int count = 0;
    for (auto &kv : handDist) {
      std::cerr << now() << handAsString(kv.first) << std::endl;
      if (count++ > 100) {
        std::cerr << now() << "..." << std::endl;
        break;
      }
    }
    throw std::runtime_error("Belief check failed.");
  } else {
    // std::cerr << now() << "CheckBeliefs: Found player " << who << " true hand " << handAsString(trueHand) << " among " << handDist.size() << " beliefs." << std::endl;
  }
}


static std::uniform_real_distribution<double> real_dist(0., 1.);

Hand sampleFromCDF_(const HandDistCDF &cdf, std::mt19937 &gen) {

  double prob = real_dist(gen);
  auto it = std::upper_bound(cdf.probs.begin(), cdf.probs.end(), prob);
  int idx = it - cdf.probs.begin() - 1;
  if (idx >= cdf.hands.size())  {
    std::cerr << now() << "CDF uhoh: " << idx << " >= " << cdf.hands.size() << " [ "
        << cdf.probs.size() << " " << cdf.probs.back() << " " << prob << std::endl;
    assert(0);
  }
  Hand hand = cdf.hands[idx];
  assert(cdf.probs[idx + 1] - cdf.probs[idx] > 0 || idx + 1 == cdf.probs.size());
  // std::cerr << now() << "Got prob " << prob << " sampled idx " << idx << " i.e. hand " << handAsString(hand) << " cdf: " << cdf.probs[idx] << std::endl;
  return hand;
}

bool canPruneMove(const SearchStats &stats, Move move, Move bp_move) {
  if (SEARCH_BASELINE && move == bp_move) {
    return false;
  }

  if (!UCB) {
    return false;
  }

  const UCBStats &this_ucb = stats.at(move);

  Move best_move(INVALID_MOVE, 0);
  if (SEARCH_BASELINE) {
    double best_stderr = 0;
    double best_mean = -100;
    for (auto &kv: stats) {
      if (!kv.second.pruned && (kv.second.mean + kv.second.bias > best_mean || best_move.type == INVALID_MOVE)) {
        best_stderr = kv.second.search_baseline_stderr();
        best_move = kv.first;
        best_mean = kv.second.mean + kv.second.bias;
      }
    }
    assert(best_move.type != INVALID_MOVE);

    double this_stderr = this_ucb.search_baseline_stderr();
    double this_mean = this_ucb.mean + this_ucb.bias;

    double diff = best_mean - this_mean;
    double combined_err = sqrt((this_stderr * this_stderr) + (best_stderr * best_stderr));
    return diff - 2.5 * combined_err > 0;
  }
  else {
    double best_lcb = 0;
    for (auto &kv: stats) {
      // std::cout << move.toString() << " : " << kv.second.lcb() << " " << best_lcb << std::endl;
      if (kv.second.lcb() > best_lcb || best_move.type == INVALID_MOVE) {
        best_lcb = kv.second.lcb();
        best_move = kv.first;
      }
    }
    assert(best_move.type != INVALID_MOVE);
    return this_ucb.ucb() < best_lcb;
  }
}



std::string oneMoveStatToString(const SearchStats &stats, Move m) {
  if(stats.count(m)) {
    char buff[100];
    if (SEARCH_BASELINE) {
      snprintf(buff, sizeof(buff), "%6.2f +/- %4.2g (%4d)",
        stats.at(m).mean,
        ((float)((int) (stats.at(m).search_baseline_stderr() * 100))) / 100, stats.at(m).N);
    }
    else {
      snprintf(buff, sizeof(buff), "%6.2f +/- %4.2g (%4d)",
        stats.at(m).mean,
        ((float)((int) (stats.at(m).stderr() * 100))) / 100, stats.at(m).N);
    }
    return std::string(buff);
  } else {
    return "         ---          ";
  }
}

void logSearchResults(const SearchStats &stats, int numPlayers, int me) {
  std::cerr << now() << "Play:            ";
  for (int i = 0; i < 5; i++) {
    std::cerr << i << ": ";
    std::cerr << oneMoveStatToString(stats, Move(PLAY_CARD, i)) << " ";
  }
  std::cerr << std::endl;
  std::cerr << now() << "Discard:         ";
  for (int i = 0; i < 5; i++) {
    std::cerr << i << ": ";
    std::cerr << oneMoveStatToString(stats, Move(DISCARD_CARD, i)) << " ";
  }
  std::cerr << std::endl;
  for (int to = 0; to < numPlayers; to++) {
    if (to == me) continue;
    std::cerr << now() << "Hint Color to " << to << ": ";
    for (Color color = RED; color < NUMCOLORS; color++) {
      std::cerr << colorname(color)[0] << ": ";
      std::cerr << oneMoveStatToString(stats, Move(HINT_COLOR, color, to)) << " ";
    }
    std::cerr << std::endl;
    std::cerr << now() << "Hint Value to " << to << ": ";
    for (Value value = ONE; value <= VALUE_MAX; value++) {
      std::cerr << value << ": ";
      std::cerr << oneMoveStatToString(stats, Move(HINT_VALUE, value, to)) << " ";
    }
    std::cerr << std::endl;
  }
}


template<class It, class Gen>
static void portable_shuffle(It first, It last, Gen& g)
{
    const int n = (last - first);
    for (int i=0; i < n; ++i) {
        int j = (g() % (i + 1));
        if (j != i) {
            using std::swap;
            swap(first[i], first[j]);
        }
    }
}


class Barrier {
public:
    explicit Barrier(std::size_t iCount) :
      mThreshold(iCount),
      mCount(iCount),
      mGeneration(0) {
    }

    void wait() {
        std::unique_lock<std::mutex> lLock{mMutex};
        auto lGen = mGeneration;
        if (!--mCount) {
            mGeneration++;
            mCount = mThreshold;
            mCond.notify_all();
        } else {
            mCond.wait(lLock, [this, lGen] { return lGen != mGeneration; });
        }
    }

private:
    std::mutex mMutex;
    boost::fibers::condition_variable_any mCond;
    std::size_t mThreshold;
    std::size_t mCount;
    std::size_t mGeneration;
};


int oneSearchIter_(
  Bot *me_bot,
  int who,
  const Move &sampled_move,
  const HandDistCDF &cdf,
  const Server &server,
  const HandDist &handDist,
  std::mt19937 &gen
){
  // pick a hand from the beliefs, and a move, and a deck
  Hand sampled_hand = sampleFromCDF_(cdf, gen);

  // sample a deck
  DeckComposition search_deck = getCurrentDeckComposition(server, who);
  removeFromDeck(sampled_hand, search_deck);
  std::vector<Card> deck_order;
  for(auto kv : search_deck) {
    for (int i = 0; i < kv.second; i++) deck_order.push_back(kv.first);
  }
  portable_shuffle(deck_order.begin(), deck_order.end(), gen);

  // setup server
  SimulServer search_server(server);

  auto &distval = handDist.at(sampled_hand);
  BotVec search_bots;
  for(int p = 0; p < server.numPlayers(); p++) {
    if (p == who) search_bots.push_back(std::shared_ptr<Bot>(me_bot->clone()));
    else search_bots.push_back(distval.getPartner(p));
  }

  search_server.setPlayers(search_bots);
  search_server.setHand(who, sampled_hand);
  search_server.setDeck(deck_order);

  execute_(server.whoAmI(), sampled_move, search_server);

  for(int i = 0; i < search_bots.size(); i++) {
    search_server.setObservingPlayer(i);
    search_bots[i]->pleaseObserveAfterMove(search_server);
  }
  search_server.incrementActivePlayer();
  // sanity check
  for(int p = 0; p < search_server.numPlayers(); p++) {
    const Hand &hand = search_server.cheatGetHand(p);
    for (const Card &card : hand) {
      assert(card.color != INVALID_COLOR);
    }
  }

  // simulate!
  int score = search_server.runToCompletion();

  return score;
}


inline void accumScore(int score, int bp_score, Move &move, SearchStats &stats, SearchStats *win_stats) {
  if (score == -1) { // skipped
    return;
  }
  assert(score >= 0);

  int adj_score = score;
  if (SEARCH_BASELINE) {
    assert(bp_score >= 0);
    adj_score = score - bp_score;
  }

  stats[move].add(OPTIMIZE_WINS ? (score == 25 ? 1 : 0) : adj_score);
  if (win_stats) {
    (*win_stats)[move].add(score == 25);
  }
}

Move SearchBot::doSearch_(
    int who,
    Move bp_move,
    Move frame_move,
    Bot *me_bot,
    const HandDist &handDist,
    const HandDistCDF &cdf,
    SearchStats &stats,
    std::mt19937 &gen,
    const Server &server,
    bool verbose,
    SearchStats *win_stats) const {

  // n.b. the probabilities in handDist may not be right, because it's too
  // slow to update them for public -> private conversion. The probabilities in
  // cdf are considered the ground truth for the purposes of search

  std::vector<Move> moves = enumerateLegalMoves(server);
  int num_moves = moves.size();

  for (auto &move : moves) {
    stats[move] = UCBStats();
    if (win_stats) (*win_stats)[move] = UCBStats();
  }
  stats[bp_move].bias = SEARCH_THRESH;
  std::atomic<int> loop_count(0);
  if (verbose) {
    std::cerr << now() << "search player " << server.whoAmI() << " start" << std::endl;
  }

  bool frame_bail = false;
  int prune_count = 0;
  int bp_mi = -1;
  int frame_mi = -1;
  for (int mi = 0; mi < num_moves; mi++) {
    if (moves.at(mi) == bp_move) {
      bp_mi = mi;
    }
    if (frame_move != Move() && moves.at(mi) == frame_move) {
      frame_mi = mi;
    }
  }

  //We use this to facilitate baseline usage. Shouldn't make a big difference
  int temp_num_threads = NUM_THREADS - (NUM_THREADS % num_moves);
  assert(temp_num_threads >= num_moves);

  //std::cerr << "Temporary number of threads: " << temp_num_threads << std::endl;
  int temp_search_n = SEARCH_N - (SEARCH_N % temp_num_threads);

  std::vector<boost::fibers::future<void>> futures;
  std::mutex mtx;
  Barrier barrier(temp_num_threads);

  std::uniform_int_distribution<int> uid1(0, 1 << 30);
  std::vector<int> seeds(SEARCH_N / num_moves + 1);
  for(int i = 0; i < seeds.size(); i++) seeds[i] = uid1(gen);

  std::vector<int> scores(SEARCH_N, -2);
  int accumed = 0;
  for (int t = 0; t < temp_num_threads; t++) {
    futures.push_back(getThreadPool().enqueue([&, t](){
      for (int j = t; j < temp_search_n; j += temp_num_threads) {
        if (frame_bail || prune_count >= num_moves - 1) {
          break;
        }

        // multi-threaded stuff
        int mi = j % num_moves;
        int g = j / num_moves;
        if (seeds[g] == 0) {
          std::cerr << "WARNING: seed is 0!\n";
        }
        assert(g < seeds.size());
        std::mt19937 my_gen(seeds[g]);

        auto sampled_move = moves.at(mi);
        if (!stats[sampled_move].pruned) {
          loop_count++;
          scores[j] = oneSearchIter_(me_bot, who, sampled_move, cdf, server, handDist, my_gen);
         } else {
          scores[j] = -1; // sentinel
        }

        // single-threaded stuff
        if (UCB && j + temp_num_threads < temp_search_n) {
          barrier.wait();

          if (t == 0) {
            for (int k = j; k < j + temp_num_threads; k++) {
              int bp_score = scores[k - (k % num_moves) + bp_mi];
              accumScore(scores[k], bp_score, moves[k % num_moves], stats, win_stats);
            }

            for (int mi = 0; mi < num_moves; mi++) {
              if (!stats[moves[mi]].pruned && canPruneMove(stats, moves[mi], bp_move)) {
                stats[moves[mi]].pruned = true;
                prune_count++;
                if (moves[mi] == frame_move) {
                  frame_bail = true;
                }
              }
            }
            accumed += temp_num_threads;
          } // if (t == 0)

          barrier.wait();
        }
      }
    }));
  }
  for(auto &f: futures) {
    f.get();
  }
  if (frame_bail) { //Then all that matters is we didn't choose the observed action
    return Move();
  }
  if (prune_count < num_moves - 1) { // accumulate the stragglers
    for (int k = accumed; k < temp_search_n; k++) {
      int bp_score = scores[k - (k % num_moves) + bp_mi];
      accumScore(scores[k], bp_score, moves[k % num_moves], stats, win_stats);
    }
  }

  total_iters_ += loop_count;
  Move best_move;
  double best_score = -1;
  for(auto &kv: stats) {
    if (kv.second.pruned) continue;
    if (kv.second.mean + kv.second.bias > best_score) {
      best_move = kv.first;
      best_score = kv.second.mean + kv.second.bias;
    }
  }
  if (verbose) {
    std::cerr << now() << "Ran " << loop_count << " search iters over " << num_moves << " moves. ( " << server.handsAsString()
              << " ) , p " << server.whoAmI() << " --> " << best_move.toString() << " (" << stats[best_move].mean << ") [bp " << bp_move.toString() << " (" << stats[bp_move].mean << ") ]" << std::endl << std::flush;
  }
  return best_move;
}

void SearchBot::pleaseMakeMove(Server &server)
{
    simulserver_.sync(server);
    Move bp_move = simulserver_.simulatePlayerMove(me_, players_[me_].get());
    std::cerr << now() << "Blueprint strat says to play " << bp_move.toString() << std::endl;

    SearchStats stats;
    auto hand_dist_keys = copyKeys(hand_distribution_);
    applyDelayedObservations(hand_distribution_, hand_dist_keys);
    HandDistCDF cdf = populateHandDistCDF(hand_distribution_);
    Move move = doSearch_(me_, bp_move, Move(), players_[me_].get(), hand_distribution_, cdf, stats, gen_, server);
    logSearchResults(stats, server.numPlayers(), me_);
    if (bp_move != move) std::cerr << now() << "Search changed move. ";
    std::cerr << now() << "Blueprint picked " << bp_move.toString() << " with average score " << stats[bp_move].mean
              << "; search picked " << move.toString() << " with average score " << stats[move].mean << std::endl;
    if (move != bp_move) {
      changed_moves_++;
      score_difference_ += stats[move].mean - stats[bp_move].mean;
      if (DOUBLE_SEARCH) {
        SearchStats unbiased_stats;
        SearchStats unbiased_win_stats;
        doSearch_(me_, bp_move, Move(), players_[me_].get(), hand_distribution_, cdf, unbiased_stats, gen_, server, false, &unbiased_win_stats);
        unbiased_score_difference_ += unbiased_stats[move].mean - unbiased_stats[bp_move].mean;
        unbiased_win_difference_ += unbiased_win_stats[move].mean - unbiased_win_stats[bp_move].mean;
      }
    }

    execute_(me_, move, server);
}
