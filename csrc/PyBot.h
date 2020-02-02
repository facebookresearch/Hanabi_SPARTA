// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include "Hanabi.h"
#include "BotUtils.h"

struct PyBot : public Hanabi::Bot {
  /* public API */
  PyBot(int index, int numPlayers, int handSize) : index_(index), handSize_(handSize), numPlayers_(numPlayers), last_active_card_(Hanabi::RED, 1) {}

  void wakeUp() {
    assert(!gameOver_);
    py_wakeup_ = true;
    c_wakeup_ = false;
    py_cv_.notify_all();

    std::unique_lock<std::mutex> lk(mtx_);
    std::cerr << "C wait" << std::endl << std::flush;
    c_cv_.wait(lk, [this]{ return this->c_wakeup_; });
    std::cerr << "C wakeup" << std::endl << std::flush;
  }

  void pleaseObserveBeforeMove(const Hanabi::Server &server) override {
    server_ = &server;
    if (beliefs_.empty()) {
      for (int i = 0; i < numPlayers_; i++) beliefs_.push_back(FactorizedBeliefs(server, i));
    }
    if (server.activePlayer() != server.whoAmI()) {
      wakeUp();
    }

  }

  void pleaseMakeMove(Hanabi::Server &server) override {
    wakeUp();
    if (server.gameOver()) return;
    assert(my_next_move_.type != INVALID_MOVE);
    execute_(server.whoAmI(), my_next_move_, server);
    my_next_move_ = Move();
  }

  void pleaseObserveBeforeDiscard(const Hanabi::Server &server, int from, int card_index) override {
    obs_ = Move(DISCARD_CARD, card_index);
    move_history_.emplace_back(from, obs_);
    last_active_card_ = (from == server.whoAmI()) ? server.activeCard() : server.handOfPlayer(from)[card_index];
    player_about_to_draw_ = from;
  }
  void pleaseObserveBeforePlay(const Hanabi::Server &server, int from, int card_index) override {
    obs_ = Move(PLAY_CARD, card_index);
    move_history_.emplace_back(from, obs_);
    last_active_card_ = (from == server.whoAmI()) ? server.activeCard() : server.handOfPlayer(from)[card_index];
    player_about_to_draw_ = from;
  }
  void pleaseObserveColorHint(const Hanabi::Server &server, int from, int to, Hanabi::Color color, Hanabi::CardIndices card_indices) override {
    obs_ = Move(HINT_COLOR, (int) color, to);
    move_history_.emplace_back(from, obs_);
    indices_ = card_indices;
    beliefs_[to].updateFromHint(obs_, card_indices, server);
  };
  void pleaseObserveValueHint(const Hanabi::Server &server, int from, int to, Hanabi::Value value, Hanabi::CardIndices card_indices) override {
    obs_ = Move(HINT_VALUE, (int) value, to);
    move_history_.emplace_back(from, obs_);
    indices_ = card_indices;
    beliefs_[to].updateFromHint(obs_, card_indices, server);
  }
  void pleaseObserveAfterMove(const Hanabi::Server &server) override {
    if (player_about_to_draw_ != -1) {
      DeckComposition deck = getCurrentDeckComposition(server, -1);
      for (int p = 0; p < server.numPlayers(); p++) {
        beliefs_[p].updateFromRevealedCard(last_active_card_, deck, server);
      }
      beliefs_[player_about_to_draw_].updateFromDraw(deck, obs_.value, server);
      player_about_to_draw_ = -1;
    }

    if (server.gameOver()) {
      gameOver_ = true;
      py_wakeup_ = true;
      py_cv_.notify_all();
    }

  }

  void setMove(const Move& move) {
    my_next_move_ = move;
  }

  void wait() {
    assert(!gameOver_);
    py_wakeup_ = false;
    c_wakeup_ = true;
    c_cv_.notify_all();
    std::unique_lock<std::mutex> lk(mtx_);
    std::cerr << "python wait" << std::endl << std::flush;
    py_cv_.wait(lk, [this]{ return this->py_wakeup_; }); //
    std::cerr << "python wakeup" << std::endl << std::flush;
  }

  ~PyBot() override {}

  int index_;
  int handSize_;
  int numPlayers_;
  bool gameOver_ = false;

  Move obs_;
  Hanabi::CardIndices indices_;
  int player_about_to_draw_ = -1;
  Hanabi::Card last_active_card_;
  std::vector<FactorizedBeliefs> beliefs_;
  const Hanabi::Server *server_;

  std::mutex mtx_;
  std::condition_variable c_cv_;
  bool c_wakeup_ = false;
  std::condition_variable py_cv_;
  bool py_wakeup_ = false;

  Move my_next_move_;
  std::vector<std::tuple<int, Move>> move_history_;
};
