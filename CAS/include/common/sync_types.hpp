#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <deque>
#include <vector>
#include "common/logic_val.hpp"

struct WaitingThread {
    size_t pid = (size_t)-1;
    int keys_requested = 0;       // For semaphore.get(keys)
    std::string dest_var_name;    // For mailbox.get(dest_var)
    LogicVal val_to_put;          // For bounded mailbox.put(val)
    bool is_put = false;          // True if thread is waiting on mailbox.put
};

struct SemaphoreObject {
    uint64_t handle_id = 0;
    std::string name;
    int keys = 0;
    std::deque<WaitingThread> waiting_threads;
};

struct MailboxObject {
    uint64_t handle_id = 0;
    std::string name;
    int bound = 0; // 0 = unbounded, >0 = bounded
    std::deque<LogicVal> queue;
    std::deque<WaitingThread> waiting_get_threads;
    std::deque<WaitingThread> waiting_put_threads;
};
