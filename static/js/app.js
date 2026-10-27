window.StudioApp = {
    isInspectorOpen: false,
    isOptionsOpen: true,

    init() {
        window.StudioEditor.init();
        this.initResizers();
    },

    selectTab(t) {
        document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
        document.querySelectorAll('.panel').forEach(p => p.classList.remove('active'));

        if (t === 'terminal') {
            document.getElementById('tab-btn-term').classList.add('active');
            document.getElementById('panel-terminal').classList.add('active');
        } else if (t === 'waveform') {
            document.getElementById('tab-btn-wave').classList.add('active');
            document.getElementById('panel-waveform').classList.add('active');
            window.StudioWaveform.render();
        } else if (t === 'vcd-raw') {
            document.getElementById('tab-btn-vcd').classList.add('active');
            document.getElementById('panel-vcd-raw').classList.add('active');
        }
    },

    toggleOptionsSidebar() {
        this.isOptionsOpen = !this.isOptionsOpen;
        const side = document.getElementById('options-sidebar');
        const resizer = document.getElementById('left-resizer');
        if (this.isOptionsOpen) {
            side.classList.remove('collapsed');
            resizer.style.display = 'block';
        } else {
            side.classList.add('collapsed');
            resizer.style.display = 'none';
        }
        window.StudioEditor.relayout();
    },

    toggleInspectorSidebar() {
        this.isInspectorOpen = !this.isInspectorOpen;
        const bar = document.getElementById('inspector-sidebar');
        const resizer = document.getElementById('right-resizer');
        const btn = document.getElementById('btn-toggle-inspector');

        if (this.isInspectorOpen) {
            bar.classList.add('open');
            resizer.style.display = 'block';
            btn.classList.add('btn-active');
        } else {
            bar.classList.remove('open');
            resizer.style.display = 'none';
            btn.classList.remove('btn-active');
        }
        window.StudioEditor.relayout();
    },

    switchSidebarTab(tab) {
        document.getElementById('btn-side-profiler').classList.remove('active');
        document.getElementById('btn-side-debug').classList.remove('active');
        document.getElementById('side-tab-profiler').style.display = 'none';
        document.getElementById('side-tab-debug').style.display = 'none';

        if (tab === 'profiler') {
            document.getElementById('btn-side-profiler').classList.add('active');
            document.getElementById('side-tab-profiler').style.display = 'flex';
        } else {
            document.getElementById('btn-side-debug').classList.add('active');
            document.getElementById('side-tab-debug').style.display = 'flex';
        }
    },

    async run() {
        const btnRun = document.getElementById('btn-run');
        const bulb = document.getElementById('status-bulb');
        const statusMsg = document.getElementById('status-msg');

        const topModule = document.getElementById('cfg-top-module').value;
        const timescale = document.getElementById('cfg-timescale').value;
        const enableProfiler = document.getElementById('chk-profiler').checked;
        const enableDebug = document.getElementById('chk-debug-trace').checked;
        const enableVcd = document.getElementById('chk-vcd-enable').checked;

        document.getElementById('lbl-timescale').textContent = timescale;

        window.StudioEditor.projectFiles[window.StudioEditor.activeTbFile] = window.StudioEditor.tbEditor.getValue();
        window.StudioEditor.projectFiles[window.StudioEditor.activeRtlFile] = window.StudioEditor.rtlEditor.getValue();

        btnRun.disabled = true;
        bulb.className = "status-indicator busy";
        statusMsg.textContent = "Simulating...";
        window.StudioConsole.render("[CAS] Executing simulation...\n");

        try {
            const res = await fetch('/api/simulate', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    files: window.StudioEditor.projectFiles,
                    entry_tb: window.StudioEditor.activeTbFile,
                    top_module: topModule,
                    project_name: window.StudioProjects.activeProject,
                    enable_profiler: enableProfiler,
                    enable_debug: enableDebug,
                    enable_vcd: enableVcd,
                    timescale: timescale
                })
            });

            const data = await res.json();
            window.StudioConsole.render(data.output);

            if (data.status === "success") {
                bulb.className = "status-indicator ready";
                statusMsg.textContent = "Simulation Converged";
                document.getElementById('debug-status-pill').textContent = "Converged";
                document.getElementById('debug-status-pill').style.background = "#064e3b";
                document.getElementById('debug-status-pill').style.color = "#34d399";
            } else {
                bulb.className = "status-indicator err";
                statusMsg.textContent = "Simulation Failed";
                document.getElementById('debug-status-pill').textContent = "Failed";
                document.getElementById('debug-status-pill').style.background = "#450a0a";
                document.getElementById('debug-status-pill').style.color = "#ef4444";
            }

            if (enableDebug && data.debug_log && data.debug_log.trim().length > 0) {
                document.getElementById('debug-trace-log').textContent = data.debug_log;
            } else {
                document.getElementById('debug-trace-log').textContent = "// Compiler Phase Trace disabled in Settings sidebar.";
            }

            if (enableProfiler && data.telemetry && data.telemetry.profiling_enabled) {
                const t = data.telemetry;
                document.getElementById('prof-wall').textContent = t.wallclock_ms.toFixed(2) + " ms";
                document.getElementById('prof-user').textContent = t.user_cpu_ms.toFixed(2) + " ms";
                document.getElementById('prof-sys').textContent = t.sys_cpu_ms.toFixed(2) + " ms";
                document.getElementById('prof-eff').textContent = t.cpu_util_pct.toFixed(1) + "% CPU";
                document.getElementById('prof-rss').textContent = (t.peak_rss_kb / 1024.0).toFixed(2) + " MB";
                document.getElementById('prof-src-bytes').textContent = t.source_bytes + " B";
                document.getElementById('prof-modules').textContent = t.modules_count;

                document.getElementById('prof-nets').textContent = t.nets_count + " / " + t.nets_cap;
                document.getElementById('prof-procs').textContent = t.procs_count;
                document.getElementById('prof-slices').textContent = t.slices_count;
                document.getElementById('prof-rams').textContent = t.rams_count;
                document.getElementById('prof-load').textContent = t.net_map_load.toFixed(2);

                document.getElementById('prof-events').textContent = t.events_dispatched;
                document.getElementById('prof-deltas').textContent = t.delta_cycles;
                document.getElementById('prof-nbas').textContent = t.nba_commits;
            } else {
                document.getElementById('prof-wall').textContent = "Bypassed";
                document.getElementById('prof-user').textContent = "--";
                document.getElementById('prof-sys').textContent = "--";
                document.getElementById('prof-eff').textContent = "Disabled";
                document.getElementById('prof-rss').textContent = "--";
                document.getElementById('prof-src-bytes').textContent = "--";
                document.getElementById('prof-modules').textContent = "--";
                document.getElementById('prof-nets').textContent = "--";
                document.getElementById('prof-procs').textContent = "--";
                document.getElementById('prof-slices').textContent = "--";
                document.getElementById('prof-rams').textContent = "--";
                document.getElementById('prof-load').textContent = "--";
                document.getElementById('prof-events').textContent = "--";
                document.getElementById('prof-deltas').textContent = "--";
                document.getElementById('prof-nbas').textContent = "--";
            }

            if (data.vcd && enableVcd) {
                window.StudioWaveform.activeVcdContent = data.vcd;
                window.StudioWaveform.activeVcdFilename = data.vcd_filename;
                document.getElementById('panel-vcd-raw').textContent = data.vcd;
                document.getElementById('btn-export-vcd').disabled = false;
                window.StudioWaveform.render();

                if (window.StudioProjects.activeProject) {
                    const pres = await fetch(`/api/projects/${window.StudioProjects.activeProject}`);
                    const pdata = await pres.json();
                    window.StudioProjects.refreshWaveDropdown(pdata.waves);
                    document.getElementById('wave-select').value = data.vcd_filename;
                }
            } else if (!enableVcd) {
                const sel = document.getElementById('wave-select');
                if (sel.value && sel.value !== "") {
                    window.StudioProjects.loadVcd(sel.value);
                } else if (window.StudioProjects.activeProject) {
                    const pres = await fetch(`/api/projects/${window.StudioProjects.activeProject}`);
                    const pdata = await pres.json();
                    window.StudioProjects.refreshWaveDropdown(pdata.waves);
                    if (pdata.waves.length > 0) window.StudioProjects.loadVcd(pdata.waves[0].filename);
                    else window.StudioWaveform.clear();
                } else {
                    window.StudioWaveform.clear();
                }
            } else {
                window.StudioWaveform.clear();
            }
        } catch (e) {
            window.StudioConsole.render("[UI FETCH ERROR] " + e.message);
            bulb.className = "status-indicator err";
            statusMsg.textContent = "Invocation Error";
        } finally {
            btnRun.disabled = false;
        }
    },

    initResizers() {
        const resizerMain = document.getElementById('resizer-main');
        const editorsWrapper = document.getElementById('editors-wrapper');
        const workspace = document.getElementById('workspace');
        let isDraggingV = false;
        resizerMain.addEventListener('mousedown', () => isDraggingV = true);

        const resizerH = document.getElementById('resizer-editors');
        const colTb = document.getElementById('col-tb');
        let isDraggingH = false;
        resizerH.addEventListener('mousedown', () => isDraggingH = true);

        const leftResizer = document.getElementById('left-resizer');
        const leftSidebar = document.getElementById('options-sidebar');
        let isDraggingLeft = false;
        leftResizer.addEventListener('mousedown', () => isDraggingLeft = true);

        const rightResizer = document.getElementById('right-resizer');
        const rightSidebar = document.getElementById('inspector-sidebar');
        let isDraggingRight = false;
        rightResizer.addEventListener('mousedown', () => isDraggingRight = true);

        window.addEventListener('mousemove', (e) => {
            if (isDraggingV) {
                const h = workspace.offsetHeight;
                const topH = ((e.clientY - workspace.offsetTop) / h) * 100;
                if (topH > 20 && topH < 85) {
                    editorsWrapper.style.height = topH + '%';
                    window.StudioEditor.relayout();
                }
            }
            if (isDraggingH) {
                const w = editorsWrapper.offsetWidth;
                const leftW = ((e.clientX - editorsWrapper.offsetLeft) / w) * 100;
                if (leftW > 20 && leftW < 80) {
                    colTb.style.flex = "none";
                    colTb.style.width = leftW + '%';
                    window.StudioEditor.relayout();
                }
            }
            if (isDraggingLeft) {
                const newW = e.clientX;
                if (newW > 180 && newW < 450) {
                    leftSidebar.style.width = newW + 'px';
                    window.StudioEditor.relayout();
                }
            }
            if (isDraggingRight) {
                const newW = window.innerWidth - e.clientX;
                if (newW > 240 && newW < 600) {
                    rightSidebar.style.width = newW + 'px';
                    window.StudioEditor.relayout();
                }
            }
        });

        window.addEventListener('mouseup', () => {
            if (isDraggingV || isDraggingH || isDraggingLeft || isDraggingRight) {
                window.StudioEditor.relayout();
            }
            isDraggingV = false;
            isDraggingH = false;
            isDraggingLeft = false;
            isDraggingRight = false;
        });
    }
};

window.onload = () => window.StudioApp.init();
window.selectTab = (t) => window.StudioApp.selectTab(t);
window.toggleOptionsSidebar = () => window.StudioApp.toggleOptionsSidebar();
window.toggleInspectorSidebar = () => window.StudioApp.toggleInspectorSidebar();
window.switchSidebarTab = (t) => window.StudioApp.switchSidebarTab(t);
window.executeSimulation = () => window.StudioApp.run();
window.promptNewFile = (p) => window.StudioEditor.promptNewFile(p);