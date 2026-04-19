#include "rpc.hpp"
#include "vrpc/server.hpp"

#include <kosio/signal/signal.hpp>

vrpc::Server server(8080);

auto handle_math_add_request(std::string_view req_payload, std::span<char> resp_payload) -> kosio::async::Task<vrpc::InvokeResult> {
    MathAddReqeust request;
    if (!request.ParseFromArray(req_payload.data(), static_cast<int>(req_payload.size()))) {
        co_return vrpc::make_result(vrpc::StatusCode::kInternal);
    }

    auto augend = request.augend();
    auto addend = request.addend();
    auto result = augend + addend;

    MathAddResponse response;
    response.set_result(result);
    co_return vrpc::make_result(response, resp_payload);
}

auto handle_math_sub_request(std::string_view req_payload, std::span<char> resp_payload) -> kosio::async::Task<vrpc::InvokeResult> {
    MathSubReqeust request;
    if (!request.ParseFromArray(req_payload.data(), static_cast<int>(req_payload.size()))) {
        co_return vrpc::make_result(vrpc::StatusCode::kInternal);
    }

    auto minuend = request.minuend();
    auto subtrahend = request.subtrahend();
    auto result = minuend - subtrahend;

    MathSubResponse response;
    response.set_result(result);
    co_return vrpc::make_result(response, resp_payload);
}

auto shutdown_handler(uint64_t timeout = 0) -> kosio::async::Task<void> {
    if (timeout == 0) {
        co_await kosio::signal::ctrl_c();
    } else {
        co_await kosio::time::sleep(timeout);
    }
    co_await server.shutdown();
}

auto main_loop() -> kosio::async::Task<void> {
    server.register_invoke(ServiceType::kMath, InvokeType::kMathAdd, handle_math_add_request);
    server.register_invoke(ServiceType::kMath, InvokeType::kMathSub, handle_math_sub_request);
    kosio::spawn(shutdown_handler());
    co_await server.wait();
}

auto main() -> int {
    kosio::runtime::CurrentThreadBuilder::default_create().block_on(main_loop());
}