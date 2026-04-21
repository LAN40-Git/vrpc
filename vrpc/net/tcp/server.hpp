#pragma once
#include "vrpc/net/pb/status.hpp"
#include "vrpc/net/tcp/detail/server_cache.hpp"
#include "vrpc/net/tcp/detail/manager.hpp"

namespace vrpc {
using Method = std::function<kosio::async::Task<RpcResponseMessage>(const RpcRequestMessage& request)>;
class TcpServer {
public:
    explicit TcpServer(const detail::Config& config)
        : config_(config) {
        auto has_addr = kosio::net::SocketAddr::parse(config_.ip, config_.port);
        if (!has_addr) {
            LOG_ERROR("{}", has_addr.value());
        } else {
            addr_ = has_addr.value();
        }
    }

    // Delete copy
    TcpServer(const TcpServer&) = delete;
    auto operator=(const TcpServer&) -> TcpServer& = delete;

    // Delete move
    TcpServer(TcpServer&&) = delete;
    auto operator=(TcpServer&&) -> TcpServer& = delete;

public:
    auto port() const -> uint16_t {
        return config_.port;
    }

public:
    void register_method(
        const std::string& service_name,
        const std::string& method_name,
        const Method& method) {
        services_[service_name][method_name] = method;
    }

public:
    void wait() {
        kosio::runtime::MultiThreadBuilder::options()
            .set_num_workers(config_.thread_nums)
            .build()
            .block_on(process());
    }

    [[REMEMBER_CO_AWAIT]]
    auto shutdown() -> kosio::async::Task<void> {
        is_shutdown_.store(true, std::memory_order_release);
        co_await kosio::net::TcpStream::connect(addr_);

        while (!manager_.empty()) {
            co_await manager_.cancel_all();
            co_await kosio::time::sleep(detail::SHUT_DOWN_WAITING_INTERVAL);
        }
        LOG_INFO("vrpc server on {} stop", addr_);
        co_await latch_.arrive_and_wait();
    }

private:
    auto process() -> kosio::async::Task<void> {
        auto has_listener = kosio::net::TcpListener::bind(addr_);
        if (!has_listener) {
            LOG_ERROR("{}", has_listener.error());
            co_return;
        }
        auto listener = std::move(has_listener.value());

        is_shutdown_.store(false, std::memory_order_release);
        LOG_INFO("vrpc tcp server listening on {}", addr_);
        while (!is_shutdown_.load(std::memory_order_acquire)) {
            auto has_stream = co_await listener.accept();
            if (!has_stream) {
                continue;
            }
            auto& [stream, peer_addr] = has_stream.value();
            if (auto ret = stream.set_nodelay(true); !ret) {
                LOG_ERROR("{}", ret.error());
                continue;
            }
            auto conn = co_await manager_.assign(config_, peer_addr, std::move(stream));
            if (!conn) {
                continue;
            }
            LOG_INFO("accept connection from {}", peer_addr);
            kosio::spawn(handle_request_loop(conn));
            kosio::spawn(send_response_loop(conn));
        }
        co_await listener.close();
        co_await latch_.arrive_and_wait();
    }

    static auto handle_request_loop(std::shared_ptr<detail::Connection> conn) -> kosio::async::Task<void> {
        auto& stream = conn->stream_;
        auto& sender = conn->sender_;
        auto& buf = conn->req_buf_;

        detail::RpcMessageHeader header;
        while (true) {
            // 读取请求报文头
            if (auto ret = co_await stream.read_exact(
            {reinterpret_cast<char*>(&header), sizeof(header)}); !ret) {
                break;
            }

            // 校验消息大小
            auto msg_size = be32toh(header.msg_size);
            if (msg_size > detail::MAX_RPC_MESSAGE_SIZE) {
                LOG_ERROR("rpc response message too large");
                break;
            }

            // 读取请求报文
            if (auto ret = co_await stream.read_exact({buf.data(), msg_size}); !ret) {
                LOG_ERROR("{}", ret.error());
                break;
            }

            // 解析请求报文
            auto has_request = RpcRequestMessage::parse_from(buf.data(), msg_size);
            if (!has_request) {
                LOG_ERROR("rpc request message parse failed");
                break;
            }
            auto request = std::move(has_request.value());

            if (auto ret = co_await sender.send(request); !ret) {
                LOG_ERROR("{}", ret.error());
                break;
            }
        }

        LOG_INFO("connection from {} closed", conn->addr_);
        sender.close();
    }

    auto send_response_loop(std::shared_ptr<detail::Connection> conn) -> kosio::async::Task<void> {
        auto& stream = conn->stream_;
        auto& receiver = conn->receiver_;
        auto cache = std::make_shared<detail::ServerCache>(conn->addr_);

        detail::RpcMessageHeader header;
        while (true) {
            auto has_request = co_await receiver.recv();
            if (!has_request) {
                break;
            }
            auto request = std::move(has_request.value());
            // 更新缓存
            detail::set_server_cache(cache);
            // 进行 RPC 调用，获取回复报文
            auto response = co_await do_call(request);
            header.msg_size = htobe32(response.bytes_size());
            auto status_code = response.status_.code();
            // 转为网络字节序
            response.htobe();

            // 发送回复报文
            auto ret = co_await stream.write_vectored(
                std::span<const char>(reinterpret_cast<char*>(&header), sizeof(header)),
                std::span<const char>(reinterpret_cast<char*>(&response.seq_), sizeof(response.seq_)),
                std::span<const char>(reinterpret_cast<char*>(&status_code), sizeof(status_code)),
                std::span<const char>(reinterpret_cast<char*>(&response.err_msg_size_), sizeof(response.err_msg_size_)),
                response.err_msg_,
                std::span<const char>(reinterpret_cast<char*>(&response.payload_size_), sizeof(response.payload_size_)),
                response.payload_,
                std::span<const char>(reinterpret_cast<char*>(&response.check_sum_), sizeof(response.check_sum_))
            );

            if (!ret) {
                LOG_ERROR("[{}] send rpc response message failed: {}", conn->addr_, ret.error());
            }
        }
        manager_.remove(conn->addr_.to_string());
    }

    [[REMEMBER_CO_AWAIT]]
    auto do_call(const RpcRequestMessage& request) -> kosio::async::Task<RpcResponseMessage> {
        auto service_it = services_.find(request.service_name_);
        if (service_it == services_.end()) {
            auto err_msg = std::format("rpc service {} not found", request.service_name_);
            RpcResponseMessage response{request.seq_, Status::kNotFound, err_msg, ""};
        }
        auto method_it = service_it->second.find(request.method_name_);
        if (method_it == service_it->second.end()) {
            auto err_msg = std::format("rpc service {} not found", request.service_name_);
            RpcResponseMessage response{request.seq_, Status::kNotFound, err_msg, ""};
        }
        auto& method = method_it->second;
        co_return co_await method(request);
    }

private:
    using MethodMap = std::unordered_map<std::string, Method>;
    using ServiceMap = std::unordered_map<std::string, MethodMap>;

    kosio::sync::Latch        latch_{2};
    std::atomic<bool>         is_shutdown_{true};
    ServiceMap                services_;
    detail::ConnectionManager manager_;
    detail::Config            config_;
    kosio::net::SocketAddr    addr_{};
};
} // namespace vrpc