#!/bin/bash
set -e
cd /mnt/c/dev/cxx-coroutine/libco

echo "=== 编译 ==="
make -C build-linux 2>&1 | grep -E "error:|warning:|Linking|Built target" || true

echo ""
echo "=== 单元测试 ==="
TESTS=(
    build-linux/tests/unit/test_context
    build-linux/tests/unit/test_allocator
    build-linux/tests/unit/test_stack_pool
    build-linux/tests/unit/test_routine
    build-linux/tests/unit/test_scheduler
    build-linux/tests/unit/test_timer
    build-linux/tests/unit/test_sleep
    build-linux/tests/unit/test_example_c
    build-linux/tests/unit/test_example_cpp
)

PASS=0
FAIL=0
for t in "${TESTS[@]}"; do
    name=$(basename $t)
    if $t > /tmp/${name}.log 2>&1; then
        echo "  [PASS] $name"
        PASS=$((PASS+1))
    else
        echo "  [FAIL] $name"
        cat /tmp/${name}.log
        FAIL=$((FAIL+1))
    fi
done

echo ""
echo "=== 集成测试 (echo) ==="
pkill -9 -f demo_echo_server 2>/dev/null || true; sleep 0.3
./build-linux/examples/demo_echo_server 7777 >/tmp/echo_server.log 2>&1 &
S=$!
sleep 1
./build-linux/examples/demo_echo_client 127.0.0.1 7777 5
kill $S 2>/dev/null

echo ""
echo "=== 结果 ==="
echo "  单元测试: ${PASS} passed, ${FAIL} failed"
