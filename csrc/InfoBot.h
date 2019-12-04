// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include "Hanabi.h"
#include <memory>

class InfoBot : public Hanabi::Bot {
    std::unique_ptr<InfoBot> impl_;

    static std::unique_ptr<InfoBot> makeImpl(int index, int numPlayers, int handSize);

  protected:
    InfoBot() = default;

  public:
    InfoBot(int index, int numPlayers, int handSize) : impl_(makeImpl(index, numPlayers, handSize)) {}
    void pleaseObserveBeforeMove(const Hanabi::Server& server) override {
        return impl_->pleaseObserveBeforeMove(server);
    }
    void pleaseMakeMove(Hanabi::Server& server) override {
        return impl_->pleaseMakeMove(server);
    }
    void pleaseObserveBeforeDiscard(const Hanabi::Server& server, int from, int card_index) override {
        return impl_->pleaseObserveBeforeDiscard(server, from, card_index);
    }
    void pleaseObserveBeforePlay(const Hanabi::Server& server, int from, int card_index) override {
        return impl_->pleaseObserveBeforePlay(server, from, card_index);
    }
    void pleaseObserveColorHint(const Hanabi::Server& server, int from, int to, Hanabi::Color color, Hanabi::CardIndices card_indices) override {
        return impl_->pleaseObserveColorHint(server, from, to, color, card_indices);
    }
    void pleaseObserveValueHint(const Hanabi::Server& server, int from, int to, Hanabi::Value value, Hanabi::CardIndices card_indices) override {
        return impl_->pleaseObserveValueHint(server, from, to, value, card_indices);
    }
    void pleaseObserveAfterMove(const Hanabi::Server&) override {}
    virtual ~InfoBot() = default;

    Bot *clone() const override {
      InfoBot * ret = new InfoBot();
      ret->impl_.reset((InfoBot*) impl_->clone());
      return ret;
    }
    void setPermissive(bool permissive) override { permissive_ = permissive; impl_->permissive_ = permissive; }

};
