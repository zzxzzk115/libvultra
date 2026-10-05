'use strict';

const vscode = require('vscode');
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFile } = require('node:child_process');
const { toGenerated, toOriginal, samePath } = require('./source_map');

function activate(context) {
    const states = new Map();
    const diagnostics = vscode.languages.createDiagnosticCollection('vultra-shader');
    const output = vscode.window.createOutputChannel('Vultra Shader');
    context.subscriptions.push(diagnostics, output);
    const selector = { scheme: 'file', language: 'vshader' };

    function stateFor(document) {
        const key = document.uri.toString();
        if (!states.has(key)) {
            states.set(key, { version: -1, documents: [], compilerDiagnostics: [], job: undefined, timer: undefined });
        }
        return states.get(key);
    }

    function parseDiagnostics(text, document) {
        const lines = text.split(/\r?\n/);
        const result = [];
        for (let i = 0; i < lines.length; ++i) {
            const match = lines[i].match(/(?:-->\s*)?([A-Za-z]:[\\/].*?\.(?:vshader|slangh?|h)):(\d+):(\d+):?\s*(.*)/) ||
                lines[i].match(/(?:-->\s*)?(\/.*?\.(?:vshader|slangh?|h)):(\d+):(\d+):?\s*(.*)/);
            if (!match || !samePath(match[1], document.uri.fsPath)) {
                continue;
            }
            const position = new vscode.Position(Math.max(0, Number(match[2]) - 1), Math.max(0, Number(match[3]) - 1));
            const message = match[4] || lines[i - 1] || 'Shader compilation failed';
            const severity = /warning/i.test(message) ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error;
            const item = new vscode.Diagnostic(new vscode.Range(position, position.translate(0, 1)), message, severity);
            item.source = 'Vultra / Slang';
            result.push(item);
        }
        return result;
    }

    async function project(document) {
        const state = stateFor(document);
        if (state.version === document.version) {
            return state;
        }
        if (state.job) {
            await state.job;
            return project(document);
        }
        state.job = (async () => {
            const folder = vscode.workspace.getWorkspaceFolder(document.uri);
            if (!folder || !vscode.workspace.isTrusted) {
                state.version = document.version;
                return;
            }
            const root = folder.uri.fsPath;
            const config = vscode.workspace.getConfiguration('vultra.shader', document.uri);
            const expand = value => value.replaceAll('${workspaceFolder}', root);
            const executable = expand(config.get('compilerPath') || path.join(root, 'build',
                process.platform === 'win32' ? 'windows' : process.platform === 'darwin' ? 'macosx' : 'linux',
                process.arch, 'release', process.platform === 'win32' ? 'vultra-shader.exe' : 'vultra-shader'));
            const key = crypto.createHash('sha256').update(document.uri.toString()).digest('hex').slice(0, 20);
            const directory = path.join(root, 'build', 'generated', 'shader_editor', key);
            await fs.mkdir(directory, { recursive: true });
            const input = path.join(directory, 'input.txt');
            const version = document.version;
            await fs.writeFile(input, document.getText());
            await fs.rm(path.join(directory, 'source_map.json'), { force: true });
            const args = [document.uri.fsPath, '--project-source', directory, '--input-text', input, '--check'];
            for (const include of [path.dirname(document.uri.fsPath), ...config.get('includeDirectories', [])]) {
                args.push('--include', expand(include));
            }
            const result = await new Promise(resolve => {
                execFile(executable, args, { cwd: root, windowsHide: true, maxBuffer: 8 * 1024 * 1024 },
                    (error, stdout, stderr) => resolve({ error, text: stdout + stderr }));
            });
            if (version !== document.version || document.isClosed) {
                return;
            }
            output.appendLine(result.text);
            state.documents = [];
            try {
                state.documents = JSON.parse(await fs.readFile(path.join(directory, 'source_map.json'), 'utf8'));
                for (const generated of state.documents) {
                    await vscode.workspace.openTextDocument(vscode.Uri.file(generated.file));
                }
            } catch (error) {
                output.appendLine(String(error));
            }
            state.version = version;
            state.compilerDiagnostics = parseDiagnostics(result.text, document);
            if (result.error && !state.compilerDiagnostics.length) {
                const message = result.error.code === 'ENOENT' ? 'Build vultra-shader or configure vultra.shader.compilerPath.' :
                    result.text.trim() || String(result.error);
                state.compilerDiagnostics.push(new vscode.Diagnostic(new vscode.Range(0, 0, 0, 1), message));
            }
            diagnostics.set(document.uri, state.compilerDiagnostics);
        })();
        try {
            await state.job;
        } finally {
            state.job = undefined;
        }
        if (state.version !== document.version && !document.isClosed) {
            return project(document);
        }
        return state;
    }

    function originalRange(document, range, editableOnly = false) {
        const start = toOriginal(document, range.start);
        const end = toOriginal(document, range.end);
        if (!start || !end || !samePath(start.file, end.file) || (editableOnly && (!start.editable || !end.editable))) {
            return undefined;
        }
        return { uri: vscode.Uri.file(start.file), range: new vscode.Range(
            new vscode.Position(start.position.line, start.position.character),
            new vscode.Position(end.position.line, end.position.character)) };
    }

    async function forward(document, position, command, ...args) {
        const version = document.version;
        const state = await project(document);
        if (state.version !== version || document.version !== version) { return undefined; }
        const mapped = toGenerated(state.documents, document.uri.fsPath, position);
        if (!mapped || mapped.position.character < 0) {
            return undefined;
        }
        const target = new vscode.Position(mapped.position.line, mapped.position.character);
        const result = await vscode.commands.executeCommand(command, vscode.Uri.file(mapped.document.file), target, ...args);
        if (document.version !== version || document.isClosed) { return undefined; }
        return { result, mapped, state };
    }

    context.subscriptions.push(vscode.languages.registerCompletionItemProvider(selector, {
        async provideCompletionItems(document, position, token, request) {
            const forwarded = await forward(document, position, 'vscode.executeCompletionItemProvider', request.triggerCharacter, 50);
            if (!forwarded?.result || token.isCancellationRequested) {
                return undefined;
            }
            const items = forwarded.result.items.filter(item => {
                if (item.range) {
                    if (item.range.inserting) {
                        const inserting = originalRange(forwarded.mapped.document, item.range.inserting, true);
                        const replacing = originalRange(forwarded.mapped.document, item.range.replacing, true);
                        if (!inserting || !replacing) { return false; }
                        item.range = { inserting: inserting.range, replacing: replacing.range };
                    } else {
                        const mapped = originalRange(forwarded.mapped.document, item.range, true);
                        if (!mapped) { return false; }
                        item.range = mapped.range;
                    }
                }
                // Never apply generated declaration edits or provider commands to the game source.
                item.additionalTextEdits = (item.additionalTextEdits || []).flatMap(edit => {
                    const mapped = originalRange(forwarded.mapped.document, edit.range, true);
                    return mapped ? [new vscode.TextEdit(mapped.range, edit.newText)] : [];
                });
                item.command = undefined;
                return true;
            });
            return new vscode.CompletionList(items, forwarded.result.isIncomplete);
        }
    }, '.', ':'));
    context.subscriptions.push(vscode.languages.registerDefinitionProvider(selector, {
        async provideDefinition(document, position) {
            const forwarded = await forward(document, position, 'vscode.executeDefinitionProvider');
            if (!forwarded?.result) { return undefined; }
            return forwarded.result.map(location => {
                const uri = location.targetUri || location.uri;
                const range = location.targetSelectionRange || location.range;
                const generated = forwarded.state.documents.find(item => samePath(item.file, uri.fsPath));
                const mapped = generated && originalRange(generated, range);
                return mapped ? new vscode.Location(mapped.uri, mapped.range) : location;
            });
        }
    }));
    context.subscriptions.push(vscode.languages.registerHoverProvider(selector, {
        async provideHover(document, position) {
            const forwarded = await forward(document, position, 'vscode.executeHoverProvider');
            const hover = forwarded?.result?.[0];
            if (!hover) { return undefined; }
            const range = hover.range && originalRange(forwarded.mapped.document, hover.range);
            return new vscode.Hover(hover.contents, range?.range);
        }
    }));
    context.subscriptions.push(vscode.commands.registerCommand('vultra.shader.generatedSource', async () => {
        const document = vscode.window.activeTextEditor?.document;
        if (document?.languageId !== 'vshader') { return; }
        const state = await project(document);
        const selected = await vscode.window.showQuickPick(state.documents.map(item => ({ label: path.basename(item.file), file: item.file })));
        if (selected) { await vscode.window.showTextDocument(vscode.Uri.file(selected.file), { viewColumn: vscode.ViewColumn.Beside }); }
    }));
    context.subscriptions.push(vscode.languages.onDidChangeDiagnostics(event => {
        for (const [key, state] of states) {
            if (!event.uris.some(uri => state.documents.some(item => samePath(item.file, uri.fsPath)))) { continue; }
            const original = vscode.workspace.textDocuments.find(document => document.uri.toString() === key);
            if (!original || original.version !== state.version) { continue; }
            const items = [...state.compilerDiagnostics];
            for (const generated of state.documents) {
                for (const item of vscode.languages.getDiagnostics(vscode.Uri.file(generated.file))) {
                    const mapped = originalRange(generated, item.range);
                    if (mapped?.uri.toString() === key) {
                        const value = new vscode.Diagnostic(mapped.range, item.message, item.severity);
                        value.source = 'Slang';
                        if (!items.some(old => old.message === value.message && old.range.isEqual(value.range))) { items.push(value); }
                    }
                }
            }
            diagnostics.set(vscode.Uri.parse(key), items);
        }
    }));

    function schedule(document) {
        if (document.languageId !== 'vshader') { return; }
        const state = stateFor(document);
        clearTimeout(state.timer);
        state.timer = setTimeout(() => project(document).catch(error => output.appendLine(String(error))), 400);
    }
    context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(schedule));
    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(event => schedule(event.document)));
    context.subscriptions.push(vscode.workspace.onDidCloseTextDocument(document => {
        const state = states.get(document.uri.toString());
        if (state) { clearTimeout(state.timer); }
        states.delete(document.uri.toString());
        diagnostics.delete(document.uri);
    }));
    context.subscriptions.push({ dispose() { for (const state of states.values()) { clearTimeout(state.timer); } } });
    for (const document of vscode.workspace.textDocuments) { schedule(document); }
}

module.exports = { activate };
