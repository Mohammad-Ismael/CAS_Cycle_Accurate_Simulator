#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

struct ResolvedContinuousAssign;
struct BoundProcess;
class Engine;

class NativeCodeGen {
public:
    static bool generate_comb_source(
        const std::string& output_cpp_path,
        const std::vector<ResolvedContinuousAssign>& rcas,
        std::vector<BoundProcess>& procs,
        Engine& engine
    );

    static bool generate_comb_source(
        const std::string& output_cpp_path,
        const std::vector<ResolvedContinuousAssign>& rcas,
        std::vector<BoundProcess>& procs,
        size_t num_nets
    );
};
