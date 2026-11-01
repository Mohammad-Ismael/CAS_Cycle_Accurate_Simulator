#include "../include/lexer.hpp"
#include "../include/parser.hpp"
#include "../include/engine.hpp"
#include "../include/profiler/profiler.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: cas [options] <source_files...>\n";
        return 1;
    }

    bool enable_profiling = true;
    bool verbose = true;
    bool enable_vcd = true;
    bool enable_display = true;
    bool enable_monitor = true;
    bool enable_finish_msg = true;
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
        } else if (std::strcmp(argv[i], "--no-display") == 0) {
            enable_display = false;
        } else if (std::strcmp(argv[i], "--no-monitor") == 0) {
            enable_monitor = false;
        } else if (std::strcmp(argv[i], "--no-finish-msg") == 0) {
            enable_finish_msg = false;
        } else if ((std::strcmp(argv[i], "--top") == 0 || std::strcmp(argv[i], "-top") == 0) && i + 1 < argc) {
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

    Profiler profiler(enable_profiling, verbose, enable_vcd);
    profiler.start();

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
    Lexer::reset_timescale();
    Parser::clear_global_packages();

    struct FileSourceEntry {
        std::string filename;
        std::string content;
        std::string base_dir;
    };
    std::vector<FileSourceEntry> file_sources;

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

            size_t slash = file.find_last_of("/\\");
            std::string base_dir = (slash == std::string::npos) ? "." : file.substr(0, slash);

            if (verbose && f_idx > 0) {
                debug_ss << "  [Source Accessed] #" << (f_idx + 1) << ": '" << file << "' (" << content.size() << " bytes)\n";
            }
            file_sources.push_back({file, std::move(content), base_dir});
        }

        // Pass 1: Discover and register all PackageNode definitions across all source files
        for (const auto& entry : file_sources) {
            Lexer lexer(entry.filename, entry.content, entry.base_dir, nullptr);
            Parser parser(lexer);
            parser.parse_packages_only();
        }

        // Pass 2: Full parsing of all modules, interfaces, and compilation units
        for (const auto& entry : file_sources) {
            Lexer lexer(entry.filename, entry.content, entry.base_dir, debug_ptr);
            Parser parser(lexer);
            auto mods = parser.parse_all();
            for (auto& m : mods) {
                all_modules.push_back(std::move(m));
            }
        }

        profiler.set_source_metrics(total_bytes, all_modules.size());

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

        Engine engine;
        for (const auto& file : source_files) {
            size_t slash = file.find_last_of("/\\");
            if (slash != std::string::npos) {
                engine.source_search_paths.push_back(file.substr(0, slash));
            }
        }
        engine.set_vcd_allowed(enable_vcd);
        engine.set_display_allowed(enable_display);
        engine.set_monitor_allowed(enable_monitor);
        engine.set_finish_msg_allowed(enable_finish_msg);
        engine.package_defs = Parser::global_packages;

        engine.elaborate(all_modules, top_override, debug_ptr);
        engine.run(debug_ptr);

        ProfileMetrics metrics = profiler.sample(engine.get_stats());
        Profiler::emit_json_telemetry(metrics, std::cout);

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