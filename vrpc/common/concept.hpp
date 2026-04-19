#pragma once
#include <type_traits>
#include <google/protobuf/message.h>
#include "vrpc/core/type.hpp"

namespace vrpc {
template <typename T>
concept VrpcType = std::is_enum_v<T> &&
                   std::is_same_v<std::underlying_type_t<T>, Type>;

template <typename T>
    concept Proto = std::is_base_of_v<google::protobuf::Message, T>;
} // namespace vrpc