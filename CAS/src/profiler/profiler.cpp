#include "../../include/profiler/profiler.hpp"
#include <iomanip>
#include <sys/resource.h>

Profiler::Profiler(bool enable, bool debug, bool vcd)
    : enabled(enable), debug_mode(debug), vcd_mode(vcd) {}

void Profiler::start() {
    start_time = std::chrono::high_resolution_clock::now();
}

void Profiler::set_source_metrics(size_t bytes, size_t modules) {
    source_bytes_total = bytes;
    modules_parsed_total = modules;
}

ProfileMetrics Profiler::sample(const EngineStats& stats) {
    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed_wall_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    ProfileMetrics m;
    m.profiling_enabled = enabled;
    m.debug_enabled = debug_mode;
    m.vcd_enabled = vcd_mode;
    m.wallclock_ms = elapsed_wall_ms;
    m.source_bytes = source_bytes_total;
    m.modules_count = modules_parsed_total;
    m.engine_stats = stats;

    if (enabled) {
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);

        m.user_cpu_ms = (usage.ru_utime.tv_sec * 1000.0) + (usage.ru_utime.tv_usec / 1000.0);
        m.sys_cpu_ms  = (usage.ru_stime.tv_sec * 1000.0) + (usage.ru_stime.tv_usec / 1000.0);
        m.total_cpu_ms = m.user_cpu_ms + m.sys_cpu_ms;
        m.cpu_util_pct = (elapsed_wall_ms > 0) ? (m.total_cpu_ms / elapsed_wall_ms) * 100.0 : 0.0;
        m.peak_rss_kb = usage.ru_maxrss;
    }

    return m;
}

void Profiler::print_terminal_report(const ProfileMetrics& m, std::ostream& out) {
    const auto& s = m.engine_stats;

    out << "\n========================================================\n";
    out << "          CAS ENGINE PROFILER & DATA TOPOLOGY           \n";
    out << "========================================================\n";
    out << "[DATA STRUCTURE METRICS]\n";
    out << "  * Netlist Vector         : " << s.net_count << " / " << s.net_cap << " capacity\n";
    out << "  * Processes Vector       : " << s.proc_count << " / " << s.proc_cap << " capacity\n";
    out << "  * Interconnect Slices    : " << s.slice_count << " / " << s.slice_cap << " capacity\n";
    out << "  * Gate Primitives        : " << s.gate_count << " / " << s.gate_cap << " capacity\n";
    out << "  * Continuous Assigns     : " << s.ca_count << " / " << s.ca_cap << " capacity\n";
    out << "  * Memory Array Storage   : " << s.ram_count << " arrays (" << s.ram_total_words << " words)\n";
    out << "\n[HASH TABLE METRICS]\n";
    out << "  * Net Identifier Table   : " << s.net_map_keys << " keys, " << s.net_map_buckets << " buckets, load factor: "
        << std::fixed << std::setprecision(2) << s.net_map_load << "\n";
    out << "\n[DISCRETE-EVENT SCHEDULER STATISTICS]\n";
    out << "  * Time Wheel Events Run  : " << s.events_dispatched << " events\n";
    out << "  * Combinational Passes   : " << s.delta_cycles << " delta cycles\n";
    out << "  * NBA Commit Iterations  : " << s.nba_commits << " sync rounds\n";

    if (m.profiling_enabled) {
        out << "\n[RESOURCE USAGE & EXECUTION METRICS]\n";
        out << "  * Wallclock Real Time    : " << std::fixed << std::setprecision(2) << m.wallclock_ms << " ms\n";
        out << "  * User CPU Time          : " << m.user_cpu_ms << " ms\n";
        out << "  * System Kernel CPU Time : " << m.sys_cpu_ms << " ms\n";
        out << "  * Total CPU Core Time    : " << m.total_cpu_ms << " ms ("
            << std::setprecision(1) << m.cpu_util_pct << "% CPU efficiency)\n";
        out << "  * Peak Resident RAM (RSS): " << (m.peak_rss_kb / 1024.0) << " MB (" << m.peak_rss_kb << " KB)\n";
    }
    out << "========================================================\n";
}

void Profiler::emit_json_telemetry(const ProfileMetrics& m, std::ostream& out) {
    const auto& s = m.engine_stats;

    out << "\n---BEGIN_CAS_TELEMETRY---\n";
    out << "{\n";
    out << "  \"profiling_enabled\": " << (m.profiling_enabled ? "true" : "false") << ",\n";
    out << "  \"debug_enabled\": " << (m.debug_enabled ? "true" : "false") << ",\n";
    out << "  \"vcd_enabled\": " << (m.vcd_enabled ? "true" : "false") << ",\n";
    out << "  \"wallclock_ms\": " << m.wallclock_ms << ",\n";
    out << "  \"user_cpu_ms\": " << m.user_cpu_ms << ",\n";
    out << "  \"sys_cpu_ms\": " << m.sys_cpu_ms << ",\n";
    out << "  \"cpu_util_pct\": " << m.cpu_util_pct << ",\n";
    out << "  \"peak_rss_kb\": " << m.peak_rss_kb << ",\n";
    out << "  \"source_bytes\": " << m.source_bytes << ",\n";
    out << "  \"modules_count\": " << m.modules_count << ",\n";
    out << "  \"nets_count\": " << s.net_count << ",\n";
    out << "  \"nets_cap\": " << s.net_cap << ",\n";
    out << "  \"procs_count\": " << s.proc_count << ",\n";
    out << "  \"slices_count\": " << s.slice_count << ",\n";
    out << "  \"rams_count\": " << s.ram_count << ",\n";
    out << "  \"net_map_load\": " << s.net_map_load << ",\n";
    out << "  \"events_dispatched\": " << s.events_dispatched << ",\n";
    out << "  \"delta_cycles\": " << s.delta_cycles << ",\n";
    out << "  \"nba_commits\": " << s.nba_commits << "\n";
    out << "}\n";
    out << "---END_CAS_TELEMETRY---\n";
}