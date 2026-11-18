#include <stdlib.h>
#include <rapidjson/document.h>
#include "utils/assert.hpp"
#include "utils/logger.hpp"
#include "metabinary/platform/lowLevel.hpp"
#include "metabinary/binaryDescriptor.hpp"

// Windows.h (pulled in by htmodloader.h) defines GetObject, which collides
// with rapidjson::Value::GetObject().
#ifdef GetObject
#undef GetObject
#endif

// ----------------------------------------------------------------------------
// [SECTION] Helpers
// ----------------------------------------------------------------------------

// Reads one hex byte (1 or 2 hex digits) at `*cursor`, writes the value to
// `*out` and advances `*cursor` past it. Returns false when no hex digit is
// present at the cursor.
static bool s_ReadHexByte(const char **cursor, u08 *out) {
  const char *p = *cursor;
  u32 v = 0;
  u32 digits = 0;
  while (digits < 2) {
    u32 d;
    char c = p[digits];
    if (c >= '0' && c <= '9')
      d = (u32)(c - '0');
    else if (c >= 'a' && c <= 'f')
      d = (u32)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
      d = (u32)(c - 'A' + 10);
    else
      break;
    v = (v << 4) | d;
    digits++;
  }
  if (digits == 0)
    return false;
  *out = (u08)v;
  *cursor = p + digits;
  return true;
}

// Counts the bytes spelled by `sig`, skipping runs of spaces/tabs between them.
// A '?' (or '??') is a wildcard byte, admitted only when `allowWildcard` is
// set. Returns false when the signature is malformed.
static bool s_CountSignature(cstring sig, bool allowWildcard, u64 *count) {
  const char *p = sig;
  u64 n = 0;
  while (*p) {
    while (*p == ' ' || *p == '\t')
      p++;
    if (!*p)
      break;
    if (*p == '?') {
      if (!allowWildcard)
        return false;
      p++;
      if (*p == '?')
        p++;
      n++;
    } else {
      u08 b;
      if (!s_ReadHexByte(&p, &b))
        return false;
      n++;
    }
  }
  *count = n;
  return true;
}

// Builds the byte/mask sequences handed to ScanPattern() from a descriptor's
// `pattern`/`mask` strings.
//
//   - Byte wildcard mode (mask == nullptr): "48 8D ? E8 ? ? ?". Each hex pair
//     becomes a 0xFF mask byte; each '?' (one or two, bytes separated by any
//     number of spaces) becomes a 0x00 mask byte over a 0x00 pattern byte.
//   - Bit-mask mode (mask != nullptr): both strings spell hex bytes; each mask
//     byte is the bitwise keep-mask for the matching pattern byte. '?' is not
//     allowed here.
//
// Both sequences are `*outLength` bytes long, the length ScanPattern() takes.
// The caller owns the two arrays and releases them with delete[].
static void s_BuildPattern(
  const u08 **outPattern,
  const u08 **outMask,
  u32 *outLength,
  cstring pattern,
  cstring mask
) {
  // A descriptor with no signature has nothing to build.
  if (!pattern) {
    *outPattern = nullptr;
    *outMask = nullptr;
    *outLength = 0;
    return;
  }

  const bool isByteWildcard = (mask == nullptr);

  u64 patternLen = 0;
  if (!s_CountSignature(pattern, isByteWildcard, &patternLen))
    ASSERTMSG(false, "Invalid pattern [%s]", pattern);

  if (patternLen == 0) {
    *outPattern = nullptr;
    *outMask = nullptr;
    *outLength = 0;
    return;
  }

  // ScanPattern() takes the length as a u32.
  ASSERTMSG(patternLen <= 0xFFFFFFFFull, "Signature is too long");

  u64 maskLen = 0;
  if (!isByteWildcard) {
    if (!s_CountSignature(mask, false, &maskLen))
      ASSERTMSG(false, "Invalid pattern [%s]", pattern);
    ASSERTMSG(maskLen == patternLen, "Signature pattern/mask length mismatch");
  } else {
    maskLen = patternLen;
  }

  u08 *bytes = new u08[patternLen];
  u08 *masks = new u08[patternLen];

  const char *p = pattern;
  const char *m = mask;
  u64 i = 0;
  while (*p) {
    while (*p == ' ' || *p == '\t')
      p++;
    if (!*p)
      break;

    if (*p == '?') {
      p++;
      if (*p == '?')
        p++;
      bytes[i] = 0x00;
      masks[i] = 0x00;
    } else {
      u08 b;
      s_ReadHexByte(&p, &b);
      bytes[i] = b;
      if (isByteWildcard) {
        masks[i] = 0xFF;
      } else {
        while (*m == ' ' || *m == '\t')
          m++;
        u08 mk;
        s_ReadHexByte(&m, &mk);
        masks[i] = mk;
      }
    }
    i++;
  }

  *outPattern = bytes;
  *outMask = masks;
  *outLength = (u32)patternLen;
}

// ----------------------------------------------------------------------------
// [SECTION] BinaryDescriptor
// ----------------------------------------------------------------------------

void BinaryDescriptor::s_EvaluateObject(
  BinaryDescriptor &desc
) {
  desc.Evaluate();
}

void BinaryDescriptor::Initialize(
  cstring name,
  const InitTypedef &init
) {
  ASSERT(!m_initialized);

  m_name = name;

  m_module = GetDlModule(init.module);
  m_rva = init.rva;
  m_slot = init.slot;

  // Compile the signature into the byte/mask sequences ScanPattern() takes.
  // Leaves all three empty when the descriptor carries no signature.
  s_BuildPattern(&m_pattern, &m_mask, &m_patternLen, init.pattern, init.mask);

  // Use the address of the signature by default.
  m_match.Initialize(init.match ? init.match : "$@");
  ASSERTMSG(m_match.IsValid(),
    "Invalid pattern expression [%s] in [%s]",
    init.match,
    m_name.c_str());

  m_initialized = true;
}

void BinaryDescriptor::Terminate() {
  if (!m_initialized)
    return;

  m_match.Terminate();

  delete[] m_pattern;
  delete[] m_mask;
  m_pattern = nullptr;
  m_mask = nullptr;
  m_patternLen = 0;

  m_name.clear();
  m_module = 0;
  m_rva = kInvalidRva;
  m_slot = -1;
  m_result = 0;

  m_evaluated = false;
  m_initialized = false;
}

void BinaryDescriptor::Evaluate() {
  if (!m_initialized || m_evaluated)
    return;

  // Virtual function descriptor: report the vtable slot directly.
  if (m_slot >= 0) {
    m_result = m_slot;
    m_evaluated = true;
    return;
  }

  uintptr_t addr = 0;

  // The signature goes first: it survives the code moving, which is the whole
  // point of scanning for one. Skipped when the descriptor carries none.
  if (m_patternLen)
    addr = ScanPattern(m_module, m_pattern, m_mask, m_patternLen);

  // Fall back to the RVA when the scan came up empty, and use it outright for a
  // descriptor with no signature at all. GetDlModule() hands back the module
  // base, so the address is base + RVA.
  if (!addr && m_module && m_rva != kInvalidRva)
    addr = m_module + m_rva;

  if (!addr)
    return;

  // Solve() hands `inst` straight to the decoder, which reads a whole encoding
  // at a time and so can run up to GetMaxInsnLength() bytes past the last
  // instruction a "$a:b" term references. Pad the buffer by that much and leave
  // the tail zeroed: the padding keeps the decode in bounds while the count
  // stays at m_patternLen, so an instruction straddling the end of the sequence
  // is still rejected rather than assembled out of bytes nobody matched.
  //
  // Only the matched bytes are read. Reading the padding too would fail the
  // whole descriptor whenever the sequence sits at the end of a section.
  std::vector<u08> inst(m_patternLen + GetArch()->GetMaxInsnLength());
  if (m_patternLen && !ReadMemory(addr, inst.data(), m_patternLen))
    return;

  if (!m_match.Solve(inst.data(), addr, m_patternLen, &m_result))
    return;

  m_evaluated = true;
}

// ----------------------------------------------------------------------------
// [SECTION] BinaryDescriptorBarn
// ----------------------------------------------------------------------------

static BinaryDescriptorBarn *g_binaryDescriptorBarn = nullptr;

static bool s_ReadNumberLike(
  u64 *out,
  const rapidjson::Value &value
) {
  // WARN: In some cases, this function will silently fail, causing the RVA to
  // not be correctly recognized.
  if (value.IsInt64()) {
    *out = value.GetInt64();
    return true;
  }

  if (value.IsString()) {
    cstring s = value.GetString();
    char *endptr = nullptr;
    u64 offset = strtoull(s, &endptr, 0);

    if (endptr == s)
      return false;

    *out = offset;
    return true;
  }

  return false;
}

static void s_ReadDescFromJson(
  BinaryDescriptor::InitTypedef *init,
  const rapidjson::Value &json
) {
  if (json.HasMember("rva"))
    s_ReadNumberLike(&init->rva, json["rva"]);

  if (json.HasMember("slot") && json["slot"].IsInt())
    init->slot = json["slot"].GetInt();

  if (json.HasMember("module") && json["module"].IsString())
    init->module = json["module"].GetString();

  if (json.HasMember("sig") && json["sig"].IsObject()) {
    const rapidjson::Value &sig = json["sig"];

    if (sig.HasMember("pattern") && sig["pattern"].IsString())
      init->pattern = sig["pattern"].GetString();

    if (sig.HasMember("mask") && sig["mask"].IsString())
      init->mask = sig["mask"].GetString();

    if (sig.HasMember("match") && sig["match"].IsString())
      init->match = sig["match"].GetString();
  }
}

void BinaryDescriptorBarn::Initialize(
  cstring json
) {
  ASSERTMSG(!g_binaryDescriptorBarn, "BinaryDescriptorBarn is a singleton and must be initialized only once.");
  g_binaryDescriptorBarn = this;

  rapidjson::Document root;
  root.Parse(json);

  if (root.HasParseError() || !root.IsObject())
    return;

  cstring tag = GetArch()->GetTag();
  if (!root.HasMember(tag) || !root[tag].IsObject())
    return;

  for (auto &it: root[tag].GetObject()) {
    rapidjson::Value &value = it.value;
    if (!value.IsObject())
      continue;

    cstring name = it.name.GetString();
    BinaryDescriptor::InitTypedef init;

    s_ReadDescFromJson(&init, value);

    auto &desc = m_descriptors[name];
    desc.Initialize(name, init);

    // TODO(HTMonkeyG): Multi-thread evaluate.
    //BinaryDescriptor::s_EvaluateObject(m_descriptors[name]);
    desc.Evaluate();
  }
}

void BinaryDescriptorBarn::Terminate() {
  g_binaryDescriptorBarn = nullptr;
  m_descriptors.clear();
}

const BinaryDescriptor *BinaryDescriptorBarn::GetDescriptorByName(
  cstring name
) {
  auto it = m_descriptors.find(name);
  if (it == m_descriptors.end())
    return nullptr;
  return &it->second;
}
