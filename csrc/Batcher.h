// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include <boost/fiber/mutex.hpp>
#include <boost/fiber/condition_variable.hpp>

#include <torch/script.h> // One-stop header.


// utils for convert dict[str, tensor] <-> ivalue
using TensorDict = std::unordered_map<std::string, torch::Tensor>;
using TorchTensorDict = torch::Dict<std::string, torch::Tensor>;

inline TensorDict iValueToTensorDict(
    const torch::IValue& value, torch::DeviceType device, bool detach) {
  std::unordered_map<std::string, torch::Tensor> map;
  auto dict = value.toGenericDict();
  // auto ivalMap = dict->elements();
  for (auto& name2tensor : dict) {
    auto name = name2tensor.key().toString();
    torch::Tensor tensor = name2tensor.value().toTensor();
    if (detach) {
      tensor = tensor.detach();
    }
    tensor = tensor.to(device);
    map.insert({name->string(), tensor});
  }
  return map;
}

// TODO: this may be simplified with constructor in the future version
inline TorchTensorDict tensorDictToTorchDict(
    const TensorDict& tensorDict, const torch::Device& device) {
  TorchTensorDict dict;
  for (const auto& name2tensor : tensorDict) {
    dict.insert(name2tensor.first, name2tensor.second.to(device));
  }
  return dict;
}


class FutureReply {
 public:
  FutureReply()
      : ready_(false) {}

  TensorDict get(int slot) {
    std::unique_lock<std::mutex> lk(mReady_);
    cvReady_.wait(lk, [this] { return ready_; });
    lk.unlock();

    TensorDict e;
    for (const auto& kv : data_) {
      assert(slot >= 0 && slot < kv.second.size(0));
      e[kv.first] = kv.second[slot];
    }
    return e;
    // return data_[slot];
  }

  void set(TensorDict&& t) {
    // assert(t.device().is_cpu());
    {
      std::lock_guard<std::mutex> lk(mReady_);
      ready_ = true;
      data_ = std::move(t);
    }
    cvReady_.notify_all();
  }

 private:
  // no need for protection, only set() can set it
  // torch::Tensor data_;
  TensorDict data_;

  std::mutex mReady_;
  bool ready_;
  boost::fibers::condition_variable_any cvReady_;
};

class ExitThread: public std::exception {};

class Batcher {
 public:
  Batcher(int batchsize)
   : batchsize_(batchsize)
   , nextSlot_(0)
   , numActiveWrite_(0)
   // , buffer_(torch::zeros({batchsize, dim}))
   , currentReply_(nullptr)
   , nextReply_(std::make_shared<FutureReply>()){
  }

  // send data into batcher
  std::shared_ptr<FutureReply> send(const TensorDict& t, int* slot) {
    std::unique_lock<std::mutex> lk(mNextSlot_);

    // init buffer
    if (buffer_.empty()) {
      for (const auto& kv : t) {
        auto t = kv.second.sizes();
        std::vector<int64_t> sizes;
        sizes.push_back(batchsize_);
        sizes.insert(sizes.end(), t.begin(), t.end());

        buffer_[kv.first] = torch::zeros(sizes, kv.second.dtype());
      }
    }

    assert(nextSlot_ <= batchsize_);
    // wait if current batch is full and not extracted
    cvNextSlot_.wait(lk, [this] { return nextSlot_ < batchsize_; });

    *slot = nextSlot_;
    ++nextSlot_;
    ++numActiveWrite_;
    lk.unlock();

    // this will copy
    for (const auto& kv : t) {
      buffer_[kv.first][*slot] = kv.second;
    }

    // batch has not been extracted yet
    assert(numActiveWrite_ > 0);
    assert(nextReply_ != nullptr);
    auto reply = nextReply_;
    lk.lock();
    --numActiveWrite_;
    lk.unlock();
    if (numActiveWrite_ == 0) {
      cvGetBatch_.notify_one();
    }
    return reply;
  }

  // get batch input from batcher
  TensorDict get() {
    std::unique_lock<std::mutex> lk(mNextSlot_);
    cvGetBatch_.wait(lk, [this] { return nextSlot_ > 0 && numActiveWrite_ == 0 || exit_; });
    if (exit_) {
      throw ExitThread();
    }
    TensorDict batch;
    for (const auto& kv : buffer_) {
      batch[kv.first] = kv.second.narrow_copy(0, 0, nextSlot_).contiguous();
    }
    // auto batch = buffer_.narrow_copy(0, 0, nextSlot_);

    // assert currentReply has been handled
    assert(currentReply_ == nullptr);
    currentReply_ = std::move(nextReply_);
    nextReply_ = std::make_shared<FutureReply>();

    nextSlot_ = 0;
    lk.unlock();

    cvNextSlot_.notify_all();
    return batch;
  }

  // set batch reply for batcher
  void set(TensorDict&& t) {
    for (const auto& kv : t) {
      assert(kv.second.device().is_cpu());
    }
    assert(currentReply_ != nullptr);
    currentReply_->set(std::move(t));
    currentReply_ = nullptr;
  }

  // hack: public so that they can coordinate thread exit
  bool exit_ = false;
  boost::fibers::condition_variable_any cvGetBatch_;
  std::mutex mNextSlot_;

 private:
  const int batchsize_;

  int nextSlot_;
  int numActiveWrite_;
  boost::fibers::condition_variable_any cvNextSlot_;

  TensorDict buffer_;

  std::shared_ptr<FutureReply> currentReply_;
  std::shared_ptr<FutureReply> nextReply_;
};
