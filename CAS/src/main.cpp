#include "../include/lexer.hpp"
#include "../include/parser.hpp"
#include "../include/engine.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <sys/resource.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: cas [options] <source_files...>\n";
        return 1;
    }

    bool enable_profiling = true;
    bool verbose = true;
    bool enable_vcd = true;
    std::string top_override = "";
    std::string timescale_unit = "1ns";
    std::vector<std::string> source_files;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-profile") == 0) {
            enable_profiling = false;
        } else if (std::strcmp(argv[i], "--quiet") == 0) {
            verbose = false;
        } else if (std::strcmp(argv[i], "--no-vcd") == 0) {
            enable_vcd = false;
        } else if (std::strcmp(argv[i], "--top") == 0 && i + 1 < argc) {
            top_override = argv[++i];
        } else if (std::strcmp(argv[i], "--timescale") == 0 && i + 1 < argc) {
            timescale_unit = argv[++i];
        } else {
            source_files.push_back(argv[i]);
        }
    }

    if (source_files.empty()) {
        std::cerr << "[CAS ERROR] No source files provided.\n";
        return 1;
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    std::stringstream debug_ss;
    std::ostream* debug_ptr = verbose ? &debug_ss : nullptr;

    if (verbose) {
        debug_ss << "==================================================================\n";
        debug_ss << " CAS COMPILER & PREPROCESSOR FILE ACCESS TRACE\n";
        debug_ss << "==================================================================\n";
        debug_ss << "[PHASE 1: Ingestion & Preprocessor Hierarchy]\n";
        debug_ss << "  -> CLI Root Entry: '" << source_files[0] << "'\n";
    }

    std::vector<std::unique_ptr<ModuleNode>> all_modules;
    size_t total_bytes = 0;
    std::vector<std::string> loaded_files;

    try {
        for (size_t f_idx = 0; f_idx < source_files.size(); ++f_idx) {
            const auto& file = source_files[f_idx];
            std::ifstream f(file);
            if (!f.is_open()) {
                std::cerr << "[CAS ERROR] Cannot open source file: " << file << "\n";
                return 1;
            }
            std::stringstream ss;
            ss << f.rdbuf();
            std::string content = ss.str();
            total_bytes += content.size();
            loaded_files.push_back(file);

            size_t slash = file.find_last_of("/\\");
            std::string base_dir = (slash == std::string::npos) ? "." : file.substr(0, slash);

            if (verbose && f_idx > 0) {
                debug_ss << "  [Source Accessed] #" << (f_idx + 1) << ": '" << file << "' (" << content.size() << " bytes)\n";
            }

            Lexer lexer(file, content, base_dir, debug_ptr);
            Parser parser(lexer);
            auto mods = parser.parse_all();
            for (auto& m : mods) {
                all_modules.push_back(std::move(m));
            }
        }

        if (verbose) {
            debug_ss << "\n[PHASE 2: Parsed AST & Module Origin Attribution]\n";
            debug_ss << "  -> Discovered " << all_modules.size() << " module definition(s) across source files:\n";
            for (const auto& mod : all_modules) {
                debug_ss << "     * module '" << mod->name << "'\n"
                         << "       - Defined in : " << mod->defined_in_file << ":" << mod->defined_on_line << "\n"
                         << "       - Interface  : " << mod->ports.size() << " port(s), " 
                                                    << mod->parameters.size() << " parameter(s)\n"
                         << "       - Internal   : " << mod->signals.size() << " net(s), " 
                                                    << mod->gates.size() << " gate(s), " 
                                                    << mod->processes.size() << " process(es)\n";
            }
            debug_ss << "\n";
        }

        // ----------------------------------------------------
        // PHASE 3 & 4: Elaboration & Simulation Execution
        // ----------------------------------------------------
        Engine engine;
        engine.set_vcd_allowed(enable_vcd);
        engine.elaborate(all_modules, top_override, debug_ptr);
        engine.run(debug_ptr);

        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed_wall_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        double user_cpu_ms = 0.0;
        double sys_cpu_ms = 0.0;
        double total_cpu_ms = 0.0;
        double cpu_util = 0.0;
        long max_rss_kb = 0;

        if (enable_profiling) {
            struct rusage usage;
            getrusage(RUSAGE_SELF, &usage);

            user_cpu_ms = (usage.ru_utime.tv_sec * 1000.0) + (usage.ru_utime.tv_usec / 1000.0);
            sys_cpu_ms  = (usage.ru_stime.tv_sec * 1000.0) + (usage.ru_stime.tv_usec / 1000.0);
            total_cpu_ms = user_cpu_ms + sys_cpu_ms;
            cpu_util = (elapsed_wall_ms > 0) ? (total_cpu_ms / elapsed_wall_ms) * 100.0 : 0.0;
            max_rss_kb = usage.ru_maxrss;
        }

        EngineStats st = engine.get_stats();

        std::cout << "\n---BEGIN_CAS_TELEMETRY---\n";
        std::cout << "{\n";
        std::cout << "  \"profiling_enabled\": " << (enable_profiling ? "true" : "false") << ",\n";
        std::cout << "  \"debug_enabled\": " << (verbose ? "true" : "false") << ",\n";
        std::cout << "  \"vcd_enabled\": " << (enable_vcd ? "true" : "false") << ",\n";
        std::cout << "  \"wallclock_ms\": " << elapsed_wall_ms << ",\n";
        std::cout << "  \"user_cpu_ms\": " << user_cpu_ms << ",\n";
        std::cout << "  \"sys_cpu_ms\": " << sys_cpu_ms << ",\n";
        std::cout << "  \"cpu_util_pct\": " << cpu_util << ",\n";
        std::cout << "  \"peak_rss_kb\": " << max_rss_kb << ",\n";
        std::cout << "  \"source_bytes\": " << total_bytes << ",\n";
        std::cout << "  \"modules_count\": " << all_modules.size() << ",\n";
        std::cout << "  \"nets_count\": " << st.net_count << ",\n";
        std::cout << "  \"nets_cap\": " << st.net_cap << ",\n";
        std::cout << "  \"procs_count\": " << st.proc_count << ",\n";
        std::cout << "  \"slices_count\": " << st.slice_count << ",\n";
        std::cout << "  \"rams_count\": " << st.ram_count << ",\n";
        std::cout << "  \"net_map_load\": " << st.net_map_load << ",\n";
        std::cout << "  \"events_dispatched\": " << st.events_dispatched << ",\n";
        std::cout << "  \"delta_cycles\": " << st.delta_cycles << ",\n";
        std::cout << "  \"nba_commits\": " << st.nba_commits << "\n";
        std::cout << "}\n";
        std::cout << "---END_CAS_TELEMETRY---\n";

        std::cout << "\n---BEGIN_CAS_DEBUG_LOG---\n";
        if (verbose) {
            std::cout << debug_ss.str();
        }
        std::cout << "---END_CAS_DEBUG_LOG---\n";

    } catch (const std::exception& e) {
        std::cerr << "\n" << e.what() << "\n";
        return 2;
    }

    return 0;
}