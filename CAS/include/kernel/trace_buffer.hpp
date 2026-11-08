#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <ostream>
#include <unordered_map>
#include <mutex>

enum class TracePacketType : uint8_t {
    LOG_STRING = 0,
    UART_CHAR = 1,
    CORE_TRACE = 2,
    NET_CHANGE = 3
};

struct TracePacket {
    uint64_t timestamp = 0;
    uint32_t pc = 0;
    uint32_t insn = 0;
    uint32_t reg_addr = 0;
    uint64_t reg_wdata = 0;
    uint32_t net_id = 0;
    uint64_t net_val = 0;
    int fd = 0;
    uint32_t text_len = 0;
    TracePacketType packet_type = TracePacketType::LOG_STRING;
    char text[128] = {0};
};

class AsyncTraceBuffer {
public:
    static constexpr size_t RING_SIZE = 1048576; // 1M packets (power of 2)
    static constexpr size_t RING_MASK = RING_SIZE - 1;

private:
    std::unique_ptr<TracePacket[]> ring_;
    alignas(64) std::atomic<size_t> write_head_{0};
    alignas(64) std::atomic<size_t> read_tail_{0};
    alignas(64) std::atomic<bool> stop_worker_{false};
    std::atomic<bool> is_running_{false};
    std::thread worker_thread_;

    std::mutex stream_mutex_;
    std::unordered_map<int, std::shared_ptr<std::ostream>> streams_;

    void worker_loop();

    AsyncTraceBuffer();

public:
    ~AsyncTraceBuffer();
    AsyncTraceBuffer(const AsyncTraceBuffer&) = delete;
    AsyncTraceBuffer& operator=(const AsyncTraceBuffer&) = delete;

    static AsyncTraceBuffer& instance();

    void start();
    void stop();
    bool is_running() const { return is_running_.load(std::memory_order_relaxed); }

    void register_fd(int fd, std::shared_ptr<std::ostream> stream);
    void unregister_fd(int fd);

    bool push(const TracePacket& pkt);
    void log_string(int fd, const std::string& str);
    void log_char(int fd, char ch);
    void log_core_trace(uint64_t timestamp, uint32_t pc, uint32_t insn, uint32_t reg_addr, uint64_t reg_wdata);
    void flush();
};
