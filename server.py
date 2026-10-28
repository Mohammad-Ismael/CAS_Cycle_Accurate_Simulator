from fastapi import FastAPI, HTTPException
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
from typing import Optional, Dict
import subprocess
import tempfile
import shutil
import json
import os
import glob
import re
from datetime import datetime

app = FastAPI()

PROJECTS_DIR = "/app/projects"
os.makedirs(PROJECTS_DIR, exist_ok=True)

class SimRequest(BaseModel):
    files: Dict[str, str]
    entry_tb: Optional[str] = "testbench.sv"
    top_module: Optional[str] = ""
    project_name: Optional[str] = None
    enable_profiler: Optional[bool] = True
    enable_debug: Optional[bool] = True
    enable_vcd: Optional[bool] = True
    vcd_filename: Optional[str] = "trace.vcd"
    vcd_levels: Optional[int] = 0
    enable_display: Optional[bool] = True
    enable_monitor: Optional[bool] = True
    enable_finish_msg: Optional[bool] = True
    timescale: Optional[str] = "1ns"

class SaveProjectRequest(BaseModel):
    project_name: str
    files: Dict[str, str]
    settings: Optional[Dict] = {}
    status: Optional[Dict] = {}

class RenameProjectRequest(BaseModel):
    old_name: str
    new_name: str

# ----------------- Project Management APIs ----------------- #

@app.get("/api/projects")
def list_projects():
    projects = []
    if not os.path.exists(PROJECTS_DIR):
        return {"projects": []}

    for name in os.listdir(PROJECTS_DIR):
        pdir = os.path.join(PROJECTS_DIR, name)
        if os.path.isdir(pdir):
            waves_dir = os.path.join(pdir, "waves")
            vcd_count = len(glob.glob(os.path.join(waves_dir, "*.vcd"))) if os.path.exists(waves_dir) else 0
            mtime = os.path.getmtime(pdir)
            projects.append({
                "name": name,
                "last_modified": datetime.fromtimestamp(mtime).strftime("%Y-%m-%d %H:%M:%S"),
                "vcd_count": vcd_count
            })
    projects.sort(key=lambda x: x["last_modified"], reverse=True)
    return {"projects": projects}

@app.post("/api/projects/save")
def save_project(req: SaveProjectRequest):
    pname = req.project_name.strip()
    if not pname:
        raise HTTPException(status_code=400, detail="Project name cannot be empty.")
    
    pname = "".join(c for c in pname if c.isalnum() or c in ('_', '-'))
    pdir = os.path.join(PROJECTS_DIR, pname)
    waves_dir = os.path.join(pdir, "waves")
    os.makedirs(waves_dir, exist_ok=True)

    for f in os.listdir(pdir):
        if f != "waves" and f != "project.json":
            fpath = os.path.join(pdir, f)
            if os.path.isfile(fpath):
                os.remove(fpath)

    for fname, content in req.files.items():
        with open(os.path.join(pdir, fname), "w") as f:
            f.write(content)

    meta = {
        "project_name": pname,
        "saved_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "files": list(req.files.keys()),
        "settings": req.settings or {},
        "status": req.status or {}
    }
    with open(os.path.join(pdir, "project.json"), "w") as f:
        json.dump(meta, f, indent=2)

    return {"status": "success", "project_name": pname}

@app.post("/api/projects/rename")
def rename_project(req: RenameProjectRequest):
    old_pname = req.old_name.strip()
    new_pname = req.new_name.strip()
    
    if not old_pname or not new_pname:
        raise HTTPException(status_code=400, detail="Project names cannot be empty.")

    old_clean = "".join(c for c in old_pname if c.isalnum() or c in ('_', '-'))
    new_clean = "".join(c for c in new_pname if c.isalnum() or c in ('_', '-'))

    old_dir = os.path.join(PROJECTS_DIR, old_clean)
    new_dir = os.path.join(PROJECTS_DIR, new_clean)

    if not os.path.exists(old_dir):
        raise HTTPException(status_code=404, detail="Source project does not exist.")
    if os.path.exists(new_dir) and old_clean != new_clean:
        raise HTTPException(status_code=400, detail="A project with the new name already exists.")

    if old_clean != new_clean:
        shutil.move(old_dir, new_dir)

    # Update metadata file with new name
    meta_path = os.path.join(new_dir, "project.json")
    meta = {}
    if os.path.exists(meta_path):
        try:
            with open(meta_path, "r") as f:
                meta = json.load(f)
        except Exception:
            meta = {}
    
    meta["project_name"] = new_clean
    meta["saved_at"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    with open(meta_path, "w") as f:
        json.dump(meta, f, indent=2)

    return {"status": "success", "old_name": old_clean, "new_name": new_clean}

@app.get("/api/projects/{name}")
def load_project(name: str):
    pdir = os.path.join(PROJECTS_DIR, name)
    if not os.path.exists(pdir):
        raise HTTPException(status_code=404, detail="Project not found.")

    files = {}
    for fname in os.listdir(pdir):
        fpath = os.path.join(pdir, fname)
        if os.path.isfile(fpath) and fname != "project.json":
            with open(fpath, "r") as f:
                files[fname] = f.read()

    if "testbench.sv" not in files:
        files["testbench.sv"] = ""
    if "design.sv" not in files:
        files["design.sv"] = ""

    waves = []
    waves_dir = os.path.join(pdir, "waves")
    if os.path.exists(waves_dir):
        for wf in os.listdir(waves_dir):
            if wf.endswith(".vcd"):
                wpath = os.path.join(waves_dir, wf)
                waves.append({
                    "filename": wf,
                    "timestamp": datetime.fromtimestamp(os.path.getmtime(wpath)).strftime("%Y-%m-%d %H:%M:%S")
                })
    waves.sort(key=lambda x: x["timestamp"], reverse=True)

    settings = {}
    status = {}
    meta_path = os.path.join(pdir, "project.json")
    if os.path.exists(meta_path):
        try:
            with open(meta_path, "r") as f:
                meta = json.load(f)
                settings = meta.get("settings", {})
                status = meta.get("status", {})
        except Exception as e:
            print(f"[ERROR] Failed to read project.json for {name}: {e}")
            settings = {}
            status = {}

    return {
        "project_name": name,
        "files": files,
        "waves": waves,
        "settings": settings,
        "status": status
    }

@app.delete("/api/projects/{name}")
def delete_project(name: str):
    pdir = os.path.join(PROJECTS_DIR, name)
    if os.path.exists(pdir):
        shutil.rmtree(pdir)
        return {"status": "success"}
    raise HTTPException(status_code=404, detail="Project not found.")

@app.get("/api/projects/{name}/waves/{filename}")
def get_vcd_file(name: str, filename: str):
    wpath = os.path.join(PROJECTS_DIR, name, "waves", filename)
    if not os.path.exists(wpath):
        raise HTTPException(status_code=404, detail="VCD file not found.")
    with open(wpath, "r") as f:
        content = f.read()
    return {"filename": filename, "vcd": content}

@app.delete("/api/projects/{name}/waves/{filename}")
def delete_vcd_file(name: str, filename: str):
    wpath = os.path.join(PROJECTS_DIR, name, "waves", filename)
    if os.path.exists(wpath):
        os.remove(wpath)
        return {"status": "success"}
    raise HTTPException(status_code=404, detail="VCD file not found.")

# ----------------- Simulation Execution ----------------- #

@app.post("/api/simulate")
def simulate(req: SimRequest):
    if not req.files:
        return {
            "status": "error",
            "output": "[ERROR] No source files provided.",
            "vcd": None,
            "vcd_filename": None,
            "telemetry": None,
            "debug_log": None
        }

    entry_tb = req.entry_tb if (req.entry_tb in req.files) else "testbench.sv"
    processed_files = dict(req.files)

    # Inspect source files for manual $dumpfile / $dumpvars
    dump_regex = re.compile(r'\$(dumpfile|dumpvars)\b')
    hardcoded_locations = []

    for fname, content in req.files.items():
        lines = content.split('\n')
        for idx, line in enumerate(lines, 1):
            clean_line = line.strip()
            if clean_line.startswith('//') or clean_line.startswith('/*'):
                continue
            if dump_regex.search(clean_line):
                hardcoded_locations.append(f"{fname}:{idx}")

    has_hardcoded_vcd = len(hardcoded_locations) > 0

    # Rule 1: If UI toggle is checked AND hardcoded lines exist -> abort
    if req.enable_vcd and has_hardcoded_vcd:
        locs_str = ", ".join(hardcoded_locations)
        abort_msg = (
            f"[CAS VCD CONFLICT ERROR] Hardcoded waveform dump routine detected at {locs_str}.\n"
            f"Action Required:\n"
            f"  1. Delete or comment out the manual $dumpfile/$dumpvars lines from your testbench, OR\n"
            f"  2. Uncheck 'Enable VCD Wave Dump' in the left sidebar to allow manual dumping."
        )
        return {
            "status": "error",
            "output": abort_msg,
            "vcd": None,
            "vcd_filename": None,
            "telemetry": None,
            "debug_log": None
        }

    # Rule 2: If UI toggle is checked AND NO hardcoded lines exist -> auto-inject VCD block
    if req.enable_vcd and not has_hardcoded_vcd and entry_tb in processed_files:
        tb_src = processed_files[entry_tb]
        endmod_pos = tb_src.rfind("endmodule")
        if endmod_pos != -1:
            raw_vcd_name = (req.vcd_filename or "trace.vcd").strip()
            if not raw_vcd_name.endswith(".vcd"):
                raw_vcd_name += ".vcd"
            
            levels = req.vcd_levels if (req.vcd_levels is not None and req.vcd_levels >= 0) else 0

            injected_block = (
                f"\n// [CAS Auto-Generated VCD Waveform Injection]\n"
                f"initial begin\n"
                f"    $dumpfile(\"{raw_vcd_name}\");\n"
                f"    $dumpvars({levels});\n"
                f"end\n\n"
            )
            processed_files[entry_tb] = tb_src[:endmod_pos] + injected_block + tb_src[endmod_pos:]

    with tempfile.TemporaryDirectory() as tmpdir:
        for fname, content in processed_files.items():
            fpath = os.path.join(tmpdir, fname)
            os.makedirs(os.path.dirname(fpath), exist_ok=True)
            with open(fpath, "w") as f:
                f.write(content)

        files_to_run = [os.path.join(tmpdir, entry_tb)]

        tb_text = processed_files.get(entry_tb, "")
        if "design.sv" in processed_files:
            if '`include "design.sv"' not in tb_text and "`include 'design.sv'" not in tb_text:
                files_to_run.append(os.path.join(tmpdir, "design.sv"))

        cas_bin = "/app/CAS/build/cas"
        options = []
        if not req.enable_profiler:
            options.append("--no-profile")
        if not req.enable_debug:
            options.append("--quiet")

        # Disable VCD only if user explicitly unchecked the toggle AND didn't provide hardcoded VCD code
        if not req.enable_vcd and not has_hardcoded_vcd:
            options.append("--no-vcd")

        if not req.enable_display:
            options.append("--no-display")
        if not req.enable_monitor:
            options.append("--no-monitor")
        if not req.enable_finish_msg:
            options.append("--no-finish-msg")
        if req.top_module and req.top_module.strip():
            options += ["--top", req.top_module.strip()]
        if req.timescale and req.timescale.strip():
            options += ["--timescale", req.timescale.strip()]

        run_cmd = [cas_bin] + options + files_to_run

        try:
            res = subprocess.run(run_cmd, cwd=tmpdir, capture_output=True, text=True, timeout=15)
            raw_output = res.stdout if res.returncode == 0 else (res.stderr + "\n" + res.stdout)
            normalized_output = raw_output.replace(tmpdir + "/", "").replace(tmpdir + "\\", "")

            telemetry_data = None
            debug_log = ""
            clean_output = normalized_output

            if "---BEGIN_CAS_DEBUG_LOG---" in clean_output and "---END_CAS_DEBUG_LOG---" in clean_output:
                b1, p1 = clean_output.split("---BEGIN_CAS_DEBUG_LOG---", 1)
                debug_log, b2 = p1.split("---END_CAS_DEBUG_LOG---", 1)
                clean_output = b1 + b2
                debug_log = debug_log.strip()

            if "---BEGIN_CAS_TELEMETRY---" in clean_output and "---END_CAS_TELEMETRY---" in clean_output:
                before, part = clean_output.split("---BEGIN_CAS_TELEMETRY---", 1)
                json_str, after = part.split("---END_CAS_TELEMETRY---", 1)
                clean_output = before + after
                try:
                    telemetry_data = json.loads(json_str.strip())
                except Exception:
                    telemetry_data = None

            console_output = clean_output.strip()

            vcd_content = None
            vcd_filename = None

            for fname in os.listdir(tmpdir):
                if fname.endswith(".vcd"):
                    vcd_filename = fname
                    with open(os.path.join(tmpdir, fname), "r") as vf:
                        vcd_content = vf.read()

                    if req.project_name:
                        pdir = os.path.join(PROJECTS_DIR, req.project_name)
                        if os.path.exists(pdir):
                            pwaves = os.path.join(pdir, "waves")
                            os.makedirs(pwaves, exist_ok=True)
                            dest_path = os.path.join(pwaves, fname)
                            shutil.copy2(os.path.join(tmpdir, fname), dest_path)
                            vcd_filename = fname
                    break

            return {
                "status": "success" if res.returncode == 0 else "error",
                "output": console_output,
                "vcd": vcd_content,
                "vcd_filename": vcd_filename,
                "telemetry": telemetry_data,
                "debug_log": debug_log
            }
        except subprocess.TimeoutExpired as e:
            partial_out = (e.stdout or "") + "\n" + (e.stderr or "")
            cleaned = partial_out.replace(tmpdir + "/", "").replace(tmpdir + "\\", "")
            return {
                "status": "error",
                "output": f"[CAS TIMEOUT (15s)]\n{cleaned}",
                "vcd": None,
                "vcd_filename": None,
                "telemetry": None,
                "debug_log": None
            }
        except Exception as e:
            return {
                "status": "error",
                "output": f"[CAS INVOCATION ERROR] {str(e)}",
                "vcd": None,
                "vcd_filename": None,
                "telemetry": None,
                "debug_log": None
            }

app.mount("/", StaticFiles(directory="static", html=True), name="static")