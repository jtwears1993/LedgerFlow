//
// Created by jtwears on 5/6/26.
//

#pragma once

#include <atomic>
#include <csignal>
#include <functional>
#include <mutex>
#include <ranges>
#include <utility>
#include <vector>

namespace ledgerflow {

    inline volatile std::sig_atomic_t shutdown_signal_requested = 0;

    extern "C" inline void signal_handler(int signal) {
        switch (signal) {
        case SIGINT:
        case SIGTERM:
            shutdown_signal_requested = 1;
            break;
        default:
            break;
        }
    }

    class ShutdownManager {
    public:
        ShutdownManager() {
            std::signal(SIGINT, signal_handler);
            std::signal(SIGTERM, signal_handler);
        }

        ~ShutdownManager() = default;
        ShutdownManager(const ShutdownManager&) = delete;
        ShutdownManager(ShutdownManager&&) = delete;
        ShutdownManager& operator=(const ShutdownManager&) = delete;
        ShutdownManager& operator=(ShutdownManager&&) = delete;

        void request_shutdown() {
            _shutdown.store(true, std::memory_order_release);
        }

        [[nodiscard]] bool shutdown_requested() const {
            return _shutdown.load(std::memory_order_acquire) || shutdown_signal_requested != 0;
        }

        void add_shutdown_request(std::function<void()> request) {
            std::scoped_lock lock(_mutex);
            _shutdown_requests.emplace_back(std::move(request));
        }

        void run_shutdown_request() {
            if (shutdown_signal_requested != 0) {
                request_shutdown();
            }

            if (!_shutdown.load(std::memory_order_acquire)) {
                return;
            }

            if (_shutdown_ran.exchange(true, std::memory_order_acq_rel)) {
                return;
            }

            std::vector<std::function<void()>> requests;
            {
                std::scoped_lock lock(_mutex);
                requests = _shutdown_requests;
            }

            for (auto & request : std::ranges::reverse_view(requests)) {
                request();
            }
        }

    private:
        std::atomic_bool _shutdown{false};
        std::atomic_bool _shutdown_ran{false};
        std::mutex _mutex;
        std::vector<std::function<void()>> _shutdown_requests;
    };
}
