#include <stdlib.h>
#include <vector>
#include <rapidjson/document.h>
#include <rapidjson/reader.h>
#include "utils/logger.hpp"
#include "utils/archUtils.hpp"
#include "utils/loaderUtils.hpp"
#include "utils/winapicall.hpp"
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
// Builds the byte/mask sequence handed to ScanPattern() from one signature
// entry.
//
//   - Byte wildcard mode (mask == nullptr): "48 8D ? E8 ? ? ?". Each hex pair
//     becomes a 0xFF mask byte; each '?' (one or two, bytes separated by any
//     number of spaces) becomes a 0x00 mask byte over a 0x00 pattern byte.
//   - Bit-mask mode (mask != nullptr): both strings spell hex bytes; each mask
//     byte is the bitwise keep-mask for the matching pattern byte. '?' is not
//     allowed here.
//
// Returns false on a malformed signature, leaving both vectors empty.
static bool s_BuildPattern(
  std::vector<u08> *outPattern,
  std::vector<u08> *outMask,
  cstring pattern,
  cstring mask
) {
  outPattern->clear();
  outMask->clear();

  if (!pattern)
    return false;

  const bool isByteWildcard = (mask == nullptr);

  u64 patternLen = 0;
  if (!s_CountSignature(pattern, isByteWildcard, &patternLen)) {
    LOGW("Invalid pattern [%s]", pattern);
    return false;
  }

  if (patternLen == 0 || patternLen > 0xFFFFFFFFull) {
    LOGW("Signature is empty or too long [%s]", pattern);
    return false;
  }

  if (!isByteWildcard) {
    u64 maskLen = 0;
    if (!s_CountSignature(mask, false, &maskLen) || maskLen != patternLen) {
      LOGW("Signature pattern/mask length mismatch [%s] [%s]", pattern, mask);
      return false;
    }
  }

  outPattern->resize(patternLen);
  outMask->resize(patternLen);

  u08 *bytes = outPattern->data();
  u08 *masks = outMask->data();
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
        if (!s_ReadHexByte(&m, &mk)) {
          LOGW("Invalid mask [%s] for pattern [%s]", mask, pattern);
          outPattern->clear();
          outMask->clear();
          return false;
        }
        masks[i] = mk;
      }
    }
    i++;
  }

  return true;
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
  if (m_initialized)
    return;

  m_name = name;

  m_module = Win64SysCall::GetDlModule(init.module);
  m_rva = init.rva;
  m_slot = init.slot;

  // Compile every candidate in order. An entry that cannot be built or whose
  // expression does not compile is dropped; the remaining ones keep their
  // relative order, so the first usable signature in the file stays first.
  for (const auto &entryInit: init.signatures) {
    Signature sig;
    if (!s_BuildPattern(&sig.pattern, &sig.mask, entryInit.pattern, entryInit.mask))
      continue;

    sig.match.Initialize(entryInit.match ? entryInit.match : "$@");
    if (!sig.match.IsValid()) {
      LOGW(
        "Invalid pattern expression [%s] in [%s]",
        entryInit.match,
        m_name.c_str());
      continue;
    }

    sig.valid = true;
    m_signatures.push_back(std::move(sig));
  }

  m_initialized = true;
}

void BinaryDescriptor::Terminate() {
  if (!m_initialized)
    return;

  m_signatures.clear();

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

  // The signatures go first: they survive the code moving, which is the whole
  // point of scanning for one. Top to bottom, the first entry that both scans
  // and evaluates wins.
  for (const Signature &sig: m_signatures) {
    if (!sig.valid || sig.pattern.empty())
      continue;

    uintptr_t addr = HTModLoader::ScanPattern(
      m_module,
      sig.pattern.data(),
      sig.mask.data(),
      static_cast<u32>(sig.pattern.size()));
    if (!addr)
      continue;

    // Solve() hands `inst` straight to the decoder, which reads a whole
    // encoding at a time and so can run up to GetMaxInsnLength() bytes past the
    // last instruction a "$a:b" term references. Pad the buffer by that much
    // and leave the tail zeroed: the padding keeps the decode in bounds while
    // the count stays at the pattern length, so an instruction straddling the
    // end of the sequence is still rejected rather than assembled out of bytes
    // nobody matched.
    //
    // Only the matched bytes are read. Reading the padding too would fail the
    // whole descriptor whenever the sequence sits at the end of a section.
    std::vector<u08> inst(sig.pattern.size() + ArchX64::GetMaxInsnLength());
    if (!Win64SysCall::ReadMemory(
      addr,
      inst.data(),
      static_cast<size_t>(sig.pattern.size())
    )) {
      continue;
    }

    i64 result = 0;
    if (!sig.match.Solve(
      inst.data(),
      addr,
      static_cast<u32>(sig.pattern.size()),
      &result
    )) {
      continue;
    }

    m_result = result;
    m_evaluated = true;
    return;
  }

  // Virtual function descriptor: no signature produced a slot, so fall back to
  // the declared one.
  if (m_slot >= 0) {
    m_result = m_slot;
    m_evaluated = true;
    LOGW("%s failed to evaluate with signature, fallback to \"slot\"", m_name.c_str());
    return;
  }

  // Ordinary function descriptor: fall back to the module-relative address.
  // GetDlModule() hands back the module base, so the address is base + RVA.
  if (m_module && m_rva != kInvalidRva) {
    m_result = static_cast<i64>(m_module + m_rva);
    LOGW("%s failed to evaluate with signature, fallback to \"rva\"", m_name.c_str());
    m_evaluated = true;
    return;
  }
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

static void s_ReadSignatureFromJson(
  BinaryDescriptor::InitTypedef *init,
  const rapidjson::Value &sig
) {
  BinaryDescriptor::SignatureInit entry;

  if (sig.HasMember("pattern") && sig["pattern"].IsString())
    entry.pattern = sig["pattern"].GetString();

  if (sig.HasMember("mask") && sig["mask"].IsString())
    entry.mask = sig["mask"].GetString();

  if (sig.HasMember("match") && sig["match"].IsString())
    entry.match = sig["match"].GetString();

  // A signature without a pattern has nothing to scan for.
  if (entry.pattern)
    init->signatures.push_back(entry);
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

  if (json.HasMember("sig")) {
    const rapidjson::Value &sig = json["sig"];

    if (sig.IsObject()) {
      // Legacy single-signature form.
      s_ReadSignatureFromJson(init, sig);
    } else if (sig.IsArray()) {
      // Ordered candidate list: the first entry that scans and evaluates wins.
      for (const rapidjson::Value &entry: sig.GetArray()) {
        if (entry.IsObject())
          s_ReadSignatureFromJson(init, entry);
      }
    }
  }
}

void BinaryDescriptorBarn::Initialize(
  cstring json,
  cstring profile
) {
  if (g_binaryDescriptorBarn)
    return;

  g_binaryDescriptorBarn = this;

  rapidjson::Document root;
  constexpr auto flags = rapidjson::kParseDefaultFlags
    | rapidjson::kParseCommentsFlag
    | rapidjson::kParseTrailingCommasFlag;
  root.Parse<flags>(json);

  if (root.HasParseError() || !root.IsObject()) {
    LOGW("Failed to parse binary descriptor json: %d", root.GetParseError());
    return;
  }

  // Pick the descriptor list for this game edition. Profiles are ordinary
  // top-level objects, which keeps the file readable:
  //
  //   { "netease": { "x64": { ... } }, "tgc": { "x64": { ... } } }
  //
  // A file without a matching profile keeps the legacy root[arch] layout.
  const rapidjson::Value *section = &root;
  if (profile && *profile && root.HasMember(profile) &&
      root[profile].IsObject()) {
    section = &root[profile];
  }

  cstring tag = ArchX64::GetTag();
  if (!section->HasMember(tag) || !(*section)[tag].IsObject())
    return;

  for (auto &it: (*section)[tag].GetObject()) {
    const rapidjson::Value &value = it.value;
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
