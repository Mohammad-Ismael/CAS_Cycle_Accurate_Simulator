window.WaveCursorManager = {
    primaryTime: 0,
    hoverTime: null,

    setPrimary(t) {
        this.primaryTime = Math.max(0, t);
    },

    setHover(t) {
        this.hoverTime = (t !== null) ? Math.max(0, t) : null;
    },

    draw(ctx, totalHeight, stepPx) {
        if (this.hoverTime !== null && this.hoverTime !== this.primaryTime) {
            const hx = this.hoverTime * stepPx;
            ctx.strokeStyle = 'rgba(148, 163, 184, 0.35)';
            ctx.lineWidth = 1;
            ctx.setLineDash([4, 4]);
            ctx.beginPath();
            ctx.moveTo(hx, 0);
            ctx.lineTo(hx, totalHeight);
            ctx.stroke();
            ctx.setLineDash([]);
        }

        const px = this.primaryTime * stepPx;
        ctx.strokeStyle = '#fbbf24';
        ctx.lineWidth = 1.6;
        ctx.beginPath();
        ctx.moveTo(px, 0);
        ctx.lineTo(px, totalHeight);
        ctx.stroke();
    },

    getValueAtTime(sig, time) {
        if (!sig || !sig.changes || sig.changes.length === 0) return 'x';
        let val = 'x';
        for (let i = 0; i < sig.changes.length; i++) {
            if (sig.changes[i].time <= time) {
                val = sig.changes[i].val;
            } else {
                break;
            }
        }
        if (sig.size > 1 && /^[01]+$/.test(val)) {
            val = '0x' + parseInt(val, 2).toString(16).toUpperCase();
        }
        return val;
    }
};