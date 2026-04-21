#pragma once
#include <functional>
#include <kosio/sync.hpp>
#include "vrpc/net/pb/status.hpp"

namespace vrpc {
using Method = std::function<kosio::async::Task<Status::Code>(std::string_view, std::span<char>)>;
} // namespace vrpc