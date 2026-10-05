'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { spawn, execFileSync } = require('node:child_process');
const { pathToFileURL, fileURLToPath } = require('node:url');
const { randomUUID } = require('node:crypto');
const { toGenerated, toOriginal, samePath } = require('../tools/vscode_vshader/source_map');

async function main() {
    const [compiler, serverPath] = process.argv.slice(2);
    assert(compiler && serverPath, 'Usage: node tests/shader_language_service.js <vultra-shader> <slangd>');
    const root = path.resolve('build/.tmp/shader-language-service', randomUUID());
    await fs.mkdir(root, { recursive: true });
    const source = path.join(root, 'language.vshader');
    const text = `Shader "Tests/LanguageService"
{
    Properties { gain ("Gain", Float) = 1 }
    SubShader { Pass {
        Name "Compute" Compute main
        SLANGPROGRAM
        #include "constants.slangh"
        RWStructuredBuffer<float> result;
        [numthreads(1, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            result[0] = material.gain * helperGain();
        }
        ENDSLANG
    } }
}`;
    await fs.writeFile(source, text);
    await fs.writeFile(path.join(root, 'constants.slangh'), 'float helperGain() { return 2; }\n');
    execFileSync(compiler, [source, '--project-source', path.join(root, 'generated'), '--check'], { windowsHide: true });
    const documents = JSON.parse(await fs.readFile(path.join(root, 'generated/source_map.json'), 'utf8'));
    const document = documents[0];
    const generatedText = await fs.readFile(document.file, 'utf8');
    const originalLines = text.split('\n');
    const useLine = originalLines.findIndex(line => line.includes('material.gain'));
    const position = { line: useLine, character: originalLines[useLine].indexOf('material.gain') + 'material.'.length };
    const mapped = toGenerated(documents, source, position);
    assert(mapped && mapped.document === document);
    assert.deepEqual(toOriginal(document, mapped.position).position, position, 'Editable source roundtrip failed');
    const declaration = document.mappings.find(range => range.editable === false);
    assert.equal(toOriginal(document, { line: declaration.generated_line - 1, character: 10 }).editable, false,
        'Generated property declaration must never accept a completion edit');
    const server = spawn(serverPath, [], { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
    let bytes = Buffer.alloc(0);
    let nextId = 1;
    const pending = new Map();
    const diagnostics = [];
    let errors = '';
    server.stderr.on('data', chunk => { errors += chunk; });
    function send(message) {
        const payload = Buffer.from(JSON.stringify({ jsonrpc: '2.0', ...message }));
        server.stdin.write(`Content-Length: ${payload.length}\r\n\r\n`);
        server.stdin.write(payload);
    }
    function request(method, params) {
        const id = nextId++;
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timed out: ${method}\n${errors}`)); }, 15000);
            pending.set(id, { resolve, reject, timer });
            send({ id, method, params });
        });
    }
    server.stdout.on('data', chunk => {
        bytes = Buffer.concat([bytes, chunk]);
        for (;;) {
            const end = bytes.indexOf('\r\n\r\n');
            if (end < 0) { return; }
            const length = Number(bytes.subarray(0, end).toString().match(/Content-Length: (\d+)/i)?.[1]);
            if (!length || bytes.length < end + 4 + length) { return; }
            const message = JSON.parse(bytes.subarray(end + 4, end + 4 + length));
            bytes = bytes.subarray(end + 4 + length);
            if (message.method === 'workspace/configuration') {
                send({ id: message.id, result: message.params.items.map(item => {
                    if (item.section === 'slang.additionalSearchPaths') { return [root, path.resolve('builtin/shaders'), path.resolve('external')]; }
                    if (item.section === 'slang.predefinedMacros') { return []; }
                    if (item.section === 'slang.searchInAllWorkspaceDirectories') { return false; }
                    return null;
                }) });
            } else if (message.method && message.id !== undefined) {
                send({ id: message.id, result: null });
            } else if (message.method === 'textDocument/publishDiagnostics') {
                diagnostics.push(message.params);
            } else if (pending.has(message.id)) {
                const waiter = pending.get(message.id);
                pending.delete(message.id);
                clearTimeout(waiter.timer);
                if (message.error) { waiter.reject(new Error(JSON.stringify(message.error))); }
                else { waiter.resolve(message.result); }
            }
        }
    });
    try {
        await request('initialize', { processId: process.pid, rootUri: pathToFileURL(process.cwd()).href,
            capabilities: { workspace: { configuration: true } }, workspaceFolders: [{ uri: pathToFileURL(process.cwd()).href, name: 'Vultra' }] });
        send({ method: 'initialized', params: {} });
        const uri = pathToFileURL(document.file).href;
        send({ method: 'textDocument/didOpen', params: { textDocument: { uri, languageId: 'slang', version: 1, text: generatedText } } });
        const completion = await request('textDocument/completion', { textDocument: { uri }, position: mapped.position });
        const items = Array.isArray(completion) ? completion : completion?.items;
        assert(items?.some(item => item.label === 'gain'), 'Native Slang completion did not expose the material property');
        const definitions = await request('textDocument/definition', { textDocument: { uri }, position: { ...mapped.position, character: mapped.position.character + 1 } });
        const definition = (Array.isArray(definitions) ? definitions : [definitions])[0];
        assert(definition, 'Native Slang did not resolve the material property');
        const targetFile = fileURLToPath(definition.targetUri || definition.uri);
        const targetRange = definition.targetSelectionRange || definition.range;
        const origin = samePath(targetFile, document.file) && toOriginal(document, targetRange.start);
        assert(origin && samePath(origin.file, source) && origin.position.line === 2 && !origin.editable,
            'Property definition did not map to its authoring declaration');
        const helper = { line: mapped.position.line, character: generatedText.split('\n')[mapped.position.line].indexOf('helperGain') + 1 };
        const helperDefinition = await request('textDocument/definition', { textDocument: { uri }, position: helper });
        const helperTargets = Array.isArray(helperDefinition) ? helperDefinition : [helperDefinition];
        assert(helperTargets.some(item => item && (item.targetUri || item.uri) && samePath(fileURLToPath(item.targetUri || item.uri), path.join(root, 'constants.slangh'))),
            'Relative authoring include did not resolve from the generated document');
        const changed = generatedText.replace('material.gain * helperGain()', 'material.gain * not_defined_symbol');
        send({ method: 'textDocument/didChange', params: { textDocument: { uri, version: 2 }, contentChanges: [{ text: changed }] } });
        await request('textDocument/completion', { textDocument: { uri }, position: mapped.position });
        const deadline = Date.now() + 5000;
        let diagnostic;
        while (!diagnostic && Date.now() < deadline) {
            diagnostic = diagnostics.flatMap(item => item.diagnostics).find(item => item.message.includes('not_defined_symbol'));
            if (!diagnostic) { await new Promise(resolve => setTimeout(resolve, 30)); }
        }
        assert(diagnostic, 'Native Slang did not publish the source error');
        const errorOrigin = toOriginal(document, diagnostic.range.start);
        assert(errorOrigin && errorOrigin.position.line === useLine, 'Native diagnostic did not map to the editable source line');
        await request('shutdown', null);
        send({ method: 'exit', params: null });
        console.log('Shader language services passed: native completion, property/include definitions, diagnostics and editable boundaries');
    } finally {
        for (const waiter of pending.values()) { clearTimeout(waiter.timer); }
        server.kill();
    }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
