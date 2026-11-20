#include <limits>
#include "utils/types.hpp"
#include "utils/archUtils.hpp"
#include "utils/winapicall.hpp"
#include "metabinary/binaryDescPatternMatch.hpp"

namespace {

using Op = BinaryDescPattern::Op;
using OpType = BinaryDescPattern::OpType;

// The one value with no negation, no magnitude and no positive counterpart,
// which is what the checked operators below keep having to single out.
constexpr i64 kIntMin = std::numeric_limits<i64>::min();

// Widest 64-bit pattern, as the literal scanner accumulates one.
constexpr u64 kMaxWord = 0xFFFFFFFFFFFFFFFFull;

constexpr u32 kNotHex = 0xFFFFFFFF;

// ----------------------------------------------------------------------------
// [SECTION] Tokens
// ----------------------------------------------------------------------------

enum TokenType: u08 {
  kToken_End = 0,
  kToken_Number,
  kToken_Base,     // "$@"
  kToken_InsnImm,  // "$<offset>:<immRef>"
  kToken_Deref,    // "#"
  kToken_Not,      // "~"
  kToken_Add,
  kToken_Sub,
  kToken_Mul,
  kToken_Div,
  kToken_Lsh,
  kToken_Rsh,
  kToken_And,
  kToken_Or,
  kToken_LParen,
  kToken_RParen,
};

struct Token {
  TokenType type;
  u32 pos;     // Source offset, reported on error.
  u32 offset;  // kToken_InsnImm.
  u08 immRef;  // kToken_InsnImm.
  u08 derefSize;  // kToken_Deref: bytes read, 1, 2, 4 or 8.
  i64 number;  // kToken_Number.
};

// ----------------------------------------------------------------------------
// [SECTION] Lexer
// ----------------------------------------------------------------------------

bool s_IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool s_IsDigit(char c) {
  return c >= '0' && c <= '9';
}

bool s_IsAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

u32 s_HexDigit(char c) {
  if (c >= '0' && c <= '9') return (u32)(c - '0');
  if (c >= 'a' && c <= 'f') return (u32)(c - 'a') + 10;
  if (c >= 'A' && c <= 'F') return (u32)(c - 'A') + 10;
  return kNotHex;
}

// Indexing a std::string at size() is defined and yields '\0', so the scanners
// below can read one past the last character without a length test.
bool s_IsHexPrefix(
  const TgcString &expr,
  u32 pos
) {
  return expr[pos] == '0'
    && (expr[pos + 1] == 'x' || expr[pos + 1] == 'X');
}

// Scans an unsigned integer, decimal or "0x"-prefixed, for the two fields of
// "$a:b". Capped at 32 bits: one indexes a byte sequence, the other an
// immediate list, and neither comes close.
bool s_ScanUInt(
  const TgcString &expr,
  u32 *pos,
  u32 *out
) {
  u32 i = *pos;
  u64 value = 0;
  u32 digits = 0;

  if (s_IsHexPrefix(expr, i)) {
    for (i += 2; i < expr.size(); ++i, ++digits) {
      const u32 digit = s_HexDigit(expr[i]);
      if (digit == kNotHex) break;
      value = value * 16 + digit;
      if (value > 0xFFFFFFFF) return false;
    }
  } else {
    for (; i < expr.size() && s_IsDigit(expr[i]); ++i, ++digits) {
      value = value * 10 + (u32)(expr[i] - '0');
      if (value > 0xFFFFFFFF) return false;
    }
  }

  if (!digits) return false;

  *pos = i;
  *out = (u32)value;
  return true;
}

// Why a literal was not scanned, so the two reasons can be told apart in the
// diagnostic: one is a typo, the other a value the domain has no room for.
enum ScanResult: u08 {
  kScan_Ok = 0,
  kScan_Malformed,  // "0x" with no digits behind it.
  kScan_TooLarge,   // More than 64 bits.
};

// Scans a numeric literal as the 64-bit pattern it spells, decimal and
// hexadecimal alike: a value from 2^63 up is the negative one carrying those
// bits, which is how a mask like "0xFFFFFFFFFFFFF000" is meant to read. Done
// here rather than with the CRT, whose strtoll() would also take a sign and
// whose strtod() would take C99 hex floats ("0x1p3"), making the accepted syntax
// depend on the library.
ScanResult s_ScanNumber(
  const TgcString &expr,
  u32 *pos,
  i64 *out
) {
  u32 i = *pos;
  u64 value = 0;
  u32 digits = 0;

  if (s_IsHexPrefix(expr, i)) {
    for (i += 2; i < expr.size(); ++i, ++digits) {
      const u32 digit = s_HexDigit(expr[i]);
      if (digit == kNotHex) break;
      if (value > (kMaxWord >> 4)) return kScan_TooLarge;
      value = (value << 4) | digit;
    }
  } else {
    for (; i < expr.size() && s_IsDigit(expr[i]); ++i, ++digits) {
      const u32 digit = (u32)(expr[i] - '0');
      // The same bound one decimal digit at a time: neither the multiply nor the
      // add may carry out of 64 bits.
      if (value > kMaxWord / 10) return kScan_TooLarge;
      value *= 10;
      if (value > kMaxWord - digit) return kScan_TooLarge;
      value += digit;
    }
  }

  if (!digits) return kScan_Malformed;

  *pos = i;
  *out = (i64)value;
  return kScan_Ok;
}

bool s_Fail(
  cstring *error,
  u32 *errorPos,
  cstring msg,
  u32 pos
) {
  *error = msg;
  *errorPos = pos;
  return false;
}

// Scans a punctuation token. The shifts are the only two-character operators,
// and a lone '<' or '>' is a typo rather than a comparison: the grammar has no
// relational operators. It has no logical ones either, so "&&" and "||" come out
// as two operators in a row and the parser rejects them.
bool s_ScanOperator(
  const TgcString &expr,
  u32 *pos,
  Token *out
) {
  const char c = expr[*pos];

  switch (c) {
    case '#': {
      out->type = kToken_Deref;
      out->derefSize = (u08)sizeof(i64);  // A bare '#' is "#x".

      // A letter here can only be a width: the grammar has no names. The
      // spellings are the debugger's, c/s/i/x for the signed 8-, 16-, 32- and
      // 64-bit field, and an unknown one is a typo rather than a bare '#'
      // followed by something else.
      const char width = expr[*pos + 1];

      if (s_IsAlpha(width)) {
        if (width == 'c') out->derefSize = (u08)sizeof(i08);
        else if (width == 's') out->derefSize = (u08)sizeof(i16);
        else if (width == 'i') out->derefSize = (u08)sizeof(i32);
        else if (width == 'x') out->derefSize = (u08)sizeof(i64);
        else return false;

        ++*pos;
      }

      break;
    }

    case '~': out->type = kToken_Not; break;
    case '+': out->type = kToken_Add; break;
    case '-': out->type = kToken_Sub; break;
    case '*': out->type = kToken_Mul; break;
    case '/': out->type = kToken_Div; break;
    case '(': out->type = kToken_LParen; break;
    case ')': out->type = kToken_RParen; break;
    case '&': out->type = kToken_And; break;
    case '|': out->type = kToken_Or; break;

    case '<':
    case '>':
      if (expr[*pos + 1] != c) return false;
      out->type = c == '<' ? kToken_Lsh : kToken_Rsh;
      ++*pos;
      break;

    default:
      return false;
  }

  ++*pos;
  return true;
}

// Splits `expression` into tokens, always terminated by kToken_End so the parser
// can look ahead unconditionally. Returns false on a lexical error, with `error`
// and `errorPos` set; `out` is then meaningless.
bool s_Tokenize(
  const TgcString &expression,
  std::vector<Token> *out,
  cstring *error,
  u32 *errorPos
) {
  u32 i = 0;

  while (i < expression.size()) {
    if (s_IsSpace(expression[i])) {
      ++i;
      continue;
    }

    Token token = {};
    token.pos = i;

    const char c = expression[i];

    if (s_IsDigit(c)) {
      token.type = kToken_Number;

      const ScanResult scan = s_ScanNumber(expression, &i, &token.number);

      if (scan == kScan_TooLarge)
        return s_Fail(error, errorPos, "number does not fit 64 bits", token.pos);

      if (scan != kScan_Ok)
        return s_Fail(error, errorPos, "malformed number", token.pos);

      // Every operator here works on integers, so a fraction is a mistake to
      // point at rather than a value to truncate somewhere further along.
      if (expression[i] == '.')
        return s_Fail(error, errorPos, "number must be an integer", i);
    } else if (c == '$') {
      ++i;

      if (expression[i] == '@') {
        token.type = kToken_Base;
        ++i;
      } else {
        u32 immRef = 0;

        token.type = kToken_InsnImm;

        if (!s_ScanUInt(expression, &i, &token.offset))
          return s_Fail(error, errorPos, "expected a byte offset after '$'", i);

        if (expression[i] != ':')
          return s_Fail(error, errorPos, "expected ':' after the byte offset", i);
        ++i;

        if (!s_ScanUInt(expression, &i, &immRef))
          return s_Fail(error, errorPos, "expected an immediate index after ':'", i);

        // An instruction never carries more, so this can only be a typo.
        if (immRef >= ArchX64::kMaxImms)
          return s_Fail(error, errorPos, "immediate index out of range", token.pos);

        token.immRef = (u08)immRef;
      }
    } else if (!s_ScanOperator(expression, &i, &token)) {
      return s_Fail(error, errorPos,
        c == '<' ? "expected '<<'" :
        c == '>' ? "expected '>>'" :
        c == '#' ? "unknown dereference width" : "unexpected character", token.pos);
    }

    out->push_back(token);
  }

  Token end = {};
  end.type = kToken_End;
  end.pos = (u32)expression.size();
  out->push_back(end);

  return true;
}

// ----------------------------------------------------------------------------
// [SECTION] Parser
// ----------------------------------------------------------------------------

// Recursive descent, one function per precedence level, emitting postfix as it
// goes: an operator is appended after its operands have been, which is exactly
// the order Solve() wants.
struct ParseCtx {
  const std::vector<Token> *tokens;
  std::vector<Op> *out;

  u32 index;
  // Counts only the levels that recurse -- parentheses and unary chains. The
  // binary levels loop instead, so "1+1+1+..." costs no stack.
  u32 depth;

  cstring error;
  u32 errorPos;
};

const Token &s_Peek(
  ParseCtx *ctx
) {
  return (*ctx->tokens)[ctx->index];
}

void s_Advance(
  ParseCtx *ctx
) {
  if (s_Peek(ctx).type != kToken_End)
    ++ctx->index;
}

// Keeps the first error: it is the one pointing at the actual mistake, later
// ones are the fallout of unwinding.
bool s_ParseError(
  ParseCtx *ctx,
  cstring msg
) {
  if (!ctx->error) {
    ctx->error = msg;
    ctx->errorPos = s_Peek(ctx).pos;
  }
  return false;
}

// Returns the appended op so the operand cases can fill in their payload.
Op &s_Emit(
  ParseCtx *ctx,
  OpType type
) {
  Op op = {};
  op.type = type;
  ctx->out->push_back(op);
  return ctx->out->back();
}

bool s_ParseOr(ParseCtx *ctx);

// atom := number | '$@' | '$' <offset> ':' <index> | '(' expr ')'
bool s_ParseAtom(
  ParseCtx *ctx
) {
  const Token &token = s_Peek(ctx);

  switch (token.type) {
    case kToken_Number:
      s_Emit(ctx, BinaryDescPattern::kOpType_Imm).inlineNum = token.number;
      s_Advance(ctx);
      return true;

    case kToken_Base:
      s_Emit(ctx, BinaryDescPattern::kOpType_Base);
      s_Advance(ctx);
      return true;

    case kToken_InsnImm: {
      Op &op = s_Emit(ctx, BinaryDescPattern::kOpType_InsnImm);
      op.offset = token.offset;
      op.immRef = token.immRef;
      s_Advance(ctx);
      return true;
    }

    case kToken_LParen: {
      s_Advance(ctx);

      if (++ctx->depth > BinaryDescPattern::kMaxDepth)
        return s_ParseError(ctx, "expression nests too deeply");

      if (!s_ParseOr(ctx))
        return false;

      --ctx->depth;

      if (s_Peek(ctx).type != kToken_RParen)
        return s_ParseError(ctx, "expected ')'");

      s_Advance(ctx);
      return true;
    }

    default:
      return s_ParseError(ctx, "expected a value");
  }
}

// unary := (deref | '-' | '+' | '~') unary | atom
bool s_ParseUnary(
  ParseCtx *ctx
) {
  const Token &token = s_Peek(ctx);
  const TokenType type = token.type;
  const u08 derefSize = token.derefSize;

  if (type != kToken_Deref && type != kToken_Not
    && type != kToken_Sub && type != kToken_Add)
    return s_ParseAtom(ctx);

  s_Advance(ctx);

  if (++ctx->depth > BinaryDescPattern::kMaxDepth)
    return s_ParseError(ctx, "expression nests too deeply");

  if (!s_ParseUnary(ctx))
    return false;

  --ctx->depth;

  if (type == kToken_Deref)
    s_Emit(ctx, BinaryDescPattern::kOpType_Deref).derefSize = derefSize;
  else if (type == kToken_Not)
    s_Emit(ctx, BinaryDescPattern::kOpType_Not);
  else if (type == kToken_Sub)
    s_Emit(ctx, BinaryDescPattern::kOpType_Neg);

  return true;
}

// product := unary (('*' | '/') unary)*
bool s_ParseProduct(
  ParseCtx *ctx
) {
  if (!s_ParseUnary(ctx))
    return false;

  for (;;) {
    const TokenType type = s_Peek(ctx).type;

    if (type != kToken_Mul && type != kToken_Div)
      return true;

    s_Advance(ctx);

    if (!s_ParseUnary(ctx))
      return false;

    s_Emit(ctx, type == kToken_Mul
      ? BinaryDescPattern::kOpType_Mul
      : BinaryDescPattern::kOpType_Div);
  }
}

// sum := product (('+' | '-') product)*
bool s_ParseSum(
  ParseCtx *ctx
) {
  if (!s_ParseProduct(ctx))
    return false;

  for (;;) {
    const TokenType type = s_Peek(ctx).type;

    if (type != kToken_Add && type != kToken_Sub)
      return true;

    s_Advance(ctx);

    if (!s_ParseProduct(ctx))
      return false;

    s_Emit(ctx, type == kToken_Add
      ? BinaryDescPattern::kOpType_Add
      : BinaryDescPattern::kOpType_Sub);
  }
}

// shift := sum (('<<' | '>>') sum)*
bool s_ParseShift(
  ParseCtx *ctx
) {
  if (!s_ParseSum(ctx))
    return false;

  for (;;) {
    const TokenType type = s_Peek(ctx).type;

    if (type != kToken_Lsh && type != kToken_Rsh)
      return true;

    s_Advance(ctx);

    if (!s_ParseSum(ctx))
      return false;

    s_Emit(ctx, type == kToken_Lsh
      ? BinaryDescPattern::kOpType_Lsh
      : BinaryDescPattern::kOpType_Rsh);
  }
}

// and := shift ('&' shift)*
bool s_ParseAnd(
  ParseCtx *ctx
) {
  if (!s_ParseShift(ctx))
    return false;

  while (s_Peek(ctx).type == kToken_And) {
    s_Advance(ctx);

    if (!s_ParseShift(ctx))
      return false;

    s_Emit(ctx, BinaryDescPattern::kOpType_And);
  }

  return true;
}

// or := and ('|' and)*
bool s_ParseOr(
  ParseCtx *ctx
) {
  if (!s_ParseAnd(ctx))
    return false;

  while (s_Peek(ctx).type == kToken_Or) {
    s_Advance(ctx);

    if (!s_ParseAnd(ctx))
      return false;

    s_Emit(ctx, BinaryDescPattern::kOpType_Or);
  }

  return true;
}

bool s_Parse(
  const std::vector<Token> &tokens,
  std::vector<Op> *out,
  cstring *error,
  u32 *errorPos
) {
  ParseCtx ctx = {};
  ctx.tokens = &tokens;
  ctx.out = out;

  if (s_ParseOr(&ctx) && s_Peek(&ctx).type != kToken_End) {
    // A stray ')', or two values in a row: the expression parsed, but not all of
    // it, which is a mistake rather than a shorter expression.
    s_ParseError(&ctx, "unexpected token after the expression");
  }

  if (ctx.error)
    return s_Fail(error, errorPos, ctx.error, ctx.errorPos);

  return true;
}

// Walks the postfix list to confirm it is well formed and to size the value
// stack Solve() needs. A malformed list means a parser bug, but the check is one
// cheap pass and it lets Solve() run without stack guards.
bool s_MeasureStack(
  const std::vector<Op> &ops,
  u32 *depth
) {
  u32 current = 0;
  u32 highest = 0;

  for (const Op &op : ops) {
    switch (op.type) {
      case BinaryDescPattern::kOpType_Imm:
      case BinaryDescPattern::kOpType_Base:
      case BinaryDescPattern::kOpType_InsnImm:
        if (++current > highest) highest = current;
        break;

      case BinaryDescPattern::kOpType_Deref:
      case BinaryDescPattern::kOpType_Neg:
      case BinaryDescPattern::kOpType_Not:
        if (current < 1) return false;
        break;

      case BinaryDescPattern::kOpType_Add:
      case BinaryDescPattern::kOpType_Sub:
      case BinaryDescPattern::kOpType_Mul:
      case BinaryDescPattern::kOpType_Div:
      case BinaryDescPattern::kOpType_Lsh:
      case BinaryDescPattern::kOpType_Rsh:
      case BinaryDescPattern::kOpType_And:
      case BinaryDescPattern::kOpType_Or:
        if (current < 2) return false;
        --current;
        break;

      default:
        return false;
    }
  }

  if (current != 1) return false;

  *depth = highest;
  return true;
}

// ----------------------------------------------------------------------------
// [SECTION] Evaluation
// ----------------------------------------------------------------------------

// No instruction decoded yet. A real offset is bounded by the sequence length,
// so it can never collide.
constexpr u32 kNoInsn = 0xFFFFFFFF;

struct SolveState {
  const u08 *inst;
  uintptr_t address;
  u32 count;

  // One-entry memo. Expressions routinely pull two immediates out of the same
  // instruction ("$0:0 + $0:1"), and the decode is the expensive half.
  u32 cachedOffset;
  ArchX64::Insn cached;
};

// "$a:b": decodes the instruction at byte offset a and hands back immediate b,
// as the decoder reported it -- sign-extended when the field is signed,
// zero-extended when it is not.
bool s_SolveInsnImm(
  SolveState *state,
  const Op &op,
  i64 *out
) {
  if (op.offset >= state->count)
    return false;

  if (state->cachedOffset != op.offset) {
    ArchX64::Insn insn = {};

    // Decoded out of the local bytes, addressed as if it were executing at its
    // place in the target, so PC-relative operands resolve to target addresses.
    const u32 length = ArchX64::Decode(
      state->inst + op.offset,
      state->address + op.offset,
      &insn);

    // A zero length means the bytes do not decode; a length running past the end
    // means the sequence was cut short of a whole instruction, so the immediate
    // it reports would be assembled from bytes nobody matched.
    if (!length || op.offset + length > state->count)
      return false;

    state->cached = insn;
    state->cachedOffset = op.offset;
  }

  if (op.immRef >= state->cached.immCount)
    return false;

  *out = state->cached.imms[op.immRef].value;
  return true;
}

// "#x" and the narrower "#c", "#s", "#i": reads `size` bytes at x as a signed
// field. The read goes through OsApi (ReadProcessMemory on Win64), which reports
// unmapped pages instead of raising, so following a chain through a binary that
// does not match this descriptor fails the expression rather than the process.
bool s_Deref(
  i64 value,
  u08 size,
  i64 *out
) {
  // A negative address is one the expression drifted into rather than one worth
  // reading: everything a descriptor follows lives in the low half of the
  // address space, and the reader would turn a kernel address down anyway.
  if (value < 0)
    return false;

  // The four widths the parser records, and the four that fit the buffer.
  if (size != sizeof(i08) && size != sizeof(i16)
    && size != sizeof(i32) && size != sizeof(i64))
    return false;

  u08 bytes[sizeof(i64)] = {};

  if (!Win64SysCall::ReadMemory((uintptr_t)value, bytes, size))
    return false;

  // Assembled little-endian, which every target the framework supports is, and
  // sign-extended, the field being signed: "#i" over a 32-bit -0x10 solves to
  // -0x10 rather than to 0xFFFFFFF0.
  u64 raw = 0;

  for (u32 i = 0; i < size; ++i)
    raw |= (u64)bytes[i] << (i * 8);

  switch (size) {
    case sizeof(i08): *out = (i08)raw; break;
    case sizeof(i16): *out = (i16)raw; break;
    case sizeof(i32): *out = (i32)raw; break;
    default: *out = (i64)raw; break;
  }

  return true;
}

// The checked arithmetic. i64 spans every address, displacement and immediate a
// descriptor computes with, so a step that leaves the range is a broken
// expression rather than a value worth wrapping around: it fails the way an
// unreadable address does. Overflow is detected on the unsigned pattern, signed
// overflow itself being undefined rather than observable.
bool s_Add(
  i64 lhs,
  i64 rhs,
  i64 *out
) {
  const i64 result = (i64)((u64)lhs + (u64)rhs);

  // Overflow exactly when the operands agree on a sign the result does not.
  if (((lhs ^ result) & (rhs ^ result)) < 0)
    return false;

  *out = result;
  return true;
}

bool s_Sub(
  i64 lhs,
  i64 rhs,
  i64 *out
) {
  const i64 result = (i64)((u64)lhs - (u64)rhs);

  // Overflow exactly when the operands differ in sign and the result takes the
  // subtrahend's.
  if (((lhs ^ rhs) & (lhs ^ result)) < 0)
    return false;

  *out = result;
  return true;
}

bool s_Mul(
  i64 lhs,
  i64 rhs,
  i64 *out
) {
  if (!lhs || !rhs) {
    *out = 0;
    return true;
  }

  // -1 against the lowest value is the one product whose magnitude has no
  // representation, and the one case the division below could not itself test.
  if ((lhs == -1 && rhs == kIntMin) || (rhs == -1 && lhs == kIntMin))
    return false;

  const i64 result = (i64)((u64)lhs * (u64)rhs);

  // Exact exactly when dividing one operand back out recovers the other.
  if (result / lhs != rhs)
    return false;

  *out = result;
  return true;
}

bool s_Div(
  i64 lhs,
  i64 rhs,
  i64 *out
) {
  // Division by zero, and the one quotient with no representation. Failing here
  // rather than at whatever would have read the result keeps the reported cause
  // where the mistake is.
  if (!rhs || (lhs == kIntMin && rhs == -1))
    return false;

  // Truncated toward zero, as C has it: "-3 / 2" is -1.
  *out = lhs / rhs;
  return true;
}

bool s_Neg(
  i64 value,
  i64 *out
) {
  if (value == kIntMin)
    return false;

  *out = -value;
  return true;
}

// Shifts take a count of 0..63; anything else is a mistake rather than a result
// the platform gets to pick.
bool s_Shift(
  i64 lhs,
  i64 rhs,
  bool right,
  i64 *out
) {
  if (rhs < 0 || rhs > 63)
    return false;

  const u32 count = (u32)rhs;

  // Shifting left is done on the unsigned pattern: the bits pushed out are meant
  // to go, and signed overflow would be undefined rather than discarded.
  // Shifting right is arithmetic, as it is in C on a signed operand, so a
  // negative displacement scaled down stays negative.
  *out = right ? (lhs >> count) : (i64)((u64)lhs << count);
  return true;
}

} // namespace

// ----------------------------------------------------------------------------
// [SECTION] BinaryDescPattern
// ----------------------------------------------------------------------------

void BinaryDescPattern::Initialize(
  cstring expr
) {
  m_ops.clear();
  m_ops.shrink_to_fit();

  m_error = nullptr;
  m_errorPos = 0;
  m_valid = false;

  if (!expr) {
    m_error = "null expression";
    return;
  }

  const TgcString expression(expr);

  std::vector<Token> tokens;
  if (!s_Tokenize(expression, &tokens, &m_error, &m_errorPos))
    return;

  if (!s_Parse(tokens, &m_ops, &m_error, &m_errorPos)) {
    m_ops.clear();
    return;
  }

  u32 depth = 0;
  if (!s_MeasureStack(m_ops, &depth) || depth > kMaxStack) {
    m_ops.clear();
    m_error = "expression needs too many temporaries";
    return;
  }

  m_valid = true;
}

void BinaryDescPattern::Terminate() {
  m_ops.clear();
  m_ops.shrink_to_fit();

  m_error = nullptr;
  m_errorPos = 0;
  m_valid = false;
}

bool BinaryDescPattern::Solve(
  const u08 *inst,
  uintptr_t address,
  u32 count,
  i64 *out
) const {
  if (!m_valid || !inst || !out)
    return false;

  SolveState state = {};
  state.inst = inst;
  state.address = address;
  state.count = count;
  state.cachedOffset = kNoInsn;

  // Initialize() proved the postfix list is balanced and fits, so nothing below
  // needs to test the stack bounds.
  i64 stack[kMaxStack];
  u32 top = 0;

  // A step with no result to produce ends the evaluation there: the domain has no
  // failure value to carry along, and nothing further up the expression could do
  // anything with one.
  for (const Op &op : m_ops) {
    switch (op.type) {
      case kOpType_Imm:
        stack[top++] = op.inlineNum;
        break;

      case kOpType_Base:
        stack[top++] = (i64)address;
        break;

      case kOpType_InsnImm:
        if (!s_SolveInsnImm(&state, op, &stack[top]))
          return false;
        ++top;
        break;

      case kOpType_Deref: {
        i64 &value = stack[top - 1];

        if (!s_Deref(value, op.derefSize, &value))
          return false;
        break;
      }

      case kOpType_Neg: {
        i64 &value = stack[top - 1];

        if (!s_Neg(value, &value))
          return false;
        break;
      }

      // Every bit flipped, which on a two's-complement integer is -x - 1, and
      // which nothing can overflow out of.
      case kOpType_Not:
        stack[top - 1] = ~stack[top - 1];
        break;

      default: {
        const i64 rhs = stack[--top];
        i64 &lhs = stack[top - 1];

        switch (op.type) {
          case kOpType_Add:
            if (!s_Add(lhs, rhs, &lhs)) return false;
            break;

          case kOpType_Sub:
            if (!s_Sub(lhs, rhs, &lhs)) return false;
            break;

          case kOpType_Mul:
            if (!s_Mul(lhs, rhs, &lhs)) return false;
            break;

          case kOpType_Div:
            if (!s_Div(lhs, rhs, &lhs)) return false;
            break;

          case kOpType_Lsh:
            if (!s_Shift(lhs, rhs, false, &lhs)) return false;
            break;

          case kOpType_Rsh:
            if (!s_Shift(lhs, rhs, true, &lhs)) return false;
            break;

          // The two patterns combined, which is where the signs come from:
          // "-1 & 0xFF" is 0xFF, "x | -1" is -1, and neither can overflow.
          case kOpType_And: lhs &= rhs; break;
          case kOpType_Or: lhs |= rhs; break;

          default: return false;
        }
        break;
      }
    }
  }

  *out = stack[0];
  return true;
}
