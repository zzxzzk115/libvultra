// `import vultra.keywords;` declares a program's keywords, compile-time choices that cooking expands into variants
// (ShaderKeyword in vultra/shader/shader_program.hpp). They sit on a function named vultraKeywords, which no entry
// point needs to call:
//
//     [VultraKeyword("SHADOWS", "bool")]
//     [VultraKeyword("QUALITY", "enum:low,medium,high")]
//     void vultraKeywords() {}
//
// The code tests a keyword with `#if SHADOWS` or `#if QUALITY == 2`; its first value leaves the macro undefined, so it
// reads as 0. Macros reach the modules the program imports as well.
[__AttributeUsage(_AttributeTargets.Function)]
public struct VultraKeywordAttribute
{
    string name;
    string type; // "bool", or "enum:" and the value names separated by commas.
};
