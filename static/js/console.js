window.StudioConsole = {
    render(rawText) {
        const term = document.getElementById('panel-terminal');
        term.innerHTML = '';

        if (!rawText) return;

        const lines = rawText.split('\n');
        const fileLineRegex = /([a-zA-Z0-9_\-\.\/]+\.(?:sv|v|vh|h)):(\d+)/g;

        lines.forEach(lineText => {
            const lineDiv = document.createElement('div');
            const lower = lineText.toLowerCase();
            const isErrorLine = lower.includes('error:') || lower.includes('fail');
            const isWarningLine = lower.includes('warning:');
            const isPrintLine = lineText.includes('[CAS $display') || lineText.includes('[CAS $monitor') || lineText.includes('[CAS $finish');

            let lastIndex = 0;
            let match;

            while ((match = fileLineRegex.exec(lineText)) !== null) {
                const matchStart = match.index;
                const matchEnd = fileLineRegex.lastIndex;

                if (matchStart > lastIndex) {
                    lineDiv.appendChild(document.createTextNode(lineText.substring(lastIndex, matchStart)));
                }

                const fullPath = match[1];
                const baseFilename = fullPath.replace(/^.*[\\\/]/, '');
                const lineNum = match[2];

                const linkSpan = document.createElement('span');
                let chipClass = 'cas-code-link';
                if (isErrorLine) chipClass += ' error-link';
                else if (isWarningLine) chipClass += ' warning-link';
                else if (isPrintLine) chipClass += ' print-link';

                linkSpan.className = chipClass;
                linkSpan.textContent = `${baseFilename}:${lineNum}`;
                linkSpan.title = `Jump to ${baseFilename} at line ${lineNum}`;
                linkSpan.onclick = () => window.jumpToSource(baseFilename, lineNum);

                lineDiv.appendChild(linkSpan);
                lastIndex = matchEnd;
            }

            if (lastIndex < lineText.length) {
                lineDiv.appendChild(document.createTextNode(lineText.substring(lastIndex)));
            }

            term.appendChild(lineDiv);
        });
    },

    clear() {
        document.getElementById('panel-terminal').innerHTML = "";
    }
};

window.clearConsole = () => window.StudioConsole.clear();