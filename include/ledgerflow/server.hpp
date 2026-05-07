//
// Created by jtwears on 4/30/26.
//
#pragma once

#include <liburing.h>
#include <list>
#include <netinet/in.h>
#include <stop_token>
#include <vector>

#include "ingress.pb.h"
#include "position_engine.hpp"
#include "wal/wal.hpp"

namespace ledgerflow {

    constexpr int DEFAULT_PORT = 4000;
    constexpr int DEFAULT_QUEUE_SIZE = 256;
    constexpr std::string DEFAULT_HOST = "127.0.0.1";

    enum class IoRingEventType : int {
        ACCEPT = 1,
        READ = 2,
        WRITE = 3,
        CLOSE = 4,
    };

    struct ServerConfig {
        const int port = DEFAULT_PORT;
        const std::string host = DEFAULT_HOST;
        const int max_queue_size = DEFAULT_QUEUE_SIZE;
    };

    struct connection;

    struct IoRingEvent {
        IoRingEventType type;
        int fd;
        connection* conn;
    };

    struct connection {
        explicit connection(const int fd) : fd(fd) {}
        int fd;
        std::vector<char> read_buffer;
        std::vector<char> write_buffer;
        iovec read_iov{};
        iovec write_iov{};
        IoRingEvent read_event{};
        IoRingEvent write_event{};
    };

    inline connection* find_connection(const int fd, std::list<connection>& connections) {
        for (auto& conn : connections) {
            if (conn.fd == fd) {
                return &conn;
            }
        }
        return nullptr;
    }

    class Server {
        public:
            explicit Server(const ServerConfig& config, wal::WriteAheadLog& wal, PositionEngine& engine);
            ~Server() = default;
            int start(const std::stop_token& stop_token);
            int stop();
        private:
            ServerConfig _config;
            wal::WriteAheadLog& _wal;
            PositionEngine& _engine;
            int _listen_fd;
            int _ring_fd;
            std::list<connection> _connection_fds;
            io_uring _ring;
            IoRingEvent _accept_event{};

            int handleClientRequest(const IoRingEvent& event);
            void buildPositionResponse(const IngressRequest& request, IngressResponse& response) const;
            void buildAllPositionsResponse(IngressResponse& response) const;
            void dispatchEvent(const IngressRequest& request) const;
            int sendResponse(const IoRingEvent& event, const IngressResponse& response);

            static int handleClientResponse(const IoRingEvent& event);
            int handleClose(int fd);
            int configure_socket();
            int submit_accept(sockaddr_in *client_addr,
                   socklen_t *client_addr_len);
            int submit_read(int client_fd);
            int submit_write(const IoRingEvent& event);
            void cleanup();

    };
}
