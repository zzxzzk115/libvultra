'use strict';

// Maps use one-based lines from the native parser; VS Code/LSP positions are zero-based.
function toGenerated(documents, file, position) {
    for (const document of documents) {
        for (const range of document.mappings) {
            const relative = position.line - (range.line - 1);
            if (range.editable !== false && samePath(range.file, file) && relative >= 0 && relative < range.line_count) {
                return { document, position: { line: range.generated_line - 1 + relative,
                    character: position.character - (relative === 0 ? range.column - 1 : 0) } };
            }
        }
    }
    return undefined;
}

function toOriginal(document, position) {
    for (const range of document.mappings) {
        const relative = position.line - (range.generated_line - 1);
        if (relative >= 0 && relative < range.line_count) {
            return { file: range.file, editable: range.editable !== false, position: { line: range.line - 1 + relative,
                character: range.editable === false ? range.column - 1 : position.character + (relative === 0 ? range.column - 1 : 0) } };
        }
    }
    return undefined;
}

function samePath(a, b) {
    const normalize = value => process.platform === 'win32' ? value.replaceAll('\\', '/').toLowerCase() : value;
    return normalize(a) === normalize(b);
}

module.exports = { toGenerated, toOriginal, samePath };
