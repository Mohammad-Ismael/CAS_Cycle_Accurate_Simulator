window.VcdParser = {
    parse(vcdText) {
        if (!vcdText || typeof vcdText !== 'string') {
            return { signals: {}, signalList: [], maxTime: 0, timescale: '1ns' };
        }

        const lines = vcdText.split(/\r?\n/);
        const idToSignal = {};
        const signals = {};
        const scopeStack = [];
        let inDefinitions = true;
        let currentTime = 0;
        let maxTime = 0;
        let timescale = '1ns';

        for (let i = 0; i < lines.length; i++) {
            let line = lines[i].trim();
            if (!line) continue;

            if (inDefinitions) {
                if (line.startsWith('$timescale')) {
                    // Extract timescale until $end
                    let tsContent = line;
                    while (!tsContent.includes('$end') && i + 1 < lines.length) {
                        i++;
                        tsContent += ' ' + lines[i].trim();
                    }
                    const m = tsContent.match(/\$timescale\s+([^\$]+)\s*\$end/);
                    if (m) timescale = m[1].trim();
                    continue;
                }

                if (line.startsWith('$scope')) {
                    const tokens = line.split(/\s+/);
                    if (tokens.length >= 3) {
                        scopeStack.push(tokens[2]);
                    }
                    continue;
                }

                if (line.startsWith('$upscope')) {
                    if (scopeStack.length > 0) scopeStack.pop();
                    continue;
                }

                if (line.startsWith('$var')) {
                    const tokens = line.split(/\s+/);
                    if (tokens.length >= 5) {
                        const type = tokens[1];
                        const size = parseInt(tokens[2], 10) || 1;
                        const id = tokens[3];
                        const rawName = tokens[4];

                        // Build scoped hierarchy name
                        const fullPath = scopeStack.length > 0 
                            ? (scopeStack.join('.') + '.' + rawName)
                            : rawName;

                        // Create clean display leaf name
                        const cleanLeaf = rawName;

                        const sigObj = {
                            id: id,
                            name: cleanLeaf,
                            fullName: fullPath,
                            size: size,
                            type: type,
                            changes: []
                        };

                        idToSignal[id] = sigObj;
                        signals[fullPath] = sigObj;
                    }
                    continue;
                }

                if (line.startsWith('$enddefinitions')) {
                    inDefinitions = false;
                    continue;
                }
            } else {
                // Value changes and simulation timestamps
                if (line.startsWith('#')) {
                    currentTime = parseInt(line.substring(1), 10) || 0;
                    if (currentTime > maxTime) maxTime = currentTime;
                    continue;
                }

                if (line.startsWith('$dumpvars') || line.startsWith('$end')) {
                    continue;
                }

                // Vector value change: b1010 identifier or bX identifier
                if (line.startsWith('b') || line.startsWith('B')) {
                    const tokens = line.substring(1).trim().split(/\s+/);
                    if (tokens.length >= 2) {
                        const val = tokens[0].toLowerCase();
                        const id = tokens[1];
                        if (idToSignal[id]) {
                            idToSignal[id].changes.push({ time: currentTime, value: val });
                        }
                    }
                    continue;
                }

                // Single-bit value change: 0identifier or 1identifier or xidentifier
                const bitVal = line[0].toLowerCase();
                const id = line.substring(1).trim();
                if (idToSignal[id]) {
                    idToSignal[id].changes.push({ time: currentTime, value: bitVal });
                }
            }
        }

        const signalList = Object.values(signals);

        return {
            signals: signals,
            signalList: signalList,
            maxTime: maxTime,
            timescale: timescale
        };
    }
};