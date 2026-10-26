from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
import subprocess
import tempfile
import os

app = FastAPI()

class SimRequest(BaseModel):
    design_code: str
    tb_code: str
    top_module: str

@app.post("/api/simulate")
def simulate(req: SimRequest):
    if not req.design_code.strip() and not req.tb_code.strip():
        return {"status": "error", "output": "[ERROR] Both RTL and Testbench are empty."}

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
            res = subprocess.run(run_cmd, capture_output=True, text=True, timeout=10)
            return {
                "status": "success" if res.returncode == 0 else "error",
                "output": res.stdout if res.returncode == 0 else (res.stderr + "\n" + res.stdout)
            }
        except Exception as e:
            return {"status": "error", "output": f"[CAS INVOCATION ERROR] {str(e)}"}

app.mount("/", StaticFiles(directory="static", html=True), name="static")