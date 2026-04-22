[TOC]

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
# 默认安装路径是 /usr/loacl/include/vrpc
sudo make install
```

### Uninstallation
```shell
sudo rm -rf /usr/local/include/vrpc
```

## TODO-Lists

- [x] 服务端与客户端基本通信
- [x] 优雅关闭机制
- [x] 自主设计 RPC 报文
- [ ] RPC 请求超时
- [x] 客户端退避策略
- [x] 消息大小限制
- [x] 自定义配置
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

2.服务端

由于没有兼容 protobuf 的反射机制，目前不支持一次性注册所有服务，需要手动指定 protobuf 的请求和回复类型，有些蛋疼，但目前的实现可读性也挺强

`server.cpp`

```c++
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
        .set_thread_nums(4)
        .build()
        .register_method<MathAddRequest, MathAddResponse>("math", "add", add)
        .register_method<MathSubRequest, MathSubResponse>("math", "sub", sub)
        .wait();
}
```

3.客户端

客户端的 `call_method` 方法几乎不阻塞当前协程，可以放心 `co_await`

`client.cpp`

```c++
// an simple rpc tcp client
// ctrl + c to close
#include <kosio/signal/signal.hpp>
#include "../api/mathpb/math.pb.h"
#include "vrpc/net/builder.hpp"

auto main_coro() -> kosio::async::Task<void> {
    auto rpc_client = vrpc::TcpClientBuilder::options()
        .set_ip("127.0.0.1")
        .set_port(8080)
        .build();

    // 模拟 RPC 调用
    MathAddRequest add_request;
    add_request.set_augend(123);
    add_request.set_addend(456);
    co_await rpc_client.call_method<MathAddRequest, MathAddResponse>(
        "math", "add", add_request,
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
        "math", "sub", sub_request,
        [](const vrpc::Status& status, const MathSubResponse& response) -> kosio::async::Task<void> {
            if (!status.ok()) {
                LOG_ERROR("{}", status.message());
                co_return;
            }

            LOG_INFO("get math.sub result {}", response.result());
            co_return;
        });
    co_await kosio::time::sleep(3000);

    // 优雅关闭
    co_await rpc_client.shutdown();
}

auto main() -> int {
    kosio::runtime::MultiThreadBuilder::default_create().block_on(main_coro());
}
```
