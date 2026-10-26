from fastapi import FastAPI, HTTPException
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
from typing import Optional
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
    design_code: str
    tb_code: str
    top_module: str
    project_name: Optional[str] = None

class SaveProjectRequest(BaseModel):
    project_name: str
    design_code: str
    tb_code: str

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
    
    # Sanitize name
    pname = "".join(c for c in pname if c.isalnum() or c in ('_', '-'))
    pdir = os.path.join(PROJECTS_DIR, pname)
    waves_dir = os.path.join(pdir, "waves")
    os.makedirs(waves_dir, exist_ok=True)

    with open(os.path.join(pdir, "design.sv"), "w") as f:
        f.write(req.design_code)

    with open(os.path.join(pdir, "testbench.sv"), "w") as f:
        f.write(req.tb_code)

    meta = {
        "project_name": pname,
        "saved_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    }
    with open(os.path.join(pdir, "project.json"), "w") as f:
        json.dump(meta, f, indent=2)

    return {"status": "success", "project_name": pname}

@app.get("/api/projects/{name}")
def load_project(name: str):
    pdir = os.path.join(PROJECTS_DIR, name)
    if not os.path.exists(pdir):
        raise HTTPException(status_code=404, detail="Project not found.")

    design_code = ""
    tb_code = ""
    d_path = os.path.join(pdir, "design.sv")
    t_path = os.path.join(pdir, "testbench.sv")

    if os.path.exists(d_path):
        with open(d_path, "r") as f:
            design_code = f.read()

    if os.path.exists(t_path):
        with open(t_path, "r") as f:
            tb_code = f.read()

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
        "design_code": design_code,
        "tb_code": tb_code,
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
    if not req.design_code.strip() and not req.tb_code.strip():
        return {"status": "error", "output": "[ERROR] Both RTL and Testbench are empty.", "vcd": None, "vcd_filename": None}

    with tempfile.TemporaryDirectory() as tmpdir:
        files_to_run = []

        if req.tb_code.strip():
            tb_path = os.path.join(tmpdir, "testbench.sv")
            with open(tb_path, "w") as f:
                f.write(req.tb_code)
            files_to_run.append(tb_path)

        if req.design_code.strip():
            dut_path = os.path.join(tmpdir, "design.sv")
            with open(dut_path, "w") as f:
                f.write(req.design_code)
            files_to_run.append(dut_path)

        cas_bin = "/app/CAS/build/cas"
        run_cmd = [cas_bin] + files_to_run

        try:
            res = subprocess.run(run_cmd, cwd=tmpdir, capture_output=True, text=True, timeout=10)
            output_msg = res.stdout if res.returncode == 0 else (res.stderr + "\n" + res.stdout)
            vcd_content = None
            vcd_filename = None

            for fname in os.listdir(tmpdir):
                if fname.endswith(".vcd"):
                    vcd_filename = fname
                    with open(os.path.join(tmpdir, fname), "r") as vf:
                        vcd_content = vf.read()

                    # If project is active, persist to project's waves/ folder with a timestamped prefix
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
                "output": output_msg,
                "vcd": vcd_content,
                "vcd_filename": vcd_filename
            }
        except Exception as e:
            return {"status": "error", "output": f"[CAS INVOCATION ERROR] {str(e)}", "vcd": None, "vcd_filename": None}

app.mount("/", StaticFiles(directory="static", html=True), name="static")