#include "kernel/trace_buffer.hpp"
#include <iostream>
#include <chrono>
#include <cstring>
#include <algorithm>

AsyncTraceBuffer::AsyncTraceBuffer()
    : ring_(std::make_unique<TracePacket[]>(RING_SIZE)) {
}

AsyncTraceBuffer::~AsyncTraceBuffer() {
    stop();
}

AsyncTraceBuffer& AsyncTraceBuffer::instance() {
    static AsyncTraceBuffer inst;
    return inst;
}

void AsyncTraceBuffer::start() {
    if (is_running_.exchange(true)) return;
    stop_worker_.store(false, std::memory_order_release);
    write_head_.store(0, std::memory_order_relaxed);
    read_tail_.store(0, std::memory_order_relaxed);
    worker_thread_ = std::thread(&AsyncTraceBuffer::worker_loop, this);
}

void AsyncTraceBuffer::stop() {
    if (!is_running_.exchange(false)) return;
    stop_worker_.store(true, std::memory_order_release);
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void AsyncTraceBuffer::register_fd(int fd, std::shared_ptr<std::ostream> stream) {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    streams_[fd] = stream;
}

void AsyncTraceBuffer::unregister_fd(int fd) {
    flush();
    std::lock_guard<std::mutex> lock(stream_mutex_);
    streams_.erase(fd);
}

bool AsyncTraceBuffer::push(const TracePacket& pkt) {
    if (!is_running_.load(std::memory_order_relaxed)) return false;
    size_t head = write_head_.load(std::memory_order_relaxed);
    size_t tail = read_tail_.load(std::memory_order_acquire);

    if (head - tail >= RING_SIZE) {
        // Buffer full - yield briefly
        std::this_thread::yield();
        tail = read_tail_.load(std::memory_order_acquire);
        if (head - tail >= RING_SIZE) {
            return false;
        }
    }

    ring_[head & RING_MASK] = pkt;
    write_head_.store(head + 1, std::memory_order_release);
    return true;
}

void AsyncTraceBuffer::log_string(int fd, const std::string& str) {
    size_t offset = 0;
    while (offset < str.size()) {
        TracePacket pkt;
        pkt.fd = fd;
        pkt.packet_type = TracePacketType::LOG_STRING;
        size_t chunk = std::min(str.size() - offset, sizeof(pkt.text) - 1);
        std::memcpy(pkt.text, str.data() + offset, chunk);
        pkt.text[chunk] = '\0';
        pkt.text_len = static_cast<uint32_t>(chunk);
        push(pkt);
        offset += chunk;
    }
}

void AsyncTraceBuffer::log_char(int fd, char ch) {
    TracePacket pkt;
    pkt.fd = fd;
    pkt.packet_type = TracePacketType::UART_CHAR;
    pkt.text[0] = ch;
    pkt.text[1] = '\0';
    pkt.text_len = 1;
    push(pkt);
}

void AsyncTraceBuffer::log_core_trace(uint64_t timestamp, uint32_t pc, uint32_t insn, uint32_t reg_addr, uint64_t reg_wdata) {
    TracePacket pkt;
    pkt.timestamp = timestamp;
    pkt.pc = pc;
    pkt.insn = insn;
    pkt.reg_addr = reg_addr;
    pkt.reg_wdata = reg_wdata;
    pkt.packet_type = TracePacketType::CORE_TRACE;
    push(pkt);
}

void AsyncTraceBuffer::flush() {
    while (write_head_.load(std::memory_order_acquire) != read_tail_.load(std::memory_order_relaxed)) {
        std::this_thread::yield();
    }
    std::lock_guard<std::mutex> lock(stream_mutex_);
    for (auto& kv : streams_) {
        if (kv.second) kv.second->flush();
    }
    std::cout.flush();
}

void AsyncTraceBuffer::worker_loop() {
    while (!stop_worker_.load(std::memory_order_relaxed) ||
           (read_tail_.load(std::memory_order_relaxed) != write_head_.load(std::memory_order_acquire))) {
        size_t tail = read_tail_.load(std::memory_order_relaxed);
        size_t head = write_head_.load(std::memory_order_acquire);

        if (tail == head) {
            std::this_thread::yield();
            continue;
        }

        while (tail != head) {
            const auto& pkt = ring_[tail & RING_MASK];
            std::shared_ptr<std::ostream> target_stream;
            {
                std::lock_guard<std::mutex> lock(stream_mutex_);
                auto it = streams_.find(pkt.fd);
                if (it != streams_.end()) {
                    target_stream = it->second;
                }
            }

            if (pkt.packet_type == TracePacketType::LOG_STRING || pkt.packet_type == TracePacketType::UART_CHAR) {
                if (pkt.fd == 1) {
                    std::cout.write(pkt.text, pkt.text_len);
                } else if (target_stream) {
                    target_stream->write(pkt.text, pkt.text_len);
                    if (std::memchr(pkt.text, '\n', pkt.text_len) != nullptr) {
                        target_stream->flush();
                    }
                }
            } else if (pkt.packet_type == TracePacketType::CORE_TRACE) {
                if (target_stream) {
                    *target_stream << pkt.timestamp << "\t" << pkt.pc << "\t" << pkt.insn << "\t"
                                   << pkt.reg_addr << "\t" << pkt.reg_wdata << "\n";
                }
            }

            tail++;
        }
        read_tail_.store(tail, std::memory_order_release);
    }

    std::lock_guard<std::mutex> lock(stream_mutex_);
    for (auto& kv : streams_) {
        if (kv.second) kv.second->flush();
    }
    std::cout.flush();
}
