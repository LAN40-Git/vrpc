#pragma once
#include <type_traits>
#include <google/protobuf/message.h>

namespace vrpc {
template <typename T>
    concept ProtobufMessage = std::is_base_of_v<google::protobuf::Message, T>;
} // namespace vrpc