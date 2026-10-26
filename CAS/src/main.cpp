#include "../include/lexer.hpp"
#include "../include/parser.hpp"
#include "../include/engine.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sys/resource.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: cas <source_files...>\n";
        return 1;
    }

    std::cout << "========================================================\n";
    std::cout << " CAS (Cycle Accurate Simulator) v1.0.0 [EDA Kernel]     \n";
    std::cout << "========================================================\n";

    auto t0 = std::chrono::high_resolution_clock::now();

    // ----------------------------------------------------
    // PHASE 1: Source Ingestion
    // ----------------------------------------------------
    std::cout << "[PHASE 1: Ingestion] Reading source files...\n";
    std::string combined_src;
    size_t total_bytes = 0;
    for (int i = 1; i < argc; ++i) {
        std::ifstream f(argv[i]);
        if (!f.is_open()) {
            std::cerr << "[CAS ERROR] Cannot open source file: " << argv[i] << "\n";
            return 1;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        std::string content = ss.str();
        total_bytes += content.size();
        combined_src += content + "\n";
        std::cout << "  -> Loaded '" << argv[i] << "' (" << content.size() << " bytes)\n";
    }

    try {
        // ----------------------------------------------------
        // PHASE 2: Lexical Analysis & AST Parsing
        // ----------------------------------------------------
        std::cout << "[PHASE 2: Frontend] Parsing AST & Module Definitions...\n";
        Lexer lexer(combined_src);
        Parser parser(lexer);
        auto modules = parser.parse_all();

        std::cout << "  -> Parsed " << modules.size() << " module declaration(s):\n";
        for (const auto& mod : modules) {
            std::cout << "     * module " << mod->name 
                      << " (ports: " << mod->ports.size()
                      << ", params: " << mod->parameters.size()
                      << ", signals: " << mod->signals.size()
                      << ", gates: " << mod->gates.size()
                      << ", tasks: " << mod->tasks.size()
                      << ", processes: " << mod->processes.size() << ")\n";
        }

        // ----------------------------------------------------
        // PHASE 3 & 4: Elaboration & Simulation Execution
        // ----------------------------------------------------
        Engine engine;
        engine.elaborate(modules);
        engine.run();

        // ----------------------------------------------------
        // PERFORMANCE & RESOURCE PROFILER
        // ----------------------------------------------------
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed_wall_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);

        double user_cpu_ms = (usage.ru_utime.tv_sec * 1000.0) + (usage.ru_utime.tv_usec / 1000.0);
        double sys_cpu_ms  = (usage.ru_stime.tv_sec * 1000.0) + (usage.ru_stime.tv_usec / 1000.0);
        double total_cpu_ms = user_cpu_ms + sys_cpu_ms;
        double cpu_util = (elapsed_wall_ms > 0) ? (total_cpu_ms / elapsed_wall_ms) * 100.0 : 0.0;
        long max_rss_kb = usage.ru_maxrss;

        engine.print_profile_report();

        std::cout << "\n[RESOURCE USAGE & EXECUTION METRICS]\n";
        std::cout << "  * Wallclock Real Time    : " << std::fixed << std::setprecision(2) << elapsed_wall_ms << " ms\n";
        std::cout << "  * User CPU Time          : " << user_cpu_ms << " ms\n";
        std::cout << "  * System Kernel CPU Time : " << sys_cpu_ms << " ms\n";
        std::cout << "  * Total CPU Core Time    : " << total_cpu_ms << " ms ("
                  << std::setprecision(1) << cpu_util << "% CPU efficiency)\n";
        std::cout << "  * Peak Resident RAM (RSS): " << (max_rss_kb / 1024.0) << " MB (" << max_rss_kb << " KB)\n";
        std::cout << "========================================================\n";

        // Structured JSON payload for CAS Studio UI
        std::cout << "\n---BEGIN_CAS_TELEMETRY---\n";
        std::cout << "{\n";
        std::cout << "  \"wallclock_ms\": " << elapsed_wall_ms << ",\n";
        std::cout << "  \"user_cpu_ms\": " << user_cpu_ms << ",\n";
        std::cout << "  \"sys_cpu_ms\": " << sys_cpu_ms << ",\n";
        std::cout << "  \"cpu_util_pct\": " << cpu_util << ",\n";
        std::cout << "  \"peak_rss_kb\": " << max_rss_kb << ",\n";
        std::cout << "  \"source_bytes\": " << total_bytes << ",\n";
        std::cout << "  \"modules_count\": " << modules.size() << "\n";
        std::cout << "}\n";
        std::cout << "---END_CAS_TELEMETRY---\n";

    } catch (const std::exception& e) {
        std::cerr << "\n[CAS FATAL ERROR] " << e.what() << "\n";
        return 2;
    }

    return 0;
}