//
// Created by jtwears on 5/2/26.
//

#include "ledgerflow/server.hpp"
#include "ledgerflow/event_parser.hpp"
#include "ledgerflow/core/event_mapper.hpp"

#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <liburing.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <chrono>
#include <cerrno>
#include <stdexcept>


namespace ledgerflow {

    Server::Server(const ServerConfig& config, wal::WriteAheadLog& wal, PositionEngine& engine)
        : _config(config), _wal(wal), _engine(engine) {}

    int Server::start(const std::stop_token& stop_token) {
        io_uring_cqe *cqe;
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        if (io_uring_queue_init(_config.max_queue_size, &_ring, 0) < 0) {
           return -1;
        }

        if (configure_socket() < 0) {
            return -1;
        }

        if (submit_accept(&client_addr, &client_addr_len) < 0) {
            return -1;
        }
        while (!stop_token.stop_requested()) {
            __kernel_timespec timeout{.tv_sec = 0, .tv_nsec = 100'000'000};
            const int ret = io_uring_wait_cqe_timeout(&_ring, &cqe, &timeout);
            if ( ret < 0) {
                if (ret == -ETIME) {
                    continue;
                }
                perror("io_uring_wait_cqe");
                continue;
            }
            const auto event = reinterpret_cast<IoRingEvent *>(cqe->user_data);
            if (cqe->res < 0) {
                fprintf(stderr, "Async request failed: %s for event: %d\n",
                        strerror(-cqe->res), (int)event->type);
                io_uring_cqe_seen(&_ring, cqe);
                continue;
            }

            switch (event->type) {
                case IoRingEventType::ACCEPT:
                    _connection_fds.emplace_back(cqe->res);
                    submit_accept(&client_addr, &client_addr_len);
                    submit_read(cqe->res);
                    break;
                case IoRingEventType::READ:
                    if (cqe->res == 0) {
                        handleClose(event->fd);
                        break;
                    }
                    event->conn->read_buffer.resize(static_cast<std::size_t>(cqe->res));
                    handleClientRequest(*event);
                    break;
                case IoRingEventType::WRITE:
                    handleClientResponse(*event);
                    submit_read(event->fd); // keep the connection alive for more requests
                    break;
                case IoRingEventType::CLOSE:
                    handleClose(event->fd);
                    break;
            }
            io_uring_cqe_seen(&_ring, cqe);
        }
        cleanup();
        return 0;
    }

    int Server::stop() {
        cleanup();
        return 0;
    }


    int Server::configure_socket() {
        sockaddr_in srv_addr{};

        const int sock = socket(PF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
            perror("socket()");
            return -1;
        }

        constexpr int enable = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
            perror("setsockopt(SO_REUSEADDR)");
            return -1;
        }


        memset(&srv_addr, 0, sizeof(srv_addr));
        srv_addr.sin_family = AF_INET;
        srv_addr.sin_port = htons(_config.port);
        srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

        /* We bind to a port and turn this socket into a listening
         * socket.
         * */
        if (bind(sock,
                 reinterpret_cast<const sockaddr *>(&srv_addr),
                 sizeof(srv_addr)) < 0) {
            perror("bind()");
            return -1;
        }

        if (listen(sock, 10) < 0) {
            perror("listen()");
            return -1;
        }
        _listen_fd = sock;
        return 0;
    }

    int Server::submit_accept(sockaddr_in * client_addr, socklen_t *client_addr_len) {
        io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
        if (sqe == nullptr) {
            perror("io_uring_get_sqe()");
            return -1;
        }
        io_uring_prep_accept(sqe, _listen_fd, reinterpret_cast<sockaddr *>(client_addr), client_addr_len, 0);
        _accept_event = {.type = IoRingEventType::ACCEPT, .fd = _listen_fd, .conn = nullptr};
        io_uring_sqe_set_data(sqe, &_accept_event);
        io_uring_submit(&_ring);
        return 0;
    }

    int Server::submit_read(const int client_fd) {
        io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
        if (sqe == nullptr) {
            perror("io_uring_get_sqe()");
            return -1;
        }
        connection* conn = find_connection(client_fd, _connection_fds);
        if (conn == nullptr) {
            perror("find_connection()");
            return -1;
        }
        conn->read_buffer.resize(4096);
        conn->read_iov = {.iov_base = conn->read_buffer.data(), .iov_len = conn->read_buffer.size()};
        conn->read_event = {.type = IoRingEventType::READ, .fd = client_fd, .conn = conn};
        io_uring_prep_readv(sqe, client_fd, &conn->read_iov, 1, 0);
        io_uring_sqe_set_data(sqe, &conn->read_event);
        io_uring_submit(&_ring);
        return 0;
    }


    int Server::submit_write(const IoRingEvent& event) {
        io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
        if (sqe == nullptr) {
            perror("io_uring_get_sqe()");
            return -1;
        }
        connection* conn = event.conn;
        if (conn == nullptr) {
            perror("find_connection()");
            return -1;
        }
        conn->write_iov = {.iov_base = conn->write_buffer.data(), .iov_len = conn->write_buffer.size()};
        conn->write_event = {.type = IoRingEventType::WRITE, .fd = event.fd, .conn = conn};
        io_uring_prep_writev(sqe, event.fd, &conn->write_iov, 1, 0);
        io_uring_sqe_set_data(sqe, &conn->write_event);
        io_uring_submit(&_ring);
        return 0;
    }

    int Server::handleClientRequest(const IoRingEvent& event) {
        const connection* conn = event.conn;
        IngressRequest request;

        switch (read_event(conn->read_buffer, &request)) {
            case ParseResult::Truncated:  return submit_read(event.fd);
            case ParseResult::Corrupted:  return handleClose(event.fd), -1;
            case ParseResult::Ok:         break;
        }


        const std::size_t wal_size = request.ByteSizeLong();
        std::vector<std::byte> wal_data(wal_size);
        request.SerializeToArray(wal_data.data(), static_cast<int>(wal_size));
        _wal.append(wal_data, static_cast<std::uint16_t>(request.type()));

        IngressResponse response;
        try {
            switch (request.request_case()) {
                case IngressRequest::kGetPositionRequest:     buildPositionResponse(request, response); break;
                case IngressRequest::kGetAllPositionsRequest: buildAllPositionsResponse(response);      break;
                default:                                      dispatchEvent(request);                   break;
            }
        } catch (const std::invalid_argument&) {
            return handleClose(event.fd), -1;
        }

        return sendResponse(event, response);
    }

    void Server::buildPositionResponse(const IngressRequest& request, IngressResponse& response) const {
        const auto* pos = _engine.getPosition(request.get_position_request().symbol());
        if (pos == nullptr) return;
        auto* p = response.mutable_position_response()->mutable_position();
        p->set_sym(pos->sym);
        p->set_quantity(pos->quantity);
        p->set_realized_pnl(pos->realized_pnl);
        p->set_unrealized_pnl(pos->unrealized_pnl);
        p->set_average_entry_price(pos->average_entry_price);
        p->set_average_exit_price(pos->average_exit_price);
        p->set_last_sequence_number(pos->last_sequence_number);
    }

    void Server::buildAllPositionsResponse(IngressResponse& response) const {
        const auto* positions = _engine.getPositions();
        if (positions == nullptr) return;
        auto* all = response.mutable_all_positions_response();
        for (const auto& pos : positions->positions) {
            auto* p = all->add_positions();
            p->set_sym(pos.sym);
            p->set_quantity(pos.quantity);
            p->set_realized_pnl(pos.realized_pnl);
            p->set_unrealized_pnl(pos.unrealized_pnl);
            p->set_average_entry_price(pos.average_entry_price);
            p->set_average_exit_price(pos.average_exit_price);
            p->set_last_sequence_number(pos.last_sequence_number);
        }
        all->set_max_exposure(positions->max_exposure);
        all->set_current_exposure(positions->current_exposure);
        all->set_available_exposure_capacity(positions->available_exposure_capacity);
        all->set_total_realized_pnl(positions->total_realized_pnl);
        all->set_total_unrealized_pnl(positions->total_unrealized_pnl);
    }

    void Server::dispatchEvent(const IngressRequest& request) const {
        const auto internal_event = core::event_mapper::toInternalEvent(request);
        _engine.onEvent(internal_event);
    }

    int Server::sendResponse(const IoRingEvent& event, const IngressResponse& response) {
        connection* conn = event.conn;

        if (write_event(response, conn->write_buffer) != ParseResult::Ok) {
            return handleClose(event.fd), -1;
        }

        const auto ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const Header resp_header{
            .timestampNS    = static_cast<std::uint64_t>(ts_ns),
            .proto_size     = static_cast<std::uint32_t>(response.ByteSizeLong()),
            .idempotencyKey = 0,
        };
        write_header(resp_header, conn->write_buffer.data());

        return submit_write(event);
    }

    int Server::handleClientResponse(const IoRingEvent& event) {
        event.conn->write_buffer.clear();
        return 0;
    }


    int Server::handleClose(int fd) {
        const connection* conn = find_connection(fd, _connection_fds);
        if (conn == nullptr) {
            perror("find_connection()");
            return -1;
        }
        ::close(conn->fd);
        std::erase_if(_connection_fds, [fd](const connection& c) { return c.fd == fd; });
        return 0;
    }


    void Server::cleanup() {
        if (_listen_fd > 0) {
            close(_listen_fd);
        }
        for (const auto& conn : _connection_fds) {
            close(conn.fd);
        }
        io_uring_queue_exit(&_ring);
    }

}
