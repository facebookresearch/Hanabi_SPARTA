// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include <iostream>
#include <vector>

#include <torch/torch.h>
#include <torch/script.h>

#include <chrono>
using namespace std::chrono;

#include "Batcher.h"

class AsyncModelWrapper {
 public:
  AsyncModelWrapper(const std::string& path,
                    const std::string& device,
                    int batchsize)
      : model_(torch::jit::load(path, torch::Device(device)))
      , device_(torch::Device(device))
      , batcher_(batchsize)
  {
    forwardThread_ = std::thread(&AsyncModelWrapper::batchForward, this);
  }

  TensorDict forward(TensorDict input) {
    int slot = -1;
    auto reply = batcher_.send(input, &slot);
    auto output = reply->get(slot);
    return output;
  }

  void batchForward() {
    torch::NoGradGuard noGrad;
    int i = 0;
    while (true) {
      i += 1;
      int B = 1000;
      int P = 1000000;
      if (i % B == 0) {
        if (i % P == 0) std::cerr << "avg time (over " << B << " runs): " << std::endl;
        for (auto& kv : timer_) {
          if (i % P == 0) std::cerr << kv.first << ", " << kv.second / B << std::endl;
          timer_[kv.first] = 0;
        }
        if (i % P == 0) std::cerr << "===================" << std::endl;
      }

      auto start = high_resolution_clock::now();

      TensorDict input;
      try {
        input = batcher_.get();
      } catch (ExitThread &e) {
        break;
      }
      timer_["batch_size"] += input["s"].size(0);

      auto stop = high_resolution_clock::now();
      auto duration = duration_cast<microseconds>(stop - start).count();
      timer_["wait_for_batch"] += duration;

      start = high_resolution_clock::now();
      std::vector<torch::jit::IValue> jitInput;
      jitInput.push_back(tensorDictToTorchDict(input, device_));

      stop = high_resolution_clock::now();
      duration = duration_cast<microseconds>(stop - start).count();
      timer_["to_device"] += duration;

      start = high_resolution_clock::now();
      auto jitOutput = model_.forward(jitInput);;
      auto output = iValueToTensorDict(jitOutput, torch::kCPU, true);

      stop = high_resolution_clock::now();
      duration = duration_cast<microseconds>(stop - start).count();
      timer_["forward"] += duration;

      // stop = high_resolution_clock::now();
      // duration = duration_cast<microseconds>(stop - start).count();
      // timer_["forward"] += duration;

      start = high_resolution_clock::now();
      batcher_.set(std::move(output));
      stop = high_resolution_clock::now();
      duration = duration_cast<microseconds>(stop - start).count();
      timer_["post_process"] += duration;
    }
  }

  ~AsyncModelWrapper() {
    {
      std::unique_lock<std::mutex> lock(batcher_.mNextSlot_);
      batcher_.exit_ = true;
    }
    batcher_.cvGetBatch_.notify_all();
    forwardThread_.join();
  }


 private:
  torch::jit::script::Module model_;
  torch::Device device_;

  Batcher batcher_;
  std::thread forwardThread_;

  std::unordered_map<std::string, float> timer_;
};
