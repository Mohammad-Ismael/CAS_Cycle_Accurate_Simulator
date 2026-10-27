window.StudioEditor = {
    tbEditor: null,
    rtlEditor: null,
    projectFiles: {
        "testbench.sv": `module tb_counter;
    logic clk;
    logic rst_n;
    logic [3:0] d;
    logic [3:0] q;

    counter u_dut (.clk(clk), .rst_n(rst_n), .d(d), .q(q));

    initial begin
        $dumpfile("counter.vcd");
        $dumpvars;
        clk = 0; rst_n = 0; d = 4'd2;
        #5 rst_n = 1;
        #5 clk = 1; #5 clk = 0;
        #5 clk = 1; #5 clk = 0;
        #5 $finish;
    end
endmodule`,
        "design.sv": `module counter (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [3:0] d,
    output logic [3:0] q
);

always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        q <= 0;
    end else begin
        q <= q + d;
    end
end
endmodule`
    },
    activeTbFile: "testbench.sv",
    activeRtlFile: "design.sv",
    tbDecorations: [],
    rtlDecorations: [],

    init() {
        require.config({ paths: { 'vs': 'https://cdnjs.cloudflare.com/ajax/libs/monaco-editor/0.44.0/min/vs' }});
        require(['vs/editor/editor.main'], () => {
            monaco.editor.defineTheme('cas-dark', {
                base: 'vs-dark',
                inherit: true,
                rules: [
                    { token: 'keyword', foreground: '569cd6', fontStyle: 'bold' },
                    { token: 'identifier', foreground: '9cdcfe' },
                    { token: 'number', foreground: 'b5cea8' },
                    { token: 'string', foreground: 'ce9178' },
                    { token: 'comment', foreground: '6a9955', fontStyle: 'italic' }
                ],
                colors: {
                    'editor.background': '#131418',
                    'editor.lineHighlightBackground': '#1a1c22',
                    'editorCursor.foreground': '#007acc'
                }
            });

            this.tbEditor = monaco.editor.create(document.getElementById('editor-tb'), {
                value: this.projectFiles[this.activeTbFile],
                language: 'systemverilog',
                theme: 'cas-dark',
                fontSize: 13,
                fontFamily: 'JetBrains Mono',
                automaticLayout: true,
                minimap: { enabled: false }
            });

            this.rtlEditor = monaco.editor.create(document.getElementById('editor-rtl'), {
                value: this.projectFiles[this.activeRtlFile],
                language: 'systemverilog',
                theme: 'cas-dark',
                fontSize: 13,
                fontFamily: 'JetBrains Mono',
                automaticLayout: true,
                minimap: { enabled: false }
            });

            this.tbEditor.onDidChangeModelContent(() => {
                this.projectFiles[this.activeTbFile] = this.tbEditor.getValue();
            });

            this.rtlEditor.onDidChangeModelContent(() => {
                this.projectFiles[this.activeRtlFile] = this.rtlEditor.getValue();
            });

            this.renderTabs();
        });
    },

    renderTabs() {
        const tbContainer = document.getElementById('tb-tabs-list');
        const rtlContainer = document.getElementById('rtl-tabs-list');
        tbContainer.innerHTML = '';
        rtlContainer.innerHTML = '';

        for (const fname in this.projectFiles) {
            const isTbActive = (fname === this.activeTbFile);
            const isRtlActive = (fname === this.activeRtlFile);

            const isTbFile = fname.startsWith("tb_") || fname.startsWith("testbench");
            const targetContainer = isTbFile ? tbContainer : rtlContainer;
            const isActive = isTbFile ? isTbActive : isRtlActive;

            const tab = document.createElement('div');
            tab.className = 'editor-tab' + (isActive ? ' active' : '');
            
            const titleSpan = document.createElement('span');
            titleSpan.textContent = fname;
            titleSpan.onclick = () => this.switchTab(fname, isTbFile ? 'tb' : 'rtl');
            tab.appendChild(titleSpan);

            if (fname !== "testbench.sv" && fname !== "design.sv") {
                const closeBtn = document.createElement('span');
                closeBtn.className = 'tab-close-btn';
                closeBtn.textContent = '×';
                closeBtn.title = "Delete file";
                closeBtn.onclick = (e) => {
                    e.stopPropagation();
                    this.deleteFile(fname, isTbFile ? 'tb' : 'rtl');
                };
                tab.appendChild(closeBtn);
            }

            targetContainer.appendChild(tab);
        }
    },

    switchTab(fname, pane) {
        if (pane === 'tb') {
            this.projectFiles[this.activeTbFile] = this.tbEditor.getValue();
            this.activeTbFile = fname;
            this.tbEditor.setValue(this.projectFiles[fname]);
        } else {
            this.projectFiles[this.activeRtlFile] = this.rtlEditor.getValue();
            this.activeRtlFile = fname;
            this.rtlEditor.setValue(this.projectFiles[fname]);
        }
        this.renderTabs();
    },

    promptNewFile(pane) {
        const defaultName = (pane === 'tb') ? "tb_helper.sv" : "module.sv";
        const fname = prompt(`Enter new ${pane === 'tb' ? 'testbench' : 'design'} file name:`, defaultName);
        if (!fname || !fname.trim()) return;

        const cleanName = fname.trim();
        if (this.projectFiles[cleanName]) {
            alert(`File '${cleanName}' already exists!`);
            return;
        }

        this.projectFiles[cleanName] = `// ${cleanName}\n`;
        if (pane === 'tb') this.switchTab(cleanName, 'tb');
        else this.switchTab(cleanName, 'rtl');
    },

    deleteFile(fname, pane) {
        if (!confirm(`Delete file '${fname}'?`)) return;

        delete this.projectFiles[fname];
        if (pane === 'tb') {
            if (this.activeTbFile === fname) this.activeTbFile = "testbench.sv";
            this.tbEditor.setValue(this.projectFiles[this.activeTbFile]);
        } else {
            if (this.activeRtlFile === fname) this.activeRtlFile = "design.sv";
            this.rtlEditor.setValue(this.projectFiles[this.activeRtlFile]);
        }
        this.renderTabs();
    },

    jumpToSource(filename, lineNumber) {
        lineNumber = parseInt(lineNumber, 10);
        if (!filename || isNaN(lineNumber)) return;

        filename = filename.replace(/^.*[\\\/]/, '').trim();

        const isTbFile = filename.startsWith("tb_") || filename.startsWith("testbench");
        const targetEditor = isTbFile ? this.tbEditor : this.rtlEditor;
        const currentActiveFile = isTbFile ? this.activeTbFile : this.activeRtlFile;

        if (this.projectFiles[filename] && currentActiveFile !== filename) {
            this.switchTab(filename, isTbFile ? 'tb' : 'rtl');
        }

        targetEditor.revealLineInCenter(lineNumber);
        targetEditor.setPosition({ lineNumber: lineNumber, column: 1 });
        targetEditor.focus();

        const newDecoration = [{
            range: new monaco.Range(lineNumber, 1, lineNumber, 1),
            options: {
                isWholeLine: true,
                className: 'cas-highlight-line'
            }
        }];

        if (isTbFile) {
            this.tbDecorations = this.tbEditor.deltaDecorations(this.tbDecorations, newDecoration);
            setTimeout(() => {
                this.tbDecorations = this.tbEditor.deltaDecorations(this.tbDecorations, []);
            }, 3000);
        } else {
            this.rtlDecorations = this.rtlEditor.deltaDecorations(this.rtlDecorations, newDecoration);
            setTimeout(() => {
                this.rtlDecorations = this.rtlEditor.deltaDecorations(this.rtlDecorations, []);
            }, 3000);
        }
    },

    relayout() {
        setTimeout(() => {
            if (this.tbEditor) this.tbEditor.layout();
            if (this.rtlEditor) this.rtlEditor.layout();
        }, 50);
    }
};

window.jumpToSource = (fname, line) => window.StudioEditor.jumpToSource(fname, line);