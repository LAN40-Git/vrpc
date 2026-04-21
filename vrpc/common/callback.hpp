#pragma once
#include <functional>
#include <kosio/sync.hpp>
#include "vrpc/net/pb/status.hpp"

namespace vrpc {
using RpcCallback = std::function<kosio::async::Task<void>(Status::Code, std::string_view)>;
} // namespace vrpc