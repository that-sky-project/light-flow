#ifndef __API_MOD_ASSEMBLY_BINARYDESCRIPTOR_HPP__
#define __API_MOD_ASSEMBLY_BINARYDESCRIPTOR_HPP__

#include <map>
#include <vector>
#include "utils/types.hpp"
#include "metabinary/binaryDescPatternMatch.hpp"

// ----------------------------------------------------------------------------
// [SECTION] BinaryDescriptor
// ----------------------------------------------------------------------------

class BinaryDescriptor {
public:
  // Descriptor type. JSON carries it in "desc_type"; a descriptor without a
  // valid type is rejected.
  enum class Kind: u08 {
    kInvalid = 0,
    // Plain (non-virtual) function: the evaluated value is an address.
    kFunction,
    // Virtual function: the evaluated value is a vtable slot index.
    kVirtualFunction,
    // Global variable: the evaluated value is an address.
    kVariable,
    // Member variable: the evaluated value is an offset from the object.
    kMemberVariable,
    // Constant: the evaluated value is the literal value.
    kConstant,
  };

  // One candidate signature: a byte pattern with an optional bit mask and the
  // expression that turns a match into the descriptor's result.
  struct SignatureInit {
    cstring pattern = nullptr;
    cstring mask = nullptr;
    cstring match = nullptr;
  };

  struct InitTypedef {
    cstring module = nullptr;
    Kind kind = Kind::kInvalid;
    // Value used when no signature resolves. Its meaning depends on `kind`:
    //   kFunction/kVariable  module-relative address (module base + value)
    //   kVirtualFunction     vtable slot index
    //   kMemberVariable      member offset
    //   kConstant            literal value
    i64 fallback = 0;
    bool hasFallback = false;
    // Signatures are tried top to bottom. The first one that both scans and
    // evaluates successfully wins; the others are ignored.
    std::vector<SignatureInit> signatures = {};
  };

public:
  // NOTE: This function is multi threaded. It carries no synchronization of its
  // own, so two threads may evaluate two different descriptors concurrently but
  // never the same one, and neither may run alongside Initialize()/Terminate().
  static void s_EvaluateObject(BinaryDescriptor &desc);

  BinaryDescriptor() = default;
  ~BinaryDescriptor() { Terminate(); }

  inline bool IsValid() const { return m_initialized && m_evaluated; }
  inline i64 GetResult() const { return m_result; }
  inline Kind GetKind() const { return m_kind; }

  void Initialize(cstring name, const InitTypedef &init);
  void Terminate();

  // NOTE: This function is multi threaded. See s_EvaluateObject() for the
  // contract it comes with.
  void Evaluate();

private:
  // A compiled candidate signature.
  struct Signature {
    std::vector<u08> pattern = {};
    std::vector<u08> mask = {};
    BinaryDescPattern match = {};
    bool valid = false;
  };

  // Name of the descriptor.
  TgcString m_name = "";
  // Module handle returned by OsApi::GetDlModule().
  uintptr_t m_module = 0;
  // Interpretation of the result and of the fallback value.
  Kind m_kind = Kind::kInvalid;
  // Fallback value, valid when m_hasFallback is set.
  i64 m_fallback = 0;
  bool m_hasFallback = false;
  // Candidate signatures, in declaration order.
  std::vector<Signature> m_signatures = {};
  // Evaluated result.
  i64 m_result = 0;
  // Flags.
  bool m_initialized = false;
  bool m_evaluated = false;
};

// ----------------------------------------------------------------------------
// [SECTION] BinaryDescriptorBarn
// ----------------------------------------------------------------------------

class BinaryDescriptorBarn {
public:
  BinaryDescriptorBarn() = default;
  ~BinaryDescriptorBarn() = default;

  // `profile` selects a top-level object of the JSON document, e.g. "tgc" or
  // "netease". Each profile owns an "x64" descriptor list. When the profile is
  // null, missing or has no matching arch list, the root object is used so a
  // single-profile descriptor file keeps working.
  void Initialize(cstring json, cstring profile = nullptr);
  void Terminate();

  const BinaryDescriptor *GetDescriptorByName(cstring name);

private:
  std::map<TgcString, BinaryDescriptor> m_descriptors = {};
};

#endif
