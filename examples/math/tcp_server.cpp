// an simple rpc tcp server
// ctrl + c to close
#include <kosio/signal/signal.hpp>
#include "../api/mathpb/math.pb.h"
#include "vrpc/net/builder.hpp"

auto add(const MathAddRequest& request) -> kosio::async::Task<MathAddResponse> {
    auto augend = request.augend();
    auto addend = request.addend();
    MathAddResponse response;
    response.set_result(augend + addend);
    co_return response;
}

auto sub(const MathSubRequest& request) -> kosio::async::Task<MathSubResponse> {
    auto minuend = request.minuend();
    auto subtrahend = request.subtrahend();
    MathSubResponse response;
    response.set_result(minuend - subtrahend);
    co_return response;
}

auto main() -> int {
    vrpc::TcpServerBuilder::options()
        .set_ip("0.0.0.0")
        .set_port(8080)
        .build()
        .register_method<MathAddRequest, MathAddResponse>("math", "add", add)
        .register_method<MathSubRequest, MathSubResponse>("math", "sub", sub)
        .wait();
}