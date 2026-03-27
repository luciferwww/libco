/**
 * @file coxx.hpp
 * @brief libcoxx — C++17 wrapper for libco.
 *
 * Single-header inclusion for the full C++ API:
 *
 *   #include <coxx/coxx.hpp>
 *
 * Components:
 *   co::Scheduler, co::Task        — scheduler.hpp
 *   co::Mutex, co::CondVar,
 *   co::WaitGroup                  — sync.hpp
 *   co::Channel<T>                 — channel.hpp
 *
 * Requirements: C++17, libco (linked via target_link_libraries(... coxx)).
 */
#pragma once

#include <coxx/scheduler.hpp>
#include <coxx/sync.hpp>
#include <coxx/channel.hpp>
