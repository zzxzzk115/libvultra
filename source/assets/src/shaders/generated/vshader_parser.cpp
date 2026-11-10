
// Generated from vshader_parser.g4 by ANTLR 4.13.2



#include "vshader_parser.h"


using namespace antlrcpp;

using namespace antlr4;

namespace {

struct Vshader_parserStaticData final {
  Vshader_parserStaticData(std::vector<std::string> ruleNames,
                        std::vector<std::string> literalNames,
                        std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  Vshader_parserStaticData(const Vshader_parserStaticData&) = delete;
  Vshader_parserStaticData(Vshader_parserStaticData&&) = delete;
  Vshader_parserStaticData& operator=(const Vshader_parserStaticData&) = delete;
  Vshader_parserStaticData& operator=(Vshader_parserStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag vshader_parserParserOnceFlag;
#if ANTLR4_USE_THREAD_LOCAL_CACHE
static thread_local
#endif
std::unique_ptr<Vshader_parserStaticData> vshader_parserParserStaticData = nullptr;

void vshader_parserParserInitialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  if (vshader_parserParserStaticData != nullptr) {
    return;
  }
#else
  assert(vshader_parserParserStaticData == nullptr);
#endif
  auto staticData = std::make_unique<Vshader_parserStaticData>(
    std::vector<std::string>{
      "file", "properties", "property", "attribute", "attributeArgument",
      "propertyType", "value", "variant", "constants", "modules", "defines",
      "conformances", "conformance", "subshader", "tags", "requires", "common",
      "program", "surface", "pass", "stage", "state", "stateValue"
    },
    std::vector<std::string>{
      "", "'Shader'", "'Properties'", "'SubShader'", "'Pass'", "'Surface'",
      "'Tags'", "'Name'", "'Model'", "'Entry'", "'Variant'", "'Constants'",
      "'Modules'", "'Defines'", "'Conformances'", "'Requires'", "'Vertex'",
      "'Fragment'", "'Task'", "'Mesh'", "'Compute'", "'Cull'", "'FrontFace'",
      "'ZWrite'", "'ZTest'", "'DepthBias'", "'Blend'", "'BlendOp'", "'ColorMask'",
      "'Stencil'", "", "", "", "", "", "", "'{'", "'}'", "'('", "')'", "'['",
      "']'", "','", "'='", "';'"
    },
    std::vector<std::string>{
      "", "SHADER", "PROPERTIES", "SUBSHADER", "PASS", "SURFACE", "TAGS",
      "NAME", "MODEL", "ENTRY", "VARIANT", "CONSTANTS", "MODULES", "DEFINES",
      "CONFORMANCES", "REQUIRES", "VERTEX", "FRAGMENT", "TASK", "MESH",
      "COMPUTE", "CULL", "FRONT_FACE", "ZWRITE", "ZTEST", "DEPTH_BIAS",
      "BLEND", "BLEND_OP", "COLOR_MASK", "STENCIL", "SLANG_PROGRAM", "SLANG_INCLUDE",
      "TEXTURE_TYPE", "IDENTIFIER", "NUMBER", "STRING", "LBRACE", "RBRACE",
      "LPAREN", "RPAREN", "LBRACKET", "RBRACKET", "COMMA", "EQUAL", "SEMICOLON",
      "LINE_COMMENT", "BLOCK_COMMENT", "WHITESPACE", "PROGRAM_END", "PROGRAM_TEXT"
    }
  );
  static const int32_t serializedATNSegment[] = {
      4,1,49,339,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
      7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
      14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
      21,2,22,7,22,1,0,1,0,1,0,1,0,1,0,1,0,5,0,53,8,0,10,0,12,0,56,9,0,1,0,
      1,0,1,0,1,1,1,1,1,1,5,1,64,8,1,10,1,12,1,67,9,1,1,1,1,1,1,2,5,2,72,8,
      2,10,2,12,2,75,9,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,3,1,3,1,3,1,
      3,1,3,1,3,5,3,92,8,3,10,3,12,3,95,9,3,1,3,1,3,3,3,99,8,3,1,3,1,3,1,4,
      1,4,1,5,1,5,1,5,1,5,1,5,1,5,3,5,111,8,5,1,6,1,6,1,6,1,6,1,6,3,6,118,8,
      6,1,6,1,6,1,6,1,6,5,6,124,8,6,10,6,12,6,127,9,6,1,6,3,6,130,8,6,1,7,1,
      7,1,7,1,7,1,7,1,7,1,7,5,7,139,8,7,10,7,12,7,142,9,7,1,7,1,7,1,8,1,8,1,
      8,1,8,1,8,1,8,5,8,152,8,8,10,8,12,8,155,9,8,1,8,1,8,1,9,1,9,1,9,5,9,162,
      8,9,10,9,12,9,165,9,9,1,9,1,9,1,10,1,10,1,10,1,10,1,10,5,10,174,8,10,
      10,10,12,10,177,9,10,1,10,1,10,1,11,1,11,1,11,5,11,184,8,11,10,11,12,
      11,187,9,11,1,11,1,11,1,12,1,12,1,12,1,12,1,12,1,13,1,13,1,13,1,13,1,
      13,1,13,1,13,1,13,5,13,204,8,13,10,13,12,13,207,9,13,1,13,1,13,1,14,1,
      14,1,14,1,14,1,14,5,14,216,8,14,10,14,12,14,219,9,14,1,14,1,14,1,15,1,
      15,1,15,1,15,5,15,227,8,15,10,15,12,15,230,9,15,1,16,1,16,5,16,234,8,
      16,10,16,12,16,237,9,16,1,16,1,16,1,17,1,17,5,17,243,8,17,10,17,12,17,
      246,9,17,1,17,1,17,1,18,1,18,1,18,1,18,1,18,1,18,1,18,1,18,5,18,258,8,
      18,10,18,12,18,261,9,18,1,18,1,18,1,19,1,19,1,19,1,19,1,19,1,19,1,19,
      1,19,1,19,5,19,274,8,19,10,19,12,19,277,9,19,1,19,1,19,1,20,1,20,1,20,
      1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,3,21,296,
      8,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,3,21,305,8,21,3,21,307,8,21,1,
      21,1,21,1,21,1,21,3,21,313,8,21,1,21,1,21,1,21,3,21,318,8,21,1,21,1,21,
      1,21,1,21,5,21,324,8,21,10,21,12,21,327,9,21,1,21,3,21,330,8,21,1,22,
      1,22,1,22,1,22,1,22,3,22,337,8,22,1,22,0,0,23,0,2,4,6,8,10,12,14,16,18,
      20,22,24,26,28,30,32,34,36,38,40,42,44,0,4,1,0,33,35,1,0,32,33,1,0,33,
      34,1,0,16,20,372,0,46,1,0,0,0,2,60,1,0,0,0,4,73,1,0,0,0,6,85,1,0,0,0,
      8,102,1,0,0,0,10,104,1,0,0,0,12,129,1,0,0,0,14,131,1,0,0,0,16,145,1,0,
      0,0,18,158,1,0,0,0,20,168,1,0,0,0,22,180,1,0,0,0,24,190,1,0,0,0,26,195,
      1,0,0,0,28,210,1,0,0,0,30,222,1,0,0,0,32,231,1,0,0,0,34,240,1,0,0,0,36,
      249,1,0,0,0,38,264,1,0,0,0,40,280,1,0,0,0,42,329,1,0,0,0,44,336,1,0,0,
      0,46,47,5,1,0,0,47,48,5,35,0,0,48,54,5,36,0,0,49,53,3,2,1,0,50,53,3,14,
      7,0,51,53,3,26,13,0,52,49,1,0,0,0,52,50,1,0,0,0,52,51,1,0,0,0,53,56,1,
      0,0,0,54,52,1,0,0,0,54,55,1,0,0,0,55,57,1,0,0,0,56,54,1,0,0,0,57,58,5,
      37,0,0,58,59,5,0,0,1,59,1,1,0,0,0,60,61,5,2,0,0,61,65,5,36,0,0,62,64,
      3,4,2,0,63,62,1,0,0,0,64,67,1,0,0,0,65,63,1,0,0,0,65,66,1,0,0,0,66,68,
      1,0,0,0,67,65,1,0,0,0,68,69,5,37,0,0,69,3,1,0,0,0,70,72,3,6,3,0,71,70,
      1,0,0,0,72,75,1,0,0,0,73,71,1,0,0,0,73,74,1,0,0,0,74,76,1,0,0,0,75,73,
      1,0,0,0,76,77,5,33,0,0,77,78,5,38,0,0,78,79,5,35,0,0,79,80,5,42,0,0,80,
      81,3,10,5,0,81,82,5,39,0,0,82,83,5,43,0,0,83,84,3,12,6,0,84,5,1,0,0,0,
      85,86,5,40,0,0,86,98,5,33,0,0,87,88,5,38,0,0,88,93,3,8,4,0,89,90,5,42,
      0,0,90,92,3,8,4,0,91,89,1,0,0,0,92,95,1,0,0,0,93,91,1,0,0,0,93,94,1,0,
      0,0,94,96,1,0,0,0,95,93,1,0,0,0,96,97,5,39,0,0,97,99,1,0,0,0,98,87,1,
      0,0,0,98,99,1,0,0,0,99,100,1,0,0,0,100,101,5,41,0,0,101,7,1,0,0,0,102,
      103,7,0,0,0,103,9,1,0,0,0,104,110,7,1,0,0,105,106,5,38,0,0,106,107,5,
      34,0,0,107,108,5,42,0,0,108,109,5,34,0,0,109,111,5,39,0,0,110,105,1,0,
      0,0,110,111,1,0,0,0,111,11,1,0,0,0,112,130,5,34,0,0,113,130,5,33,0,0,
      114,117,5,35,0,0,115,116,5,36,0,0,116,118,5,37,0,0,117,115,1,0,0,0,117,
      118,1,0,0,0,118,130,1,0,0,0,119,120,5,38,0,0,120,125,5,34,0,0,121,122,
      5,42,0,0,122,124,5,34,0,0,123,121,1,0,0,0,124,127,1,0,0,0,125,123,1,0,
      0,0,125,126,1,0,0,0,126,128,1,0,0,0,127,125,1,0,0,0,128,130,5,39,0,0,
      129,112,1,0,0,0,129,113,1,0,0,0,129,114,1,0,0,0,129,119,1,0,0,0,130,13,
      1,0,0,0,131,132,5,10,0,0,132,133,5,35,0,0,133,140,5,36,0,0,134,139,3,
      16,8,0,135,139,3,18,9,0,136,139,3,20,10,0,137,139,3,22,11,0,138,134,1,
      0,0,0,138,135,1,0,0,0,138,136,1,0,0,0,138,137,1,0,0,0,139,142,1,0,0,0,
      140,138,1,0,0,0,140,141,1,0,0,0,141,143,1,0,0,0,142,140,1,0,0,0,143,144,
      5,37,0,0,144,15,1,0,0,0,145,146,5,11,0,0,146,153,5,36,0,0,147,148,5,33,
      0,0,148,149,5,33,0,0,149,150,5,43,0,0,150,152,7,2,0,0,151,147,1,0,0,0,
      152,155,1,0,0,0,153,151,1,0,0,0,153,154,1,0,0,0,154,156,1,0,0,0,155,153,
      1,0,0,0,156,157,5,37,0,0,157,17,1,0,0,0,158,159,5,12,0,0,159,163,5,36,
      0,0,160,162,5,35,0,0,161,160,1,0,0,0,162,165,1,0,0,0,163,161,1,0,0,0,
      163,164,1,0,0,0,164,166,1,0,0,0,165,163,1,0,0,0,166,167,5,37,0,0,167,
      19,1,0,0,0,168,169,5,13,0,0,169,175,5,36,0,0,170,171,5,33,0,0,171,172,
      5,43,0,0,172,174,7,0,0,0,173,170,1,0,0,0,174,177,1,0,0,0,175,173,1,0,
      0,0,175,176,1,0,0,0,176,178,1,0,0,0,177,175,1,0,0,0,178,179,5,37,0,0,
      179,21,1,0,0,0,180,181,5,14,0,0,181,185,5,36,0,0,182,184,3,24,12,0,183,
      182,1,0,0,0,184,187,1,0,0,0,185,183,1,0,0,0,185,186,1,0,0,0,186,188,1,
      0,0,0,187,185,1,0,0,0,188,189,5,37,0,0,189,23,1,0,0,0,190,191,5,35,0,
      0,191,192,5,35,0,0,192,193,5,43,0,0,193,194,5,34,0,0,194,25,1,0,0,0,195,
      196,5,3,0,0,196,205,5,36,0,0,197,204,3,28,14,0,198,204,3,30,15,0,199,
      204,3,42,21,0,200,204,3,32,16,0,201,204,3,36,18,0,202,204,3,38,19,0,203,
      197,1,0,0,0,203,198,1,0,0,0,203,199,1,0,0,0,203,200,1,0,0,0,203,201,1,
      0,0,0,203,202,1,0,0,0,204,207,1,0,0,0,205,203,1,0,0,0,205,206,1,0,0,0,
      206,208,1,0,0,0,207,205,1,0,0,0,208,209,5,37,0,0,209,27,1,0,0,0,210,211,
      5,6,0,0,211,217,5,36,0,0,212,213,5,35,0,0,213,214,5,43,0,0,214,216,5,
      35,0,0,215,212,1,0,0,0,216,219,1,0,0,0,217,215,1,0,0,0,217,218,1,0,0,
      0,218,220,1,0,0,0,219,217,1,0,0,0,220,221,5,37,0,0,221,29,1,0,0,0,222,
      223,5,15,0,0,223,228,5,33,0,0,224,225,5,42,0,0,225,227,5,33,0,0,226,224,
      1,0,0,0,227,230,1,0,0,0,228,226,1,0,0,0,228,229,1,0,0,0,229,31,1,0,0,
      0,230,228,1,0,0,0,231,235,5,31,0,0,232,234,5,49,0,0,233,232,1,0,0,0,234,
      237,1,0,0,0,235,233,1,0,0,0,235,236,1,0,0,0,236,238,1,0,0,0,237,235,1,
      0,0,0,238,239,5,48,0,0,239,33,1,0,0,0,240,244,5,30,0,0,241,243,5,49,0,
      0,242,241,1,0,0,0,243,246,1,0,0,0,244,242,1,0,0,0,244,245,1,0,0,0,245,
      247,1,0,0,0,246,244,1,0,0,0,247,248,5,48,0,0,248,35,1,0,0,0,249,250,5,
      5,0,0,250,259,5,36,0,0,251,252,5,8,0,0,252,258,5,33,0,0,253,254,5,9,0,
      0,254,258,5,33,0,0,255,258,3,42,21,0,256,258,3,34,17,0,257,251,1,0,0,
      0,257,253,1,0,0,0,257,255,1,0,0,0,257,256,1,0,0,0,258,261,1,0,0,0,259,
      257,1,0,0,0,259,260,1,0,0,0,260,262,1,0,0,0,261,259,1,0,0,0,262,263,5,
      37,0,0,263,37,1,0,0,0,264,265,5,4,0,0,265,275,5,36,0,0,266,267,5,7,0,
      0,267,274,5,35,0,0,268,274,3,28,14,0,269,274,3,40,20,0,270,274,3,30,15,
      0,271,274,3,42,21,0,272,274,3,34,17,0,273,266,1,0,0,0,273,268,1,0,0,0,
      273,269,1,0,0,0,273,270,1,0,0,0,273,271,1,0,0,0,273,272,1,0,0,0,274,277,
      1,0,0,0,275,273,1,0,0,0,275,276,1,0,0,0,276,278,1,0,0,0,277,275,1,0,0,
      0,278,279,5,37,0,0,279,39,1,0,0,0,280,281,7,3,0,0,281,282,5,33,0,0,282,
      41,1,0,0,0,283,284,5,21,0,0,284,330,3,44,22,0,285,286,5,22,0,0,286,330,
      3,44,22,0,287,288,5,23,0,0,288,330,3,44,22,0,289,290,5,24,0,0,290,330,
      3,44,22,0,291,292,5,25,0,0,292,293,3,44,22,0,293,295,3,44,22,0,294,296,
      3,44,22,0,295,294,1,0,0,0,295,296,1,0,0,0,296,330,1,0,0,0,297,298,5,26,
      0,0,298,306,3,44,22,0,299,304,3,44,22,0,300,301,5,42,0,0,301,302,3,44,
      22,0,302,303,3,44,22,0,303,305,1,0,0,0,304,300,1,0,0,0,304,305,1,0,0,
      0,305,307,1,0,0,0,306,299,1,0,0,0,306,307,1,0,0,0,307,330,1,0,0,0,308,
      309,5,27,0,0,309,312,3,44,22,0,310,311,5,42,0,0,311,313,3,44,22,0,312,
      310,1,0,0,0,312,313,1,0,0,0,313,330,1,0,0,0,314,315,5,28,0,0,315,317,
      3,44,22,0,316,318,5,34,0,0,317,316,1,0,0,0,317,318,1,0,0,0,318,330,1,
      0,0,0,319,320,5,29,0,0,320,325,5,36,0,0,321,322,5,33,0,0,322,324,3,44,
      22,0,323,321,1,0,0,0,324,327,1,0,0,0,325,323,1,0,0,0,325,326,1,0,0,0,
      326,328,1,0,0,0,327,325,1,0,0,0,328,330,5,37,0,0,329,283,1,0,0,0,329,
      285,1,0,0,0,329,287,1,0,0,0,329,289,1,0,0,0,329,291,1,0,0,0,329,297,1,
      0,0,0,329,308,1,0,0,0,329,314,1,0,0,0,329,319,1,0,0,0,330,43,1,0,0,0,
      331,337,5,33,0,0,332,337,5,34,0,0,333,334,5,40,0,0,334,335,5,33,0,0,335,
      337,5,41,0,0,336,331,1,0,0,0,336,332,1,0,0,0,336,333,1,0,0,0,337,45,1,
      0,0,0,34,52,54,65,73,93,98,110,117,125,129,138,140,153,163,175,185,203,
      205,217,228,235,244,257,259,273,275,295,304,306,312,317,325,329,336
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) {
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  vshader_parserParserStaticData = std::move(staticData);
}

}

vshader_parser::vshader_parser(TokenStream *input) : vshader_parser(input, antlr4::atn::ParserATNSimulatorOptions()) {}

vshader_parser::vshader_parser(TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options) : Parser(input) {
  vshader_parser::initialize();
  _interpreter = new atn::ParserATNSimulator(this, *vshader_parserParserStaticData->atn, vshader_parserParserStaticData->decisionToDFA, vshader_parserParserStaticData->sharedContextCache, options);
}

vshader_parser::~vshader_parser() {
  delete _interpreter;
}

const atn::ATN& vshader_parser::getATN() const {
  return *vshader_parserParserStaticData->atn;
}

std::string vshader_parser::getGrammarFileName() const {
  return "vshader_parser.g4";
}

const std::vector<std::string>& vshader_parser::getRuleNames() const {
  return vshader_parserParserStaticData->ruleNames;
}

const dfa::Vocabulary& vshader_parser::getVocabulary() const {
  return vshader_parserParserStaticData->vocabulary;
}

antlr4::atn::SerializedATNView vshader_parser::getSerializedATN() const {
  return vshader_parserParserStaticData->serializedATN;
}


//----------------- FileContext ------------------------------------------------------------------

vshader_parser::FileContext::FileContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::FileContext::SHADER() {
  return getToken(vshader_parser::SHADER, 0);
}

tree::TerminalNode* vshader_parser::FileContext::STRING() {
  return getToken(vshader_parser::STRING, 0);
}

tree::TerminalNode* vshader_parser::FileContext::LBRACE() {
  return getToken(vshader_parser::LBRACE, 0);
}

tree::TerminalNode* vshader_parser::FileContext::RBRACE() {
  return getToken(vshader_parser::RBRACE, 0);
}

tree::TerminalNode* vshader_parser::FileContext::EOF() {
  return getToken(vshader_parser::EOF, 0);
}

std::vector<vshader_parser::PropertiesContext *> vshader_parser::FileContext::properties() {
  return getRuleContexts<vshader_parser::PropertiesContext>();
}

vshader_parser::PropertiesContext* vshader_parser::FileContext::properties(size_t i) {
  return getRuleContext<vshader_parser::PropertiesContext>(i);
}

std::vector<vshader_parser::VariantContext *> vshader_parser::FileContext::variant() {
  return getRuleContexts<vshader_parser::VariantContext>();
}

vshader_parser::VariantContext* vshader_parser::FileContext::variant(size_t i) {
  return getRuleContext<vshader_parser::VariantContext>(i);
}

std::vector<vshader_parser::SubshaderContext *> vshader_parser::FileContext::subshader() {
  return getRuleContexts<vshader_parser::SubshaderContext>();
}

vshader_parser::SubshaderContext* vshader_parser::FileContext::subshader(size_t i) {
  return getRuleContext<vshader_parser::SubshaderContext>(i);
}


size_t vshader_parser::FileContext::getRuleIndex() const {
  return vshader_parser::RuleFile;
}


vshader_parser::FileContext* vshader_parser::file() {
  FileContext *_localctx = _tracker.createInstance<FileContext>(_ctx, getState());
  enterRule(_localctx, 0, vshader_parser::RuleFile);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(46);
    match(vshader_parser::SHADER);
    setState(47);
    match(vshader_parser::STRING);
    setState(48);
    match(vshader_parser::LBRACE);
    setState(54);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1036) != 0)) {
      setState(52);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case vshader_parser::PROPERTIES: {
          setState(49);
          properties();
          break;
        }

        case vshader_parser::VARIANT: {
          setState(50);
          variant();
          break;
        }

        case vshader_parser::SUBSHADER: {
          setState(51);
          subshader();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(56);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(57);
    match(vshader_parser::RBRACE);
    setState(58);
    match(vshader_parser::EOF);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PropertiesContext ------------------------------------------------------------------

vshader_parser::PropertiesContext::PropertiesContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::PropertiesContext::PROPERTIES() {
  return getToken(vshader_parser::PROPERTIES, 0);
}

tree::TerminalNode* vshader_parser::PropertiesContext::LBRACE() {
  return getToken(vshader_parser::LBRACE, 0);
}

tree::TerminalNode* vshader_parser::PropertiesContext::RBRACE() {
  return getToken(vshader_parser::RBRACE, 0);
}

std::vector<vshader_parser::PropertyContext *> vshader_parser::PropertiesContext::property() {
  return getRuleContexts<vshader_parser::PropertyContext>();
}

vshader_parser::PropertyContext* vshader_parser::PropertiesContext::property(size_t i) {
  return getRuleContext<vshader_parser::PropertyContext>(i);
}


size_t vshader_parser::PropertiesContext::getRuleIndex() const {
  return vshader_parser::RuleProperties;
}


vshader_parser::PropertiesContext* vshader_parser::properties() {
  PropertiesContext *_localctx = _tracker.createInstance<PropertiesContext>(_ctx, getState());
  enterRule(_localctx, 2, vshader_parser::RuleProperties);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(60);
    match(vshader_parser::PROPERTIES);
    setState(61);
    match(vshader_parser::LBRACE);
    setState(65);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == vshader_parser::IDENTIFIER

    || _la == vshader_parser::LBRACKET) {
      setState(62);
      property();
      setState(67);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(68);
    match(vshader_parser::RBRACE);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PropertyContext ------------------------------------------------------------------

vshader_parser::PropertyContext::PropertyContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::PropertyContext::IDENTIFIER() {
  return getToken(vshader_parser::IDENTIFIER, 0);
}

tree::TerminalNode* vshader_parser::PropertyContext::LPAREN() {
  return getToken(vshader_parser::LPAREN, 0);
}

tree::TerminalNode* vshader_parser::PropertyContext::STRING() {
  return getToken(vshader_parser::STRING, 0);
}

tree::TerminalNode* vshader_parser::PropertyContext::COMMA() {
  return getToken(vshader_parser::COMMA, 0);
}

vshader_parser::PropertyTypeContext* vshader_parser::PropertyContext::propertyType() {
  return getRuleContext<vshader_parser::PropertyTypeContext>(0);
}

tree::TerminalNode* vshader_parser::PropertyContext::RPAREN() {
  return getToken(vshader_parser::RPAREN, 0);
}

tree::TerminalNode* vshader_parser::PropertyContext::EQUAL() {
  return getToken(vshader_parser::EQUAL, 0);
}

vshader_parser::ValueContext* vshader_parser::PropertyContext::value() {
  return getRuleContext<vshader_parser::ValueContext>(0);
}

std::vector<vshader_parser::AttributeContext *> vshader_parser::PropertyContext::attribute() {
  return getRuleContexts<vshader_parser::AttributeContext>();
}

vshader_parser::AttributeContext* vshader_parser::PropertyContext::attribute(size_t i) {
  return getRuleContext<vshader_parser::AttributeContext>(i);
}


size_t vshader_parser::PropertyContext::getRuleIndex() const {
  return vshader_parser::RuleProperty;
}


vshader_parser::PropertyContext* vshader_parser::property() {
  PropertyContext *_localctx = _tracker.createInstance<PropertyContext>(_ctx, getState());
  enterRule(_localctx, 4, vshader_parser::RuleProperty);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(73);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == vshader_parser::LBRACKET) {
      setState(70);
      attribute();
      setState(75);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(76);
    match(vshader_parser::IDENTIFIER);
    setState(77);
    match(vshader_parser::LPAREN);
    setState(78);
    match(vshader_parser::STRING);
    setState(79);
    match(vshader_parser::COMMA);
    setState(80);
    propertyType();
    setState(81);
    match(vshader_parser::RPAREN);
    setState(82);
    match(vshader_parser::EQUAL);
    setState(83);
    value();

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AttributeContext ------------------------------------------------------------------

vshader_parser::AttributeContext::AttributeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::AttributeContext::LBRACKET() {
  return getToken(vshader_parser::LBRACKET, 0);
}

tree::TerminalNode* vshader_parser::AttributeContext::IDENTIFIER() {
  return getToken(vshader_parser::IDENTIFIER, 0);
}

tree::TerminalNode* vshader_parser::AttributeContext::RBRACKET() {
  return getToken(vshader_parser::RBRACKET, 0);
}

tree::TerminalNode* vshader_parser::AttributeContext::LPAREN() {
  return getToken(vshader_parser::LPAREN, 0);
}

std::vector<vshader_parser::AttributeArgumentContext *> vshader_parser::AttributeContext::attributeArgument() {
  return getRuleContexts<vshader_parser::AttributeArgumentContext>();
}

vshader_parser::AttributeArgumentContext* vshader_parser::AttributeContext::attributeArgument(size_t i) {
  return getRuleContext<vshader_parser::AttributeArgumentContext>(i);
}

tree::TerminalNode* vshader_parser::AttributeContext::RPAREN() {
  return getToken(vshader_parser::RPAREN, 0);
}

std::vector<tree::TerminalNode *> vshader_parser::AttributeContext::COMMA() {
  return getTokens(vshader_parser::COMMA);
}

tree::TerminalNode* vshader_parser::AttributeContext::COMMA(size_t i) {
  return getToken(vshader_parser::COMMA, i);
}


size_t vshader_parser::AttributeContext::getRuleIndex() const {
  return vshader_parser::RuleAttribute;
}


vshader_parser::AttributeContext* vshader_parser::attribute() {
  AttributeContext *_localctx = _tracker.createInstance<AttributeContext>(_ctx, getState());
  enterRule(_localctx, 6, vshader_parser::RuleAttribute);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(85);
    match(vshader_parser::LBRACKET);
    setState(86);
    match(vshader_parser::IDENTIFIER);
    setState(98);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == vshader_parser::LPAREN) {
      setState(87);
      match(vshader_parser::LPAREN);
      setState(88);
      attributeArgument();
      setState(93);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == vshader_parser::COMMA) {
        setState(89);
        match(vshader_parser::COMMA);
        setState(90);
        attributeArgument();
        setState(95);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(96);
      match(vshader_parser::RPAREN);
    }
    setState(100);
    match(vshader_parser::RBRACKET);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AttributeArgumentContext ------------------------------------------------------------------

vshader_parser::AttributeArgumentContext::AttributeArgumentContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::AttributeArgumentContext::IDENTIFIER() {
  return getToken(vshader_parser::IDENTIFIER, 0);
}

tree::TerminalNode* vshader_parser::AttributeArgumentContext::NUMBER() {
  return getToken(vshader_parser::NUMBER, 0);
}

tree::TerminalNode* vshader_parser::AttributeArgumentContext::STRING() {
  return getToken(vshader_parser::STRING, 0);
}


size_t vshader_parser::AttributeArgumentContext::getRuleIndex() const {
  return vshader_parser::RuleAttributeArgument;
}


vshader_parser::AttributeArgumentContext* vshader_parser::attributeArgument() {
  AttributeArgumentContext *_localctx = _tracker.createInstance<AttributeArgumentContext>(_ctx, getState());
  enterRule(_localctx, 8, vshader_parser::RuleAttributeArgument);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(102);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 60129542144) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PropertyTypeContext ------------------------------------------------------------------

vshader_parser::PropertyTypeContext::PropertyTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::PropertyTypeContext::IDENTIFIER() {
  return getToken(vshader_parser::IDENTIFIER, 0);
}

tree::TerminalNode* vshader_parser::PropertyTypeContext::TEXTURE_TYPE() {
  return getToken(vshader_parser::TEXTURE_TYPE, 0);
}

tree::TerminalNode* vshader_parser::PropertyTypeContext::LPAREN() {
  return getToken(vshader_parser::LPAREN, 0);
}

std::vector<tree::TerminalNode *> vshader_parser::PropertyTypeContext::NUMBER() {
  return getTokens(vshader_parser::NUMBER);
}

tree::TerminalNode* vshader_parser::PropertyTypeContext::NUMBER(size_t i) {
  return getToken(vshader_parser::NUMBER, i);
}

tree::TerminalNode* vshader_parser::PropertyTypeContext::COMMA() {
  return getToken(vshader_parser::COMMA, 0);
}

tree::TerminalNode* vshader_parser::PropertyTypeContext::RPAREN() {
  return getToken(vshader_parser::RPAREN, 0);
}


size_t vshader_parser::PropertyTypeContext::getRuleIndex() const {
  return vshader_parser::RulePropertyType;
}


vshader_parser::PropertyTypeContext* vshader_parser::propertyType() {
  PropertyTypeContext *_localctx = _tracker.createInstance<PropertyTypeContext>(_ctx, getState());
  enterRule(_localctx, 10, vshader_parser::RulePropertyType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(104);
    _la = _input->LA(1);
    if (!(_la == vshader_parser::TEXTURE_TYPE

    || _la == vshader_parser::IDENTIFIER)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(110);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == vshader_parser::LPAREN) {
      setState(105);
      match(vshader_parser::LPAREN);
      setState(106);
      match(vshader_parser::NUMBER);
      setState(107);
      match(vshader_parser::COMMA);
      setState(108);
      match(vshader_parser::NUMBER);
      setState(109);
      match(vshader_parser::RPAREN);
    }

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ValueContext ------------------------------------------------------------------

vshader_parser::ValueContext::ValueContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> vshader_parser::ValueContext::NUMBER() {
  return getTokens(vshader_parser::NUMBER);
}

tree::TerminalNode* vshader_parser::ValueContext::NUMBER(size_t i) {
  return getToken(vshader_parser::NUMBER, i);
}

tree::TerminalNode* vshader_parser::ValueContext::IDENTIFIER() {
  return getToken(vshader_parser::IDENTIFIER, 0);
}

tree::TerminalNode* vshader_parser::ValueContext::STRING() {
  return getToken(vshader_parser::STRING, 0);
}

tree::TerminalNode* vshader_parser::ValueContext::LBRACE() {
  return getToken(vshader_parser::LBRACE, 0);
}

tree::TerminalNode* vshader_parser::ValueContext::RBRACE() {
  return getToken(vshader_parser::RBRACE, 0);
}

tree::TerminalNode* vshader_parser::ValueContext::LPAREN() {
  return getToken(vshader_parser::LPAREN, 0);
}

tree::TerminalNode* vshader_parser::ValueContext::RPAREN() {
  return getToken(vshader_parser::RPAREN, 0);
}

std::vector<tree::TerminalNode *> vshader_parser::ValueContext::COMMA() {
  return getTokens(vshader_parser::COMMA);
}

tree::TerminalNode* vshader_parser::ValueContext::COMMA(size_t i) {
  return getToken(vshader_parser::COMMA, i);
}


size_t vshader_parser::ValueContext::getRuleIndex() const {
  return vshader_parser::RuleValue;
}


vshader_parser::ValueContext* vshader_parser::value() {
  ValueContext *_localctx = _tracker.createInstance<ValueContext>(_ctx, getState());
  enterRule(_localctx, 12, vshader_parser::RuleValue);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(129);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case vshader_parser::NUMBER: {
        enterOuterAlt(_localctx, 1);
        setState(112);
        match(vshader_parser::NUMBER);
        break;
      }

      case vshader_parser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(113);
        match(vshader_parser::IDENTIFIER);
        break;
      }

      case vshader_parser::STRING: {
        enterOuterAlt(_localctx, 3);
        setState(114);
        match(vshader_parser::STRING);
        setState(117);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == vshader_parser::LBRACE) {
          setState(115);
          match(vshader_parser::LBRACE);
          setState(116);
          match(vshader_parser::RBRACE);
        }
        break;
      }

      case vshader_parser::LPAREN: {
        enterOuterAlt(_localctx, 4);
        setState(119);
        match(vshader_parser::LPAREN);
        setState(120);
        match(vshader_parser::NUMBER);
        setState(125);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == vshader_parser::COMMA) {
          setState(121);
          match(vshader_parser::COMMA);
          setState(122);
          match(vshader_parser::NUMBER);
          setState(127);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
        setState(128);
        match(vshader_parser::RPAREN);
        break;
      }

    default:
      throw NoViableAltException(this);
    }

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- VariantContext ------------------------------------------------------------------

vshader_parser::VariantContext::VariantContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::VariantContext::VARIANT() {
  return getToken(vshader_parser::VARIANT, 0);
}

tree::TerminalNode* vshader_parser::VariantContext::STRING() {
  return getToken(vshader_parser::STRING, 0);
}

tree::TerminalNode* vshader_parser::VariantContext::LBRACE() {
  return getToken(vshader_parser::LBRACE, 0);
}

tree::TerminalNode* vshader_parser::VariantContext::RBRACE() {
  return getToken(vshader_parser::RBRACE, 0);
}

std::vector<vshader_parser::ConstantsContext *> vshader_parser::VariantContext::constants() {
  return getRuleContexts<vshader_parser::ConstantsContext>();
}

vshader_parser::ConstantsContext* vshader_parser::VariantContext::constants(size_t i) {
  return getRuleContext<vshader_parser::ConstantsContext>(i);
}

std::vector<vshader_parser::ModulesContext *> vshader_parser::VariantContext::modules() {
  return getRuleContexts<vshader_parser::ModulesContext>();
}

vshader_parser::ModulesContext* vshader_parser::VariantContext::modules(size_t i) {
  return getRuleContext<vshader_parser::ModulesContext>(i);
}

std::vector<vshader_parser::DefinesContext *> vshader_parser::VariantContext::defines() {
  return getRuleContexts<vshader_parser::DefinesContext>();
}

vshader_parser::DefinesContext* vshader_parser::VariantContext::defines(size_t i) {
  return getRuleContext<vshader_parser::DefinesContext>(i);
}

std::vector<vshader_parser::ConformancesContext *> vshader_parser::VariantContext::conformances() {
  return getRuleContexts<vshader_parser::ConformancesContext>();
}

vshader_parser::ConformancesContext* vshader_parser::VariantContext::conformances(size_t i) {
  return getRuleContext<vshader_parser::ConformancesContext>(i);
}


size_t vshader_parser::VariantContext::getRuleIndex() const {
  return vshader_parser::RuleVariant;
}


vshader_parser::VariantContext* vshader_parser::variant() {
  VariantContext *_localctx = _tracker.createInstance<VariantContext>(_ctx, getState());
  enterRule(_localctx, 14, vshader_parser::RuleVariant);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(131);
    match(vshader_parser::VARIANT);
    setState(132);
    match(vshader_parser::STRING);
    setState(133);
    match(vshader_parser::LBRACE);
    setState(140);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 30720) != 0)) {
      setState(138);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case vshader_parser::CONSTANTS: {
          setState(134);
          constants();
          break;
        }

        case vshader_parser::MODULES: {
          setState(135);
          modules();
          break;
        }

        case vshader_parser::DEFINES: {
          setState(136);
          defines();
          break;
        }

        case vshader_parser::CONFORMANCES: {
          setState(137);
          conformances();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(142);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(143);
    match(vshader_parser::RBRACE);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConstantsContext ------------------------------------------------------------------

vshader_parser::ConstantsContext::ConstantsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::ConstantsContext::CONSTANTS() {
  return getToken(vshader_parser::CONSTANTS, 0);
}

tree::TerminalNode* vshader_parser::ConstantsContext::LBRACE() {
  return getToken(vshader_parser::LBRACE, 0);
}

tree::TerminalNode* vshader_parser::ConstantsContext::RBRACE() {
  return getToken(vshader_parser::RBRACE, 0);
}

std::vector<tree::TerminalNode *> vshader_parser::ConstantsContext::IDENTIFIER() {
  return getTokens(vshader_parser::IDENTIFIER);
}

tree::TerminalNode* vshader_parser::ConstantsContext::IDENTIFIER(size_t i) {
  return getToken(vshader_parser::IDENTIFIER, i);
}

std::vector<tree::TerminalNode *> vshader_parser::ConstantsContext::EQUAL() {
  return getTokens(vshader_parser::EQUAL);
}

tree::TerminalNode* vshader_parser::ConstantsContext::EQUAL(size_t i) {
  return getToken(vshader_parser::EQUAL, i);
}

std::vector<tree::TerminalNode *> vshader_parser::ConstantsContext::NUMBER() {
  return getTokens(vshader_parser::NUMBER);
}

tree::TerminalNode* vshader_parser::ConstantsContext::NUMBER(size_t i) {
  return getToken(vshader_parser::NUMBER, i);
}


size_t vshader_parser::ConstantsContext::getRuleIndex() const {
  return vshader_parser::RuleConstants;
}


vshader_parser::ConstantsContext* vshader_parser::constants() {
  ConstantsContext *_localctx = _tracker.createInstance<ConstantsContext>(_ctx, getState());
  enterRule(_localctx, 16, vshader_parser::RuleConstants);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(145);
    match(vshader_parser::CONSTANTS);
    setState(146);
    match(vshader_parser::LBRACE);
    setState(153);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == vshader_parser::IDENTIFIER) {
      setState(147);
      match(vshader_parser::IDENTIFIER);
      setState(148);
      match(vshader_parser::IDENTIFIER);
      setState(149);
      match(vshader_parser::EQUAL);
      setState(150);
      _la = _input->LA(1);
      if (!(_la == vshader_parser::IDENTIFIER

      || _la == vshader_parser::NUMBER)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(155);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(156);
    match(vshader_parser::RBRACE);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ModulesContext ------------------------------------------------------------------

vshader_parser::ModulesContext::ModulesContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::ModulesContext::MODULES() {
  return getToken(vshader_parser::MODULES, 0);
}

tree::TerminalNode* vshader_parser::ModulesContext::LBRACE() {
  return getToken(vshader_parser::LBRACE, 0);
}

tree::TerminalNode* vshader_parser::ModulesContext::RBRACE() {
  return getToken(vshader_parser::RBRACE, 0);
}

std::vector<tree::TerminalNode *> vshader_parser::ModulesContext::STRING() {
  return getTokens(vshader_parser::STRING);
}

tree::TerminalNode* vshader_parser::ModulesContext::STRING(size_t i) {
  return getToken(vshader_parser::STRING, i);
}


size_t vshader_parser::ModulesContext::getRuleIndex() const {
  return vshader_parser::RuleModules;
}


vshader_parser::ModulesContext* vshader_parser::modules() {
  ModulesContext *_localctx = _tracker.createInstance<ModulesContext>(_ctx, getState());
  enterRule(_localctx, 18, vshader_parser::RuleModules);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(158);
    match(vshader_parser::MODULES);
    setState(159);
    match(vshader_parser::LBRACE);
    setState(163);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == vshader_parser::STRING) {
      setState(160);
      match(vshader_parser::STRING);
      setState(165);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(166);
    match(vshader_parser::RBRACE);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- DefinesContext ------------------------------------------------------------------

vshader_parser::DefinesContext::DefinesContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::DefinesContext::DEFINES() {
  return getToken(vshader_parser::DEFINES, 0);
}

tree::TerminalNode* vshader_parser::DefinesContext::LBRACE() {
  return getToken(vshader_parser::LBRACE, 0);
}

tree::TerminalNode* vshader_parser::DefinesContext::RBRACE() {
  return getToken(vshader_parser::RBRACE, 0);
}

std::vector<tree::TerminalNode *> vshader_parser::DefinesContext::IDENTIFIER() {
  return getTokens(vshader_parser::IDENTIFIER);
}

tree::TerminalNode* vshader_parser::DefinesContext::IDENTIFIER(size_t i) {
  return getToken(vshader_parser::IDENTIFIER, i);
}

std::vector<tree::TerminalNode *> vshader_parser::DefinesContext::EQUAL() {
  return getTokens(vshader_parser::EQUAL);
}

tree::TerminalNode* vshader_parser::DefinesContext::EQUAL(size_t i) {
  return getToken(vshader_parser::EQUAL, i);
}

std::vector<tree::TerminalNode *> vshader_parser::DefinesContext::NUMBER() {
  return getTokens(vshader_parser::NUMBER);
}

tree::TerminalNode* vshader_parser::DefinesContext::NUMBER(size_t i) {
  return getToken(vshader_parser::NUMBER, i);
}

std::vector<tree::TerminalNode *> vshader_parser::DefinesContext::STRING() {
  return getTokens(vshader_parser::STRING);
}

tree::TerminalNode* vshader_parser::DefinesContext::STRING(size_t i) {
  return getToken(vshader_parser::STRING, i);
}


size_t vshader_parser::DefinesContext::getRuleIndex() const {
  return vshader_parser::RuleDefines;
}


vshader_parser::DefinesContext* vshader_parser::defines() {
  DefinesContext *_localctx = _tracker.createInstance<DefinesContext>(_ctx, getState());
  enterRule(_localctx, 20, vshader_parser::RuleDefines);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(168);
    match(vshader_parser::DEFINES);
    setState(169);
    match(vshader_parser::LBRACE);
    setState(175);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == vshader_parser::IDENTIFIER) {
      setState(170);
      match(vshader_parser::IDENTIFIER);
      setState(171);
      match(vshader_parser::EQUAL);
      setState(172);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 60129542144) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(177);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(178);
    match(vshader_parser::RBRACE);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConformancesContext ------------------------------------------------------------------

vshader_parser::ConformancesContext::ConformancesContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::ConformancesContext::CONFORMANCES() {
  return getToken(vshader_parser::CONFORMANCES, 0);
}

tree::TerminalNode* vshader_parser::ConformancesContext::LBRACE() {
  return getToken(vshader_parser::LBRACE, 0);
}

tree::TerminalNode* vshader_parser::ConformancesContext::RBRACE() {
  return getToken(vshader_parser::RBRACE, 0);
}

std::vector<vshader_parser::ConformanceContext *> vshader_parser::ConformancesContext::conformance() {
  return getRuleContexts<vshader_parser::ConformanceContext>();
}

vshader_parser::ConformanceContext* vshader_parser::ConformancesContext::conformance(size_t i) {
  return getRuleContext<vshader_parser::ConformanceContext>(i);
}


size_t vshader_parser::ConformancesContext::getRuleIndex() const {
  return vshader_parser::RuleConformances;
}


vshader_parser::ConformancesContext* vshader_parser::conformances() {
  ConformancesContext *_localctx = _tracker.createInstance<ConformancesContext>(_ctx, getState());
  enterRule(_localctx, 22, vshader_parser::RuleConformances);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(180);
    match(vshader_parser::CONFORMANCES);
    setState(181);
    match(vshader_parser::LBRACE);
    setState(185);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == vshader_parser::STRING) {
      setState(182);
      conformance();
      setState(187);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(188);
    match(vshader_parser::RBRACE);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConformanceContext ------------------------------------------------------------------

vshader_parser::ConformanceContext::ConformanceContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> vshader_parser::ConformanceContext::STRING() {
  return getTokens(vshader_parser::STRING);
}

tree::TerminalNode* vshader_parser::ConformanceContext::STRING(size_t i) {
  return getToken(vshader_parser::STRING, i);
}

tree::TerminalNode* vshader_parser::ConformanceContext::EQUAL() {
  return getToken(vshader_parser::EQUAL, 0);
}

tree::TerminalNode* vshader_parser::ConformanceContext::NUMBER() {
  return getToken(vshader_parser::NUMBER, 0);
}


size_t vshader_parser::ConformanceContext::getRuleIndex() const {
  return vshader_parser::RuleConformance;
}


vshader_parser::ConformanceContext* vshader_parser::conformance() {
  ConformanceContext *_localctx = _tracker.createInstance<ConformanceContext>(_ctx, getState());
  enterRule(_localctx, 24, vshader_parser::RuleConformance);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(190);
    match(vshader_parser::STRING);
    setState(191);
    match(vshader_parser::STRING);
    setState(192);
    match(vshader_parser::EQUAL);
    setState(193);
    match(vshader_parser::NUMBER);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- SubshaderContext ------------------------------------------------------------------

vshader_parser::SubshaderContext::SubshaderContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::SubshaderContext::SUBSHADER() {
  return getToken(vshader_parser::SUBSHADER, 0);
}

tree::TerminalNode* vshader_parser::SubshaderContext::LBRACE() {
  return getToken(vshader_parser::LBRACE, 0);
}

tree::TerminalNode* vshader_parser::SubshaderContext::RBRACE() {
  return getToken(vshader_parser::RBRACE, 0);
}

std::vector<vshader_parser::TagsContext *> vshader_parser::SubshaderContext::tags() {
  return getRuleContexts<vshader_parser::TagsContext>();
}

vshader_parser::TagsContext* vshader_parser::SubshaderContext::tags(size_t i) {
  return getRuleContext<vshader_parser::TagsContext>(i);
}

std::vector<vshader_parser::RequiresContext *> vshader_parser::SubshaderContext::requires_() {
  return getRuleContexts<vshader_parser::RequiresContext>();
}

vshader_parser::RequiresContext* vshader_parser::SubshaderContext::requires_(size_t i) {
  return getRuleContext<vshader_parser::RequiresContext>(i);
}

std::vector<vshader_parser::StateContext *> vshader_parser::SubshaderContext::state() {
  return getRuleContexts<vshader_parser::StateContext>();
}

vshader_parser::StateContext* vshader_parser::SubshaderContext::state(size_t i) {
  return getRuleContext<vshader_parser::StateContext>(i);
}

std::vector<vshader_parser::CommonContext *> vshader_parser::SubshaderContext::common() {
  return getRuleContexts<vshader_parser::CommonContext>();
}

vshader_parser::CommonContext* vshader_parser::SubshaderContext::common(size_t i) {
  return getRuleContext<vshader_parser::CommonContext>(i);
}

std::vector<vshader_parser::SurfaceContext *> vshader_parser::SubshaderContext::surface() {
  return getRuleContexts<vshader_parser::SurfaceContext>();
}

vshader_parser::SurfaceContext* vshader_parser::SubshaderContext::surface(size_t i) {
  return getRuleContext<vshader_parser::SurfaceContext>(i);
}

std::vector<vshader_parser::PassContext *> vshader_parser::SubshaderContext::pass() {
  return getRuleContexts<vshader_parser::PassContext>();
}

vshader_parser::PassContext* vshader_parser::SubshaderContext::pass(size_t i) {
  return getRuleContext<vshader_parser::PassContext>(i);
}


size_t vshader_parser::SubshaderContext::getRuleIndex() const {
  return vshader_parser::RuleSubshader;
}


vshader_parser::SubshaderContext* vshader_parser::subshader() {
  SubshaderContext *_localctx = _tracker.createInstance<SubshaderContext>(_ctx, getState());
  enterRule(_localctx, 26, vshader_parser::RuleSubshader);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(195);
    match(vshader_parser::SUBSHADER);
    setState(196);
    match(vshader_parser::LBRACE);
    setState(205);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 3219161200) != 0)) {
      setState(203);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case vshader_parser::TAGS: {
          setState(197);
          tags();
          break;
        }

        case vshader_parser::REQUIRES: {
          setState(198);
          requires_();
          break;
        }

        case vshader_parser::CULL:
        case vshader_parser::FRONT_FACE:
        case vshader_parser::ZWRITE:
        case vshader_parser::ZTEST:
        case vshader_parser::DEPTH_BIAS:
        case vshader_parser::BLEND:
        case vshader_parser::BLEND_OP:
        case vshader_parser::COLOR_MASK:
        case vshader_parser::STENCIL: {
          setState(199);
          state();
          break;
        }

        case vshader_parser::SLANG_INCLUDE: {
          setState(200);
          common();
          break;
        }

        case vshader_parser::SURFACE: {
          setState(201);
          surface();
          break;
        }

        case vshader_parser::PASS: {
          setState(202);
          pass();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(207);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(208);
    match(vshader_parser::RBRACE);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TagsContext ------------------------------------------------------------------

vshader_parser::TagsContext::TagsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::TagsContext::TAGS() {
  return getToken(vshader_parser::TAGS, 0);
}

tree::TerminalNode* vshader_parser::TagsContext::LBRACE() {
  return getToken(vshader_parser::LBRACE, 0);
}

tree::TerminalNode* vshader_parser::TagsContext::RBRACE() {
  return getToken(vshader_parser::RBRACE, 0);
}

std::vector<tree::TerminalNode *> vshader_parser::TagsContext::STRING() {
  return getTokens(vshader_parser::STRING);
}

tree::TerminalNode* vshader_parser::TagsContext::STRING(size_t i) {
  return getToken(vshader_parser::STRING, i);
}

std::vector<tree::TerminalNode *> vshader_parser::TagsContext::EQUAL() {
  return getTokens(vshader_parser::EQUAL);
}

tree::TerminalNode* vshader_parser::TagsContext::EQUAL(size_t i) {
  return getToken(vshader_parser::EQUAL, i);
}


size_t vshader_parser::TagsContext::getRuleIndex() const {
  return vshader_parser::RuleTags;
}


vshader_parser::TagsContext* vshader_parser::tags() {
  TagsContext *_localctx = _tracker.createInstance<TagsContext>(_ctx, getState());
  enterRule(_localctx, 28, vshader_parser::RuleTags);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(210);
    match(vshader_parser::TAGS);
    setState(211);
    match(vshader_parser::LBRACE);
    setState(217);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == vshader_parser::STRING) {
      setState(212);
      match(vshader_parser::STRING);
      setState(213);
      match(vshader_parser::EQUAL);
      setState(214);
      match(vshader_parser::STRING);
      setState(219);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(220);
    match(vshader_parser::RBRACE);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RequiresContext ------------------------------------------------------------------

vshader_parser::RequiresContext::RequiresContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::RequiresContext::REQUIRES() {
  return getToken(vshader_parser::REQUIRES, 0);
}

std::vector<tree::TerminalNode *> vshader_parser::RequiresContext::IDENTIFIER() {
  return getTokens(vshader_parser::IDENTIFIER);
}

tree::TerminalNode* vshader_parser::RequiresContext::IDENTIFIER(size_t i) {
  return getToken(vshader_parser::IDENTIFIER, i);
}

std::vector<tree::TerminalNode *> vshader_parser::RequiresContext::COMMA() {
  return getTokens(vshader_parser::COMMA);
}

tree::TerminalNode* vshader_parser::RequiresContext::COMMA(size_t i) {
  return getToken(vshader_parser::COMMA, i);
}


size_t vshader_parser::RequiresContext::getRuleIndex() const {
  return vshader_parser::RuleRequires;
}


vshader_parser::RequiresContext* vshader_parser::requires_() {
  RequiresContext *_localctx = _tracker.createInstance<RequiresContext>(_ctx, getState());
  enterRule(_localctx, 30, vshader_parser::RuleRequires);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(222);
    match(vshader_parser::REQUIRES);
    setState(223);
    match(vshader_parser::IDENTIFIER);
    setState(228);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == vshader_parser::COMMA) {
      setState(224);
      match(vshader_parser::COMMA);
      setState(225);
      match(vshader_parser::IDENTIFIER);
      setState(230);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- CommonContext ------------------------------------------------------------------

vshader_parser::CommonContext::CommonContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::CommonContext::SLANG_INCLUDE() {
  return getToken(vshader_parser::SLANG_INCLUDE, 0);
}

tree::TerminalNode* vshader_parser::CommonContext::PROGRAM_END() {
  return getToken(vshader_parser::PROGRAM_END, 0);
}

std::vector<tree::TerminalNode *> vshader_parser::CommonContext::PROGRAM_TEXT() {
  return getTokens(vshader_parser::PROGRAM_TEXT);
}

tree::TerminalNode* vshader_parser::CommonContext::PROGRAM_TEXT(size_t i) {
  return getToken(vshader_parser::PROGRAM_TEXT, i);
}


size_t vshader_parser::CommonContext::getRuleIndex() const {
  return vshader_parser::RuleCommon;
}


vshader_parser::CommonContext* vshader_parser::common() {
  CommonContext *_localctx = _tracker.createInstance<CommonContext>(_ctx, getState());
  enterRule(_localctx, 32, vshader_parser::RuleCommon);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(231);
    match(vshader_parser::SLANG_INCLUDE);
    setState(235);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == vshader_parser::PROGRAM_TEXT) {
      setState(232);
      match(vshader_parser::PROGRAM_TEXT);
      setState(237);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(238);
    match(vshader_parser::PROGRAM_END);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ProgramContext ------------------------------------------------------------------

vshader_parser::ProgramContext::ProgramContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::ProgramContext::SLANG_PROGRAM() {
  return getToken(vshader_parser::SLANG_PROGRAM, 0);
}

tree::TerminalNode* vshader_parser::ProgramContext::PROGRAM_END() {
  return getToken(vshader_parser::PROGRAM_END, 0);
}

std::vector<tree::TerminalNode *> vshader_parser::ProgramContext::PROGRAM_TEXT() {
  return getTokens(vshader_parser::PROGRAM_TEXT);
}

tree::TerminalNode* vshader_parser::ProgramContext::PROGRAM_TEXT(size_t i) {
  return getToken(vshader_parser::PROGRAM_TEXT, i);
}


size_t vshader_parser::ProgramContext::getRuleIndex() const {
  return vshader_parser::RuleProgram;
}


vshader_parser::ProgramContext* vshader_parser::program() {
  ProgramContext *_localctx = _tracker.createInstance<ProgramContext>(_ctx, getState());
  enterRule(_localctx, 34, vshader_parser::RuleProgram);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(240);
    match(vshader_parser::SLANG_PROGRAM);
    setState(244);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == vshader_parser::PROGRAM_TEXT) {
      setState(241);
      match(vshader_parser::PROGRAM_TEXT);
      setState(246);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(247);
    match(vshader_parser::PROGRAM_END);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- SurfaceContext ------------------------------------------------------------------

vshader_parser::SurfaceContext::SurfaceContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::SurfaceContext::SURFACE() {
  return getToken(vshader_parser::SURFACE, 0);
}

tree::TerminalNode* vshader_parser::SurfaceContext::LBRACE() {
  return getToken(vshader_parser::LBRACE, 0);
}

tree::TerminalNode* vshader_parser::SurfaceContext::RBRACE() {
  return getToken(vshader_parser::RBRACE, 0);
}

std::vector<tree::TerminalNode *> vshader_parser::SurfaceContext::MODEL() {
  return getTokens(vshader_parser::MODEL);
}

tree::TerminalNode* vshader_parser::SurfaceContext::MODEL(size_t i) {
  return getToken(vshader_parser::MODEL, i);
}

std::vector<tree::TerminalNode *> vshader_parser::SurfaceContext::IDENTIFIER() {
  return getTokens(vshader_parser::IDENTIFIER);
}

tree::TerminalNode* vshader_parser::SurfaceContext::IDENTIFIER(size_t i) {
  return getToken(vshader_parser::IDENTIFIER, i);
}

std::vector<tree::TerminalNode *> vshader_parser::SurfaceContext::ENTRY() {
  return getTokens(vshader_parser::ENTRY);
}

tree::TerminalNode* vshader_parser::SurfaceContext::ENTRY(size_t i) {
  return getToken(vshader_parser::ENTRY, i);
}

std::vector<vshader_parser::StateContext *> vshader_parser::SurfaceContext::state() {
  return getRuleContexts<vshader_parser::StateContext>();
}

vshader_parser::StateContext* vshader_parser::SurfaceContext::state(size_t i) {
  return getRuleContext<vshader_parser::StateContext>(i);
}

std::vector<vshader_parser::ProgramContext *> vshader_parser::SurfaceContext::program() {
  return getRuleContexts<vshader_parser::ProgramContext>();
}

vshader_parser::ProgramContext* vshader_parser::SurfaceContext::program(size_t i) {
  return getRuleContext<vshader_parser::ProgramContext>(i);
}


size_t vshader_parser::SurfaceContext::getRuleIndex() const {
  return vshader_parser::RuleSurface;
}


vshader_parser::SurfaceContext* vshader_parser::surface() {
  SurfaceContext *_localctx = _tracker.createInstance<SurfaceContext>(_ctx, getState());
  enterRule(_localctx, 36, vshader_parser::RuleSurface);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(249);
    match(vshader_parser::SURFACE);
    setState(250);
    match(vshader_parser::LBRACE);
    setState(259);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2145387264) != 0)) {
      setState(257);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case vshader_parser::MODEL: {
          setState(251);
          match(vshader_parser::MODEL);
          setState(252);
          match(vshader_parser::IDENTIFIER);
          break;
        }

        case vshader_parser::ENTRY: {
          setState(253);
          match(vshader_parser::ENTRY);
          setState(254);
          match(vshader_parser::IDENTIFIER);
          break;
        }

        case vshader_parser::CULL:
        case vshader_parser::FRONT_FACE:
        case vshader_parser::ZWRITE:
        case vshader_parser::ZTEST:
        case vshader_parser::DEPTH_BIAS:
        case vshader_parser::BLEND:
        case vshader_parser::BLEND_OP:
        case vshader_parser::COLOR_MASK:
        case vshader_parser::STENCIL: {
          setState(255);
          state();
          break;
        }

        case vshader_parser::SLANG_PROGRAM: {
          setState(256);
          program();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(261);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(262);
    match(vshader_parser::RBRACE);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PassContext ------------------------------------------------------------------

vshader_parser::PassContext::PassContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::PassContext::PASS() {
  return getToken(vshader_parser::PASS, 0);
}

tree::TerminalNode* vshader_parser::PassContext::LBRACE() {
  return getToken(vshader_parser::LBRACE, 0);
}

tree::TerminalNode* vshader_parser::PassContext::RBRACE() {
  return getToken(vshader_parser::RBRACE, 0);
}

std::vector<tree::TerminalNode *> vshader_parser::PassContext::NAME() {
  return getTokens(vshader_parser::NAME);
}

tree::TerminalNode* vshader_parser::PassContext::NAME(size_t i) {
  return getToken(vshader_parser::NAME, i);
}

std::vector<tree::TerminalNode *> vshader_parser::PassContext::STRING() {
  return getTokens(vshader_parser::STRING);
}

tree::TerminalNode* vshader_parser::PassContext::STRING(size_t i) {
  return getToken(vshader_parser::STRING, i);
}

std::vector<vshader_parser::TagsContext *> vshader_parser::PassContext::tags() {
  return getRuleContexts<vshader_parser::TagsContext>();
}

vshader_parser::TagsContext* vshader_parser::PassContext::tags(size_t i) {
  return getRuleContext<vshader_parser::TagsContext>(i);
}

std::vector<vshader_parser::StageContext *> vshader_parser::PassContext::stage() {
  return getRuleContexts<vshader_parser::StageContext>();
}

vshader_parser::StageContext* vshader_parser::PassContext::stage(size_t i) {
  return getRuleContext<vshader_parser::StageContext>(i);
}

std::vector<vshader_parser::RequiresContext *> vshader_parser::PassContext::requires_() {
  return getRuleContexts<vshader_parser::RequiresContext>();
}

vshader_parser::RequiresContext* vshader_parser::PassContext::requires_(size_t i) {
  return getRuleContext<vshader_parser::RequiresContext>(i);
}

std::vector<vshader_parser::StateContext *> vshader_parser::PassContext::state() {
  return getRuleContexts<vshader_parser::StateContext>();
}

vshader_parser::StateContext* vshader_parser::PassContext::state(size_t i) {
  return getRuleContext<vshader_parser::StateContext>(i);
}

std::vector<vshader_parser::ProgramContext *> vshader_parser::PassContext::program() {
  return getRuleContexts<vshader_parser::ProgramContext>();
}

vshader_parser::ProgramContext* vshader_parser::PassContext::program(size_t i) {
  return getRuleContext<vshader_parser::ProgramContext>(i);
}


size_t vshader_parser::PassContext::getRuleIndex() const {
  return vshader_parser::RulePass;
}


vshader_parser::PassContext* vshader_parser::pass() {
  PassContext *_localctx = _tracker.createInstance<PassContext>(_ctx, getState());
  enterRule(_localctx, 38, vshader_parser::RulePass);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(264);
    match(vshader_parser::PASS);
    setState(265);
    match(vshader_parser::LBRACE);
    setState(275);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2147451072) != 0)) {
      setState(273);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case vshader_parser::NAME: {
          setState(266);
          match(vshader_parser::NAME);
          setState(267);
          match(vshader_parser::STRING);
          break;
        }

        case vshader_parser::TAGS: {
          setState(268);
          tags();
          break;
        }

        case vshader_parser::VERTEX:
        case vshader_parser::FRAGMENT:
        case vshader_parser::TASK:
        case vshader_parser::MESH:
        case vshader_parser::COMPUTE: {
          setState(269);
          stage();
          break;
        }

        case vshader_parser::REQUIRES: {
          setState(270);
          requires_();
          break;
        }

        case vshader_parser::CULL:
        case vshader_parser::FRONT_FACE:
        case vshader_parser::ZWRITE:
        case vshader_parser::ZTEST:
        case vshader_parser::DEPTH_BIAS:
        case vshader_parser::BLEND:
        case vshader_parser::BLEND_OP:
        case vshader_parser::COLOR_MASK:
        case vshader_parser::STENCIL: {
          setState(271);
          state();
          break;
        }

        case vshader_parser::SLANG_PROGRAM: {
          setState(272);
          program();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(277);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(278);
    match(vshader_parser::RBRACE);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StageContext ------------------------------------------------------------------

vshader_parser::StageContext::StageContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::StageContext::IDENTIFIER() {
  return getToken(vshader_parser::IDENTIFIER, 0);
}

tree::TerminalNode* vshader_parser::StageContext::VERTEX() {
  return getToken(vshader_parser::VERTEX, 0);
}

tree::TerminalNode* vshader_parser::StageContext::FRAGMENT() {
  return getToken(vshader_parser::FRAGMENT, 0);
}

tree::TerminalNode* vshader_parser::StageContext::TASK() {
  return getToken(vshader_parser::TASK, 0);
}

tree::TerminalNode* vshader_parser::StageContext::MESH() {
  return getToken(vshader_parser::MESH, 0);
}

tree::TerminalNode* vshader_parser::StageContext::COMPUTE() {
  return getToken(vshader_parser::COMPUTE, 0);
}


size_t vshader_parser::StageContext::getRuleIndex() const {
  return vshader_parser::RuleStage;
}


vshader_parser::StageContext* vshader_parser::stage() {
  StageContext *_localctx = _tracker.createInstance<StageContext>(_ctx, getState());
  enterRule(_localctx, 40, vshader_parser::RuleStage);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(280);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2031616) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(281);
    match(vshader_parser::IDENTIFIER);

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StateContext ------------------------------------------------------------------

vshader_parser::StateContext::StateContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::StateContext::CULL() {
  return getToken(vshader_parser::CULL, 0);
}

std::vector<vshader_parser::StateValueContext *> vshader_parser::StateContext::stateValue() {
  return getRuleContexts<vshader_parser::StateValueContext>();
}

vshader_parser::StateValueContext* vshader_parser::StateContext::stateValue(size_t i) {
  return getRuleContext<vshader_parser::StateValueContext>(i);
}

tree::TerminalNode* vshader_parser::StateContext::FRONT_FACE() {
  return getToken(vshader_parser::FRONT_FACE, 0);
}

tree::TerminalNode* vshader_parser::StateContext::ZWRITE() {
  return getToken(vshader_parser::ZWRITE, 0);
}

tree::TerminalNode* vshader_parser::StateContext::ZTEST() {
  return getToken(vshader_parser::ZTEST, 0);
}

tree::TerminalNode* vshader_parser::StateContext::DEPTH_BIAS() {
  return getToken(vshader_parser::DEPTH_BIAS, 0);
}

tree::TerminalNode* vshader_parser::StateContext::BLEND() {
  return getToken(vshader_parser::BLEND, 0);
}

tree::TerminalNode* vshader_parser::StateContext::COMMA() {
  return getToken(vshader_parser::COMMA, 0);
}

tree::TerminalNode* vshader_parser::StateContext::BLEND_OP() {
  return getToken(vshader_parser::BLEND_OP, 0);
}

tree::TerminalNode* vshader_parser::StateContext::COLOR_MASK() {
  return getToken(vshader_parser::COLOR_MASK, 0);
}

tree::TerminalNode* vshader_parser::StateContext::NUMBER() {
  return getToken(vshader_parser::NUMBER, 0);
}

tree::TerminalNode* vshader_parser::StateContext::STENCIL() {
  return getToken(vshader_parser::STENCIL, 0);
}

tree::TerminalNode* vshader_parser::StateContext::LBRACE() {
  return getToken(vshader_parser::LBRACE, 0);
}

tree::TerminalNode* vshader_parser::StateContext::RBRACE() {
  return getToken(vshader_parser::RBRACE, 0);
}

std::vector<tree::TerminalNode *> vshader_parser::StateContext::IDENTIFIER() {
  return getTokens(vshader_parser::IDENTIFIER);
}

tree::TerminalNode* vshader_parser::StateContext::IDENTIFIER(size_t i) {
  return getToken(vshader_parser::IDENTIFIER, i);
}


size_t vshader_parser::StateContext::getRuleIndex() const {
  return vshader_parser::RuleState;
}


vshader_parser::StateContext* vshader_parser::state() {
  StateContext *_localctx = _tracker.createInstance<StateContext>(_ctx, getState());
  enterRule(_localctx, 42, vshader_parser::RuleState);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(329);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case vshader_parser::CULL: {
        enterOuterAlt(_localctx, 1);
        setState(283);
        match(vshader_parser::CULL);
        setState(284);
        stateValue();
        break;
      }

      case vshader_parser::FRONT_FACE: {
        enterOuterAlt(_localctx, 2);
        setState(285);
        match(vshader_parser::FRONT_FACE);
        setState(286);
        stateValue();
        break;
      }

      case vshader_parser::ZWRITE: {
        enterOuterAlt(_localctx, 3);
        setState(287);
        match(vshader_parser::ZWRITE);
        setState(288);
        stateValue();
        break;
      }

      case vshader_parser::ZTEST: {
        enterOuterAlt(_localctx, 4);
        setState(289);
        match(vshader_parser::ZTEST);
        setState(290);
        stateValue();
        break;
      }

      case vshader_parser::DEPTH_BIAS: {
        enterOuterAlt(_localctx, 5);
        setState(291);
        match(vshader_parser::DEPTH_BIAS);
        setState(292);
        stateValue();
        setState(293);
        stateValue();
        setState(295);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 1125281431552) != 0)) {
          setState(294);
          stateValue();
        }
        break;
      }

      case vshader_parser::BLEND: {
        enterOuterAlt(_localctx, 6);
        setState(297);
        match(vshader_parser::BLEND);
        setState(298);
        stateValue();
        setState(306);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 1125281431552) != 0)) {
          setState(299);
          stateValue();
          setState(304);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == vshader_parser::COMMA) {
            setState(300);
            match(vshader_parser::COMMA);
            setState(301);
            stateValue();
            setState(302);
            stateValue();
          }
        }
        break;
      }

      case vshader_parser::BLEND_OP: {
        enterOuterAlt(_localctx, 7);
        setState(308);
        match(vshader_parser::BLEND_OP);
        setState(309);
        stateValue();
        setState(312);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == vshader_parser::COMMA) {
          setState(310);
          match(vshader_parser::COMMA);
          setState(311);
          stateValue();
        }
        break;
      }

      case vshader_parser::COLOR_MASK: {
        enterOuterAlt(_localctx, 8);
        setState(314);
        match(vshader_parser::COLOR_MASK);
        setState(315);
        stateValue();
        setState(317);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == vshader_parser::NUMBER) {
          setState(316);
          match(vshader_parser::NUMBER);
        }
        break;
      }

      case vshader_parser::STENCIL: {
        enterOuterAlt(_localctx, 9);
        setState(319);
        match(vshader_parser::STENCIL);
        setState(320);
        match(vshader_parser::LBRACE);
        setState(325);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == vshader_parser::IDENTIFIER) {
          setState(321);
          match(vshader_parser::IDENTIFIER);
          setState(322);
          stateValue();
          setState(327);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
        setState(328);
        match(vshader_parser::RBRACE);
        break;
      }

    default:
      throw NoViableAltException(this);
    }

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StateValueContext ------------------------------------------------------------------

vshader_parser::StateValueContext::StateValueContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* vshader_parser::StateValueContext::IDENTIFIER() {
  return getToken(vshader_parser::IDENTIFIER, 0);
}

tree::TerminalNode* vshader_parser::StateValueContext::NUMBER() {
  return getToken(vshader_parser::NUMBER, 0);
}

tree::TerminalNode* vshader_parser::StateValueContext::LBRACKET() {
  return getToken(vshader_parser::LBRACKET, 0);
}

tree::TerminalNode* vshader_parser::StateValueContext::RBRACKET() {
  return getToken(vshader_parser::RBRACKET, 0);
}


size_t vshader_parser::StateValueContext::getRuleIndex() const {
  return vshader_parser::RuleStateValue;
}


vshader_parser::StateValueContext* vshader_parser::stateValue() {
  StateValueContext *_localctx = _tracker.createInstance<StateValueContext>(_ctx, getState());
  enterRule(_localctx, 44, vshader_parser::RuleStateValue);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(336);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case vshader_parser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(331);
        match(vshader_parser::IDENTIFIER);
        break;
      }

      case vshader_parser::NUMBER: {
        enterOuterAlt(_localctx, 2);
        setState(332);
        match(vshader_parser::NUMBER);
        break;
      }

      case vshader_parser::LBRACKET: {
        enterOuterAlt(_localctx, 3);
        setState(333);
        match(vshader_parser::LBRACKET);
        setState(334);
        match(vshader_parser::IDENTIFIER);
        setState(335);
        match(vshader_parser::RBRACKET);
        break;
      }

    default:
      throw NoViableAltException(this);
    }

  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

void vshader_parser::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  vshader_parserParserInitialize();
#else
  ::antlr4::internal::call_once(vshader_parserParserOnceFlag, vshader_parserParserInitialize);
#endif
}
