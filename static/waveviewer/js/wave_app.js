window.WaveApp = {
    canvas: null,
    ctx: null,
    stepPx: 14,
    rulerHeight: 28,
    rowHeight: 34,
    parsedVcd: null,

    init() {
        this.canvas = document.getElementById('wv-canvas');
        if (!this.canvas) return;
        this.ctx = this.canvas.getContext('2d');
        this.bindEvents();
    },

    loadTrace(vcdText, filename = "trace.vcd") {
        if (!this.canvas || !this.ctx) {
            this.init();
        }
        if (!this.canvas) return;

        this.rawVcdContent = vcdText;
        this.rawVcdFilename = filename;

        this.parsedVcd = window.VcdParser.parse(vcdText);
        window.WaveSignalTree.init(this.parsedVcd.signalList);
        window.WaveCursorManager.setPrimary(0);

        const overlay = document.getElementById('wv-empty-overlay');
        if (overlay) overlay.style.display = 'none';

        const exportBtn = document.getElementById('btn-export-vcd');
        if (exportBtn) exportBtn.disabled = false;

        this.updateLabels();
        this.render();
    },

    bindEvents() {
        this.canvas.addEventListener('click', (e) => {
            const rect = this.canvas.getBoundingClientRect();
            const t = Math.round((e.clientX - rect.left) / this.stepPx);
            window.WaveCursorManager.setPrimary(t);
            this.updateLabels();
            this.render();
        });

        this.canvas.addEventListener('mousemove', (e) => {
            const rect = this.canvas.getBoundingClientRect();
            const t = Math.round((e.clientX - rect.left) / this.stepPx);
            window.WaveCursorManager.setHover(t);
            this.render();
        });

        this.canvas.addEventListener('mouseleave', () => {
            window.WaveCursorManager.setHover(null);
            this.render();
        });

        const vp = document.getElementById('wv-viewport');
        const sb = document.getElementById('wv-sidebar-list');
        if (vp && sb) {
            vp.addEventListener('scroll', () => { sb.scrollTop = vp.scrollTop; });
        }
    },

    loadTrace(vcdText) {
        this.parsedVcd = window.VcdParser.parse(vcdText);
        window.WaveSignalTree.init(this.parsedVcd.signalList);
        window.WaveCursorManager.setPrimary(0);
        document.getElementById('wv-empty-overlay').style.display = 'none';
        this.updateLabels();
        this.render();
    },

    clear() {
        this.parsedVcd = null;
        document.getElementById('wv-empty-overlay').style.display = 'flex';
        document.getElementById('wv-sidebar-list').innerHTML = '';
        if (this.ctx) this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
    },

    zoom(delta) {
        this.stepPx = Math.max(4, Math.min(60, this.stepPx + delta));
        this.render();
    },

    updateLabels() {
        const lbl = document.getElementById('wv-lbl-cursor');
        if (lbl) lbl.textContent = `${window.WaveCursorManager.primaryTime} ns`;
    },

    render() {
        if (!this.parsedVcd || !this.canvas) return;
        const sigs = this.parsedVcd.signals;
        const activeKeys = window.WaveSignalTree.selectedKeys;
        const maxTime = this.parsedVcd.maxTime;

        const totalW = Math.max(900, (maxTime + 10) * this.stepPx);
        const totalH = this.rulerHeight + (activeKeys.length * this.rowHeight);

        this.canvas.width = totalW;
        this.canvas.height = totalH;

        window.WaveSignalTree.renderList(
            document.getElementById('wv-sidebar-list'),
            sigs,
            window.WaveCursorManager.primaryTime
        );

        this.ctx.fillStyle = '#0b0c0e';
        this.ctx.fillRect(0, 0, totalW, totalH);

        window.WaveRuler.drawBackgroundGrid(this.ctx, totalW, totalH, this.rulerHeight, maxTime, this.stepPx);

        activeKeys.forEach((key, idx) => {
            const sig = sigs[key];
            if (!sig) return;
            const yTop = this.rulerHeight + (idx * this.rowHeight);

            this.ctx.strokeStyle = '#181a22';
            this.ctx.beginPath();
            this.ctx.moveTo(0, yTop + this.rowHeight);
            this.ctx.lineTo(totalW, yTop + this.rowHeight);
            this.ctx.stroke();

            if (sig.size === 1) {
                window.WaveSignalRenderer.draw1Bit(this.ctx, sig.changes, maxTime, this.stepPx, yTop, this.rowHeight);
            } else {
                window.WaveSignalRenderer.drawBus(this.ctx, sig.changes, maxTime, this.stepPx, yTop, this.rowHeight);
            }
        });

        window.WaveRuler.draw(this.ctx, totalW, this.rulerHeight, maxTime, this.stepPx);
        window.WaveCursorManager.draw(this.ctx, totalH, this.stepPx);
    }
};