window.StudioApp = {
    isInspectorOpen: false,
    isOptionsOpen: true,

    init() {
        window.StudioEditor.init();
        if (window.WaveApp) {
            window.WaveApp.init();
        }
        this.initResizers();
    },

    init() {
        window.StudioEditor.init();
        if (window.WaveApp) {
            window.WaveApp.init();
        }
        this.initResizers();
    },

    selectTab(t) {
        const btnTerm = document.getElementById('tab-btn-term');
        const btnWave = document.getElementById('tab-btn-wave');
        const panelTerm = document.getElementById('panel-terminal');
        const panelWave = document.getElementById('panel-waveform');

        btnTerm.classList.remove('active');
        btnWave.classList.remove('active');
        panelTerm.classList.remove('active');
        panelWave.classList.remove('active');
        panelTerm.style.display = 'none';
        panelWave.style.display = 'none';

        if (t === 'terminal') {
            btnTerm.classList.add('active');
            panelTerm.classList.add('active');
            panelTerm.style.display = 'block';
        } else if (t === 'waveform') {
            btnWave.classList.add('active');
            panelWave.classList.add('active');
            panelWave.style.display = 'flex';
            if (window.WaveApp) {
                window.WaveApp.render();
            }
        }
    },

    toggleInspectorSidebar() {
        this.isInspectorOpen = !this.isInspectorOpen;
        const bar = document.getElementById('inspector-sidebar');
        const resizer = document.getElementById('right-resizer');
        const btn = document.getElementById('btn-toggle-inspector');

        if (this.isInspectorOpen) {
            bar.classList.add('open');
            if (resizer) resizer.style.display = 'block';
            if (btn) btn.classList.add('btn-active');
        } else {
            bar.classList.remove('open');
            if (resizer) resizer.style.display = 'none';
            if (btn) btn.classList.remove('btn-active');
        }
        if (window.StudioEditor && typeof window.StudioEditor.relayout === 'function') {
            window.StudioEditor.relayout();
        }
    },

    switchSidebarTab(tab) {
        const btnProf = document.getElementById('btn-side-profiler');
        const btnDebug = document.getElementById('btn-side-debug');
        const tabProf = document.getElementById('side-tab-profiler');
        const tabDebug = document.getElementById('side-tab-debug');

        if (btnProf) btnProf.classList.remove('active');
        if (btnDebug) btnDebug.classList.remove('active');
        if (tabProf) tabProf.style.display = 'none';
        if (tabDebug) tabDebug.style.display = 'none';

        if (tab === 'profiler') {
            if (btnProf) btnProf.classList.add('active');
            if (tabProf) tabProf.style.display = 'flex';
        } else {
            if (btnDebug) btnDebug.classList.add('active');
            if (tabDebug) tabDebug.style.display = 'flex';
        }
    },

    toggleVcdSettingsDrawer(enabled) {
        const drawer = document.getElementById('vcd-settings-drawer');
        if (drawer) {
            if (enabled) drawer.classList.remove('collapsed');
            else drawer.classList.add('collapsed');
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

        // Custom VCD injection parameters
        const vcdFilename = document.getElementById('cfg-vcd-filename').value;
        const vcdLevels = parseInt(document.getElementById('cfg-vcd-levels').value, 10) || 0;

        const enableDisplay = document.getElementById('chk-opt-display').checked;
        const enableMonitor = document.getElementById('chk-opt-monitor').checked;
        const enableFinish = document.getElementById('chk-opt-finish').checked;

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
                    vcd_filename: vcdFilename,
                    vcd_levels: vcdLevels,
                    enable_display: enableDisplay,
                    enable_monitor: enableMonitor,
                    enable_finish_msg: enableFinish,
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
                window.WaveApp.rawVcdContent = data.vcd;
                window.WaveApp.rawVcdFilename = data.vcd_filename;
                document.getElementById('btn-export-vcd').disabled = false;
                window.WaveApp.loadTrace(data.vcd, data.vcd_filename);

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
                    else window.WaveApp.clear();
                } else {
                    window.WaveApp.clear();
                }
            } else {
                window.WaveApp.clear();
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
window.toggleVcdSettingsDrawer = (enabled) => window.StudioApp.toggleVcdSettingsDrawer(enabled);
window.executeSimulation = () => window.StudioApp.run();
window.promptNewFile = (p) => window.StudioEditor.promptNewFile(p);