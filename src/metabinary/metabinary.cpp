#include "utils/stringUtils.hpp"
#include "utils/logger.hpp"
#include "metabinary/binaryDescriptor.hpp"
#include "metabinary/metabinary.hpp"

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

  for (auto *p = MetaBinaryLister<MetaBinaryVariable>::m_List(); p; p = p->GetPrev()) {
    auto it = m_variables.find(p->GetName());
    if (it != m_variables.end()) {
      LOGW(
        "Duplicated name of MetaBinaryVariable [%s], please check the initializ"
        "ation code.",
        p->GetName());
      continue;
    }

    m_variables[p->GetName()] = p;
  }

  for (auto *p = MetaBinaryLister<MetaBinaryMemberVariable>::m_List(); p; p = p->GetPrev()) {
    auto it = m_memberVariables.find(p->GetName());
    if (it != m_memberVariables.end()) {
      LOGW(
        "Duplicated name of MetaBinaryMemberVariable [%s], please check the ini"
        "tialization code.",
        p->GetName());
      continue;
    }

    m_memberVariables[p->GetName()] = p;
  }

  for (auto *p = MetaBinaryLister<MetaBinaryConstant>::m_List(); p; p = p->GetPrev()) {
    auto it = m_constants.find(p->GetName());
    if (it != m_constants.end()) {
      LOGW(
        "Duplicated name of MetaBinaryConstant [%s], please check the initializ"
        "ation code.",
        p->GetName());
      continue;
    }

    m_constants[p->GetName()] = p;
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

static cstring s_KindName(
  BinaryDescriptor::Kind kind
) {
  using Kind = BinaryDescriptor::Kind;
  switch (kind) {
    case Kind::kFunction: return "function";
    case Kind::kVirtualFunction: return "virtual_function";
    case Kind::kVariable: return "variable";
    case Kind::kMemberVariable: return "member_variable";
    case Kind::kConstant: return "constant";
    default: return "invalid";
  }
}

void MetaBinarySystem::m_CheckDescriptors(
  BinaryDescriptorBarn *binaryDescriptorBarn
) {
  auto check = [&](const auto &map, cstring typeName,
                   BinaryDescriptor::Kind expected) {
    for (auto &it: map) {
      cstring name = it.second->GetName();
      const auto *desc = binaryDescriptorBarn->GetDescriptorByName(name);
      if (!desc) {
        LOGW("%s [%s] has no descriptor.", typeName, name);
        m_hasError = true;
        continue;
      }
      if (desc->GetKind() != expected) {
        LOGW(
          "%s [%s] expects desc_type \"%s\", got \"%s\".",
          typeName,
          name,
          s_KindName(expected),
          s_KindName(desc->GetKind()));
        m_hasError = true;
      }
    }
  };

  check(m_functions, "MetaBinaryFunction", BinaryDescriptor::Kind::kFunction);
  check(
    m_virtualFunctions,
    "MetaBinaryVirtualFunction",
    BinaryDescriptor::Kind::kVirtualFunction);
  check(m_variables, "MetaBinaryVariable", BinaryDescriptor::Kind::kVariable);
  check(
    m_memberVariables,
    "MetaBinaryMemberVariable",
    BinaryDescriptor::Kind::kMemberVariable);
  check(m_constants, "MetaBinaryConstant", BinaryDescriptor::Kind::kConstant);
}

void MetaBinarySystem::m_AssignDescriptors(
  BinaryDescriptorBarn *binaryDescriptorBarn
) {
  size_t count = 0;

  for (auto &it: m_functions) {
    auto *mbf = it.second;
    const auto *desc = binaryDescriptorBarn->GetDescriptorByName(mbf->GetName());
    if (!desc || !desc->IsValid() ||
        desc->GetKind() != BinaryDescriptor::Kind::kFunction) {
      if (desc && !desc->IsValid())
        LOGW("Descriptor [%s] is invalid.", mbf->GetName());
      continue;
    }
    mbf->Initialize((uintptr_t)desc->GetResult());
    if (!mbf->IsInitialized()) {
      LOGW("Descriptor [%s] did not yield an executable address.", mbf->GetName());
      m_hasError = true;
      continue;
    }
    count++;
  }

  LOG("Initialized %llu of %llu MetaBinaryFunction", count, m_functions.size());

  count = 0;
  for (auto &it: m_virtualFunctions) {
    auto *mbvf = it.second;
    const auto *desc = binaryDescriptorBarn->GetDescriptorByName(mbvf->GetName());
    if (!desc || !desc->IsValid() ||
        desc->GetKind() != BinaryDescriptor::Kind::kVirtualFunction) {
      if (desc && !desc->IsValid())
        LOGW("Descriptor [%s] is invalid.", mbvf->GetName());
      continue;
    }
    mbvf->Initialize((i32)desc->GetResult());
    if (!mbvf->IsInitialized()) {
      LOGW("Descriptor [%s] did not yield a vtable slot.", mbvf->GetName());
      m_hasError = true;
      continue;
    }
    count++;
  }

  LOG(
    "Initialized %llu of %llu MetaBinaryVirtualFunction",
    count,
    m_virtualFunctions.size());

  count = 0;
  for (auto &it: m_variables) {
    auto *var = it.second;
    const auto *desc = binaryDescriptorBarn->GetDescriptorByName(var->GetName());
    if (!desc || !desc->IsValid() ||
        desc->GetKind() != BinaryDescriptor::Kind::kVariable) {
      if (desc && !desc->IsValid())
        LOGW("Descriptor [%s] is invalid.", var->GetName());
      continue;
    }
    var->Initialize((uintptr_t)desc->GetResult());
    if (!var->IsInitialized()) {
      LOGW("Descriptor [%s] did not yield a variable address.", var->GetName());
      m_hasError = true;
      continue;
    }
    count++;
  }

  LOG("Initialized %llu of %llu MetaBinaryVariable", count, m_variables.size());

  count = 0;
  for (auto &it: m_memberVariables) {
    auto *var = it.second;
    const auto *desc = binaryDescriptorBarn->GetDescriptorByName(var->GetName());
    if (!desc || !desc->IsValid() ||
        desc->GetKind() != BinaryDescriptor::Kind::kMemberVariable) {
      if (desc && !desc->IsValid())
        LOGW("Descriptor [%s] is invalid.", var->GetName());
      continue;
    }
    var->Initialize((i32)desc->GetResult());
    if (!var->IsInitialized()) {
      LOGW("Descriptor [%s] did not yield a member offset.", var->GetName());
      m_hasError = true;
      continue;
    }
    count++;
  }

  LOG(
    "Initialized %llu of %llu MetaBinaryMemberVariable",
    count,
    m_memberVariables.size());

  count = 0;
  for (auto &it: m_constants) {
    auto *constant = it.second;
    const auto *desc =
      binaryDescriptorBarn->GetDescriptorByName(constant->GetName());
    if (!desc || !desc->IsValid() ||
        desc->GetKind() != BinaryDescriptor::Kind::kConstant) {
      if (desc && !desc->IsValid())
        LOGW("Descriptor [%s] is invalid.", constant->GetName());
      continue;
    }
    constant->Initialize((uintptr_t)desc->GetResult());
    if (!constant->IsInitialized()) {
      LOGW("Descriptor [%s] did not yield a constant value.", constant->GetName());
      m_hasError = true;
      continue;
    }
    count++;
  }

  LOG("Initialized %llu of %llu MetaBinaryConstant", count, m_constants.size());
}
