// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include "Hanabi.h"
#include <memory>
#include <iostream>
#include "BotUtils.h"
#include "HleUtils.h"
#include "SmartBot.h"
#include "AsyncModelWrapper.h"

#include <torch/script.h> // One-stop header.
#include <torch/torch.h>
#include <torch/csrc/autograd/grad_mode.h>


namespace TorchBotParams {

  const std::string TORCHBOT_MODEL = Params::getParameterString("TORCHBOT_MODEL", "",
    "File path to the TorchBot model, saved as serialized TorchScript (required for TorchBot)");

} /* namespace TorchBotParams */


class TorchBot final : public Hanabi::Bot {
    int me_, numPlayers_, handSize_;
    int frame_idx_ = 0;

    // std::shared_ptr<SimulServer> simulserver_;
    // std::shared_ptr<Bot> inner_;
    TensorDict hx_;
    // std::shared_ptr<torch::jit::IValue> hx_;
    std::vector<FactorizedBeliefs> hand_distribution_v0_;

    /* keep track when a partner plays/discards, so that I can update
     * the belief distribution based on their new card */
    int player_about_to_draw_ = -1;
    /* keep track of the last card that I played or discarded
     * to get around server limitations */
    Move last_move_;
    Move the_move_;
    // int last_move_index_;
    Hanabi::CardIndices last_move_indices_;
    Hanabi::Card last_active_card_ = Hanabi::Card(Hanabi::RED, 5);

    int prev_score_ = 0; // server.currentScore();
    int prev_num_hint_ = 0; //server.hintStoneRemaining();

    int debug_last_player_ = 1;
    int debug_last_obs_ = 2;
    at::Tensor applyModel(const HleSerializedMove &frame);
    void checkBeliefs_(const Hanabi::Server &server);

    std::map<int, float> action_probs_;
    double action_unc_ = 0;
    std::mt19937 gen_; // FIXME: seed?

    void updateActionProbs(
      at::Tensor model_output,
      const std::vector<Move> &legal_moves,
      int num_moves,
      const Hanabi::Server &server
    );

  public:
    TorchBot(int index, int numPlayers, int handSize);
    void pleaseObserveBeforeMove(const Hanabi::Server &) override;
    void pleaseMakeMove(Hanabi::Server &) override;
      void pleaseObserveBeforeDiscard(const Hanabi::Server &, int from, int card_index) override;
      void pleaseObserveBeforePlay(const Hanabi::Server &, int from, int card_index) override;
      void pleaseObserveColorHint(const Hanabi::Server &, int from, int to, Hanabi::Color color, Hanabi::CardIndices card_indices) override;
      void pleaseObserveValueHint(const Hanabi::Server &, int from, int to, Hanabi::Value value, Hanabi::CardIndices card_indices) override;
    void pleaseObserveAfterMove(const Hanabi::Server &) override;
    const std::map<int, float> &getActionProbs() const override;
    void setActionUncertainty(float boltzmann_unc) override;
    TorchBot *clone() const override;
};
