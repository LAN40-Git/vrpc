# vrpc
![Static Badge](https://img.shields.io/badge/standard-c++23-blue?logo=cplusplus) ![Static Badge](https://img.shields.io/badge/platform-linux-orange?logo=linux)

## About

`vrpc` 是一个基于 `io_uring` 和 `c++无栈协程` ([kosio](https://github.com/LAN40-Git/kosio)) 实现的轻量 RPC 通信框架，内置避让策略、连接管理、重试机制等模块，提供异步非阻塞的 RPC 通信


## Getting started

### Installation

```shell
sudo apt update
# 安装依赖
sudo apt install -y build-essential git cmake pkg-config liburing-dev protobuf-compiler libprotobuf-dev

# 安装 kosio
git clone git@github.com:LAN40-Git/kosio.git
cd kosio
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install

# 安装 vrpc
git clone git@github.com:LAN40-Git/vrpc.git
cd vrpc
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

### Uninstallation
```shell
sudo rm -rf /usr/local/include/vrpc
```

## TODO-Lists

- [x] 服务端与客户端基本通信
- [x] 优雅关闭机制
- [ ] RPC 请求超时
- [ ] 自定义客户端退避策略
- [x] 消息大小限制
- [ ] 请求速率限制
- [ ] 实现 IDL，自动定义枚举和方法
- [x] 利用固长消息头解决 TCP 粘包
- [ ] 手动二进制编码（目前依赖于 `protobuf`）
- [ ] 性能测试

## Example
1.`protobuf` 文件

`math.proto`

```protobuf
syntax = "proto3";

message MathAddReqeust {
  int64 augend = 1;
  int64 addend = 2;
}

message MathAddResponse {
  int64 result = 1;
}

message MathSubReqeust {
  int64 minuend = 1;
  int64 subtrahend = 2;
}

message MathSubResponse {
  int64 result = 1;
}
```

这里有两个 RPC 服务，分别是加法和减法操作。

2.定义枚举

由于没有IDL，所以需要手动枚举上述 RPC 服务和调用

`rpc.hpp`

```c++
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
```

3.服务端

`server.cpp`

```c++
#include "rpc.hpp"
#include "vrpc/server.hpp"

#include <kosio/signal/signal.hpp>

vrpc::Server server(8080);

auto handle_math_add_request(std::string_view req_payload, std::span<char> resp_payload) -> kosio::async::Task<vrpc::InvokeResult> {
    MathAddReqeust request;
    if (!request.ParseFromArray(req_payload.data(), static_cast<int>(req_payload.size()))) {
        co_return vrpc::make_result(vrpc::StatusCode::kUnknown);
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
        co_return vrpc::make_result(vrpc::StatusCode::kUnknown);
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
```

4.客户端

`client.cpp`

```c++
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
```

5.编译运行

客户端得到结果

![image-20260419205713699](/home/lan/CLionProjects/vrpc/.typora/image-20260419205713699.png)
