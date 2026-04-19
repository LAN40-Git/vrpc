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



### Example





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
