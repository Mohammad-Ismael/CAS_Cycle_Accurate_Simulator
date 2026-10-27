window.StudioProjects = {
    activeProject: null,

    newProject() {
        if (confirm("Start a new scratchpad? Any unsaved edits will be discarded.")) {
            this.activeProject = null;
            document.getElementById('active-project-label').textContent = "[Unsaved Scratchpad]";
            window.StudioWaveform.clear();

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
            this.refreshWaveDropdown([]);
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

        const res = await fetch('/api/projects/save', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                project_name: name,
                files: window.StudioEditor.projectFiles
            })
        });
        const data = await res.json();
        if (data.status === "success") {
            this.activeProject = data.project_name;
            document.getElementById('active-project-label').textContent = this.activeProject;
            alert(`Project '${this.activeProject}' (${Object.keys(window.StudioEditor.projectFiles).length} file(s)) saved.`);
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
                <div>
                    <div class="project-item-title">${p.name}</div>
                    <div class="project-item-meta">Modified: ${p.last_modified} | ${p.vcd_count} waveform(s)</div>
                </div>
                <button class="btn btn-secondary" style="color: var(--accent-red); font-size: 11px;" onclick="StudioProjects.delete(event, '${p.name}')">Delete</button>
            `;
            row.onclick = () => this.load(p.name);
            list.appendChild(row);
        });
    },

    closeModal() {
        document.getElementById('projects-modal').classList.remove('open');
    },

    async load(name) {
        this.closeModal();
        const res = await fetch(`/api/projects/${name}`);
        const data = await res.json();

        this.activeProject = data.project_name;
        document.getElementById('active-project-label').textContent = this.activeProject;

        for (const key in window.StudioEditor.projectFiles) {
            delete window.StudioEditor.projectFiles[key];
        }
        Object.assign(window.StudioEditor.projectFiles, data.files);

        window.StudioEditor.activeTbFile = window.StudioEditor.projectFiles["testbench.sv"] ? "testbench.sv" : Object.keys(window.StudioEditor.projectFiles)[0];
        window.StudioEditor.activeRtlFile = window.StudioEditor.projectFiles["design.sv"] ? "design.sv" : Object.keys(window.StudioEditor.projectFiles)[0];

        window.StudioEditor.tbEditor.setValue(window.StudioEditor.projectFiles[window.StudioEditor.activeTbFile]);
        window.StudioEditor.rtlEditor.setValue(window.StudioEditor.projectFiles[window.StudioEditor.activeRtlFile]);
        window.StudioEditor.renderTabs();

        this.refreshWaveDropdown(data.waves);
        window.StudioWaveform.clear();

        if (data.waves.length > 0) {
            this.loadVcd(data.waves[0].filename);
        }
    },

    async delete(e, name) {
        e.stopPropagation();
        if (confirm(`Delete project '${name}' and all its waveform history?`)) {
            await fetch(`/api/projects/${name}`, { method: 'DELETE' });
            if (this.activeProject === name) this.newProject();
            this.openModal();
        }
    },

    refreshWaveDropdown(waves) {
        const sel = document.getElementById('wave-select');
        sel.innerHTML = "";
        if (waves.length === 0) {
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
        if (!this.activeProject) return;
        const res = await fetch(`/api/projects/${this.activeProject}/waves/${filename}`);
        const data = await res.json();
        window.StudioWaveform.activeVcdContent = data.vcd;
        window.StudioWaveform.activeVcdFilename = filename;
        document.getElementById('btn-export-vcd').disabled = false;
        document.getElementById('panel-vcd-raw').textContent = data.vcd;
        window.StudioWaveform.render();
    },

    async deleteSelectedWave() {
        const sel = document.getElementById('wave-select');
        const fname = sel.value;
        if (!fname || !this.activeProject) return;
        if (confirm(`Delete waveform '${fname}'?`)) {
            await fetch(`/api/projects/${this.activeProject}/waves/${fname}`, { method: 'DELETE' });
            this.load(this.activeProject);
        }
    }
};

window.newProject = () => window.StudioProjects.newProject();
window.saveProjectPrompt = () => window.StudioProjects.save();
window.openProjectsModal = () => window.StudioProjects.openModal();
window.closeProjectsModal = () => window.StudioProjects.closeModal();
window.onSelectWaveTrace = (fname) => {
    if (!fname) window.StudioWaveform.clear();
    else window.StudioProjects.loadVcd(fname);
};
window.deleteSelectedWave = () => window.StudioProjects.deleteSelectedWave();