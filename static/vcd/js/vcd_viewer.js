window.VcdViewer = {
    canvas: null,
    ctx: null,
    activeVcdContent: null,
    activeVcdFilename: null,

    parsedData: null,
    activeSignalKeys: [], // Full scoped names currently displayed

    // View metrics
    timeStepPx: 14,
    rowHeight: 34,
    timeHeaderHeight: 28,
    cursorTime: 0,

    init() {
        this.canvas = document.getElementById('vcd-canvas');
        if (this.canvas) {
            this.ctx = this.canvas.getContext('2d');
            this.setupEvents();
        }
    },

    setupEvents() {
        if (!this.canvas) return;

        this.canvas.addEventListener('click', (e) => {
            const rect = this.canvas.getBoundingClientRect();
            const clickX = e.clientX - rect.left;
            const t = Math.max(0, Math.round(clickX / this.timeStepPx));
            this.setCursorTime(t);
        });

        const viewport = document.getElementById('vcd-viewport-pane');
        const headerPane = document.getElementById('vcd-names-pane');
        if (viewport && headerPane) {
            viewport.addEventListener('scroll', () => {
                headerPane.scrollTop = viewport.scrollTop;
            });
        }
    },

    setCursorTime(t) {
        this.cursorTime = t;
        const lbl = document.getElementById('vcd-marker-time');
        if (lbl) lbl.textContent = `${t} ns`;
        this.render();
    },

    zoomIn() {
        this.timeStepPx = Math.min(80, this.timeStepPx + 4);
        this.render();
    },

    zoomOut() {
        this.timeStepPx = Math.max(4, this.timeStepPx - 4);
        this.render();
    },

    stepNext() {
        if (!this.parsedData) return;
        let nextTime = Infinity;
        this.activeSignalKeys.forEach(k => {
            const sig = this.parsedData.signals[k];
            if (sig) {
                sig.changes.forEach(c => {
                    if (c.time > this.cursorTime && c.time < nextTime) {
                        nextTime = c.time;
                    }
                });
            }
        });
        if (nextTime !== Infinity) this.setCursorTime(nextTime);
    },

    stepPrev() {
        if (!this.parsedData) return;
        let prevTime = -1;
        this.activeSignalKeys.forEach(k => {
            const sig = this.parsedData.signals[k];
            if (sig) {
                sig.changes.forEach(c => {
                    if (c.time < this.cursorTime && c.time > prevTime) {
                        prevTime = c.time;
                    }
                });
            }
        });
        if (prevTime !== -1) this.setCursorTime(prevTime);
    },

    loadTrace(vcdText, filename = "trace.vcd") {
        this.activeVcdContent = vcdText;
        this.activeVcdFilename = filename;
        this.parsedData = window.VcdParser.parse(vcdText);

        // Preload up to 16 signals on initial trace load
        this.activeSignalKeys = this.parsedData.signalList.slice(0, 16).map(s => s.fullName);
        this.cursorTime = 0;

        const tsLbl = document.getElementById('vcd-timescale-lbl');
        if (tsLbl && this.parsedData.timescale) {
            tsLbl.textContent = this.parsedData.timescale;
        }

        const exportBtn = document.getElementById('btn-export-vcd');
        if (exportBtn) exportBtn.disabled = false;

        this.render();
    },

    clear() {
        this.activeVcdContent = null;
        this.activeVcdFilename = null;
        this.parsedData = null;
        this.activeSignalKeys = [];
        const exportBtn = document.getElementById('btn-export-vcd');
        if (exportBtn) exportBtn.disabled = true;
        this.render();
    },

    getValueAtTime(sigObj, time) {
        if (!sigObj || !sigObj.changes || sigObj.changes.length === 0) return 'x';
        let val = 'x';
        for (let i = 0; i < sigObj.changes.length; i++) {
            if (sigObj.changes[i].time <= time) {
                val = sigObj.changes[i].value;
            } else {
                break;
            }
        }
        return val;
    },

    render() {
        if (!this.canvas) this.canvas = document.getElementById('vcd-canvas');
        if (!this.canvas) return;
        this.ctx = this.canvas.getContext('2d');

        const namesPane = document.getElementById('vcd-names-pane');
        const emptyState = document.getElementById('vcd-empty-state');

        if (!this.activeVcdContent || !this.parsedData) {
            if (emptyState) emptyState.style.display = 'flex';
            if (namesPane) namesPane.innerHTML = '';
            this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
            return;
        }

        if (emptyState) emptyState.style.display = 'none';

        const maxTime = Math.max(10, this.parsedData.maxTime);
        const totalW = Math.max(900, (maxTime + 10) * this.timeStepPx);
        const totalH = this.timeHeaderHeight + (this.activeSignalKeys.length * this.rowHeight);

        this.canvas.width = totalW;
        this.canvas.height = totalH;

        // 1. Render Left Names Pane
        if (namesPane) {
            namesPane.innerHTML = `
                <div class="vcd-header-row">
                    <span>SIGNAL</span>
                    <span>VAL @ T</span>
                </div>
            `;

            this.activeSignalKeys.forEach(k => {
                const sig = this.parsedData.signals[k];
                if (!sig) return;

                const isBus = sig.size > 1;
                const rawVal = this.getValueAtTime(sig, this.cursorTime);

                const row = document.createElement('div');
                row.className = 'vcd-sig-row';
                row.innerHTML = `
                    <div style="display: flex; align-items: center; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;" title="${sig.fullName}">
                        <span style="color: ${isBus ? '#38bdf8' : '#34d399'}; font-weight: 600;">${sig.name}</span>
                        <span class="vcd-sig-meta">${isBus ? '[' + (sig.size-1) + ':0]' : 'wire'}</span>
                    </div>
                    <span style="font-size: 10px; font-weight: 600; color: #fbbf24;">${rawVal}</span>
                `;
                namesPane.appendChild(row);
            });
        }

        // 2. Clear Canvas & Draw Time Ruler
        this.ctx.fillStyle = '#0b0c0e';
        this.ctx.fillRect(0, 0, totalW, totalH);

        this.ctx.fillStyle = '#14161b';
        this.ctx.fillRect(0, 0, totalW, this.timeHeaderHeight);
        this.ctx.strokeStyle = '#282b36';
        this.ctx.lineWidth = 1;
        this.ctx.beginPath();
        this.ctx.moveTo(0, this.timeHeaderHeight);
        this.ctx.lineTo(totalW, this.timeHeaderHeight);
        this.ctx.stroke();

        this.ctx.font = '10px JetBrains Mono';
        this.ctx.fillStyle = '#64748b';
        for (let t = 0; t <= maxTime + 10; t += 5) {
            const x = t * this.timeStepPx;
            this.ctx.beginPath();
            this.ctx.moveTo(x, this.timeHeaderHeight);
            this.ctx.lineTo(x, totalH);
            this.ctx.strokeStyle = (t % 10 === 0) ? '#1f232d' : '#14171e';
            this.ctx.stroke();

            if (t % 10 === 0) {
                this.ctx.fillText(`${t}ns`, x + 3, this.timeHeaderHeight - 8);
            }
        }

        // 3. Draw Signal Lanes
        this.activeSignalKeys.forEach((k, rowIdx) => {
            const sig = this.parsedData.signals[k];
            if (!sig) return;

            const isBus = sig.size > 1;
            const yTop = this.timeHeaderHeight + (rowIdx * this.rowHeight);
            const yMid = yTop + (this.rowHeight / 2);
            const yHigh = yTop + 7;
            const yLow = yTop + this.rowHeight - 7;

            // Lane Divider
            this.ctx.strokeStyle = '#181a20';
            this.ctx.beginPath();
            this.ctx.moveTo(0, yTop + this.rowHeight);
            this.ctx.lineTo(totalW, yTop + this.rowHeight);
            this.ctx.stroke();

            const changes = sig.changes || [];
            if (changes.length === 0) return;

            if (!isBus) {
                // 1-Bit Digital Wire
                for (let i = 0; i < changes.length; i++) {
                    const ev = changes[i];
                    const nextT = (i + 1 < changes.length) ? changes[i + 1].time : (maxTime + 10);
                    const xStart = ev.time * this.timeStepPx;
                    const xEnd = nextT * this.timeStepPx;

                    const isHigh = (ev.value === '1');
                    const isX = (ev.value === 'x' || ev.value === 'z');

                    this.ctx.lineWidth = 1.8;
                    if (isX) {
                        this.ctx.strokeStyle = '#f43f5e';
                        this.ctx.beginPath();
                        this.ctx.moveTo(xStart, yMid);
                        this.ctx.lineTo(xEnd, yMid);
                        this.ctx.stroke();
                    } else {
                        this.ctx.strokeStyle = '#34d399';
                        const yVal = isHigh ? yHigh : yLow;
                        this.ctx.beginPath();
                        this.ctx.moveTo(xStart, yVal);
                        this.ctx.lineTo(xEnd, yVal);
                        this.ctx.stroke();
                    }

                    if (i + 1 < changes.length) {
                        const nextEv = changes[i + 1];
                        const nextHigh = (nextEv.value === '1');
                        const nextX = (nextEv.value === 'x' || nextEv.value === 'z');

                        const yCur = isX ? yMid : (isHigh ? yHigh : yLow);
                        const yNext = nextX ? yMid : (nextHigh ? yHigh : yLow);

                        this.ctx.strokeStyle = (isX || nextX) ? '#f43f5e' : '#34d399';
                        this.ctx.beginPath();
                        this.ctx.moveTo(xEnd, yCur);
                        this.ctx.lineTo(xEnd, yNext);
                        this.ctx.stroke();
                    }
                }
            } else {
                // Multi-Bit Hex/Bin Bus
                for (let i = 0; i < changes.length; i++) {
                    const ev = changes[i];
                    const nextT = (i + 1 < changes.length) ? changes[i + 1].time : (maxTime + 10);
                    const xStart = ev.time * this.timeStepPx;
                    const xEnd = nextT * this.timeStepPx;
                    const boxW = xEnd - xStart;

                    if (boxW <= 0) continue;

                    const isX = ev.value.includes('x') || ev.value.includes('z');
                    this.ctx.strokeStyle = isX ? '#f43f5e' : '#38bdf8';
                    this.ctx.fillStyle = isX ? 'rgba(244, 63, 94, 0.15)' : 'rgba(56, 189, 248, 0.12)';
                    this.ctx.lineWidth = 1.5;

                    this.ctx.beginPath();
                    this.ctx.moveTo(xStart + 3, yHigh);
                    this.ctx.lineTo(xEnd - 3, yHigh);
                    this.ctx.lineTo(xEnd, yMid);
                    this.ctx.lineTo(xEnd - 3, yLow);
                    this.ctx.lineTo(xStart + 3, yLow);
                    this.ctx.lineTo(xStart, yMid);
                    this.ctx.closePath();
                    this.ctx.fill();
                    this.ctx.stroke();

                    this.ctx.fillStyle = isX ? '#fda4af' : '#e2e8f0';
                    this.ctx.font = '10.5px JetBrains Mono';
                    if (boxW > 24) {
                        this.ctx.fillText(ev.value, xStart + 6, yMid + 3.5);
                    }
                }
            }
        });

        // 4. Yellow Interactive Cursor
        const markerX = this.cursorTime * this.timeStepPx;
        this.ctx.strokeStyle = '#fbbf24';
        this.ctx.lineWidth = 1.5;
        this.ctx.beginPath();
        this.ctx.moveTo(markerX, 0);
        this.ctx.lineTo(markerX, totalH);
        this.ctx.stroke();
    }
};

window.downloadActiveVcd = () => {
    if (!window.VcdViewer.activeVcdContent) return;
    const blob = new Blob([window.VcdViewer.activeVcdContent], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = window.VcdViewer.activeVcdFilename || 'trace.vcd';
    a.click();
    URL.revokeObjectURL(url);
};