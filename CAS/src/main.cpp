#include "../include/lexer.hpp"
#include "../include/parser.hpp"
#include "../include/engine.hpp"
#include "../include/profiler/profiler.hpp"
#include "../include/kernel/vpi_runtime.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#ifndef _WIN32
#include <unistd.h>
#endif

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
    bool enable_native_codegen = false;
    bool parse_only = false;
    bool elab_only = false;
    bool separate_compilation_units = false;
    InitRegMode init_reg_mode = InitRegMode::X;
    std::string top_override = "";
    std::string timescale_unit = "1ns";
    std::vector<std::string> source_files;
    std::vector<std::string> cli_plusargs;
    std::vector<std::string> sv_lib_paths;
    std::vector<std::string> vpi_lib_paths;
    std::vector<std::string> c_source_files;
    std::vector<std::string> temporary_so_files;

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
                } else if (tok.rfind("+initreg+", 0) == 0) {
                    std::string mode_str = tok.substr(9);
                    if (mode_str == "0") init_reg_mode = InitRegMode::ZERO;
                    else if (mode_str == "1") init_reg_mode = InitRegMode::ONE;
                    else if (mode_str == "x" || mode_str == "X") init_reg_mode = InitRegMode::X;
                    cli_plusargs.push_back(tok);
                } else if (tok == "-top" || tok == "--top") {
                    if (lss >> tok) top_override = tok;
                } else if (tok == "-sv_lib" || tok == "--sv_lib") {
                    if (lss >> tok) sv_lib_paths.push_back(tok);
                } else if (tok.rfind("-sv_lib=", 0) == 0) {
                    sv_lib_paths.push_back(tok.substr(8));
                } else if (tok.rfind("--sv_lib=", 0) == 0) {
                    sv_lib_paths.push_back(tok.substr(9));
                } else if (tok == "-vpi" || tok == "--vpi" || tok == "-loadvpi") {
                    if (lss >> tok) vpi_lib_paths.push_back(tok);
                } else if (tok.rfind("-vpi=", 0) == 0) {
                    vpi_lib_paths.push_back(tok.substr(5));
                } else if (tok.rfind("--vpi=", 0) == 0) {
                    vpi_lib_paths.push_back(tok.substr(6));
                } else if (tok.rfind("-loadvpi=", 0) == 0) {
                    vpi_lib_paths.push_back(tok.substr(9));
                } else if (tok.rfind("-", 0) == 0 || tok.rfind("+", 0) == 0) {
                    // skip unknown compiler/linter flags (e.g. -CFLAGS, +define+, etc.)
                } else {
                    // file
                    std::string fpath = tok;
                    if (!cur_fl_dir.empty() && fpath.front() != '/' && fpath.find(':') == std::string::npos) {
                        fpath = cur_fl_dir + "/" + fpath;
                    }
                    size_t dot = fpath.find_last_of('.');
                    std::string ext = (dot != std::string::npos) ? fpath.substr(dot) : "";
                    if (ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
                        c_source_files.push_back(fpath);
                    } else if (ext == ".so" || ext == ".dll") {
                        sv_lib_paths.push_back(fpath);
                    } else {
                        source_files.push_back(fpath);
                    }
                }
            }
        }
    };

    std::unordered_map<std::string, LogicVal> top_param_overrides;
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

    Lexer::set_define("__ICARUS__", "1");

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-S") == 0) {
            std::cerr << "Error: Synthesis (-S) is not supported in simulation mode.\n";
            return 1;
        } else if (std::strcmp(argv[i], "--no-profile") == 0) {
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
        } else if (std::strcmp(argv[i], "--native-codegen") == 0) {
            enable_native_codegen = true;
        } else if (std::strcmp(argv[i], "--parse-only") == 0) {
            parse_only = true;
        } else if (std::strcmp(argv[i], "--elab-only") == 0) {
            elab_only = true;
        } else if ((std::strcmp(argv[i], "--top") == 0 || std::strcmp(argv[i], "-top") == 0) && i + 1 < argc) {
            top_override = argv[++i];
        } else if (std::strcmp(argv[i], "--timescale") == 0 && i + 1 < argc) {
            timescale_unit = argv[++i];
        } else if (std::strcmp(argv[i], "--initreg") == 0 && i + 1 < argc) {
            std::string mode_str = argv[++i];
            if (mode_str == "0") {
                init_reg_mode = InitRegMode::ZERO;
            } else if (mode_str == "1") {
                init_reg_mode = InitRegMode::ONE;
            } else if (mode_str == "x" || mode_str == "X") {
                init_reg_mode = InitRegMode::X;
            } else {
                std::cerr << "[CAS WARNING] Unknown --initreg mode: " << mode_str << " (expected 0, 1, or x)\n";
            }
        } else if (std::strncmp(argv[i], "+initreg+", 9) == 0) {
            std::string mode_str = argv[i] + 9;
            if (mode_str == "0") {
                init_reg_mode = InitRegMode::ZERO;
            } else if (mode_str == "1") {
                init_reg_mode = InitRegMode::ONE;
            } else if (mode_str == "x" || mode_str == "X") {
                init_reg_mode = InitRegMode::X;
            } else {
                std::cerr << "[CAS WARNING] Unknown +initreg+ mode: " << mode_str << " (expected 0, 1, or x)\n";
            }
            cli_plusargs.push_back(argv[i]);
        } else if (std::strcmp(argv[i], "-u") == 0) {
            separate_compilation_units = true;
        } else if (std::strcmp(argv[i], "-D") == 0 && i + 1 < argc) {
            std::string def = argv[++i];
            auto eq = def.find('=');
            if (eq != std::string::npos) {
                Lexer::set_define(def.substr(0, eq), def.substr(eq + 1));
            } else {
                Lexer::set_define(def, "");
            }
        } else if (std::strncmp(argv[i], "-D", 2) == 0) {
            std::string def = argv[i] + 2;
            auto eq = def.find('=');
            if (eq != std::string::npos) {
                Lexer::set_define(def.substr(0, eq), def.substr(eq + 1));
            } else {
                Lexer::set_define(def, "");
            }
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
        } else if (std::strncmp(argv[i], "-g", 2) == 0) {
            // Icarus Verilog generation flag (-g2005, -g2005-sv, -g2009, -g2012), ignore
            continue;
        } else if (std::strncmp(argv[i], "-P", 2) == 0) {
            parse_param_arg(argv[i] + 2);
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
        } else if ((std::strcmp(argv[i], "-sv_lib") == 0 || std::strcmp(argv[i], "--sv_lib") == 0) && i + 1 < argc) {
            sv_lib_paths.push_back(argv[++i]);
        } else if (std::strncmp(argv[i], "-sv_lib=", 8) == 0) {
            sv_lib_paths.push_back(argv[i] + 8);
        } else if (std::strncmp(argv[i], "--sv_lib=", 9) == 0) {
            sv_lib_paths.push_back(argv[i] + 9);
        } else if ((std::strcmp(argv[i], "-vpi") == 0 || std::strcmp(argv[i], "--vpi") == 0 || std::strcmp(argv[i], "-loadvpi") == 0) && i + 1 < argc) {
            vpi_lib_paths.push_back(argv[++i]);
        } else if (std::strncmp(argv[i], "-vpi=", 5) == 0) {
            vpi_lib_paths.push_back(argv[i] + 5);
        } else if (std::strncmp(argv[i], "--vpi=", 6) == 0) {
            vpi_lib_paths.push_back(argv[i] + 6);
        } else if (std::strncmp(argv[i], "-loadvpi=", 9) == 0) {
            vpi_lib_paths.push_back(argv[i] + 9);
        } else {
            std::string arg = argv[i];
            size_t dot = arg.find_last_of('.');
            std::string ext = (dot != std::string::npos) ? arg.substr(dot) : "";
            if (ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
                c_source_files.push_back(arg);
            } else if (ext == ".so" || ext == ".dll") {
                sv_lib_paths.push_back(arg);
            } else {
                source_files.push_back(argv[i]);
            }
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

        auto initial_defines = Lexer::global_defines;
        auto initial_macro_defs = Lexer::global_macro_defs;
        Lexer::reset_default_nettype();

        // Pass 1: Discover and register all PackageNode definitions across all source files
        for (size_t f_idx = 0; f_idx < file_sources.size(); ++f_idx) {
            const auto& entry = file_sources[f_idx];
            if (separate_compilation_units) {
                Lexer::global_defines = initial_defines;
                Lexer::global_macro_defs = initial_macro_defs;
                Lexer::reset_default_nettype();
            }
            Lexer lexer(entry.filename, entry.content, entry.base_dir, nullptr);
            Parser parser(lexer);
            parser.set_compilation_unit_id(separate_compilation_units ? f_idx : 0);
            parser.parse_packages_only();
        }

        // Reset lexer defines and default nettype before Pass 2
        Lexer::global_defines = initial_defines;
        Lexer::global_macro_defs = initial_macro_defs;
        Lexer::reset_default_nettype();
        Lexer::reset_timescale();
        Parser::global_timeunit_val = "";
        Parser::global_timeprec_val = "";

        // Pass 2: Full parsing of all modules, interfaces, and compilation units
        for (size_t f_idx = 0; f_idx < file_sources.size(); ++f_idx) {
            const auto& entry = file_sources[f_idx];
            if (separate_compilation_units) {
                Lexer::global_defines = initial_defines;
                Lexer::global_macro_defs = initial_macro_defs;
                Lexer::reset_default_nettype();
            }
            Lexer lexer(entry.filename, entry.content, entry.base_dir, debug_ptr);
            Parser parser(lexer);
            parser.set_compilation_unit_id(separate_compilation_units ? f_idx : 0);
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

        if (parse_only) {
            if (verbose) {
                std::cout << debug_ss.str();
            }
            std::cout << "All files parsed successfully (--parse-only).\n";
            return 0;
        }

        if (all_modules.empty()) {
            Elaborator elab;
            elab.elaborate(all_modules);
            if (verbose) {
                std::cout << debug_ss.str();
            }
            std::cout << "All files parsed successfully (no modules to simulate).\n";
            return 0;
        }

        struct TempCleaner {
            std::vector<std::string>& files;
            ~TempCleaner() {
                for (const auto& f : files) {
                    std::remove(f.c_str());
                }
            }
        } temp_cleaner{temporary_so_files};

        if (!c_source_files.empty()) {
            for (size_t ci = 0; ci < c_source_files.size(); ++ci) {
                const auto& c_file = c_source_files[ci];
                std::string tmp_so = "/tmp/cas_dpi_" + std::to_string(getpid()) + "_" + std::to_string(ci) + ".so";
                size_t slash = c_file.find_last_of("/\\");
                std::string c_dir = (slash == std::string::npos) ? "." : c_file.substr(0, slash);

                std::string dpi_inc = "-I/app/CAS/include/dpi -I/app/CAS/include -I/app/CAS/include/common";
                std::string compile_cmd = "g++ -shared -fPIC -O2 " + dpi_inc + " -I\"" + c_dir + "\" \"" + c_file + "\" -o \"" + tmp_so + "\"";
                if (verbose) {
                    std::cout << "[CAS DPI] Compiling C/C++ source: " << compile_cmd << "\n";
                }
                int res = std::system(compile_cmd.c_str());
                if (res != 0) {
                    std::cerr << "[CAS ERROR] Failed to compile DPI C/C++ source '" << c_file << "' (exit code " << res << ")\n";
                    return 1;
                }
                sv_lib_paths.push_back(tmp_so);
                temporary_so_files.push_back(tmp_so);
            }
        }

        Engine engine;
        for (const auto& lib : sv_lib_paths) {
            engine.dpi_registry.add_library(lib);
        }
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
        engine.set_native_codegen(enable_native_codegen);
        engine.set_init_reg_mode(init_reg_mode);
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
        VpiRuntime::set_active_engine(&engine);
        for (const auto& vpi_lib : vpi_lib_paths) {
            VpiRuntime::load_vpi_library(vpi_lib);
        }
        if (elab_only) {
            if (verbose) {
                std::cout << debug_ss.str();
            }
            std::cout << "Elaboration completed successfully (--elab-only).\n";
            return 0;
        }
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