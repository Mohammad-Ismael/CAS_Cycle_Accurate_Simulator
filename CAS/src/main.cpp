#include "../include/lexer.hpp"
#include "../include/parser.hpp"
#include "../include/engine.hpp"
#include "../include/profiler/profiler.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

int main(int argc, char* argv[]) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
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

    std::function<void(const std::string&, const std::string&)> process_filelist;
    process_filelist = [&](const std::string& fl_path, const std::string& base_fl_dir) {
        std::string resolved_path = fl_path;
        if (!base_fl_dir.empty() && fl_path.front() != '/' && fl_path.find(':') == std::string::npos) {
            resolved_path = base_fl_dir + "/" + fl_path;
        }
        std::ifstream fl(resolved_path);
        if (!fl.is_open()) {
            std::cerr << "[CAS WARNING] Cannot open filelist: " << resolved_path << "\n";
            return;
        }
        size_t slash = resolved_path.find_last_of("/\\");
        std::string cur_fl_dir = (slash == std::string::npos) ? "." : resolved_path.substr(0, slash);

        std::string line;
        while (std::getline(fl, line)) {
            // strip comments
            size_t comm = line.find("//");
            if (comm != std::string::npos) line = line.substr(0, comm);
            comm = line.find("#");
            if (comm != std::string::npos) line = line.substr(0, comm);

            std::stringstream lss(line);
            std::string tok;
            while (lss >> tok) {
                if (tok.rfind("+incdir+", 0) == 0) {
                    std::string idir = tok.substr(8);
                    if (!cur_fl_dir.empty() && idir.front() != '/' && idir.find(':') == std::string::npos) {
                        idir = cur_fl_dir + "/" + idir;
                    }
                    Lexer::add_include_dir(idir);
                } else if (tok.rfind("+define+", 0) == 0) {
                    std::string def = tok.substr(8);
                    auto eq = def.find('=');
                    if (eq != std::string::npos) {
                        Lexer::set_define(def.substr(0, eq), def.substr(eq + 1));
                    } else {
                        Lexer::set_define(def, "");
                    }
                } else if ((tok == "-f" || tok == "-F") && (lss >> tok)) {
                    process_filelist(tok, cur_fl_dir);
                } else if (tok == "-top" || tok == "--top") {
                    if (lss >> tok) top_override = tok;
                } else if (tok.rfind("-", 0) == 0 || tok.rfind("+", 0) == 0) {
                    // skip unknown compiler/linter flags (e.g. -CFLAGS, +define+, etc.)
                } else {
                    // file
                    std::string fpath = tok;
                    if (!cur_fl_dir.empty() && fpath.front() != '/' && fpath.find(':') == std::string::npos) {
                        fpath = cur_fl_dir + "/" + fpath;
                    }
                    source_files.push_back(fpath);
                }
            }
        }
    };

    std::unordered_map<std::string, LogicVal> top_param_overrides;
    std::vector<std::string> cli_plusargs;
    uint64_t max_cycles = 0;

    auto parse_param_arg = [&](const std::string& arg) {
        auto eq = arg.find('=');
        if (eq != std::string::npos) {
            std::string pname = arg.substr(0, eq);
            std::string pval = arg.substr(eq + 1);
            if (pval.size() >= 2 && ((pval.front() == '"' && pval.back() == '"') || (pval.front() == '\'' && pval.back() == '\''))) {
                pval = pval.substr(1, pval.size() - 2);
                top_param_overrides[pname] = LogicVal::make_string(pval);
            } else {
                bool is_num = !pval.empty();
                for (size_t c = 0; c < pval.size(); ++c) {
                    if (c == 0 && (pval[c] == '-' || pval[c] == '+')) continue;
                    if (!std::isdigit((unsigned char)pval[c]) && pval[c] != 'x' && pval[c] != 'X') {
                        is_num = false;
                        break;
                    }
                }
                if (is_num) {
                    try {
                        uint64_t num = std::stoull(pval, nullptr, 0);
                        top_param_overrides[pname] = LogicVal::make_known(32, num);
                    } catch (...) {
                        top_param_overrides[pname] = LogicVal::make_string(pval);
                    }
                } else {
                    top_param_overrides[pname] = LogicVal::make_string(pval);
                }
            }
        }
    };

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
        } else if (std::strncmp(argv[i], "+incdir+", 8) == 0) {
            Lexer::add_include_dir(argv[i] + 8);
        } else if (std::strncmp(argv[i], "+define+", 8) == 0) {
            std::string def = argv[i] + 8;
            auto eq = def.find('=');
            if (eq != std::string::npos) {
                Lexer::set_define(def.substr(0, eq), def.substr(eq + 1));
            } else {
                Lexer::set_define(def, "");
            }
        } else if (std::strncmp(argv[i], "+firmware=", 10) == 0) {
            top_param_overrides["SRAMInitFile"] = LogicVal::make_string(argv[i] + 10);
        } else if (std::strcmp(argv[i], "-G") == 0 && i + 1 < argc) {
            parse_param_arg(argv[++i]);
        } else if (std::strncmp(argv[i], "-G", 2) == 0) {
            parse_param_arg(argv[i] + 2);
        } else if (std::strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            parse_param_arg(argv[++i]);
        } else if (std::strncmp(argv[i], "-p", 2) == 0) {
            parse_param_arg(argv[i] + 2);
        } else if (std::strcmp(argv[i], "--max-cycles") == 0 && i + 1 < argc) {
            max_cycles = std::stoull(argv[++i]);
        } else if (argv[i][0] == '+') {
            cli_plusargs.push_back(argv[i]);
        } else if (std::strcmp(argv[i], "-I") == 0 && i + 1 < argc) {
            Lexer::add_include_dir(argv[++i]);
        } else if (std::strncmp(argv[i], "-I", 2) == 0) {
            Lexer::add_include_dir(argv[i] + 2);
        } else if ((std::strcmp(argv[i], "-f") == 0 || std::strcmp(argv[i], "-F") == 0) && i + 1 < argc) {
            process_filelist(argv[++i], ".");
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

        if (all_modules.empty()) {
            if (verbose) {
                std::cout << debug_ss.str();
            }
            std::cout << "All files parsed successfully (no modules to simulate).\n";
            return 0;
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

        for (const auto& kv : top_param_overrides) {
            engine.set_top_param_override(kv.first, kv.second);
        }
        for (const auto& pa : cli_plusargs) {
            engine.add_plusarg(pa);
        }
        if (max_cycles > 0) {
            engine.set_max_cycles(max_cycles);
        }

        if (verbose) {
            std::cout << "[CAS] Parsing completed. Starting elaboration...\n" << std::flush;
        }
        engine.elaborate(all_modules, top_override, debug_ptr);
        if (verbose) {
            std::cout << "[CAS] Elaboration completed. Starting simulation...\n" << std::flush;
        }
        engine.run(debug_ptr);
        if (verbose) {
            std::cout << "[CAS] Simulation completed at time " << engine.get_sim_time() << ".\n" << std::flush;
        }

        ProfileMetrics metrics = profiler.sample(engine.get_stats());
        Profiler::emit_json_telemetry(metrics, std::cout);

        std::cout << "\n---BEGIN_CAS_DEBUG_LOG---\n";
        if (verbose) {
            std::cout << debug_ss.str();
        }
        std::cout << "---END_CAS_DEBUG_LOG---\n";

        if (engine.has_fatal_error()) {
            return engine.get_sim_exit_code();
        }

    } catch (const std::exception& e) {
        std::cerr << "\n" << e.what() << "\n";
        return 2;
    }

    return 0;
}