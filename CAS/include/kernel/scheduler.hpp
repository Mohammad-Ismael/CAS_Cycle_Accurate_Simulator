#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "kernel/netlist.hpp"
#include "kernel/evaluator.hpp"
#include "kernel/time_wheel.hpp"
#include "tracer/vcd_dumper.hpp"

class Engine;

struct NBAUpdate {
    size_t net_id;
    LogicVal val;
};

struct NBAMemUpdate {
    std::string mem_scoped_name;
    uint32_t addr;
    LogicVal val;
};

class StratifiedScheduler {
private:
    Engine& engine;
    TimeWheel time_wheel;

    std::vector<NBAUpdate> nba_queue;
    std::vector<NBAMemUpdate> nba_mem_queue;

    uint64_t current_sim_time = 0;
    bool simulation_active = true;

    size_t delta_cycles_count = 0;
    size_t nba_commits_count = 0;

    void propagate_comb();
    void evaluate_sequential_blocks();
    bool commit_nba_updates();
    void execute_time_slot_delta_loop();
    bool resume_waiting_processes();

public:
    explicit StratifiedScheduler(Engine& eng);

    void step_process(size_t pid);
    void schedule_event(uint64_t wake_time, uint32_t pid);
    void queue_nba(size_t net_id, const LogicVal& val);
    void queue_nba_mem(const std::string& scoped_mem, uint32_t addr, const LogicVal& val);

    void terminate();
    bool is_active() const { return simulation_active; }

    uint64_t get_sim_time() const { return current_sim_time; }
    void run_simulation();

    size_t get_delta_cycles() const { return delta_cycles_count; }
    size_t get_nba_commits() const { return nba_commits_count; }
    size_t get_events_dispatched() const { return time_wheel.events_dispatched(); }
};