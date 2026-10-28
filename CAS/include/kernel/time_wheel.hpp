#pragma once
#include <cstdint>
#include <queue>
#include <vector>

struct SimEvent {
    uint64_t time;
    uint32_t process_id;
    uint32_t order;

    bool operator>(const SimEvent& other) const {
        if (time != other.time) return time > other.time;
        return order > other.order;
    }
};

class TimeWheel {
private:
    std::priority_queue<SimEvent, std::vector<SimEvent>, std::greater<SimEvent>> queue;
    uint32_t order_counter = 0;
    size_t events_dispatched_count = 0;

public:
    TimeWheel() = default;

    void schedule(uint64_t wake_time, uint32_t process_id) {
        queue.push({wake_time, process_id, order_counter++});
    }

    bool empty() const { return queue.empty(); }

    uint64_t next_time() const {
        return queue.top().time;
    }

    SimEvent pop() {
        SimEvent ev = queue.top();
        queue.pop();
        events_dispatched_count++;
        return ev;
    }

    void clear() {
        while (!queue.empty()) queue.pop();
    }

    size_t events_dispatched() const { return events_dispatched_count; }
};