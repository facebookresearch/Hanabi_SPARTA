// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

// Code modified from code originally written under the following copyright:

/*
Copyright (c) 2012 Jakob Progsch, Václav Zeman

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/


#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <boost/fiber/all.hpp>
#include <boost/thread/barrier.hpp>

class AsyncModelWrapper;

class ThreadPool {
public:
    ThreadPool(size_t);
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> boost::fibers::future<typename std::result_of<F(Args...)>::type>;
    void close();
    std::shared_ptr<AsyncModelWrapper> model; // hack

    bool stop;
private:
    std::unique_ptr<boost::barrier> b;
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue

    // synchronization
    std::mutex mtx;
    boost::fibers::condition_variable_any condition;
};

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
    :   stop(false)
{
    b.reset(new boost::barrier(threads + 1));
    for (size_t i = 0;i<threads;++i) {
        workers.emplace_back(
            [this]
            {
                boost::fibers::use_scheduling_algorithm< boost::fibers::algo::shared_work >();
                this->b->wait();
                while(true) {
                    std::unique_lock<std::mutex> lock(this->mtx);
                    this->condition.wait(lock,
                        [this]{ return this->stop; });
                    if(this->stop)
                        return;
                }
            }
        );
    }
    boost::fibers::use_scheduling_algorithm< boost::fibers::algo::shared_work >();
    b->wait();
}

// add new work item to the pool
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> boost::fibers::future<typename std::result_of<F(Args...)>::type>
{
    if(stop)
        throw std::runtime_error("enqueue on stopped ThreadPool");
    auto res = boost::fibers::async(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    return res;
}

inline void ThreadPool::close()
{
    model.reset();
    {
        std::unique_lock<std::mutex> lock(mtx);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}


#endif
