window.VcdParser = {
    parse(vcdText) {
        if (!vcdText || typeof vcdText !== 'string') {
            return { signals: {}, signalList: [], maxTime: 0, timescale: '1ns' };
        }

        const lines = vcdText.split(/\r?\n/);
        const idToSignal = {};
        const signals = {};
        const scopeStack = [];
        let inDefs = true;
        let curTime = 0;
        let maxTime = 0;
        let timescale = '1ns';

        for (let i = 0; i < lines.length; i++) {
            let line = lines[i].trim();
            if (!line) continue;

            if (inDefs) {
                if (line.startsWith('$timescale')) {
                    let tsText = line;
                    while (!tsText.includes('$end') && i + 1 < lines.length) {
                        i++;
                        tsText += ' ' + lines[i].trim();
                    }
                    const m = tsText.match(/\$timescale\s+([^\$]+)\s*\$end/);
                    if (m) timescale = m[1].trim();
                    continue;
                }
                if (line.startsWith('$scope')) {
                    const parts = line.split(/\s+/);
                    if (parts.length >= 3) scopeStack.push(parts[2]);
                    continue;
                }
                if (line.startsWith('$upscope')) {
                    if (scopeStack.length > 0) scopeStack.pop();
                    continue;
                }
                if (line.startsWith('$var')) {
                    const parts = line.split(/\s+/);
                    if (parts.length >= 5) {
                        const type = parts[1];
                        const size = parseInt(parts[2], 10) || 1;
                        const id = parts[3];
                        const name = parts[4];
                        const fullPath = scopeStack.length > 0 ? (scopeStack.join('.') + '.' + name) : name;

                        const sig = { id, name, fullPath, size, type, changes: [] };
                        idToSignal[id] = sig;
                        signals[fullPath] = sig;
                    }
                    continue;
                }
                if (line.startsWith('$enddefinitions')) {
                    inDefs = false;
                    continue;
                }
            } else {
                if (line.startsWith('#')) {
                    curTime = parseInt(line.substring(1), 10) || 0;
                    if (curTime > maxTime) maxTime = curTime;
                    continue;
                }
                if (line.startsWith('$dumpvars') || line.startsWith('$end')) continue;

                if (line.startsWith('b') || line.startsWith('B')) {
                    const parts = line.substring(1).trim().split(/\s+/);
                    if (parts.length >= 2 && idToSignal[parts[1]]) {
                        idToSignal[parts[1]].changes.push({ time: curTime, val: parts[0].toLowerCase() });
                    }
                    continue;
                }

                const bit = line[0].toLowerCase();
                const id = line.substring(1).trim();
                if (idToSignal[id]) {
                    idToSignal[id].changes.push({ time: curTime, val: bit });
                }
            }
        }

        return {
            signals,
            signalList: Object.values(signals),
            maxTime: Math.max(10, maxTime),
            timescale
        };
    }
};