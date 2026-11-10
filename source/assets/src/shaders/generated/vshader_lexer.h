
// Generated from vshader_lexer.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  vshader_lexer : public antlr4::Lexer {
public:
  enum {
    SHADER = 1, PROPERTIES = 2, SUBSHADER = 3, PASS = 4, SURFACE = 5, TAGS = 6,
    NAME = 7, MODEL = 8, ENTRY = 9, VARIANT = 10, CONSTANTS = 11, MODULES = 12,
    DEFINES = 13, CONFORMANCES = 14, REQUIRES = 15, VERTEX = 16, FRAGMENT = 17,
    TASK = 18, MESH = 19, COMPUTE = 20, CULL = 21, FRONT_FACE = 22, ZWRITE = 23,
    ZTEST = 24, DEPTH_BIAS = 25, BLEND = 26, BLEND_OP = 27, COLOR_MASK = 28,
    STENCIL = 29, SLANG_PROGRAM = 30, SLANG_INCLUDE = 31, TEXTURE_TYPE = 32,
    IDENTIFIER = 33, NUMBER = 34, STRING = 35, LBRACE = 36, RBRACE = 37,
    LPAREN = 38, RPAREN = 39, LBRACKET = 40, RBRACKET = 41, COMMA = 42,
    EQUAL = 43, SEMICOLON = 44, LINE_COMMENT = 45, BLOCK_COMMENT = 46, WHITESPACE = 47,
    PROGRAM_END = 48, PROGRAM_TEXT = 49
  };

  enum {
    PROGRAM = 1, RAW_STRING = 2
  };

  explicit vshader_lexer(antlr4::CharStream *input);

  ~vshader_lexer() override;


      bool isProgramEnd()
      {
          if (getCharPositionInLine() != 0)
          {
              return false;
          }
          auto previous = _input->LA(-2);
          if (previous == '\r')
          {
              previous = _input->LA(-3);
          }
          if (previous == '\\')
          {
              return false;
          }
          size_t offset = 1;
          while (_input->LA(offset) == ' ' || _input->LA(offset) == '\t')
          {
              ++offset;
          }
          for (char character : std::string("ENDSLANG"))
          {
              if (_input->LA(offset++) != size_t(character))
              {
                  return false;
              }
          }
          while (_input->LA(offset) == ' ' || _input->LA(offset) == '\t')
          {
              ++offset;
          }
          return _input->LA(offset) == '\r' || _input->LA(offset) == '\n' ||
                 _input->LA(offset) == antlr4::Token::EOF;
      }
      std::string rawDelimiter;

      bool isProgramSpecial()
      {
          const auto current = _input->LA(1);
          const auto next = _input->LA(2);
          return current == '"' || current == '\'' ||
                 (current == '/' && (next == '/' || next == '*')) || (current == 'R' && next == '"');
      }

      bool isRawStringEnd()
      {
          if (_input->LA(1) != ')')
          {
              return false;
          }
          size_t offset = 2;
          for (const auto character : rawDelimiter)
          {
              if (_input->LA(offset++) != size_t(character))
              {
                  return false;
              }
          }
          return _input->LA(offset) == '"';
      }


  std::string getGrammarFileName() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const std::vector<std::string>& getChannelNames() const override;

  const std::vector<std::string>& getModeNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  const antlr4::atn::ATN& getATN() const override;

  void action(antlr4::RuleContext *context, size_t ruleIndex, size_t actionIndex) override;

  bool sempred(antlr4::RuleContext *_localctx, size_t ruleIndex, size_t predicateIndex) override;

  // By default the static state used to implement the lexer is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:

  // Individual action functions triggered by action() above.
  void PROGRAM_RAW_STARTAction(antlr4::RuleContext *context, size_t actionIndex);

  // Individual semantic predicate functions triggered by sempred() above.
  bool PROGRAM_ENDSempred(antlr4::RuleContext *_localctx, size_t predicateIndex);
  bool PROGRAM_TEXTSempred(antlr4::RuleContext *_localctx, size_t predicateIndex);
  bool RAW_STRING_ENDSempred(antlr4::RuleContext *_localctx, size_t predicateIndex);
  bool RAW_STRING_TEXTSempred(antlr4::RuleContext *_localctx, size_t predicateIndex);

};
