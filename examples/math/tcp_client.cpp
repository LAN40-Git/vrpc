#include <kosio/signal/signal.hpp>
#include "../api/mathpb/math.pb.h"
#include "vrpc/net/tcp/tcp_client.hpp"

auto main_coro() -> kosio::async::Task<void> {
    auto rpc_client = vrpc::TcpClient{"127.0.0.1", 8080};
    // 模拟 RPC 调用循环
    while (true) {
        MathAddRequest add_request;
        add_request.set_augend(123);
        add_request.set_addend(456);
        co_await rpc_client.call_method<MathAddRequest, MathAddResponse>(
            "math",
            "add",
            add_request,
            [](const vrpc::Status& status, const MathAddResponse& response) -> kosio::async::Task<void> {
                if (!status.ok()) {
                    LOG_ERROR("{}", status.message());
                    co_return;
                }

                LOG_INFO("get math.add result {}", response.result());
                co_return;
            });

        MathSubRequest sub_request;
        sub_request.set_minuend(456);
        sub_request.set_subtrahend(123);
        co_await rpc_client.call_method<MathSubRequest, MathSubResponse>(
            "math",
            "sub",
            sub_request,
            [](const vrpc::Status& status, const MathSubResponse& response) -> kosio::async::Task<void> {
                if (!status.ok()) {
                    LOG_ERROR("{}", status.message());
                    co_return;
                }

                LOG_INFO("get math.sub result {}", response.result());
                co_return;
            });
        LOG_INFO("sleep for 3s");
        co_await kosio::time::sleep(3000);
    }
}

auto main() -> int {
    kosio::runtime::MultiThreadBuilder::default_create().block_on(main_coro());
}