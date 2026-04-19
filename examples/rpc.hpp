#pragma once
#include "vrpc/core/type.hpp"
#include "pb/math.pb.h"

enum class ServiceType : vrpc::Type {
    kMath
};

enum class InvokeType : vrpc::Type {
    kMathAdd,
    kMathSub
};