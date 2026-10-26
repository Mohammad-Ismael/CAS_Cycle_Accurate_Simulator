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
from datetime import datetime

app = FastAPI()

PROJECTS_DIR = "/app/projects"
os.makedirs(PROJECTS_DIR, exist_ok=True)

class SimRequest(BaseModel):
    files: Dict[str, str]  # filename -> file content
    entry_tb: Optional[str] = "testbench.sv"
    top_module: Optional[str] = ""
    project_name: Optional[str] = None
    enable_profiler: Optional[bool] = True
    enable_debug: Optional[bool] = True
    enable_vcd: Optional[bool] = True
    timescale: Optional[str] = "1ns"

class SaveProjectRequest(BaseModel):
    project_name: str
    files: Dict[str, str]

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

    # Clean existing non-wave source files and write updated multi-file project
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
        "files": list(req.files.keys())
    }
    with open(os.path.join(pdir, "project.json"), "w") as f:
        json.dump(meta, f, indent=2)

    return {"status": "success", "project_name": pname}

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

    # Fallbacks if legacy project
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

    return {
        "project_name": name,
        "files": files,
        "waves": waves
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

    with tempfile.TemporaryDirectory() as tmpdir:
        # Write ALL created files into sandbox directory so `include can resolve any of them
        for fname, content in req.files.items():
            fpath = os.path.join(tmpdir, fname)
            os.makedirs(os.path.dirname(fpath), exist_ok=True)
            with open(fpath, "w") as f:
                f.write(content)

        # Decide CLI entry files:
        # Standard Verilog practice:
        # 1. Primary entry testbench file is passed.
        # 2. If design.sv is present and NOT `included in testbench, pass it too.
        # 3. All other helper files (+ added tabs) are included via `include.
        entry_tb = req.entry_tb if (req.entry_tb in req.files) else "testbench.sv"
        files_to_run = [os.path.join(tmpdir, entry_tb)]

        tb_text = req.files.get(entry_tb, "")
        if "design.sv" in req.files:
            # If testbench already has `include "design.sv", don't pass design.sv on CLI (avoids duplicate module definitions)
            if '`include "design.sv"' not in tb_text and "`include 'design.sv'" not in tb_text:
                files_to_run.append(os.path.join(tmpdir, "design.sv"))

        cas_bin = "/app/CAS/build/cas"
        options = []
        if not req.enable_profiler:
            options.append("--no-profile")
        if not req.enable_debug:
            options.append("--quiet")
        if not req.enable_vcd:
            options.append("--no-vcd")
        if req.top_module and req.top_module.strip():
            options += ["--top", req.top_module.strip()]
        if req.timescale and req.timescale.strip():
            options += ["--timescale", req.timescale.strip()]

        run_cmd = [cas_bin] + options + files_to_run

        try:
            res = subprocess.run(run_cmd, cwd=tmpdir, capture_output=True, text=True, timeout=15)
            raw_output = res.stdout if res.returncode == 0 else (res.stderr + "\n" + res.stdout)

            telemetry_data = None
            debug_log = ""
            clean_output = raw_output

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
                            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
                            saved_name = f"{ts}_{fname}"
                            shutil.copy2(os.path.join(tmpdir, fname), os.path.join(pwaves, saved_name))
                            vcd_filename = saved_name
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
            return {
                "status": "error",
                "output": f"[CAS TIMEOUT (15s)]\n{partial_out}",
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