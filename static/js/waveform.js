window.StudioWaveform = {
    activeVcdContent: null,
    activeVcdFilename: null,

    render() {
        if (!this.activeVcdContent) {
            document.getElementById('wave-empty').style.display = "flex";
            return;
        }
        document.getElementById('wave-empty').style.display = "none";

        const namesContainer = document.getElementById('wave-names');
        const canvas = document.getElementById('wave-canvas');
        const ctx = canvas.getContext('2d');
        namesContainer.innerHTML = '';

        const lines = this.activeVcdContent.split('\n');
        const idToVar = {};
        const signals = [];
        let maxTime = 0;
        let inVars = false;

        for (let line of lines) {
            line = line.trim();
            if (line.startsWith('$var')) {
                const parts = line.split(/\s+/);
                const width = parseInt(parts[2]);
                const id = parts[3];
                const name = parts[4];
                const sig = { id, name, width, changes: [] };
                idToVar[id] = sig;
                signals.push(sig);
            } else if (line.startsWith('$enddefinitions')) {
                inVars = true;
            } else if (inVars && line.startsWith('#')) {
                const t = parseInt(line.substring(1));
                if (t > maxTime) maxTime = t;
            }
        }

        if (maxTime === 0) maxTime = 50;

        let curTime = 0;
        for (let line of lines) {
            line = line.trim();
            if (!line || line.startsWith('$')) continue;
            if (line.startsWith('#')) {
                curTime = parseInt(line.substring(1));
                continue;
            }

            if (line.startsWith('b')) {
                const parts = line.substring(1).split(/\s+/);
                const val = parts[0];
                const id = parts[1];
                if (idToVar[id]) {
                    const isX = val.includes('x') || val.includes('z');
                    idToVar[id].changes.push({ time: curTime, val: isX ? 'x' : parseInt(val, 2), raw: val });
                }
            } else {
                const charVal = line[0].toLowerCase();
                const id = line.substring(1);
                if (idToVar[id]) {
                    idToVar[id].changes.push({ time: curTime, val: charVal });
                }
            }
        }

        const rowHeight = 36;
        const timeHeaderHeight = 28;
        const timeStepPx = 14;
        const totalWidth = Math.max(800, (maxTime + 10) * timeStepPx);
        const totalHeight = timeHeaderHeight + (signals.length * rowHeight);

        canvas.width = totalWidth;
        canvas.height = totalHeight;

        signals.forEach(s => {
            const div = document.createElement('div');
            div.className = 'wave-sig-row';
            div.innerHTML = `<span>${s.name}</span><span class="wave-sig-type">${s.width > 1 ? '[' + (s.width-1) + ':0]' : 'wire'}</span>`;
            namesContainer.appendChild(div);
        });

        ctx.fillStyle = '#0b0c0e';
        ctx.fillRect(0, 0, totalWidth, totalHeight);

        ctx.fillStyle = '#16171d';
        ctx.fillRect(0, 0, totalWidth, timeHeaderHeight);
        ctx.strokeStyle = '#272a33';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(0, timeHeaderHeight);
        ctx.lineTo(totalWidth, timeHeaderHeight);
        ctx.stroke();

        ctx.font = '10px JetBrains Mono';
        ctx.fillStyle = '#8b949e';

        for (let t = 0; t <= maxTime + 5; t += 5) {
            const x = t * timeStepPx;
            ctx.beginPath();
            ctx.moveTo(x, timeHeaderHeight - 6);
            ctx.lineTo(x, totalHeight);
            ctx.strokeStyle = (t % 10 === 0) ? '#272a33' : '#1a1b22';
            ctx.stroke();
            ctx.fillText(t + 'ns', x + 3, timeHeaderHeight - 8);
        }

        signals.forEach((sig, idx) => {
            const yTop = timeHeaderHeight + (idx * rowHeight);
            const yMid = yTop + (rowHeight / 2);
            const yHigh = yTop + 7;
            const yLow = yTop + rowHeight - 7;

            ctx.strokeStyle = '#181920';
            ctx.beginPath();
            ctx.moveTo(0, yTop + rowHeight);
            ctx.lineTo(totalWidth, yTop + rowHeight);
            ctx.stroke();

            if (sig.changes.length === 0) return;

            if (sig.width === 1) {
                for (let i = 0; i < sig.changes.length; i++) {
                    const c = sig.changes[i];
                    const nextTime = (i + 1 < sig.changes.length) ? sig.changes[i+1].time : (maxTime + 5);
                    const xStart = c.time * timeStepPx;
                    const xEnd = nextTime * timeStepPx;

                    ctx.lineWidth = 2;
                    if (c.val === 'x' || c.val === 'z') {
                        ctx.strokeStyle = '#ef4444';
                        ctx.beginPath();
                        ctx.moveTo(xStart, yMid);
                        ctx.lineTo(xEnd, yMid);
                        ctx.stroke();
                    } else {
                        ctx.strokeStyle = '#10b981';
                        const yVal = (c.val === '1' || c.val === 1) ? yHigh : yLow;
                        ctx.beginPath();
                        ctx.moveTo(xStart, yVal);
                        ctx.lineTo(xEnd, yVal);
                        ctx.stroke();
                    }

                    if (i + 1 < sig.changes.length) {
                        const nextVal = sig.changes[i+1].val;
                        const yCur = (c.val === 'x' || c.val === 'z') ? yMid : ((c.val === '1' || c.val === 1) ? yHigh : yLow);
                        const yNext = (nextVal === 'x' || nextVal === 'z') ? yMid : ((nextVal === '1' || nextVal === 1) ? yHigh : yLow);
                        ctx.strokeStyle = (nextVal === 'x' || c.val === 'x') ? '#ef4444' : '#10b981';
                        ctx.beginPath();
                        ctx.moveTo(xEnd, yCur);
                        ctx.lineTo(xEnd, yNext);
                        ctx.stroke();
                    }
                }
            } else {
                for (let i = 0; i < sig.changes.length; i++) {
                    const c = sig.changes[i];
                    const nextTime = (i + 1 < sig.changes.length) ? sig.changes[i+1].time : (maxTime + 5);
                    const xStart = c.time * timeStepPx;
                    const xEnd = nextTime * timeStepPx;
                    const boxW = xEnd - xStart;

                    if (boxW <= 0) continue;

                    const isX = (c.val === 'x');
                    ctx.strokeStyle = isX ? '#ef4444' : '#38bdf8';
                    ctx.fillStyle = isX ? '#450a0a' : '#1e293b';
                    ctx.lineWidth = 1.5;

                    ctx.beginPath();
                    ctx.moveTo(xStart + 3, yHigh);
                    ctx.lineTo(xEnd - 3, yHigh);
                    ctx.lineTo(xEnd, yMid);
                    ctx.lineTo(xEnd - 3, yLow);
                    ctx.lineTo(xStart + 3, yLow);
                    ctx.lineTo(xStart, yMid);
                    ctx.closePath();
                    ctx.fill();
                    ctx.stroke();

                    ctx.fillStyle = isX ? '#fca5a5' : '#e2e8f0';
                    ctx.font = '11px JetBrains Mono';
                    const label = isX ? "X" : ("0x" + c.val.toString(16).toUpperCase());
                    if (boxW > 25) ctx.fillText(label, xStart + 8, yMid + 4);
                }
            }
        });
    },

    clear() {
        this.activeVcdContent = null;
        this.activeVcdFilename = null;
        document.getElementById('btn-export-vcd').disabled = true;
        document.getElementById('panel-vcd-raw').textContent = "// No waveform trace generated for this run.";
        document.getElementById('wave-empty').style.display = "flex";
        document.getElementById('wave-names').innerHTML = "";
        const canvas = document.getElementById('wave-canvas');
        const ctx = canvas.getContext('2d');
        ctx.clearRect(0, 0, canvas.width, canvas.height);
    },

    download() {
        if (!this.activeVcdContent) return;
        const blob = new Blob([this.activeVcdContent], { type: 'text/plain' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = this.activeVcdFilename || "trace.vcd";
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }
};

window.downloadActiveVcd = () => window.StudioWaveform.download();