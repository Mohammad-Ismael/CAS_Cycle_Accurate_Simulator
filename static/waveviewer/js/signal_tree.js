window.WaveSignalTree = {
    selectedKeys: [],

    init(signalList) {
        this.selectedKeys = signalList.slice(0, 24).map(s => s.fullPath);
    },

    renderList(container, signals, curTime) {
        if (!container) return;
        container.innerHTML = '';

        this.selectedKeys.forEach(k => {
            const sig = signals[k];
            if (!sig) return;

            const isBus = sig.size > 1;
            const val = window.WaveCursorManager.getValueAtTime(sig, curTime);

            const row = document.createElement('div');
            row.className = 'wv-sig-row';
            row.title = sig.fullPath;
            row.innerHTML = `
                <div style="display:flex; align-items:center; gap:6px; overflow:hidden;">
                    <span style="color:${isBus ? '#38bdf8' : '#10b981'}; font-weight:600;">${sig.name}</span>
                    <span class="wv-sig-meta">${isBus ? '[' + (sig.size-1) + ':0]' : 'wire'}</span>
                </div>
                <span class="wv-val-cell">${val}</span>
            `;
            container.appendChild(row);
        });
    }
};