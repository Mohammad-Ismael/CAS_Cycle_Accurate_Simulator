window.WaveRuler = {
    draw(ctx, totalWidth, height, maxTime, stepPx) {
        ctx.fillStyle = '#14161d';
        ctx.fillRect(0, 0, totalWidth, height);

        ctx.strokeStyle = '#262933';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(0, height);
        ctx.lineTo(totalWidth, height);
        ctx.stroke();

        ctx.font = '10px JetBrains Mono';
        ctx.fillStyle = '#64748b';

        let stepTime = 5;
        if (stepPx < 8) stepTime = 20;
        else if (stepPx < 14) stepTime = 10;
        else if (stepPx > 40) stepTime = 1;

        for (let t = 0; t <= maxTime + stepTime; t += stepTime) {
            const x = t * stepPx;
            const isMajor = (t % (stepTime * 2) === 0);

            ctx.beginPath();
            ctx.moveTo(x, isMajor ? 0 : height - 6);
            ctx.lineTo(x, height);
            ctx.strokeStyle = isMajor ? '#333846' : '#212530';
            ctx.stroke();

            if (isMajor) {
                ctx.fillText(t + 'ns', x + 4, height - 8);
            }
        }
    },

    drawBackgroundGrid(ctx, totalWidth, totalHeight, rulerHeight, maxTime, stepPx) {
        let stepTime = 10;
        if (stepPx > 30) stepTime = 5;
        ctx.lineWidth = 1;

        for (let t = 0; t <= maxTime + stepTime; t += stepTime) {
            const x = t * stepPx;
            ctx.beginPath();
            ctx.moveTo(x, rulerHeight);
            ctx.lineTo(x, totalHeight);
            ctx.strokeStyle = '#14161e';
            ctx.stroke();
        }
    }
};