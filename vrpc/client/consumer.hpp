#pragma once
#include <utility>
#include <kosio/net.hpp>
#include <kosio/sync.hpp>
#include <tbb/concurrent_hash_map.h>
#include "vrpc/protocol/header.hpp"

namespace vrpc {
using RpcCallback = std::function<kosio::async::Task<void>(std::string_view resp_payload)>;
class RpcConsumer {
    using RpcCallbackMap = tbb::concurrent_hash_map<uint64_t, RpcCallback>;

private:
    explicit RpcConsumer(const kosio::net::SocketAddr& server_addr, kosio::net::TcpStream stream)
        : server_addr_(server_addr)
        , stream_(std::move(stream))
        , request_buf_(detail::MAX_RPC_MESSAGE_SIZE) { callbacks_.rehash(4096); }

public:
    // Delete copy
    RpcConsumer(const RpcConsumer&) = delete;
    auto operator=(const RpcConsumer&) -> RpcConsumer& = delete;

    // Delete move
    RpcConsumer(RpcConsumer&&) = delete;
    auto operator=(RpcConsumer&&) -> RpcConsumer& = delete;

public:
    [[REMEMBER_CO_AWAIT]]
    static auto create(std::string_view server_ip, uint16_t server_port) -> kosio::async::Task<Result<std::unique_ptr<RpcConsumer>>>;

public:
    [[REMEMBER_CO_AWAIT]]
    auto connect() -> kosio::async::Task<void>;
    [[REMEMBER_CO_AWAIT]]
    auto shutdown() -> kosio::async::Task<void>;
    [[nodiscard]]
    auto server_addr() const noexcept -> kosio::net::SocketAddr { return server_addr_; }

public:
    template <typename Request>
    [[REMEMBER_CO_AWAIT]]
    auto send_request(
        ServiceType service_type,
        MethodType method_type,
        const Request& request,
        const RpcCallback& callback) -> kosio::async::Task<void> {
        co_await mutex_.lock();
        std::lock_guard lock(mutex_, std::adopt_lock);
        if (is_shutdown_) {
            co_return;
        }

        if (!is_connected_) {
            co_await connect();
        }

        if (!is_connected_) {
            co_return;
        }

        auto payload_size = request.ByteSizeLong();

        if (!request.SerializeToArray(request_buf_.data(), static_cast<int>(payload_size))) {
            LOG_ERROR("failed to serialize request");
            co_return;
        }

        detail::FixedRpcRequestHeader req_header;
        req_header.request_id = htobe64(request_id_);
        req_header.payload_size = htobe32(payload_size);
        req_header.service_type = service_type;
        req_header.method_type = method_type;

        auto ret = co_await stream_.write_vectored(
            std::span<const char>(reinterpret_cast<char*>(&req_header), sizeof(req_header)),
            std::span<const char>(request_buf_.data(), payload_size)
        );

        callbacks_.emplace(request_id_, callback);

        if (!ret) {
            LOG_ERROR("failed to send rpc request: {}", ret.error());
            callbacks_.erase(request_id_);
            co_return;
        }

        ++request_id_;
    }

    template <typename Request>
    [[REMEMBER_CO_AWAIT]]
    auto send_request(
        ServiceType service_type,
        MethodType method_type,
        const Request& request,
        RpcCallback&& callback) -> kosio::async::Task<void> {
        co_await mutex_.lock();
        std::lock_guard lock(mutex_, std::adopt_lock);
        if (is_shutdown_) {
            co_return;
        }

        if (!is_connected_) {
            co_await connect();
        }

        if (!is_connected_) {
            co_return;
        }

        auto payload_size = request.ByteSizeLong();

        if (!request.SerializeToArray(request_buf_.data(), static_cast<int>(payload_size))) {
            LOG_ERROR("failed to serialize request");
            co_return;
        }

        detail::FixedRpcRequestHeader req_header;
        req_header.request_id = htobe64(request_id_);
        req_header.payload_size = htobe32(payload_size);
        req_header.service_type = service_type;
        req_header.method_type = method_type;

        auto ret = co_await stream_.write_vectored(
            std::span<const char>(reinterpret_cast<char*>(&req_header), sizeof(req_header)),
            std::span<const char>(request_buf_.data(), payload_size)
        );

        callbacks_.emplace(request_id_, std::move(callback));

        if (!ret) {
            LOG_ERROR("failed to send rpc request: {}", ret.error());
            callbacks_.erase(request_id_);
            co_return;
        }

        ++request_id_;
    }

private:
    [[REMEMBER_CO_AWAIT]]
    auto trigger_callback(uint64_t request_id, std::string_view resp_payload) -> kosio::async::Task<void>;

    [[REMEMBER_CO_AWAIT]]
    auto handle_response_loop() -> kosio::async::Task<void>;

private:
    kosio::sync::Mutex     mutex_;
    bool                   is_connected_{false};
    bool                   is_shutdown_{false};
    kosio::net::SocketAddr server_addr_;
    kosio::net::TcpStream  stream_;
    uint64_t               request_id_{0}; // current request id
    std::vector<char>      request_buf_;
    RpcCallbackMap         callbacks_;
};

using RpcClient = std::unique_ptr<RpcConsumer>;
} // namespace vrpc