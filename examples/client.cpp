#include <kosio/signal.hpp>
#include "rpc.hpp"
#include "vrpc/client.hpp"

void error_handler(vrpc::StatusCode code) {
    if (code != vrpc::StatusCode::kOk) {
        LOG_ERROR("failed to handle rpc");
    }
}

auto handle_math_add_response(vrpc::StatusCode code, std::string_view resp_payload) -> kosio::async::Task<void> {
    error_handler(code);

    MathAddResponse response;
    if (!response.ParseFromArray(resp_payload.data(), static_cast<int>(resp_payload.size()))) {
        LOG_ERROR("failed to parse proto message");
        co_return;
    }

    LOG_INFO("get math add result: {}", response.result());
}

auto handle_math_sub_response(vrpc::StatusCode code, std::string_view resp_payload) -> kosio::async::Task<void> {
    error_handler(code);

    MathSubResponse response;
    if (!response.ParseFromArray(resp_payload.data(), static_cast<int>(resp_payload.size()))) {
        LOG_ERROR("failed to parse proto message");
        co_return;
    }

    LOG_INFO("get math sub result: {}", response.result());
}

auto send_math_add_request(vrpc::Client& client, int64_t augend, int64_t addend) -> kosio::async::Task<void> {
    LOG_INFO("i want to know {} + {} = ?", augend, addend);
    MathAddReqeust request;
    request.set_augend(augend);
    request.set_addend(addend);
    co_await client.call(ServiceType::kMath, InvokeType::kMathAdd, request, handle_math_add_response);
}

auto send_math_sub_request(vrpc::Client& client, int64_t minuend, int64_t subtrahend) -> kosio::async::Task<void> {
    LOG_INFO("i want to know {} - {} = ?", minuend, subtrahend);
    MathSubReqeust request;
    request.set_minuend(minuend);
    request.set_subtrahend(subtrahend);
    co_await client.call(ServiceType::kMath, InvokeType::kMathSub, request, handle_math_sub_response);
}

auto shutdown_handler(vrpc::Client& client, uint64_t timeout = 0) -> kosio::async::Task<void> {
    if (timeout == 0) {
        co_await kosio::signal::ctrl_c();
    } else {
        co_await kosio::time::sleep(timeout);
    }
    co_await client.shutdown();
}

auto main_loop() -> kosio::async::Task<void> {
    auto client = vrpc::Client("localhost", 8080);
    co_await send_math_add_request(client, 199, 311);
    co_await send_math_sub_request(client, 36, 21);
    co_await shutdown_handler(client);
}

auto main() -> int {
    kosio::runtime::CurrentThreadBuilder::default_create().block_on(main_loop());
}