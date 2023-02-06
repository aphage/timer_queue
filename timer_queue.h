// MIT License

// Copyright (c) 2023 aphage

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <queue>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

class timer_queue {
public:
    timer_queue(std::uint32_t queue_size = 0, const std::function<void()>& queue_overflow_callback = {}) {
        _queue_max_size = queue_size;
        _queue_overflow_callback = queue_overflow_callback;
        _thread = std::thread([this](){ run(); });
    }

    ~timer_queue() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _stop = true;

        }
        _cond.notify_all();
        _thread.join();
    }

    timer_queue(const timer_queue&) = delete;
    timer_queue& operator=(const timer_queue&) = delete;
    timer_queue(timer_queue&&) = default;
    timer_queue& operator=(timer_queue&&) = default;

    std::uint64_t set_timeout(const std::chrono::milliseconds& timeout, const std::function<void()>& callback) {
        timer t;
        t.end = std::chrono::steady_clock::now() + timeout;
        t.callback = callback;
        std::uint64_t id = 0;
        bool has_overflow = false;

        do {
            std::lock_guard<std::mutex> lock(_mutex);
            if(_queue_max_size != 0 && _queue.size() > _queue_max_size) {
                has_overflow = true;
                break;
            }

            id = ++_id;
            t.id = id;
            _queue.push(std::move(t));
        } while(false);
        
        if(has_overflow) {
            if(_queue_overflow_callback) _queue_overflow_callback();
            return 0;
        }

        _cond.notify_one();
        return id;
    }

    std::uint64_t set_timeout(const std::uint64_t timeout, const std::function<void()>& callback) {
        return set_timeout(std::chrono::milliseconds(timeout), callback);
    }

    void cancel(const std::uint64_t id) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for(auto& t : _queue.get_container()) {
                if(t.id == id) {
                    timer empty;
                    t = empty;
                    _queue.push(std::move(empty));
                    break;
                }
            }
        }
        _cond.notify_one();
    }

    void clear() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for(auto& t : _queue.get_container()) {
                timer empty;
                t = empty;
            }
        }
        _cond.notify_one();
    }

private:
    struct timer {
        std::uint64_t id;
        std::chrono::steady_clock::time_point end;
        std::function<void()> callback;
        bool operator>(const timer& other) const {
            return end > other.end? true : end == other.end? id > other.id : false;
        }
    };
    class queue : public std::priority_queue<timer, std::vector<timer>, std::greater<timer>> {
    public:
        std::vector<timer>& get_container() {
            return c;
        }
    } _queue;

    void run() {
        while(true) {
            timer t;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _cond.wait(lock, [this](){ return _stop || !_queue.empty(); });
                if(_stop) {
                    break;
                }
                if(!_queue.top().callback) {
                    _queue.pop();
                    continue;
                }
                auto status = _cond.wait_until(lock, _queue.top().end);
                if(status != std::cv_status::timeout) {
                    continue;
                }
                t = std::move(_queue.top());
                _queue.pop();
            }
            
            if(t.callback) {
                t.callback();
            }
        }
    }

    std::uint64_t _id = 0;
    std::thread _thread;
    std::mutex _mutex;
    std::condition_variable _cond;
    bool _stop = false;
    std::uint32_t _queue_max_size = 0;
    std::function<void()> _queue_overflow_callback;
};
