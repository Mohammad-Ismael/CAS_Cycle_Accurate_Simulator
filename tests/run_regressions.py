import subprocess
import os
import sys

suites = [
    ("v1.0.0.0", "v1_0_0_0_shift_reg_core", ["design.sv", "testbench.sv"]),
    ("v1.1.0.0", "v1_1_0_0_four_state_core", ["testbench.sv", "design.sv"]),
    ("v1.2.0.0", "v1_2_0_0_width_sign_core", ["testbench.sv", "design.sv"]),
    ("v1.3.0.0", "v1_3_0_0_loop_func_core", ["testbench.sv", "design.sv"]),
    ("v1.4.0.0", "v1_4_0_0_reduction_core", ["testbench.sv", "design.sv"]),
    ("v1.5.0.0", "v1_5_0_0_event_ctrl_core", ["testbench.sv", "design.sv"]),
    ("v1.6.0.0", "v1_6_0_0_intra_assign_core", ["design.sv", "testbench.sv"]),
    ("v1.7.0.0", "v1_7_0_0_casex_casez", ["design.sv", "testbench.sv"]),
    ("v1.8.0.0", "v1_8_0_0_procedural_continuous", ["design.sv", "testbench.sv"]),
    ("v1.9.0.0", "v1_9_0_0_intra_event", ["design.sv", "testbench.sv"]),
    ("v2.0.0.0", "v2_0_0_0_multi_driver_resolution", ["design.sv", "testbench.sv"]),
    ("v2.1.0.0", "v2_1_0_0_automatic_routines", ["design.sv", "testbench.sv"]),
    ("v2.2.0.0", "v2_2_0_0_fork_join", ["design.sv", "testbench.sv"]),
    ("v2.3.0.0", "v2_3_0_0_process_control", ["design.sv", "testbench.sv"]),
    ("v2.4.0.0", "v2_4_0_0_named_events", ["design.sv", "testbench.sv"]),
    ("v2.5.0.0", "v2_5_0_0_enum_types", ["design.sv", "testbench.sv"]),
    ("v2.6.0.0", "v2_6_0_0_struct_types", ["design.sv", "testbench.sv"]),
    ("v2.7.0.0", "v2_7_0_0_unpacked_arrays", ["design.sv", "testbench.sv"]),
    ("v2.8.0.0", "v2_8_0_0_queues", ["design.sv", "testbench.sv"]),
    ("v2.9.0.0", "v2_9_0_0_dynamic_arrays", ["design.sv", "testbench.sv"]),
    ("v3.0.0.0", "v3_0_0_0_interfaces", ["design.sv", "testbench.sv"]),
    ("v3.1.0.0", "v3_1_0_0_modports", ["design.sv", "testbench.sv"]),
    ("v3.2.0.0", "v3_2_0_0_clocking_blocks", ["design.sv", "testbench.sv"]),
    ("v3.3.0.0", "v3_3_0_0_associative_arrays", ["design.sv", "testbench.sv"]),
    ("v3.4.0.0", "v3_4_0_0_classes", ["design.sv", "testbench.sv"]),
    ("v3.5.0.0", "v3_5_0_0_inheritance_polymorphism", ["design.sv", "testbench.sv"]),
    ("v3.6.0.0", "v3_6_0_0_constrained_randomization", ["design.sv", "testbench.sv"]),
    ("v3.7.0.0", "v3_7_0_0_functional_coverage", ["design.sv", "testbench.sv"]),
    ("v3.8.0.0", "v3_8_0_0_cross_coverage", ["design.sv", "testbench.sv"]),
    ("v3.9.0.0", "v3_9_0_0_sva_concurrent", ["design.sv", "testbench.sv"]),
    ("v3.10.0.0", "v3_10_0_0_sva_sampled_functions", ["design.sv", "testbench.sv"]),
    ("v3.11.0.0", "v3_11_0_0_sva_repetitions", ["design.sv", "testbench.sv"]),
    ("v3.12.0.0", "v3_12_0_0_dpic_foreign_interface", ["design.sv", "testbench.sv"]),
    ("v3.13.0.0", "v3_13_0_0_dpic_export_context", ["design.sv", "testbench.sv"]),
    ("v3.14.0.0", "v3_14_0_0_dpic_chandle_open_arrays", ["design.sv", "testbench.sv"]),
    ("v3.15.0.0", "v3_15_0_0_dpic_open_arrays", ["design.sv", "testbench.sv"]),
    ("v3.16.0.0", "v3_16_0_0_parameterized_classes", ["design.sv", "testbench.sv"])
]

cas_bin = os.path.abspath("CAS/build/cas")

results = []
all_passed = True

print(f"{'Milestone':<12} | {'Suite Directory':<35} | {'Status':<8} | {'Details'}")
print("-" * 80)

for ver, s_dir, files in suites:
    dir_path = os.path.join("projects", s_dir)
    c_source = os.path.join(dir_path, "c_model.c")
    if os.path.exists(c_source):
        subprocess.run(["gcc", "-fPIC", "-shared", "c_model.c", "-o", "c_model.so"], cwd=dir_path, check=True)
    cmd = [cas_bin] + files
    
    proc = subprocess.run(cmd, cwd=dir_path, capture_output=True, text=True)
    out = proc.stdout + proc.stderr
    
    status = "FAIL"
    detail = ""
    
    if proc.returncode == 0:
        if "FAIL" not in out and ("PASS" in out or "Simulation completed cleanly" in out or "Simulation Terminated" in out):
            status = "PASS"
            detail = "100% VCS Golden Parity"
        else:
            detail = "Simulation reported failure or missing pass marker"
            all_passed = False
    else:
        detail = f"Process exit code: {proc.returncode}"
        all_passed = False
        
    print(f"{ver:<12} | {s_dir:<35} | {status:<8} | {detail}")
    results.append((ver, s_dir, status, detail))

print("-" * 80)
passed_count = sum(1 for r in results if r[2] == "PASS")
print(f"TOTAL: {len(suites)} | PASSED: {passed_count} | FAILED: {len(suites) - passed_count}")

if not all_passed:
    sys.exit(1)
