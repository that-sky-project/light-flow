#ifndef __API_MOD_ASSEMBLY_BINARYDESCPATTERNMATCH_HPP__
#define __API_MOD_ASSEMBLY_BINARYDESCPATTERNMATCH_HPP__

#include <vector>
#include "utils/types.hpp"
#include "platform/lowlevel.hpp"

// Simulated addressing over a matched instruction sequence.
//
// A descriptor expression recovers the address a piece of code would have
// computed, without executing it: the operands come out of the disassembler
// (GetArch()->Decode()) and the pointer chases go through the OS memory reader
// (ReadMemory()). That lets a binary scan land on the object an
// instruction refers to rather than on the instruction itself. On x86-64,
//
//   48 8B 05 xx xx xx xx    mov rax, [rip + disp32]
//
// loads a global living at `end_of_insn + disp32`, i.e. "$@ + 7 + $0:0", and
// the object that global points at is "#($@ + 7 + $0:0)".
//
// Grammar, loosest binding first (C precedence):
//
//   expr    := or
//   or      := and ('|' and)*
//   and     := shift ('&' shift)*
//   shift   := sum (('<<' | '>>') sum)*
//   sum     := product (('+' | '-') product)*
//   product := unary (('*' | '/') unary)*
//   unary   := (deref | '-' | '+' | '~') unary | atom
//   deref   := '#' ('c' | 's' | 'i' | 'x')?
//   atom    := number | '$@' | '$' <offset> ':' <index> | '(' expr ')'
//
// Terminals:
//
//   number  decimal ("12") or hexadecimal ("0x1F"), taken as the 64-bit pattern
//           it spells: "0xFFFFFFFFFFFFFFFF" is -1. A fraction is not a value
//   $@      address the matched sequence starts at
//   $a:b    immediate b (0-based, in Arch::Insn::imms order) of the instruction
//           decoded at byte offset a of the sequence
//   #x      dereference: reads the signed 64-bit word at x. "#c", "#s" and "#i"
//           read the signed 8-, 16- and 32-bit field instead, sign-extended, and
//           a bare "#" is "#x"
//   ~x      bitwise complement of x
//
// Everything is evaluated as a signed 64-bit integer, the width of the
// addresses, displacements and immediates a descriptor works with: an operand
// goes in as the decoder reported it and a result comes back out unrounded. The
// operators are C's over two's complement -- "/" truncates toward zero, "&", "|"
// and "~" work on the bit pattern, so "x & ~0xFFF" truncates x to a page
// boundary, "<<" drops the bits it pushes out and ">>" keeps a negative value
// negative. A shift count outside 0..63 has no result rather than an
// implementation-defined one.
//
// Failure is reported out of band, through Solve()'s return value rather than
// through the result: an undecodable instruction, an immediate the instruction
// does not carry, an unreadable address, a division by zero and an arithmetic
// overflow each end the evaluation. A descriptor that does not fit the binary at
// hand is a false return, not a crash and not a plausible-looking address.
class BinaryDescPattern {
public:
  // Compiled form: postfix (reverse Polish), so Solve() is one left-to-right
  // pass over a small value stack with no parse state and no recursion.
  enum OpType: u08 {
    kOpType_None = 0,

    // Operands. Push one value.
    kOpType_Imm,      // Literal, in inlineNum.
    kOpType_Base,     // "$@".
    kOpType_InsnImm,  // "$<offset>:<immRef>".

    // Unary. Replace the top of the stack.
    kOpType_Deref,    // "#", at the width in derefSize.
    kOpType_Neg,      // Unary "-".
    kOpType_Not,      // "~".

    // Binary. Pop two, push one.
    kOpType_Add,
    kOpType_Sub,
    kOpType_Mul,
    kOpType_Div,
    kOpType_Lsh,
    kOpType_Rsh,
    kOpType_And,
    kOpType_Or,
  };

  struct Op {
    OpType type;

    u08 immRef;     // kOpType_InsnImm: immediate index.
    u08 derefSize;  // kOpType_Deref: bytes read, 1, 2, 4 or 8.
    u32 offset;     // kOpType_InsnImm: byte offset into the sequence.

    i64 inlineNum;  // kOpType_Imm: literal value.
  };

  // Nesting cap on parentheses and unary chains, which bounds parser recursion,
  // and the matching cap on Solve()'s value stack. Initialize() rejects any
  // expression that would exceed either, so Solve() needs no bounds checks.
  static constexpr u32 kMaxDepth = 24;
  static constexpr u32 kMaxStack = 32;

  BinaryDescPattern() = default;
  ~BinaryDescPattern() = default;

  // Compiles `expr`. On a syntax error the pattern is left empty and invalid,
  // with GetError()/GetErrorPos() describing the failure; re-initializing an
  // already initialized pattern replaces it.
  void Initialize(cstring expr);
  void Terminate();

  inline bool IsValid() const { return m_valid; }

  // Static (compile-time) diagnostics. Null error once Initialize() succeeded.
  inline cstring GetError() const { return m_error; }
  inline u32 GetErrorPos() const { return m_errorPos; }

  // Evaluates the expression against the byte sequence at `inst`, `count` bytes
  // long, as it would be laid out at `address` ("$@"), and stores the result in
  // `*out`. Instructions are decoded out of `inst` while PC-relative operands
  // resolve against `address`, so a sequence copied out of the target still
  // yields target addresses.
  //
  // `inst` is passed straight to the decoder, which reads a whole encoding at a
  // time: it must stay readable for GetArch()->GetMaxInsnLength() bytes past
  // the last instruction referenced by a "$a:b" term. A pointer into a mapped
  // image satisfies that; a tightly sized heap copy does not.
  //
  // Returns false when the expression cannot be evaluated, leaving `*out`
  // untouched.
  bool Solve(const u08 *inst, uintptr_t address, u32 count, i64 *out) const;

private:
  std::vector<BinaryDescPattern::Op> m_ops = {};

  cstring m_error = nullptr;
  u32 m_errorPos = 0;
  bool m_valid = false;
};

#endif
