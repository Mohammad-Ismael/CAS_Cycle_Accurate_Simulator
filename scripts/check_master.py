import subprocess

try:
    res = subprocess.run([
        '/app/CAS/build/bin/cas',
        '-f', '/app/scripts/zetraa_cas_files.f',
        '/app/scripts/ariane_cas_tb.sv',
        '--top', 'ariane_cas_tb',
        '-sv_lib', '/app/projects/ZETRAA/work-dpi/ariane_dpi.so',
        '--max-cycles', '1'
    ], capture_output=True, text=True, timeout=40)
    print("Returncode:", res.returncode)
    print("STDOUT tail:\n", res.stdout[-1000:])
    print("STDERR tail:\n", res.stderr[-1000:])
except Exception as e:
    print("Exception:", e)
