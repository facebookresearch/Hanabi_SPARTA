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
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <array>
#include "Hanabi.h"
#include "JointSearchBot.h"

using namespace Hanabi;
using namespace HanabiParams;
using namespace SearchBotParams;
using namespace JointSearchBotParams;

static void _registerBots() {
  registerBotFactory(
      "JointSearchBot",
      std::shared_ptr<Hanabi::BotFactory>(new ::BotFactory<JointSearchBot>())
  );
}

static int dummy =  (_registerBots(), 0);

typedef std::map<std::tuple<int, int>, std::vector<Hand>> MemoizedRange;
static MemoizedRange memoizedRange;


JointSearchBot::JointSearchBot(int index, int numPlayers, int handSize)
    : SearchBot(index, numPlayers, handSize)
    , history_(numPlayers) {
      if (numPlayers > 2) {
        throw std::runtime_error("Joint search only works for 2 players.");
      }
      std::cerr << now() << "JointSearchBotParams {" << std::endl; // legacy
      memoizedRange.clear();
}

void JointSearchBot::applyToAll(ObservationFunc f) {
  for (int p = 0; p < players_.size(); p++) {
    simulserver_.applyToAll(f, hand_dists_[p], p, p == me_);
  }
}

void JointSearchBot::init_(const Server &server) {

  std::cerr << now() << "Generating initial hand distribution..." << std::endl;
  DeckComposition deck = getCurrentDeckComposition(server, -1); // -1 means public
  for (int p = 0; p < server.numPlayers(); p++) {
    HandDist handDist;
    hand_dists_.push_back(handDist);
    Hand hand;
    BotVec partners = cloneBotVec(players_, p);
    populateInitialHandDistribution_(hand, 1, deck, server.handSize(), p, hand_dists_.back(), partners);
  }
}

void JointSearchBot::updateBeliefsFromDraw_(int who, int card_index, Card played_card, const Server &server) {
  auto &history = history_[who];
  updateBeliefsFromMyDraw_(who, card_index, played_card, server, hand_dists_[who], true);
  // the played card entered the public beliefs, so update all the hand distributions
  checkBeliefs_(server);
  if (history.size() > 0) {
    for (int i = 0; i < history.size(); i++) {
      // remove all beliefs inconsistent with the played card
      auto& frame = history[i];
      int old_index = frame.hand_map_[card_index];
      if (old_index != -1) {
        for (const Hand& hand : copyKeys(frame.hand_dist_)) {
          if (hand[old_index] != played_card) {
            frame.hand_dist_.erase(hand);
          }
        }
      }
      checkBeliefs_(server);
      // adjust hand map for drawn card
      frame.hand_map_.erase(frame.hand_map_.begin() + card_index);
      frame.hand_map_.push_back(-1);
    }
  }
  checkBeliefs_(server);

  for (int p = 0; p < server.numPlayers(); p++) {
    updateBeliefsFromRevealedCard_(-1, played_card, server, hand_dists_[p]);
    for (auto &frame : history_[p]) {
      CardIndices relevant_indices;
      for (int i = 0; i < frame.hand_map_.size(); i++) {
        if (frame.hand_map_[i] != -1) relevant_indices.add(frame.hand_map_[i]);
      }
      updateBeliefsFromRevealedCard_(-1, played_card, server, frame.hand_dist_, &relevant_indices /* ignore_index */);
      checkBeliefs_(server);
    }
  }

}

void JointSearchBot::filterBeliefsConsistentWithHint_(
    int from,
    const Move &move,
    const Hanabi::CardIndices &card_indices,
    const Hanabi::Server &server) {

  // FIXME: run hint first, but have to cache the range size
  if (history_[move.to].size() > 0) {
    for (auto &frame : history_[move.to]) {
      CardIndices hist_card_indices, relevant_indices;
      auto &hand_map = frame.hand_map_;
      for (int i = 0; i < hand_map.size(); i++) {
        if (hand_map[i] != -1) {
          relevant_indices.add(hand_map[i]);
          if(card_indices.contains(i)) {
            hist_card_indices.add(hand_map[i]);
          }
        }
      }
      SearchBot::filterBeliefsConsistentWithHint_(from, move, hist_card_indices, frame.simulserver_, frame.hand_dist_, &relevant_indices);
      checkBeliefs_(server);
        // std::cerr << now() << "Filtered historical beliefs from frame " << frame.frame_idx_ << " consistent with hint " << move.toString()
        //           << " reduced from " << hand_dist_keys.size() << " to " <<
        //           frame.hand_dist_.size() << std::endl;
    }
  }
  HandDist &handDist = hand_dists_[move.to];
  SearchBot::filterBeliefsConsistentWithHint_(from, move, card_indices, server, handDist);
}


size_t constructPrivateBeliefs_(const Hand &partnerHand, const HandDistCDF &publicPDF, HandDistCDF &privatePDF, const Server &server) {
  assert(publicPDF.probs.size() == privatePDF.probs.size());
  const DeckComposition deck = getCurrentDeckComposition(server, -1); // -1 means public
  std::atomic<int> num_private_beliefs(publicPDF.probs.size());
  std::vector<boost::fibers::future<void>> futures;
  for (int t = 0; t < NUM_THREADS; t++) {
    futures.push_back(getThreadPool().enqueue([&, t]() {
      std::array<int, 25> fast_deck;
      for (int i = 0; i < 25; i++) fast_deck[i] = deck.at(indexToCard(i));
      for (int i = t; i < publicPDF.probs.size(); i += NUM_THREADS) {
        const Hand &my_hand = publicPDF.hands.at(i);
        double old_prior = 1, new_prior = 1;
        for (const Card &card : my_hand) {
          int card_idx = cardToIndex(card);
          old_prior *= fast_deck[card_idx];
          fast_deck[card_idx]--;
        }
        assert(old_prior > 0);
        for (const Card &card : my_hand) fast_deck[cardToIndex(card)]++;  // fix the deck back up

        for (const Card &card : partnerHand) fast_deck[cardToIndex(card)]--;
        // addToDeck(my_hand, deck); // fix the deck back up
        // removeFromDeck(partnerHand, deck);
        for (const Card &card : my_hand) {
          int card_idx = cardToIndex(card);
          new_prior *= fast_deck[card_idx];
          fast_deck[card_idx]--;
        }
        for (const Card &card : my_hand) fast_deck[cardToIndex(card)]++;  // fix the deck back up
        for (const Card &card : partnerHand) fast_deck[cardToIndex(card)]++;  // fix the deck back up
        double new_prob = publicPDF.probs.at(i) * new_prior / old_prior;

        privatePDF.probs[i] = new_prob;
        if (new_prob == 0) num_private_beliefs--;
      }
    }));
  }
  for (auto &f: futures) {
    f.get();
  }
  return num_private_beliefs.load();
}

void JointSearchBot::pleaseMakeMove(Server &server)
{
    // this is the same as SearchBot::pleaseMakeMove but uses hand_dists_[me]
    updateFrames_(me_, server);
    simulserver_.sync(server);
    Move bp_move = simulserver_.simulatePlayerMove(me_, players_[me_].get());
    std::cerr << now() << "Frame " << numFrames_ << " : Blueprint strat says to play " << bp_move.toString() << std::endl;
    SearchStats stats;
    size_t num_partner_beliefs = hand_dists_[1 - me_].size();
    std::cerr << now() << "  My partner has " << num_partner_beliefs << " public beliefs. " << std::endl;
    Move move;
    if (history_[me_].size() > 0) {
      std::cerr << now() << "  Bailing from search because I dont know my beliefs." << std::endl;
      move = bp_move;
    } else {
      applyDelayedObservations(hand_dists_[me_], copyKeys(hand_dists_[me_]));
      HandDistCDF pdf = populateHandDistPDF(hand_dists_[me_]);
      size_t num_private_beliefs = constructPrivateBeliefs_(
        server.handOfPlayer(1 - me_), pdf, pdf, server);
      HandDistCDF cdf = pdf;
      pdfToCdf(pdf, cdf);

      assert(num_private_beliefs > 0);
      std::mt19937 search_gen(JOINT_SEARCH_SEED); // coordinate on seed yuck
      move = doSearch_(me_, bp_move, Move(), players_[me_].get(), hand_dists_[me_], cdf, stats, search_gen, server);
      logSearchResults(stats, server.numPlayers(), me_);
      if (move != bp_move) std::cerr << now() << "Search changed the move. ";
      std::cerr << now() << "Blueprint picked " << bp_move.toString() << " with average score " << stats[bp_move].mean
                << "; search picked " << move.toString() << " with average score " << stats[move].mean << std::endl;

      if (move != bp_move) {
        changed_moves_++;
        score_difference_ += stats[move].mean - stats[bp_move].mean;
        if (DOUBLE_SEARCH) {
          SearchStats unbiased_stats;
          SearchStats unbiased_win_stats;
          doSearch_(me_, bp_move, Move(), players_[me_].get(), hand_dists_[me_], cdf, unbiased_stats, gen_, server, false, &unbiased_win_stats);
          unbiased_score_difference_ += unbiased_stats[move].mean - unbiased_stats[bp_move].mean;
          unbiased_win_difference_ += unbiased_win_stats[move].mean - unbiased_win_stats[bp_move].mean;

          // Hanabi::BOMB0 = oldBomb0;
        }
      }
    }
    execute_(me_, move, server);
}

void JointSearchBot::checkBeliefs_(const Server &server) const {
  for (int p = 0; p < server.numPlayers(); p++) {
    SearchBot::checkBeliefs_(server, p, hand_dists_[p], server.cheatGetHand(p));
    for (auto &frame : history_[p]) {
      SearchBot::checkBeliefs_(server, p, frame.hand_dist_, frame.cheat_hand_);
    }
  }
}

void JointSearchBot::pleaseObserveBeforeMove(const Server &server) {
  SearchBot::pleaseObserveBeforeMove(server);
  updateFrames_(server.activePlayer(), server);
}

void JointSearchBot::propagatePrunedHand_(int who, int frame_idx, const Hand &hand) {
  assert(frame_idx < history_[who].size());
  // auto &cur_frame = history_[who][frame_idx];
  bool is_last_frame = frame_idx == history_[who].size() - 1;
  auto &next_hand_dist = is_last_frame ? hand_dists_[who] : history_[who][frame_idx + 1].hand_dist_;
  Move my_next_move = is_last_frame ? Move() : history_[who][frame_idx + 1].last_move_;
  int draw_index = is_last_frame ? -1 : (my_next_move.type == PLAY_CARD || my_next_move.type == DISCARD_CARD) ? my_next_move.value : -1;
  if (draw_index == -1) {
    if (next_hand_dist.count(hand)) {
      next_hand_dist.erase(hand);
      if (!is_last_frame) propagatePrunedHand_(who, frame_idx + 1, hand);
    }
  } else {
    Hand new_hand = hand;
    new_hand.erase(new_hand.begin() + draw_index);
    for (int card_idx = 0; card_idx < 25; card_idx++) {
      Card drawn = indexToCard(card_idx);
      new_hand.push_back(drawn);
      if (next_hand_dist.count(new_hand)) {
        next_hand_dist.erase(new_hand);
        if (!is_last_frame) propagatePrunedHand_(who, frame_idx + 1, new_hand);
      }
      new_hand.pop_back();
    }
  }
}

void JointSearchBot::updateFrames_(int who, const Server &server) {
  auto &history = history_[who];
  int init_num_frames = history.size();
  int from = 1 - who;
  std::cerr << now() << "(P" << me_ << ") updateFrames_ P " << who << ": " << history.size() << " frames." << std::endl;
  while (history.size() > 0) {
    auto &frame = history[0];
    auto &hand_dist = frame.hand_dist_;

    if (RANGE_MAX >= 0 && hand_dist.size() > RANGE_MAX) {
      break;
    }
    // alright! we can do an update!
    auto &frame_simulserver = frame.simulserver_;
    assert(frame_simulserver.numPlayers() == 2);
    std::cerr << now() << " Frame " << frame.frame_idx_
              << " : Looking for hands for P " << who
              << " consistent with P " << from << " action " << frame.move_.toString()
              << " (range= " << frame.hand_dist_.size() << " , partner range= " << frame.partner_hand_dist_.size() << " )" << std::endl;

    auto memoize_key = std::tie(from, frame.frame_idx_);
    if (memoizedRange.count(memoize_key)) {
      std::cerr << now() << "Using memoized values to update frame " << frame.frame_idx_ << std::endl;
      auto &my_memoized_range = memoizedRange[memoize_key];
      for (auto &hand : my_memoized_range) {
        assert(hand_dist.count(hand));
        hand_dist.erase(hand);
        propagatePrunedHand_(who, 0, hand);
      }
      std::cerr << now() << "  Filtered historical range down to " << hand_dist.size() << " (MEMOIZED) " << std::endl;
      checkBeliefs_(server);
      history.erase(history.begin());
      continue;
    }
    auto hand_dist_keys = copyKeys(hand_dist);
    std::vector<Hand> my_memoized_range;

    std::cerr << now() << "Applying delayed obs on my hand dist..." << std::endl;
    applyDelayedObservations(hand_dist, hand_dist_keys);
    std::cerr << now() << "Applying delayed obs on partner dist..." << std::endl;
    applyDelayedObservations(frame.partner_hand_dist_, copyKeys(frame.partner_hand_dist_));
    std::cerr << now() << "Done delayed updates." << std::endl;

    HandDistCDF public_pdf = populateHandDistPDF(frame.partner_hand_dist_);
    HandDistCDF private_cdf = populateHandDistPDF(frame.partner_hand_dist_); // not done
    for (int i = 0; i < hand_dist_keys.size(); i++) {
      const Hand &hand = hand_dist_keys[i];
      auto &distval = hand_dist[hand];

      SimulServer my_server(frame_simulserver);
      my_server.setHand(who, hand);
      assert(my_server.whoAmI() == frame_simulserver.whoAmI());
      auto from_bot = distval.getPartner(from);
      Move bp_move = my_server.simulatePlayerMove(from, from_bot.get());
      my_server.setObservingPlayer(from); // so that we copy over who's hand in sync()

      // size_t num_private_beliefs = constructPrivateBeliefs_(hand, frame.partner_hand_dist_, partner_private_beliefs, my_server);
      size_t num_private_beliefs = constructPrivateBeliefs_(
        hand, public_pdf, private_cdf, my_server);
      if (num_private_beliefs == 0) {
        // std::cerr << now() << "Removed " << handAsString(hand) << " because inconsistent with partner beliefs" << std::endl << std::flush;
        continue;
      }
      pdfToCdf(private_cdf, private_cdf);

      std::mt19937 search_gen(JOINT_SEARCH_SEED); // coordinate on seed yuck
      SearchStats stats;
      // move = doSearch_(me_, bp_move, players_[me_].get(), my_private_beliefs, stats, search_gen, server);
      Move cf_move = doSearch_(from, bp_move, frame.move_, from_bot.get(), frame.partner_hand_dist_, private_cdf, stats, search_gen, my_server, false);
      // std::cerr << now() << "   Hand= " << handAsString(hand)
      //   << " true_move= " << frame.move_.toString() << " (score= " << stats[frame.move_].mean
      //   << " ) pred_move= " << cf_move.toString() << " (score= " << stats[cf_move].mean << " )" << std::endl;
      if (frame.move_ != cf_move) {
        hand_dist.erase(hand);
        if(MEMOIZE_RANGE_SEARCH) {
          my_memoized_range.push_back(hand);
        }
        // std::cerr << now() << " Gonna propagate pruning of hand " << handAsString(hand) << " ; current beliefs contain " << hand_dists_[who].size() << std::endl;
        propagatePrunedHand_(who, 0, hand);
        // std::cerr << now() << " post-pruned beliefs contain " << hand_dists_[who].size() << std::endl;
        checkBeliefs_(server);
      }
    }
    if (MEMOIZE_RANGE_SEARCH) {
      memoizedRange[memoize_key] = my_memoized_range;
    }
    std::cerr << now() << "  Filtered historical range down to " << hand_dist.size() << std::endl;

    checkBeliefs_(server);
    history.erase(history.begin()); // FIXME: use more efficient data structure or use SmartPtr to avoid copies
    if (history.size() == 0) {
      std::cerr << now() << "Woo! pushed up to the present!" << std::endl;
    }
  }
  if (init_num_frames != history.size()) std::cerr << now() << "updateFrames_ reduced history from " << init_num_frames << " to " << history.size() << " frames." << std::endl;
  std::cerr << now() << "updateFrames_ done." << std::endl;
}


void JointSearchBot::filterBeliefsConsistentWithAction_(const Move &move, int from, const Server &server) {

  assert(server.numPlayers() == 2);
  int who = 1 - from;
  simulserver_.sync(server);

  updateFrames_(from, server);

  if (history_[from].size() > 0) {
    // my partner played blueprint. At most one player is playing
    // blueprint at a time, therefore I must know my beliefs, and I can always
    // update them.
    assert(history_[who].size() == 0);
    auto &hand_dist = hand_dists_[who];
    auto hand_dist_keys = copyKeys(hand_dist);
    applyDelayedObservations(hand_dist, hand_dist_keys);
    std::vector<boost::fibers::future<void>> futures;
    for (int t = 0; t < NUM_THREADS; t++) {
      futures.push_back(getThreadPool().enqueue([&, t]() {
        for (int i = t; i < hand_dist_keys.size(); i += NUM_THREADS) {
          const Hand &hand = hand_dist_keys[i];
          auto bot = hand_dist.at(hand).getPartner(from);
          SimulServer my_server(server);
          my_server.setHand(who, hand);
          assert(my_server.whoAmI() == server.whoAmI());
          Move bp_move = my_server.simulatePlayerMove(from, bot.get());
          my_server.setObservingPlayer(from); // so that we copy over who's hand in sync()
          if (move != bp_move) {
            hand_dist[hand].prob = 0;
          }
        }
      }));
    }
    for (auto &f: futures) {
      f.get();
    }
    for (auto &hand: hand_dist_keys) {
      if (hand_dist[hand].prob == 0) {
        hand_dist.erase(hand);
      }
    }
    std::cerr << now() << "Filtered current beliefs consistent with player " << from << " BLUEPRINT action '" << move.toString()
              << "' reduced from " << hand_dist_keys.size() << " to " <<
              hand_dist.size() << std::endl;
    checkBeliefs_(server);
  } else {
    // in this case my partner played search. If my history is empty I can update
    // my beliefs directly, but it's simpler to just push it onto the end of the
    // history and do the full history update
    std::cerr << now() << "Player " << from << " did search; pushing a frame for player " << who << " ; frames= " << history_[who].size() + 1 << std::endl;
    history_[who].emplace_back(*this, who, move, server);

  }
}

BeliefFrame::BeliefFrame(const JointSearchBot &bot, int who, const Move &move, const Server &server)
  : frame_idx_(bot.numFrames_)
  , move_(move)
  , last_move_(bot.last_move_[who])
  , cheat_hand_(server.cheatGetHand(who))
  , simulserver_(server) {
    for (int i = 0; i < server.sizeOfHandOfPlayer(who); i++) {
      hand_map_.push_back(i);
    }
    for (auto &kv : bot.hand_dists_[who]) {
      hand_dist_[kv.first] = kv.second;
    }
    for (auto &kv : bot.hand_dists_[1 - who]) {
      partner_hand_dist_[kv.first] = kv.second;
    }
  }
