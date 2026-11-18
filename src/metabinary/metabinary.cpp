#include "utils/stringUtils.hpp"
#include "utils/logger.hpp"
#include "binaryDescriptor.hpp"
#include "metabinary.hpp"

// ----------------------------------------------------------------------------
// [SECTION] MetaBinarySystem
// ----------------------------------------------------------------------------

static MetaBinarySystem *g_metaBinarySystem = nullptr;

void MetaBinarySystem::Initialize(
  BinaryDescriptorBarn *binaryDescriptorBarn
) {
  ASSERTMSG(!g_metaBinarySystem, "MetaBinarySystem is a singleton and must be initialized only once.");
  g_metaBinarySystem = this;

  for (auto *p = MetaBinaryLister<MetaBinaryFunction>::m_List(); p; p = p->GetPrev()) {
    auto it = m_functions.find(p->GetName());
    if (it != m_functions.end()) {
      ASSERTMSG(false,
        "Duplicated name of MetaBinaryFunction [%s], please check the initializ"
        "ation code.",
        p->GetName());
    }

    m_functions[p->GetName()] = p;
  }

  for (auto *p = MetaBinaryLister<MetaBinaryVirtualFunction>::m_List(); p; p = p->GetPrev()) {
    auto it = m_virtualFunctions.find(p->GetName());
    if (it != m_virtualFunctions.end()) {
      ASSERTMSG(false,
        "Duplicated name of MetaBinaryVirtualFunction [%s], please check the in"
        "itialization code.",
        p->GetName());
    }

    m_virtualFunctions[p->GetName()] = p;
  }

  // Redirect lazily loaded hooks.
  for (auto *p = MetaBinaryLister<MetaBinaryHookRedirect>::m_List(); p; p = p->GetPrev()) {
    p->Activate();
  }

  m_LogDescriptors(binaryDescriptorBarn);
  m_AssignDescriptors(binaryDescriptorBarn);
}

void MetaBinarySystem::Terminate() {
  g_metaBinarySystem = nullptr;
}

void MetaBinarySystem::m_LogDescriptors(
  BinaryDescriptorBarn *binaryDescriptorBarn
) {
  for (auto &it: m_functions) {
    auto mbf = it.second;
    auto name = mbf->GetName();
    auto desc = binaryDescriptorBarn->GetDescriptorByName(name);
    if (!desc) {
      LOGW("Descriptor [%s] not found.", name);
      continue;
    }
    if (!desc->IsValid()) {
      LOGW("Descriptor [%s] is invalid.", name);
      continue;
    }
  }

  for (auto &it: m_virtualFunctions) {
    auto mbvf = it.second;
    auto name = mbvf->GetName();
    auto desc = binaryDescriptorBarn->GetDescriptorByName(name);
    if (!desc) {
      LOGW("Descriptor [%s] not found.", name);
      continue;
    }
    if (!desc->IsValid()) {
      LOGW("Descriptor [%s] is invalid.", name);
      continue;
    }
  }
}

static inline const BinaryDescriptor *s_CheckDesciptor(
  BinaryDescriptorBarn *binaryDescriptorBarn,
  cstring name
) {
  const BinaryDescriptor *desc = binaryDescriptorBarn->GetDescriptorByName(name);

  ASSERTMSG(desc,
    "Descriptor [%s] not found. Was it omitted when writing the description file?",
    name);

  ASSERTMSG(desc->IsValid(),
    "Descriptor [%s] is invalid. Please update the description file.",
    name);

  return desc;
}

void MetaBinarySystem::m_AssignDescriptors(
  BinaryDescriptorBarn *binaryDescriptorBarn
) {
  for (auto &it: m_functions) {
    auto *mbf = it.second;
    const BinaryDescriptor *desc = s_CheckDesciptor(binaryDescriptorBarn, mbf->GetName());
    mbf->Initialize(desc->GetResult());
  }

  LOG("Initialized %llu MetaBinaryFunction", m_functions.size());

  for (auto &it: m_virtualFunctions) {
    auto *mbvf = it.second;
    const BinaryDescriptor *desc = s_CheckDesciptor(binaryDescriptorBarn, mbvf->GetName());
    mbvf->Initialize((i32)desc->GetResult());
  }

  LOG("Initialized %llu MetaBinaryVirtualFunction", m_virtualFunctions.size());
}
