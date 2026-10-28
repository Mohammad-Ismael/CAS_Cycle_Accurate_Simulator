window.WaveSignalRenderer = {
    draw1Bit(ctx, changes, maxTime, stepPx, yTop, rowHeight) {
        const yHigh = yTop + 6;
        const yMid = yTop + (rowHeight / 2);
        const yLow = yTop + rowHeight - 6;

        for (let i = 0; i < changes.length; i++) {
            const cur = changes[i];
            const nextTime = (i + 1 < changes.length) ? changes[i + 1].time : (maxTime + 10);
            const x1 = cur.time * stepPx;
            const x2 = nextTime * stepPx;
            const isX = (cur.val === 'x' || cur.val === 'z');
            const isHigh = (cur.val === '1');

            ctx.lineWidth = 1.8;
            if (isX) {
                ctx.strokeStyle = '#ef4444';
                ctx.beginPath();
                ctx.moveTo(x1, yMid);
                ctx.lineTo(x2, yMid);
                ctx.stroke();
            } else {
                ctx.strokeStyle = '#10b981';
                const y = isHigh ? yHigh : yLow;
                ctx.beginPath();
                ctx.moveTo(x1, y);
                ctx.lineTo(x2, y);
                ctx.stroke();
            }

            if (i + 1 < changes.length) {
                const nextVal = changes[i + 1].val;
                const nextIsX = (nextVal === 'x' || nextVal === 'z');
                const y1 = isX ? yMid : (isHigh ? yHigh : yLow);
                const y2 = nextIsX ? yMid : (nextVal === '1' ? yHigh : yLow);

                ctx.strokeStyle = (isX || nextIsX) ? '#ef4444' : '#10b981';
                ctx.beginPath();
                ctx.moveTo(x2, y1);
                ctx.lineTo(x2, y2);
                ctx.stroke();
            }
        }
    },

    drawBus(ctx, changes, maxTime, stepPx, yTop, rowHeight) {
        const yHigh = yTop + 6;
        const yMid = yTop + (rowHeight / 2);
        const yLow = yTop + rowHeight - 6;

        for (let i = 0; i < changes.length; i++) {
            const cur = changes[i];
            const nextTime = (i + 1 < changes.length) ? changes[i + 1].time : (maxTime + 10);
            const x1 = cur.time * stepPx;
            const x2 = nextTime * stepPx;
            const w = x2 - x1;
            if (w <= 0) continue;

            const isX = cur.val.includes('x') || cur.val.includes('z');
            ctx.strokeStyle = isX ? '#ef4444' : '#38bdf8';
            ctx.fillStyle = isX ? 'rgba(239, 68, 68, 0.15)' : 'rgba(56, 189, 248, 0.12)';
            ctx.lineWidth = 1.5;

            ctx.beginPath();
            ctx.moveTo(x1 + 3, yHigh);
            ctx.lineTo(x2 - 3, yHigh);
            ctx.lineTo(x2, yMid);
            ctx.lineTo(x2 - 3, yLow);
            ctx.lineTo(x1 + 3, yLow);
            ctx.lineTo(x1, yMid);
            ctx.closePath();
            ctx.fill();
            ctx.stroke();

            ctx.fillStyle = isX ? '#fca5a5' : '#e2e8f0';
            ctx.font = '10px JetBrains Mono';
            if (w > 26) {
                let text = isX ? 'X' : cur.val;
                if (!isX && /^[01]+$/.test(text)) {
                    text = '0x' + parseInt(text, 2).toString(16).toUpperCase();
                }
                ctx.fillText(text, x1 + 6, yMid + 3.5);
            }
        }
    }
};