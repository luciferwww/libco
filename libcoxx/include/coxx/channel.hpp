/**
 * @file channel.hpp
 * @brief co::Channel<T> - Go-style typed channel for inter-coroutine communication.
 *
 * Wraps co_channel_t with a type-safe C++ interface.
 *
 * REQUIREMENT: T must be trivially copyable.
 *   The C layer uses memcpy() to move values into and out of the internal
 *   ring buffer. Types with non-trivial copy semantics (e.g. std::string,
 *   std::vector, any type with a user-defined copy constructor) will cause
 *   a compile-time error.
 *
 *   To send non-trivially-copyable values, wrap them:
 *     co::Channel<std::shared_ptr<MyClass>> ch(16);
 *
 * Range-for support (C++17, no dependency on std::default_sentinel_t):
 *   co::Channel<int> ch(8);
 *   // ... producer sends values and calls ch.close() ...
 *   for (int v : ch) { process(v); }  // exits automatically on close + drain
 */
#pragma once

#include <optional>
#include <stdexcept>
#include <type_traits>
#include <libco/co_sync.h>

namespace co {

template<typename T>
class Channel {
    static_assert(
        std::is_trivially_copyable_v<T>,
        "co::Channel<T> requires T to be trivially copyable "
        "(the C layer uses memcpy to copy values into the ring buffer). "
        "Use co::Channel<std::shared_ptr<T>> to send non-trivially-copyable types."
    );

public:
    /**
     * @param capacity Buffer size. 0 = unbuffered rendezvous channel:
     *                 send() blocks until a matching recv() is ready.
     */
    explicit Channel(size_t capacity = 0)
        : ch_(co_channel_create(sizeof(T), capacity))
    {
        if (!ch_) throw std::runtime_error("co::Channel: co_channel_create failed");
    }

    ~Channel() { if (ch_) co_channel_destroy(ch_); }

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // Send

    /**
     * @brief Send a value (blocks until space is available or a receiver is ready).
     * @return true on success; false if the channel has been closed.
     */
    bool send(const T& value) {
        return co_channel_send(ch_, &value) == CO_OK;
    }

    bool send(T&& value) {
        return co_channel_send(ch_, &value) == CO_OK;
    }

    /**
     * @brief Non-blocking send.
     * @return true if the value was accepted immediately; false if the buffer
     *         is full or the channel is closed.
     */
    bool try_send(const T& value) {
        return co_channel_trysend(ch_, &value) == CO_OK;
    }

    /**
     * @brief Close the channel.
     *
     * After closing:
     *   - send() returns false.
     *   - recv() returns values still in the buffer, then nullopt.
     *   - Blocked senders/receivers are woken and get their error codes.
     */
    void close() { co_channel_close(ch_); }

    // Receive

    /**
     * @brief Receive a value (blocks until data is available or the channel closes).
     * @return The received value, or nullopt if the channel is closed and empty.
     */
    std::optional<T> recv() {
        T val;
        if (co_channel_recv(ch_, &val) == CO_OK) return val;
        return std::nullopt;  // CO_ERROR_CLOSED
    }

    /**
     * @brief Non-blocking receive.
     * @return The received value, or nullopt if no value is immediately available
     *         (buffer empty, no waiting sender, or channel closed+empty).
     */
    std::optional<T> try_recv() {
        T val;
        if (co_channel_tryrecv(ch_, &val) == CO_OK) return val;
        return std::nullopt;
    }

    // State

    size_t len()       const { return co_channel_len(ch_); }
    size_t capacity()  const { return co_channel_cap(ch_); }
    bool   is_closed() const { return co_channel_is_closed(ch_); }

    co_channel_t* native_handle() noexcept { return ch_; }

    // Range-for support

    /// Sentinel type (C++17 compatible, does not require std::default_sentinel_t).
    struct Sentinel {};

    struct iterator {
        Channel<T>*      ch;
        std::optional<T> current;

        iterator& operator++()      { current = ch->recv(); return *this; }
        T&        operator*()       { return *current; }
        bool operator!=(Sentinel)   const { return current.has_value(); }
    };

    /// Calls recv() immediately to prime the first value.
    iterator begin() { return iterator{this, recv()}; }
    Sentinel end()   { return {}; }

private:
    co_channel_t* ch_;
};

} // namespace co
