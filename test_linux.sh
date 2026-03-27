#!/bin/bash
# 只跑客户端，不启动/停止服务端
cd /mnt/c/dev/cxx-coroutine/libco
HOST=${1:-127.0.0.1}
PORT=${2:-7777}
CLIENTS=${3:-5}
./build-linux/examples/demo_echo_client $HOST $PORT $CLIENTS
