#include "../include/lexer.hpp"
#include "../include/parser.hpp"
#include "../include/engine.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iomanip>

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
    for (int i = 1; i < argc; ++i) {
        std::ifstream f(argv[i]);
        if (!f.is_open()) {
            std::cerr << "[CAS ERROR] Cannot open source file: " << argv[i] << "\n";
            return 1;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        std::string content = ss.str();
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

        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "[CAS ENGINE] Total Wallclock Execution Time: " 
                  << std::fixed << std::setprecision(2) << elapsed_ms << " ms\n";

    } catch (const std::exception& e) {
        std::cerr << "\n[CAS FATAL ERROR] " << e.what() << "\n";
        return 2;
    }

    return 0;
}