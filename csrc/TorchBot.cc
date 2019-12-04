// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#include <cassert>
#include "Hanabi.h"
#include "TorchBot.h"
#include "SmartBot.h"
#include "HleUtils.h"

#include <torch/torch.h>
#include <torch/csrc/autograd/grad_mode.h>

using namespace Hanabi;
using namespace TorchBotParams;

static void _registerBots() {
  std::cout << "Registering torchbots..." << std::endl;
  registerBotFactory("TorchBot", std::shared_ptr<Hanabi::BotFactory>(new ::BotFactory<TorchBot>()));
}

static int dummy = (_registerBots(), 0);


static torch::jit::script::Module torch_jit_load(const std::string &path) {
  // std::cerr << "Loading model from '" << path << "'" << std::endl;
  assert(path != "");
  // torch::set_num_threads(1);
  auto model = torch::jit::load(path, c10::Device(c10::kCPU));
  // std::cerr << "Done loading model from '" << path << "'" << std::endl;

  return model;
}

torch::jit::script::Module get_torchbot_module(const std::string &path) {
  static thread_local torch::jit::script::Module torchbot_module;
  static thread_local bool torchbot_module_initialized;
  if (!torchbot_module_initialized) {
    torchbot_module = torch_jit_load(path);
    torchbot_module_initialized = true;
  }
  return torchbot_module;
}


std::shared_ptr<AsyncModelWrapper> get_torchbot_async_module(const std::string &path) {
  auto &tp = getThreadPool();
  if (!tp.model) {
    tp.model = std::make_shared<AsyncModelWrapper>(path, "cuda:0", 400);
  }
  return tp.model;
}

// std::shared_ptr<torch::jit::IValue> vec_to_tuple(std::vector<c10::IValue> vec) {
//   return std::make_shared<torch::jit::IValue>(c10::ivalue::Tuple::create(
//       vec, c10::TupleType::create(fmap(vec, c10::incompleteInferTypeFrom))));
// }


TensorDict make_init_hx() {
  static TensorDict init_hx = {
    // num_layer, hid_dim
    {"h0", torch::zeros({2, 512})},  // TODO: ugly hard-coding
    {"c0", torch::zeros({2, 512})},  // TODO: ugly hard-coding
  };
  return init_hx;
  // std::vector<c10::IValue> init_hx_vec;
  // init_hx_vec.push_back(torch::zeros({1}));
  // init_hx_vec.push_back(torch::zeros({1}));
  // return vec_to_tuple(init_hx_vec);
}

void softmax_(float* model_output, const std::vector<Move> &legal_moves, const Server &server) {
  double sum = 0;
  double max_val = -1e9;
  for (auto move: legal_moves) {
    int idx = moveToIndex(move, server);
    if (model_output[idx] > max_val) {
      max_val = model_output[idx];
    }
  }
  for (auto move: legal_moves) {
    int idx = moveToIndex(move, server);
    double logit = model_output[idx] - max_val;
    double unnorm = std::exp(logit);
    sum += unnorm;
    model_output[idx] = unnorm;
  }
  sum *= 0.999999; // hack
  for (auto move: legal_moves) {
    int idx = moveToIndex(move, server);
    model_output[idx] /= sum;
  }
}

// NOTE(hengyuan): somehow static does not work
// static TensorDict init_hx = make_init_hx();

TorchBot::TorchBot(int index, int numPlayers, int handSize)
    : me_(index), numPlayers_(numPlayers), handSize_(handSize)
{
    // assert(index == 1); // we're only training TorchBot agents for player 1, this is a sanity check
    last_move_ = Move(INVALID_MOVE, 0);
    // inner_ = std::shared_ptr<SmartBot>(new SmartBot(index, numPlayers, handSize));
    // inner_->setPermissive(true);
    if(TORCHBOT_MODEL == "") {
      throw std::runtime_error("TORCHBOT_MODEL must be specified");
    }

    // just store a copy of the static value to save memory
    // hx_ = init_hx;
    hx_ = make_init_hx();

    // if (TORCHBOT_SAMPLE) {
    //   infosetHash_.reset(new InfosetHash(0)); // FIXME: seed?
    // }
}

static std::uniform_real_distribution<double> real_dist(0., 1.);


void TorchBot::updateActionProbs(
  at::Tensor model_output,
  const std::vector<Move> &legal_moves,
  int num_moves,
  const Server &server
) {
  // 1. calculate the 'temperature' that leads to the
  //    desired level of uncertainty about the move

  auto out_data = model_output.data<float>();

  double desired_max_prob = 1 - action_unc_;
  double temperature = 0.5;
  double distance = temperature / 2;
  float temp_data[num_moves];

  for (int iter = 0; iter < 10; iter++) {
    for (int j = 0; j < num_moves; j++) {
      temp_data[j] = out_data[j] / temperature;
    }

    softmax_(temp_data, legal_moves, server);
    double max_prob = 0;
    for (auto move : legal_moves) {
      int mi = moveToIndex(move, server);
      if (temp_data[mi] > max_prob) {
        max_prob = temp_data[mi];
      }
    }
    temperature = (max_prob > desired_max_prob) ?
                  temperature + distance :
                  temperature - distance;
    assert(temperature > 0);
    distance /= 2;
  }
  // clamp the temperature to a reasonable range
  temperature = std::min(std::max(temperature, 0.00001), 1000.);

  // 2. compute action probabilities from a boltzmann distribution
  //    given this temperature

  auto logits = model_output / temperature;
  softmax_(logits.data<float>(), legal_moves, server);

  action_probs_.clear();
  for (auto move : legal_moves) {
    int mi = moveToIndex(move, server);
    action_probs_[mi] = out_data[mi];
  }
}


void TorchBot::pleaseObserveBeforeMove(const Server &server)
{
  // assert(debug_last_player_ == 1 - server.activePlayer());
  // assert(debug_last_obs_ == 2);
  debug_last_player_ = server.activePlayer();
  debug_last_obs_ = 0;
  if (hand_distribution_v0_.size() == 0) {
    hand_distribution_v0_.reserve(server.numPlayers());
    for (int i = 0; i < server.numPlayers(); i++) {
      hand_distribution_v0_.emplace_back(server, i);
    }
  }

  SearchStats stats;

  checkBeliefs_(server);
  HleSerializedMove frame(server,
                          last_move_,
                          last_active_card_,
                          last_move_indices_,
                          prev_score_,
                          prev_num_hint_,
                          hand_distribution_v0_);

  auto model_output = applyModel(frame);

  if (server.activePlayer() == server.whoAmI()) {
    // we run the model here, not in pleaseMakeMove(), because
    // pleaseMakeMove() is expected to be idempotent, so should not
    // update hx

    int num_moves = frame.numMoves();
    assert(model_output.size(-1) == num_moves);

    auto out_data = model_output.data<float>();
    std::vector<Move> legal_moves = enumerateLegalMoves(server);

    float best_pred = -1e9;
    for (auto move : legal_moves) {
      int i = moveToIndex(move, server);
      if (out_data[i] > best_pred) {
        the_move_ = move;
        best_pred = out_data[i];
      }
    }
    if (action_unc_ > 0) {
      updateActionProbs(model_output, legal_moves, num_moves, server);
    }
  }
}

void TorchBot::pleaseObserveBeforeDiscard(const Hanabi::Server &server, int from, int card_index)
{
  assert(debug_last_player_ == server.activePlayer());
  assert(debug_last_obs_ == 0);
  debug_last_obs_ = 1;

  Move move(DISCARD_CARD, card_index);
  last_move_ = move;
  // last_move_index_ = card_index;
  last_active_card_ = (from == me_) ? server.activeCard() : server.handOfPlayer(from)[card_index];
  player_about_to_draw_ = from;
  // inner_->pleaseObserveBeforeDiscard(server, from, card_index);
  // if (infosetHash_) infosetHash_->updateHashBeforeDiscard(server, from, card_index);
}

void TorchBot::pleaseObserveBeforePlay(const Hanabi::Server &server, int from, int card_index)
{
  assert(debug_last_player_ == server.activePlayer());
  assert(debug_last_obs_ == 0);
  debug_last_obs_ = 1;

  Move move(PLAY_CARD, card_index);
  last_move_ = move;
  // last_move_index_ = card_index;
  last_active_card_ = (from == me_) ? server.activeCard() : server.handOfPlayer(from)[card_index];
  prev_score_ = server.currentScore();
  prev_num_hint_ = server.hintStonesRemaining();
  player_about_to_draw_ = from;
  // inner_->pleaseObserveBeforePlay(server, from, card_index);
  // if (infosetHash_) infosetHash_->updateHashBeforePlay(server, from, card_index);
}

void TorchBot::pleaseObserveColorHint(const Hanabi::Server &server, int from, int to, Color color, Hanabi::CardIndices card_indices)
{
  assert(debug_last_player_ == server.activePlayer());
  assert(debug_last_obs_ == 0);
  debug_last_obs_ = 1;

  Move move(HINT_COLOR, (int) color, to);
  last_move_ = move;
  last_move_indices_ = card_indices;
  // inner_->pleaseObserveColorHint(server, from, to, color, card_indices);
  hand_distribution_v0_[move.to].updateFromHint(move, card_indices, server);
  // if (infosetHash_) infosetHash_->updateHashColorHint(server, from, to, color, card_indices);
}

void TorchBot::pleaseObserveValueHint(const Hanabi::Server &server, int from, int to, Value value, Hanabi::CardIndices card_indices)
{
  assert(debug_last_player_ == server.activePlayer());
  assert(debug_last_obs_ == 0);
  debug_last_obs_ = 1;

  Move move(HINT_VALUE, (int) value, to);
  last_move_ = move;
  last_move_indices_ = card_indices;
  // inner_->pleaseObserveValueHint(server, from, to, value, card_indices);
  hand_distribution_v0_[move.to].updateFromHint(move, card_indices, server);
  // if (infosetHash_) infosetHash_->updateHashValueHint(server, from, to, value, card_indices);
}

void TorchBot::pleaseObserveAfterMove(const Server &server)
{
  assert(debug_last_player_ == server.activePlayer());
  assert(debug_last_obs_ == 1);
  debug_last_obs_ = 2;

  // inner_->pleaseObserveAfterMove(server);
  assert(server.whoAmI() == me_);

  if (player_about_to_draw_ != -1) {
    DeckComposition deck = getCurrentDeckComposition(server, -1);
    for (int p = 0; p < server.numPlayers(); p++) {
      hand_distribution_v0_[p].updateFromRevealedCard(last_active_card_, deck, server);
    }
    hand_distribution_v0_[player_about_to_draw_].updateFromDraw(deck, last_move_.value, server);
    player_about_to_draw_ = -1;
  }
}

torch::Tensor TorchBot::applyModel(const HleSerializedMove &frame) {
  // at::NoGradGuard no_grad_guard;

  // be careful not to run in parallel model, it sucks!
  // assert(omp_get_num_threads() == 1 || (!omp_get_nested() && omp_in_parallel()));

  std::vector<float> frame_vec = frame.toArray();
  // std::cout << ">>>applying: " << me_ << std::endl;

  auto feats = torch::zeros({(long int) frame_vec.size()});
  auto feat_data = feats.data<float>();
  for (size_t i = 0; i < frame_vec.size(); i++) {
    if (frame_vec[i] != frame_vec[i]) { // NaN
      std::cerr << "input data " << i << " = " << frame_vec[i] << std::endl;
      throw std::runtime_error("Inputs are NaN");
    }
    feat_data[i] = frame_vec[i];
  }
  // std::vector<torch::jit::IValue> inputs;
  // inputs.push_back(feats);
  // inputs.push_back(*hx_);

  // Execute the model and turn its output into a tensor.
  // std::cerr << "Got NN input of size" << feats.sizes() << std::endl;
  // std::vector<torch::jit::IValue> outputs = get_torchbot_module(TorchBotParams::TORCHBOT_MODEL).forward(inputs).toTuple()->elements();
  // assert(outputs.size() == 2);
  // at::Tensor output = outputs[0].toTensor();
  // std::cerr << "Got NN output of size " << output.sizes() << std::endl;
  // hx_ = std::make_shared<torch::jit::IValue>(outputs[1]);

  TensorDict input = hx_;
  input["s"] = feats;
  auto output = get_torchbot_async_module(TorchBotParams::TORCHBOT_MODEL)->forward(input);
  // {
  //   auto output_data = output.data<float>();
  //   for (size_t i = 0; i < 20; i++) {
  //     if (output_data[i] != output_data[i]) { // NaN
  //       std::cerr << "output data " << i << " = " << output_data[i] << std::endl;
  //       throw std::runtime_error("Model returned NaN");
  //     }
  //   }
  // }

  auto afind = output.find("a");
  assert(afind != output.end());
  auto action = afind->second;
  output.erase(afind);
  hx_ = std::move(output);

  return action;
}


void TorchBot::checkBeliefs_(const Server &server) {
  if (permissive_) return;
  for (int p = 0; p < server.numPlayers(); p++) {
    auto v0 = hand_distribution_v0_[p].get();
    auto true_hand = server.cheatGetHand(p);
    for (int card_index = 0; card_index < server.sizeOfHandOfPlayer(p); card_index++) {
      if (v0[card_index][cardToIndex(true_hand[card_index])] == 0) {
        hand_distribution_v0_[p].log();
        throw std::runtime_error("Bad v0 beliefs: " + std::to_string(card_index));
      }
    }
  }
}

void TorchBot::pleaseMakeMove(Server &server)
{
  assert(debug_last_obs_ == 0);
  assert(debug_last_player_ == server.whoAmI());

  execute_(server.whoAmI(), the_move_, server);
}

TorchBot *TorchBot::clone() const {
  TorchBot *b = new TorchBot(me_, numPlayers_, handSize_);
  b->frame_idx_ = this->frame_idx_;

  // b->simulserver_->sync(*this->simulserver_);
  // b->inner_.reset(this->inner_->clone());

  // n.b. we're taking a shortcut here to save memory, and copying hx_ by reference.
  // this works because the value of the hx_ tensors is never modified, only overwritten
  // by a new one! Unfortunately const doens't really enforce this because I can only
  // apply it on the IValue wrapper, not the tensor itself
  b->hx_ = this->hx_;

  // this is copy-by-value, but maybe be more explicit just in case
  // make sure that all fileds in FactorizedBeliefs are copied-by-value
  b->hand_distribution_v0_ = this->hand_distribution_v0_;

  b->player_about_to_draw_ = this->player_about_to_draw_;
  b->last_move_ = this->last_move_;
  b->the_move_ = this->the_move_;
  b->last_move_indices_ = this->last_move_indices_;
  b->last_active_card_ = this->last_active_card_;

  b->prev_score_ = this->prev_score_;
  b->prev_num_hint_ = this->prev_num_hint_;

  b->debug_last_player_ = this->debug_last_player_;
  b->debug_last_obs_ = this->debug_last_obs_;

  // if (this->infosetHash_) {
  //   b->infosetHash_->hash_ = this->infosetHash_->hash_;
  // }
  b->permissive_ = this->permissive_;
  b->action_probs_ = this->action_probs_;
  b->action_unc_ = this->action_unc_;
  // b->gen_ = this->gen_;
  return b;
}

const std::map<int, float> &TorchBot::getActionProbs() const { return action_probs_; }
void TorchBot::setActionUncertainty(float action_unc) { action_unc_ = action_unc; }
