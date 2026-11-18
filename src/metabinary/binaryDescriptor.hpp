#ifndef __API_MOD_ASSEMBLY_BINARYDESCRIPTOR_HH__
#define __API_MOD_ASSEMBLY_BINARYDESCRIPTOR_HH__

#include <map>
#include "utils/types.hpp"
#include "metabinary/binaryDescPatternMatch.hpp"

// ----------------------------------------------------------------------------
// [SECTION] BinaryDescriptor
// ----------------------------------------------------------------------------

class BinaryDescriptor {
public:
  // "No RVA given". A descriptor carrying this has to find its address through
  // the signature alone.
  static constexpr u64 kInvalidRva = 0xFFFFFFFFFFFFFFFFull;

  struct InitTypedef {
    cstring module = nullptr;
    u64 rva = kInvalidRva;
    cstring pattern = nullptr;
    cstring mask = nullptr;
    cstring match = nullptr;
    // Virtual function descriptors do not resolve to an address. They carry
    // the vtable slot index instead; MetaBinaryVirtualFunction::Initialize()
    // consumes GetResult() as that index.
    i32 slot = -1;
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

  void Initialize(cstring name, const InitTypedef &init);
  void Terminate();

  // NOTE: This function is multi threaded. See s_EvaluateObject() for the
  // contract it comes with.
  void Evaluate();

private:
  // Name of the descriptor.
  TgcString m_name = "";
  // Module handle returned by OsApi::GetDlModule().
  uintptr_t m_module = 0;
  // RVA inside the module, or kInvalidRva.
  u64 m_rva = kInvalidRva;
  // Vtable slot index for virtual function descriptors, or -1.
  i32 m_slot = -1;
  // Byte sequence.
  const u08 *m_pattern = nullptr;
  const u08 *m_mask = nullptr;
  u32 m_patternLen = 0;
  // Pattern expression.
  BinaryDescPattern m_match = {};
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

  void Initialize(cstring json);
  void Terminate();

  const BinaryDescriptor *GetDescriptorByName(cstring name);

private:
  std::map<TgcString, BinaryDescriptor> m_descriptors = {};
};

#endif
