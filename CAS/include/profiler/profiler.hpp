#pragma once
#include <cstddef>
#include <string>
#include <iostream>
#include <chrono>

struct EngineStats {
    size_t net_count = 0;
    size_t net_cap = 0;
    size_t proc_count = 0;
    size_t proc_cap = 0;
    size_t slice_count = 0;
    size_t slice_cap = 0;
    size_t gate_count = 0;
    size_t gate_cap = 0;
    size_t ca_count = 0;
    size_t ca_cap = 0;
    size_t ram_count = 0;
    size_t ram_total_words = 0;
    size_t net_map_keys = 0;
    size_t net_map_buckets = 0;
    float  net_map_load = 0.0f;
    size_t events_dispatched = 0;
    size_t delta_cycles = 0;
    size_t nba_commits = 0;
};

struct ProfileMetrics {
    bool profiling_enabled = false;
    bool debug_enabled = false;
    bool vcd_enabled = false;
    double wallclock_ms = 0.0;
    double user_cpu_ms = 0.0;
    double sys_cpu_ms = 0.0;
    double total_cpu_ms = 0.0;
    double cpu_util_pct = 0.0;
    long peak_rss_kb = 0;
    size_t source_bytes = 0;
    size_t modules_count = 0;
    EngineStats engine_stats;
};

class Profiler {
private:
    std::chrono::high_resolution_clock::time_point start_time;
    bool enabled;
    bool debug_mode;
    bool vcd_mode;
    size_t source_bytes_total = 0;
    size_t modules_parsed_total = 0;

public:
    explicit Profiler(bool enable = true, bool debug = true, bool vcd = true);

    void start();
    void set_source_metrics(size_t bytes, size_t modules);
    ProfileMetrics sample(const EngineStats& stats);

    static void print_terminal_report(const ProfileMetrics& m, std::ostream& out = std::cout);
    static void emit_json_telemetry(const ProfileMetrics& m, std::ostream& out = std::cout);
};