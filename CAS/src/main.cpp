#include "../include/lexer.hpp"
#include "../include/parser.hpp"
#include "../include/engine.hpp"
#include <fstream>
#include <sstream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: cas <source_files...>\n";
        return 1;
    }

    std::string combined_src;
    for (int i = 1; i < argc; ++i) {
        std::ifstream f(argv[i]);
        if (!f.is_open()) {
            std::cerr << "Error: Cannot open " << argv[i] << "\n";
            return 1;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        combined_src += ss.str() + "\n";
    }

    try {
        Lexer lexer(combined_src);
        Parser parser(lexer);
        auto modules = parser.parse_all();

        Engine engine;
        engine.elaborate(modules);
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "FATAL SIMULATOR ERROR: " << e.what() << "\n";
        return 2;
    }

    return 0;
}