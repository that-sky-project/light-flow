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
  if (g_metaBinarySystem)
    return;

  g_metaBinarySystem = this;

  for (auto *p = MetaBinaryLister<MetaBinaryFunction>::m_List(); p; p = p->GetPrev()) {
    auto it = m_functions.find(p->GetName());
    if (it != m_functions.end()) {
      LOGW(
        "Duplicated name of MetaBinaryFunction [%s], please check the initializ"
        "ation code.",
        p->GetName());
      continue;
    }

    m_functions[p->GetName()] = p;
  }

  for (auto *p = MetaBinaryLister<MetaBinaryVirtualFunction>::m_List(); p; p = p->GetPrev()) {
    auto it = m_virtualFunctions.find(p->GetName());
    if (it != m_virtualFunctions.end()) {
      LOGW(
        "Duplicated name of MetaBinaryVirtualFunction [%s], please check the in"
        "itialization code.",
        p->GetName());
      continue;
    }

    m_virtualFunctions[p->GetName()] = p;
  }

  // Redirect lazily loaded hooks.
  for (auto *p = MetaBinaryLister<MetaBinaryHookRedirect>::m_List(); p; p = p->GetPrev()) {
    p->Activate();
  }

  m_CheckDescriptors(binaryDescriptorBarn);
  m_AssignDescriptors(binaryDescriptorBarn);
}

void MetaBinarySystem::Terminate() {
  g_metaBinarySystem = nullptr;
}

void MetaBinarySystem::m_CheckDescriptors(
  BinaryDescriptorBarn *binaryDescriptorBarn
) {
  for (auto &it: m_functions) {
    auto mbf = it.second;
    auto name = mbf->GetName();
    auto desc = binaryDescriptorBarn->GetDescriptorByName(name);
    if (!desc) {
      LOGW("Descriptor [%s] not found.", name);
      m_hasError = true;
      continue;
    }
    if (!desc->IsValid()) {
      LOGW("Descriptor [%s] is invalid.", name);
      m_hasError = true;
      continue;
    }
  }

  for (auto &it: m_virtualFunctions) {
    auto mbvf = it.second;
    auto name = mbvf->GetName();
    auto desc = binaryDescriptorBarn->GetDescriptorByName(name);
    if (!desc) {
      LOGW("Descriptor [%s] not found.", name);
      m_hasError = true;
      continue;
    }
    if (!desc->IsValid()) {
      LOGW("Descriptor [%s] is invalid.", name);
      m_hasError = true;
      continue;
    }
  }
}

void MetaBinarySystem::m_AssignDescriptors(
  BinaryDescriptorBarn *binaryDescriptorBarn
) {
  size_t count = 0;
  for (auto &it: m_functions) {
    auto *mbf = it.second;
    const auto *desc = binaryDescriptorBarn->GetDescriptorByName(mbf->GetName());
    if (!desc || !desc->IsValid())
      continue;
    mbf->Initialize(desc->GetResult());
    count++;
  }

  LOG("Initialized %llu of %llu MetaBinaryFunction", count, m_functions.size());

  count = 0;
  for (auto &it: m_virtualFunctions) {
    auto *mbvf = it.second;
    const auto *desc = binaryDescriptorBarn->GetDescriptorByName(mbvf->GetName());
    if (!desc || !desc->IsValid())
      continue;
    mbvf->Initialize((i32)desc->GetResult());
    count++;
  }

  LOG("Initialized %llu of %llu MetaBinaryVirtualFunction", count, m_virtualFunctions.size());
}
