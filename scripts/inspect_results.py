
with open('/tmp/sim750_events.log') as f:
    for line in f:
        if any(k in line for k in ['DREQ_DEBUG', 'CHAIN_PROBE', 'CFG_DIAG', 'MMU_DIAG', 'AXI_AR', 'AXI_R', 'COMMIT_EVT', 'IC_REQ']):
            print(line.strip())
