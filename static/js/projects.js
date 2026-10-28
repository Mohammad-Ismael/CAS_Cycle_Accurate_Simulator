window.StudioProjects = {
    activeProject: null,

    DEFAULT_SETTINGS: {
        top_module: "",
        timescale: "1ns",
        enable_profiler: false,
        enable_debug: false,
        enable_vcd: true,
        vcd_filename: "trace.vcd",
        vcd_levels: 0,
        enable_display: true,
        enable_monitor: true,
        enable_finish_msg: true
    },

    newProject() {
        if (confirm("Start a new scratchpad? Any unsaved edits will be discarded.")) {
            this.activeProject = null;
            document.getElementById('active-project-label').textContent = "[Unsaved Scratchpad]";
            if (window.WaveApp) window.WaveApp.clear();

            for (const key in window.StudioEditor.projectFiles) {
                delete window.StudioEditor.projectFiles[key];
            }
            window.StudioEditor.projectFiles["testbench.sv"] = "";
            window.StudioEditor.projectFiles["design.sv"] = "";
            window.StudioEditor.activeTbFile = "testbench.sv";
            window.StudioEditor.activeRtlFile = "design.sv";
            window.StudioEditor.tbEditor.setValue("");
            window.StudioEditor.rtlEditor.setValue("");
            window.StudioEditor.renderTabs();

            this.applySettings(this.DEFAULT_SETTINGS);
            this.refreshWaveDropdown([]);
        }
    },

    getCurrentSettings() {
        return {
            top_module: document.getElementById('cfg-top-module').value.trim(),
            timescale: document.getElementById('cfg-timescale').value,
            enable_profiler: document.getElementById('chk-profiler').checked,
            enable_debug: document.getElementById('chk-debug-trace').checked,
            enable_vcd: document.getElementById('chk-vcd-enable').checked,
            vcd_filename: document.getElementById('cfg-vcd-filename').value.trim() || "trace.vcd",
            vcd_levels: parseInt(document.getElementById('cfg-vcd-levels').value, 10) || 0,
            enable_display: document.getElementById('chk-opt-display').checked,
            enable_monitor: document.getElementById('chk-opt-monitor').checked,
            enable_finish_msg: document.getElementById('chk-opt-finish').checked
        };
    },

    applySettings(s) {
        const cfg = Object.assign({}, this.DEFAULT_SETTINGS, s || {});

        const topInput = document.getElementById('cfg-top-module');
        if (topInput) topInput.value = cfg.top_module || "";

        const tsSelect = document.getElementById('cfg-timescale');
        if (tsSelect) {
            tsSelect.value = cfg.timescale || "1ns";
            const tsLbl = document.getElementById('lbl-timescale');
            if (tsLbl) tsLbl.textContent = cfg.timescale || "1ns";
        }

        const chkProf = document.getElementById('chk-profiler');
        if (chkProf) chkProf.checked = Boolean(cfg.enable_profiler);

        const chkDebug = document.getElementById('chk-debug-trace');
        if (chkDebug) chkDebug.checked = Boolean(cfg.enable_debug);

        const chkVcd = document.getElementById('chk-vcd-enable');
        if (chkVcd) {
            chkVcd.checked = Boolean(cfg.enable_vcd);
            const drawer = document.getElementById('vcd-settings-drawer');
            if (drawer) {
                if (chkVcd.checked) drawer.classList.remove('collapsed');
                else drawer.classList.add('collapsed');
            }
        }

        const vcdFileInput = document.getElementById('cfg-vcd-filename');
        if (vcdFileInput) {
            vcdFileInput.value = cfg.vcd_filename || "trace.vcd";
        }

        const vcdLvlSelect = document.getElementById('cfg-vcd-levels');
        if (vcdLvlSelect) {
            vcdLvlSelect.value = String(cfg.vcd_levels !== undefined ? cfg.vcd_levels : 0);
        }

        const chkDisp = document.getElementById('chk-opt-display');
        if (chkDisp) chkDisp.checked = Boolean(cfg.enable_display);

        const chkMon = document.getElementById('chk-opt-monitor');
        if (chkMon) chkMon.checked = Boolean(cfg.enable_monitor);

        const chkFin = document.getElementById('chk-opt-finish');
        if (chkFin) chkFin.checked = Boolean(cfg.enable_finish_msg);
    },

    async load(name) {
        this.closeModal();
        const res = await fetch(`/api/projects/${name}`);
        if (!res.ok) {
            alert("Failed to load project: " + name);
            return;
        }
        const data = await res.json();

        this.activeProject = data.project_name;
        document.getElementById('active-project-label').textContent = this.activeProject;

        // 1. Reset and load project source files
        for (const key in window.StudioEditor.projectFiles) {
            delete window.StudioEditor.projectFiles[key];
        }
        Object.assign(window.StudioEditor.projectFiles, data.files);

        // 2. Load and apply project settings immediately
        this.applySettings(data.settings);

        // 3. Restore active editor tabs
        const status = data.status || {};
        const targetTb = (status.active_tb_file && data.files[status.active_tb_file]) 
            ? status.active_tb_file 
            : (data.files["testbench.sv"] ? "testbench.sv" : Object.keys(data.files)[0]);

        const targetRtl = (status.active_rtl_file && data.files[status.active_rtl_file]) 
            ? status.active_rtl_file 
            : (data.files["design.sv"] ? "design.sv" : Object.keys(data.files)[0]);

        window.StudioEditor.activeTbFile = targetTb;
        window.StudioEditor.activeRtlFile = targetRtl;

        window.StudioEditor.tbEditor.setValue(window.StudioEditor.projectFiles[targetTb] || "");
        window.StudioEditor.rtlEditor.setValue(window.StudioEditor.projectFiles[targetRtl] || "");
        window.StudioEditor.renderTabs();

        // 4. Populate waves dropdown and load selected trace
        this.refreshWaveDropdown(data.waves);
        if (window.WaveApp) window.WaveApp.clear();

        if (data.waves && data.waves.length > 0) {
            const waveToLoad = (status.selected_wave && data.waves.some(w => w.filename === status.selected_wave))
                ? status.selected_wave
                : data.waves[0].filename;
            this.loadVcd(waveToLoad);
            const sel = document.getElementById('wave-select');
            if (sel) sel.value = waveToLoad;
        }
    },

    async save() {
        let name = this.activeProject;
        if (!name) {
            name = prompt("Enter project name:");
            if (!name) return;
        }

        window.StudioEditor.projectFiles[window.StudioEditor.activeTbFile] = window.StudioEditor.tbEditor.getValue();
        window.StudioEditor.projectFiles[window.StudioEditor.activeRtlFile] = window.StudioEditor.rtlEditor.getValue();

        const currentSettings = this.getCurrentSettings();
        const currentStatus = {
            active_tb_file: window.StudioEditor.activeTbFile,
            active_rtl_file: window.StudioEditor.activeRtlFile,
            selected_wave: document.getElementById('wave-select') ? document.getElementById('wave-select').value : ""
        };

        const res = await fetch('/api/projects/save', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                project_name: name,
                files: window.StudioEditor.projectFiles,
                settings: currentSettings,
                status: currentStatus
            })
        });

        const data = await res.json();
        if (data.status === "success") {
            this.activeProject = data.project_name;
            document.getElementById('active-project-label').textContent = this.activeProject;
            window.StudioConsole.render(`[CAS] Project '${this.activeProject}' saved cleanly with all files & settings.\n`);
        } else {
            alert("Error saving project: " + (data.detail || "Unknown error"));
        }
    },

    async renamePrompt(e, currentName) {
        if (e) e.stopPropagation();
        const newName = prompt(`Enter new name for project '${currentName}':`, currentName);
        if (!newName || !newName.trim() || newName.trim() === currentName) return;

        const res = await fetch('/api/projects/rename', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                old_name: currentName,
                new_name: newName.trim()
            })
        });

        const data = await res.json();
        if (res.ok && data.status === "success") {
            if (this.activeProject === currentName) {
                this.activeProject = data.new_name;
                document.getElementById('active-project-label').textContent = this.activeProject;
            }
            this.openModal();
        } else {
            alert("Rename failed: " + (data.detail || "Unknown error"));
        }
    },

    async openModal() {
        const modal = document.getElementById('projects-modal');
        const list = document.getElementById('projects-list');
        list.innerHTML = "Loading...";
        modal.classList.add('open');

        const res = await fetch('/api/projects');
        const data = await res.json();
        list.innerHTML = "";

        if (data.projects.length === 0) {
            list.innerHTML = "<div style='color: var(--text-muted); font-size: 12px;'>No projects saved yet.</div>";
            return;
        }

        data.projects.forEach(p => {
            const row = document.createElement('div');
            row.className = 'project-item';
            row.innerHTML = `
                <div style="flex: 1; overflow: hidden; cursor: pointer;">
                    <div class="project-item-title">${p.name}</div>
                    <div class="project-item-meta">Modified: ${p.last_modified} | ${p.vcd_count} waveform(s)</div>
                </div>
                <div style="display: flex; gap: 6px;">
                    <button class="btn btn-secondary" style="font-size: 11px; padding: 3px 8px;" onclick="StudioProjects.renamePrompt(event, '${p.name}')">Rename</button>
                    <button class="btn btn-secondary" style="color: var(--accent-red); font-size: 11px; padding: 3px 8px;" onclick="StudioProjects.delete(event, '${p.name}')">Delete</button>
                </div>
            `;
            row.querySelector('div:first-child').onclick = () => this.load(p.name);
            list.appendChild(row);
        });
    },

    closeModal() {
        document.getElementById('projects-modal').classList.remove('open');
    },

    async delete(e, name) {
        if (e) e.stopPropagation();
        if (confirm(`Are you sure you want to permanently delete project '${name}' and all its files?`)) {
            const res = await fetch(`/api/projects/${name}`, { method: 'DELETE' });
            if (res.ok) {
                if (this.activeProject === name) {
                    this.newProject();
                }
                this.openModal();
            } else {
                alert("Failed to delete project.");
            }
        }
    },

    refreshWaveDropdown(waves) {
        const sel = document.getElementById('wave-select');
        if (!sel) return;
        sel.innerHTML = "";
        if (!waves || waves.length === 0) {
            sel.innerHTML = '<option value="">(No traces saved)</option>';
            return;
        }
        waves.forEach(w => {
            const opt = document.createElement('option');
            opt.value = w.filename;
            opt.textContent = `${w.filename} (${w.timestamp})`;
            sel.appendChild(opt);
        });
    },

    async loadVcd(filename) {
        if (!this.activeProject || !filename) return;
        try {
            const res = await fetch(`/api/projects/${this.activeProject}/waves/${filename}`);
            if (!res.ok) return;
            const data = await res.json();
            
            if (window.WaveApp) {
                window.WaveApp.loadTrace(data.vcd, filename);
            }
        } catch (e) {
            console.error("Failed to load VCD file:", e);
        }
    },

    async deleteSelectedWave() {
        const sel = document.getElementById('wave-select');
        if (!sel) return;
        const fname = sel.value;
        if (!fname || !this.activeProject) return;

        if (confirm(`Delete waveform '${fname}'?`)) {
            const res = await fetch(`/api/projects/${this.activeProject}/waves/${fname}`, { method: 'DELETE' });
            if (res.ok) {
                const pres = await fetch(`/api/projects/${this.activeProject}`);
                if (pres.ok) {
                    const pdata = await pres.json();
                    this.refreshWaveDropdown(pdata.waves || []);

                    if (window.WaveApp && window.WaveApp.rawVcdFilename === fname) {
                        if (pdata.waves && pdata.waves.length > 0) {
                            this.loadVcd(pdata.waves[0].filename);
                            sel.value = pdata.waves[0].filename;
                        } else {
                            window.WaveApp.clear();
                        }
                    }
                }
            } else {
                alert("Failed to delete waveform file.");
            }
        }
    }
};

window.newProject = () => window.StudioProjects.newProject();
window.saveProjectPrompt = () => window.StudioProjects.save();
window.openProjectsModal = () => window.StudioProjects.openModal();
window.closeProjectsModal = () => window.StudioProjects.closeModal();
window.onSelectWaveTrace = (fname) => {
    if (!fname) {
        if (window.WaveApp) window.WaveApp.clear();
    } else {
        window.StudioProjects.loadVcd(fname);
        if (window.StudioApp) {
            window.StudioApp.selectTab('waveform');
        }
    }
};
window.deleteSelectedWave = () => window.StudioProjects.deleteSelectedWave();

window.downloadActiveVcd = () => {
    if (!window.WaveApp || !window.WaveApp.rawVcdContent) return;
    const blob = new Blob([window.WaveApp.rawVcdContent], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = window.WaveApp.rawVcdFilename || "trace.vcd";
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
};