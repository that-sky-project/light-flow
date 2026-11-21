#ifndef __API_MOD_ASSEMBLY_METABINARY_HPP__
#define __API_MOD_ASSEMBLY_METABINARY_HPP__

#include <type_traits>
#include <utility>
#include <unordered_map>
#include "utils/types.hpp"
#include "utils/abiMisc.hpp"
#include "utils/logger.hpp"
#include "utils/loaderUtils.hpp"
#include "metabinary/binaryDescriptor.hpp"

// ----------------------------------------------------------------------------
// [SECTION] BinaryCallMixin
// ----------------------------------------------------------------------------

// CRTP mixin providing Call() and operator() for MetaBinaryFunctionImpl and
// MetaBinaryVirtualFunctionImpl. PFN is the flat call pointer type. Forwarding
// uses U... (the deduced pack) instead of Args..., fixing the original
// std::forward<Args> bug.
template<typename Derived, typename PFN>
struct BinaryCallMixin {
  template<typename ...U>
  inline auto Call(U &&...args) const
    -> decltype(std::declval<PFN>()(std::forward<U>(args)...))
  {
    using Ret = decltype(std::declval<PFN>()(std::forward<U>(args)...));

    const Derived *d = static_cast<const Derived *>(this);
    if (!d->Function().IsValid()) {
      // Unresolved link. Returning a default value keeps the game alive; the
      // descriptor check has already logged which function is missing.
      LOGW("Call to uninitialized function %s", d->GetName());
      if constexpr (!std::is_void_v<Ret>)
        return Ret{};
      else
        return;
    }

    PFN fn = reinterpret_cast<PFN>(d->Function().Original());
    return fn(std::forward<U>(args)...);
  }

  template<typename ...U>
  inline auto operator()(U &&...args) const
    -> decltype(std::declval<PFN>()(std::forward<U>(args)...))
  {
    return static_cast<const Derived *>(this)->Call(std::forward<U>(args)...);
  }
};

// ----------------------------------------------------------------------------
// [SECTION] BinaryFunctionBase
// ----------------------------------------------------------------------------

// Function address and hook. Name is not stored here; it lives once in the
// owning MetaObject and is forwarded into Hook() for CreateHook logging.
class BinaryFunctionBase {
private:
  class Void;

public:
  using FuncPtr = void (*)();
  using MemberFuncPtr = void (Void::*)();

private:
  BinaryFunctionBase(const BinaryFunctionBase &) = delete;
  BinaryFunctionBase(BinaryFunctionBase &&) = delete;
  BinaryFunctionBase &operator=(const BinaryFunctionBase &) = delete;

public:
  // Raw (C-style) function.
  explicit BinaryFunctionBase(FuncPtr detour = nullptr)
    : m_detour(reinterpret_cast<uintptr_t>(detour))
  { }

  // Extract the function address from a member function pointer.
  explicit BinaryFunctionBase(MemberFuncPtr detour)
    : m_detour(detour == nullptr ? 0 : AbiMemFuncPtr(detour)->GetAddress())
  { }

  inline bool IsValid() const { return !!m_address; }
  inline bool IsHookEnabled() const { return m_hookEnabled; }

  inline uintptr_t Address() const { return m_address; }
  inline uintptr_t Trampoline() const { return m_trampoline; }
  inline uintptr_t Detour() const { return m_detour; }

  // Original function.
  inline uintptr_t Original() const { return IsHookEnabled() ? m_trampoline : m_address; }

  // Initialize the function with the given address.
  inline void Initialize(uintptr_t address) {
    if (IsValid()) {
      LOGW("BinaryFunction initialized twice");
      return;
    }
    if (!address) {
      LOGW("BinaryFunction initialized with a null address");
      return;
    }
    m_address = address;
  }

  // Terminate is safe to call even when Hook() was never invoked.
  inline void Terminate() {
    if (IsHookEnabled())
      Unhook();
    m_address = 0;
  }

  // Give a chance to redirect the detour function.
  inline void Redirect(FuncPtr detour) {
    if (IsValid()) {
      LOGW("Redirect after initialization is not supported");
      return;
    }
    m_detour = reinterpret_cast<uintptr_t>(detour);
  }
  inline void Redirect(MemberFuncPtr detour) {
    if (IsValid()) {
      LOGW("Redirect after initialization is not supported");
      return;
    }
    m_detour = detour == nullptr ? 0 : AbiMemFuncPtr(detour)->GetAddress();
  }

  // Create and enable hook. Name is passed from the owning MetaObject.
  inline bool Hook(cstring tag) {
    if (!Detour())
      return false;

    if (!IsValid() || IsHookEnabled()) {
      LOGW("Hook(%s) called in an invalid state", tag ? tag : "?");
      return false;
    }

    uintptr_t trampoline = 0;
    m_hook = HTModLoader::CreateHook(tag, m_address, m_detour, &trampoline);
    if (!m_hook) {
      LOGW("Hook(%s): CreateHook failed", tag ? tag : "?");
      return false;
    }

    if (!HTModLoader::EnableHook(m_hook)) {
      LOGW("Hook(%s): EnableHook failed", tag ? tag : "?");
      HTModLoader::DeleteHook(m_hook);
      m_hook = 0;
      return false;
    }

    // Without a trampoline the detour could not call the original function,
    // and enabling such a hook would make every game call jump to null.
    if (!trampoline) {
      LOGW("Hook(%s): loader returned no trampoline", tag ? tag : "?");
      HTModLoader::DisableHook(m_hook);
      HTModLoader::DeleteHook(m_hook);
      m_hook = 0;
      return false;
    }

    m_trampoline = trampoline;
    m_hookEnabled = true;
    return true;
  }

  // Remove hook.
  inline void Unhook() {
    if (!Detour() || !IsHookEnabled())
      return;

    HTModLoader::DisableHook(m_hook);
    HTModLoader::DeleteHook(m_hook);

    m_hookEnabled = false;
    m_hook = 0;
  }

protected:
  uintptr_t m_detour = 0;
  uintptr_t m_address = 0;
  uintptr_t m_trampoline = 0;
  uintptr_t m_hook = 0;
  bool m_hookEnabled = false;
};

// ----------------------------------------------------------------------------
// [SECTION] MetaBinaryLister
// ----------------------------------------------------------------------------

template<typename T>
class MetaBinaryLister {
public:
  static inline T *&m_List() {
    static T *p = nullptr;
    return p;
  }

  ~MetaBinaryLister() = default;
  MetaBinaryLister(cstring name): m_name(name) {
    m_prev = m_List();
    // CRTP, downcast to a pointer of the subclass.
    m_List() = static_cast<T *>(this);
  }

  inline T *GetPrev() const { return m_prev; }
  inline cstring GetName() const { return m_name; }

private:
  cstring m_name = nullptr;
  T *m_prev = nullptr;
};

// ----------------------------------------------------------------------------
// [SECTION] MetaBinaryFunction Variants
// ----------------------------------------------------------------------------

// - MetaBinaryFunction

// Use "<object_name>.<member_name>()", "<object_name>::<member_name>()" or
// "<function_name>()", e.g. "Game.Initialize()" or "MetaBinaryOffset::IsValid()".
//
// NOTE: We didn't provide the no-chain variant constructor.
class MetaBinaryFunction: public MetaBinaryLister<MetaBinaryFunction> {
private:
  MetaBinaryFunction(const MetaBinaryFunction &) = delete;
  MetaBinaryFunction(MetaBinaryFunction &&) = delete;
  MetaBinaryFunction &operator=(const MetaBinaryFunction &) = delete;

protected:
  explicit MetaBinaryFunction(
    cstring name,
    BinaryFunctionBase::FuncPtr detour = nullptr
  )
    : MetaBinaryLister<MetaBinaryFunction>(name)
    , m_func(detour)
  { }

  explicit MetaBinaryFunction(
    cstring name,
    BinaryFunctionBase::MemberFuncPtr detour
  )
    : MetaBinaryLister<MetaBinaryFunction>(name)
    , m_func(detour)
  { }

public:
  inline bool IsInitialized() const { return m_func.IsValid(); }
  inline void Initialize(uintptr_t address) { m_func.Initialize(address); }
  inline void Terminate() { m_func.Terminate(); }
  inline bool Hook() { return m_func.Hook(GetName()); }
  inline void Unhook() { m_func.Unhook(); }

  inline const BinaryFunctionBase &Function() const { return m_func; }
  inline BinaryFunctionBase &Function() { return m_func; }

protected:
  BinaryFunctionBase m_func;
};

template<typename Fn>
class MetaBinaryFunctionImpl;

template<typename Ret, typename ...Args>
class MetaBinaryFunctionImpl<Ret (*)(Args...)>
  : public MetaBinaryFunction
  , public BinaryCallMixin<MetaBinaryFunctionImpl<Ret (*)(Args...)>, Ret (*)(Args...)>
{
protected:
  using PFN_Call = Ret (*)(Args...);

private:
  MetaBinaryFunctionImpl(const MetaBinaryFunctionImpl &) = delete;
  MetaBinaryFunctionImpl(MetaBinaryFunctionImpl &&) = delete;
  MetaBinaryFunctionImpl &operator=(const MetaBinaryFunctionImpl &) = delete;

public:
  explicit MetaBinaryFunctionImpl(cstring name, PFN_Call detour = nullptr)
    : MetaBinaryFunction(name, reinterpret_cast<BinaryFunctionBase::FuncPtr>(detour))
  { }
};

template<typename Ret, typename Class, typename ...Args>
class MetaBinaryFunctionImpl<Ret (Class::*)(Args...)>
  : public MetaBinaryFunction
  , public BinaryCallMixin<MetaBinaryFunctionImpl<Ret (Class::*)(Args...)>, Ret (*)(Class *, Args...)>
{
protected:
  using PFN_Member = Ret (Class::*)(Args...);
  using PFN_Call   = Ret (*)(Class *, Args...);

private:
  MetaBinaryFunctionImpl(const MetaBinaryFunctionImpl &) = delete;
  MetaBinaryFunctionImpl(MetaBinaryFunctionImpl &&) = delete;
  MetaBinaryFunctionImpl &operator=(const MetaBinaryFunctionImpl &) = delete;

public:
  explicit MetaBinaryFunctionImpl(cstring name, PFN_Call detour = nullptr)
    : MetaBinaryFunction(name, reinterpret_cast<BinaryFunctionBase::FuncPtr>(detour))
  { }

  explicit MetaBinaryFunctionImpl(cstring name, PFN_Member detour)
    : MetaBinaryFunction(name, reinterpret_cast<BinaryFunctionBase::MemberFuncPtr>(detour))
  { }

  // Implicit cast to ABI-compatible member function pointer.
  operator PFN_Member() const {
    return reinterpret_cast<PFN_Member>(AbiMemFuncPtr(m_func.Original()).Pointer());
  }
};

// - MetaBinaryVirtualFunction

class MetaBinaryVirtualFunction: public MetaBinaryLister<MetaBinaryVirtualFunction> {
private:
  MetaBinaryVirtualFunction(const MetaBinaryVirtualFunction &) = delete;
  MetaBinaryVirtualFunction(MetaBinaryVirtualFunction &&) = delete;
  MetaBinaryVirtualFunction &operator=(const MetaBinaryVirtualFunction &) = delete;

protected:
  explicit MetaBinaryVirtualFunction(
    cstring name,
    BinaryFunctionBase::FuncPtr detour = nullptr
  )
    : MetaBinaryLister<MetaBinaryVirtualFunction>(name)
    , m_func(detour)
  { }

  explicit MetaBinaryVirtualFunction(
    cstring name,
    BinaryFunctionBase::MemberFuncPtr detour
  )
    : MetaBinaryLister<MetaBinaryVirtualFunction>(name)
    , m_func(detour)
  { }

public:
  inline bool IsInitialized() const { return m_vindex != -1; }
  inline void Initialize(i32 vindex) {
    if (m_vindex != -1)
      return;
    m_vindex = vindex;
  }
  inline void Terminate() { m_func.Terminate(); }

  // User must call this function to cache the vftable of any object.
  template<typename Class>
  inline void Cache(const Class *object) {
    static_assert(std::is_polymorphic<Class>::value,
      "MetaBinary: Object is not a polymorphic class.");
    if (!object)
      return;
    Cache(*reinterpret_cast<const uintptr_t *const *>(object));
  }

  inline void Cache(const uintptr_t *vftable) {
    if (!vftable || m_vindex == -1)
      return;
    m_cache = vftable;
    m_func.Initialize(vftable[m_vindex]);
  }

  inline bool Hook() { return m_func.Hook(GetName()); }
  inline void Unhook() { m_func.Unhook(); }

  inline const BinaryFunctionBase &Function() const { return m_func; }
  inline BinaryFunctionBase &Function() { return m_func; }

protected:
  BinaryFunctionBase m_func;
  const uintptr_t *m_cache = nullptr;
  i32 m_vindex = -1;
};

template<typename Fn>
class MetaBinaryVirtualFunctionImpl;

template<typename Ret, typename Class, typename ...Args>
class MetaBinaryVirtualFunctionImpl<Ret (Class::*)(Args...)>
  : public MetaBinaryVirtualFunction
  , public BinaryCallMixin<MetaBinaryVirtualFunctionImpl<Ret (Class::*)(Args...)>, Ret (*)(Class *, Args...)>
{
protected:
  using PFN_Call   = Ret (*)(Class *, Args...);
  using PFN_Member = Ret (Class::*)(Args...);

private:
  MetaBinaryVirtualFunctionImpl(const MetaBinaryVirtualFunctionImpl &) = delete;
  MetaBinaryVirtualFunctionImpl(MetaBinaryVirtualFunctionImpl &&) = delete;
  MetaBinaryVirtualFunctionImpl &operator=(const MetaBinaryVirtualFunctionImpl &) = delete;

public:
  MetaBinaryVirtualFunctionImpl(cstring name, PFN_Call detour = nullptr)
    : MetaBinaryVirtualFunction(name, reinterpret_cast<BinaryFunctionBase::FuncPtr>(detour))
  { }

  MetaBinaryVirtualFunctionImpl(cstring name, PFN_Member detour)
    : MetaBinaryVirtualFunction(name, reinterpret_cast<BinaryFunctionBase::MemberFuncPtr>(detour))
  { }

  // Implicit cast to ABI-compatible member function pointer.
  operator PFN_Member() const {
    return reinterpret_cast<PFN_Member>(AbiMemFuncPtr(m_func.Original()).Pointer());
  }
};

// ----------------------------------------------------------------------------
// [SECTION] MetaBinaryVariable
// ----------------------------------------------------------------------------

class MetaBinaryVariable: public MetaBinaryLister<MetaBinaryVariable> {
private:
  MetaBinaryVariable(const MetaBinaryVariable &) = delete;
  MetaBinaryVariable(MetaBinaryVariable &&) = delete;
  MetaBinaryVariable &operator=(const MetaBinaryVariable &) = delete;

protected:
  explicit MetaBinaryVariable(
    cstring name
  )
    : MetaBinaryLister<MetaBinaryVariable>(name)
  { }

public:
  inline bool IsInitialized() const { return m_address != 0xFFFFFFFFFFFFFFFFull; }
  inline void Initialize(uintptr_t address) { m_address = address; }
  inline uintptr_t Address() const { return m_address; }

protected:
  uintptr_t m_address = 0xFFFFFFFFFFFFFFFFull;
};

template<typename Type>
class MetaBinaryVariableGetter {
private:
  MetaBinaryVariableGetter(const MetaBinaryVariableGetter &) = delete;
  MetaBinaryVariableGetter(MetaBinaryVariableGetter &&) = delete;
  MetaBinaryVariableGetter &operator=(const MetaBinaryVariableGetter &) = delete;

public:
  explicit MetaBinaryVariableGetter(const MetaBinaryVariable *var): m_var(var) { }

  operator Type &() const {
    return *reinterpret_cast<Type *>(m_var->Address());
  }

private:
  const MetaBinaryVariable *m_var = nullptr;
};

class MetaBinaryMemberVariable: public MetaBinaryLister<MetaBinaryMemberVariable> {
private:
  MetaBinaryMemberVariable(const MetaBinaryMemberVariable &) = delete;
  MetaBinaryMemberVariable(MetaBinaryMemberVariable &&) = delete;
  MetaBinaryMemberVariable &operator=(const MetaBinaryMemberVariable &) = delete;

public:
  explicit MetaBinaryMemberVariable(
    cstring name
  )
    : MetaBinaryLister<MetaBinaryMemberVariable>(name)
  { }

public:
  inline bool IsInitialized() const { return m_initialized; }
  inline void Initialize(i32 offset) { m_offset = offset; m_initialized = true; }
  inline i32 Address() const { return m_offset; }

protected:
  bool m_initialized = false;
  i32 m_offset = 0;
};

template<typename Type, typename Class>
class MetaBinaryMemberVariableGetter {
protected:
  using P_Member = Type Class::*;

private:
  MetaBinaryMemberVariableGetter(const MetaBinaryMemberVariableGetter &) = delete;
  MetaBinaryMemberVariableGetter(MetaBinaryMemberVariableGetter &&) = delete;
  MetaBinaryMemberVariableGetter &operator=(const MetaBinaryMemberVariableGetter &) = delete;

public:
  explicit MetaBinaryMemberVariableGetter(const MetaBinaryMemberVariable *var): m_var(var) { }

  operator P_Member() const {
    return reinterpret_cast<P_Member>(AbiMemDataPtr(m_var->Address()).Pointer());
  }

private:
  const MetaBinaryMemberVariable *m_var = nullptr;
};

// ----------------------------------------------------------------------------
// [SECTION] MetaBinaryConstant
// ----------------------------------------------------------------------------

// MetaBinaryConstant directly saves and returns the value obtained by
// evaluating the descriptor.
class MetaBinaryConstant: public MetaBinaryLister<MetaBinaryConstant> {
private:
  MetaBinaryConstant(const MetaBinaryConstant &) = delete;
  MetaBinaryConstant(MetaBinaryConstant &&) = delete;
  MetaBinaryConstant &operator=(const MetaBinaryConstant &) = delete;

public:
  explicit MetaBinaryConstant(
    cstring name
  )
    : MetaBinaryLister<MetaBinaryConstant>(name)
  { }

public:
  inline bool IsInitialized() const { return m_initialized; }
  inline void Initialize(uintptr_t value) { m_value = value; m_initialized = true; }
  inline uintptr_t Value() const { return m_value; }

protected:
  bool m_initialized = false;
  uintptr_t m_value = 0;
};

// ----------------------------------------------------------------------------
// [SECTION] MetaBinaryHookRedirect
// ----------------------------------------------------------------------------

class MetaBinaryHookRedirect: public MetaBinaryLister<MetaBinaryHookRedirect> {
public:
  inline MetaBinaryHookRedirect(
    BinaryFunctionBase *func,
    uintptr_t detour
  )
    : MetaBinaryLister("")
    , m_func(func)
    , m_detour(detour)
  { }

  inline MetaBinaryHookRedirect(
    MetaBinaryFunction &mbf,
    uintptr_t detour
  ): MetaBinaryHookRedirect(
    &mbf.Function(),
    detour
  ) { }

  template<typename Class, typename Ret, typename ...Args>
  inline MetaBinaryHookRedirect(
    MetaBinaryFunction &mbf,
    Ret (Class::*detour)(Args...)
  ): MetaBinaryHookRedirect(
    &mbf.Function(),
    AbiMemFuncPtr(detour).GetAddress()
  ) { }

  inline MetaBinaryHookRedirect(
    MetaBinaryVirtualFunction &mbvf,
    uintptr_t detour
  ): MetaBinaryHookRedirect(
    &mbvf.Function(),
    detour
  ) { }

  template<typename Class, typename Ret, typename ...Args>
  inline MetaBinaryHookRedirect(
    MetaBinaryVirtualFunction &mbvf,
    Ret (Class::*detour)(Args...)
  ): MetaBinaryHookRedirect(
    &mbvf.Function(),
    AbiMemFuncPtr(detour).GetAddress()
  ) { }

  inline void Activate() {
    m_func->Redirect(reinterpret_cast<BinaryFunctionBase::FuncPtr>(m_detour));
  }

private:
  BinaryFunctionBase *m_func = nullptr;
  uintptr_t m_detour = 0;
};

// ----------------------------------------------------------------------------
// [SECTION] MetaBinarySystem
// ----------------------------------------------------------------------------

class MetaBinarySystem {
public:
  MetaBinarySystem() = default;
  ~MetaBinarySystem() = default;

  inline bool IsValid() const { return !m_hasError; }

  void Initialize(BinaryDescriptorBarn *binaryDescriptorBarn);
  void Terminate();

private:
  void m_CheckDescriptors(BinaryDescriptorBarn *binaryDescriptorBarn);
  void m_AssignDescriptors(BinaryDescriptorBarn *binaryDescriptorBarn);

  std::unordered_map<TgcString, MetaBinaryFunction *> m_functions = {};
  std::unordered_map<TgcString, MetaBinaryVirtualFunction *> m_virtualFunctions = {};
  std::unordered_map<TgcString, MetaBinaryVariable *> m_variables = {};
  std::unordered_map<TgcString, MetaBinaryMemberVariable *> m_memberVariables = {};
  std::unordered_map<TgcString, MetaBinaryConstant *> m_constants = {};

  bool m_hasError = false;
};

// ----------------------------------------------------------------------------
// [SECTION] Macros
// ----------------------------------------------------------------------------

// Note: Declare linking a member function via MetaBinary. Place this macro in
//       the class declaration. Afterward, it can be called just like a normal
//       member function.
//
// Example:
// ```
// class Game {
// public:
//   METABINARY_LINK_MFN(void, Game, Alloc, 0, (), ())
// };
//
// class Test {
// public:
//   METABINARY_LINK_MFN(void, Test, Func, 0, (int a1), (a1))
//   METABINARY_LINK_MFN(void, Test, Func, 1, (float a1), (a1))
// };
// ```
#define METABINARY_LINK_MFN(_return, _namespace, _name, _index, _params, _args) \
  inline static MetaBinaryFunctionImpl<_return (_namespace::*) _params> \
    __sfn_ ## _name ## _ ## _index ## __ { \
      #_namespace "::" #_name "$" #_index \
    }; \
  inline _return _name _params { \
    return (this ->* __sfn_ ## _name ## _ ## _index ## __) _args; \
  }

// Note: Declare linking a virtual function as an ordinary member function via
//       MetaBinary. Usage is similar to `METABINARY_LINK_MFN_DECL`.
#define METABINARY_LINK_MVFN(_return, _namespace, _name, _index, _params, _args) \
  inline static MetaBinaryVirtualFunctionImpl<_return (_namespace::*) _params> \
    __sfn_ ## _name ## _ ## _index ## __ { \
      #_namespace "::" #_name "$" #_index \
    }; \
  inline _return _name _params { \
    return (this ->* __sfn_ ## _name  ## _ ## _index ## __) _args; \
  }

// Note: Declare hooking a member (virtual) function via MetaBinary. Place this
//       macro in the cpp file. Implement the hook by declaring a subclass inheriting
//       from the linked class and defining the function overload.
//
// Example:
// ```
// class HOOK_MetaSystem: public MetaSystem {
// public:
//   void Initialize() {
//     // ...
//   }
// };
// METABINARY_HOOK_MFN(MetaSystem, Initialize, 0, &HOOK_MetaSystem::Initialize)
// ```
#define METABINARY_HOOK_MFN(_namespace, _name, _index, _detour) \
  static MetaBinaryHookRedirect hook_ ## _namespace ## _ ## _name ## _ ## _index = { \
    _namespace::__sfn_ ## _name  ## _ ## _index ## __, \
    _detour \
  };

// Note: Declare a global function linked via MetaBinary. This macro must be
//       placed within the namespace where the function's declaration resides;
//       afterward, it can be called just like a normal member function.
//
// Example:
// ```
// namespace Hook {
// METABINARY_LINK_FN(PCMetaClass, GetMetaClassById, 0, (u32 id), (id))
// }
// ```
#define METABINARY_LINK_FN(_return, _name, _index, _params, _args) \
  inline MetaBinaryFunctionImpl<_return (*) _params> \
    __sfn_ ## _name ## _ ## _index ## __ { \
      #_name "$" #_index \
    }; \
  inline _return _name _params { \
    return __sfn_ ## _name ## _ ## _index ## __ _args; \
  }

// Note: Define a hook function for a global function linked via MetaBinary. This
//       macro must be placed in the same namespace as the hooked function.
//
// Example:
// ```
// namespace Hook {
// METABINARY_HOOK_FN(PCMetaClass, GetMetaClassById, 0, (u32 id)) {
//   if (!GetOverride())
//     return GetMetaClassById(id);
//   return ::GetMetaClassById(id);
// }
// }
// ```
#define METABINARY_HOOK_FN(_return, _name, _index, _params) \
  inline _return __hfn ## _ ## _name ## _ ## _index ## __ _params;\
  inline MetaBinaryHookRedirect __hook_ ## _name ## _ ## _index = { \
    __sfn_ ## _name ## _ ## _index ## __, \
    reinterpret_cast<uintptr_t>(__hfn ## _ ## _name ## _ ## _index ## __) \
  };\
  inline _return __hfn ## _ ## _name ## _ ## _index ## __ _params

// Note: Get the symbol of a MetaBinary linked function.
//
// Example:
// ```
// METABINARY_FN(BaseHooks, GetMetaClassById, 0).Hook();
// ```
#define METABINARY_FN(_namespace, _name, _index) \
  (_namespace :: __sfn_ ## _name ## _ ## _index ## __)

// Note: Declare linking a member variable via MetaBinary. Place this macro in
//       the class declaration. Afterward, it can be referenced like a normal
//       member variable pointer.
//       After using the MetaBinary declaration, it is not allowed to declare
//       an ordinary member variable with the same name.
//
// Example:
// ```
// class Event {
// public:
//   METABINARY_LINK_MVAR(bool, Event, autoStart)
//
// private:
//   bool IsAutoStart() const {
//     return this->*autoStart;
//   }
// };
// ```
#define METABINARY_LINK_MVAR(_type, _namespace, _name) \
  inline static MetaBinaryMemberVariable \
    __var_ ## _name ## __ { \
      #_namespace "::" #_name \
    }; \
  inline static MetaBinaryMemberVariableGetter<_type, _namespace> _name { \
    &__var_ ## _name ## __ \
  };

// Note: Declare a global variable linked via MetaBinary. This macro must be
//       placed within the namespace where the variable's declaration resides;
//       afterward, it can be referenced just like a normal global variable.
//
// Example:
// ```
// namespace Sky {
// METABINARY_LINK_VAR(Renderer *, g_Renderer)
// }
//
// Renderer *GetRenderer() {
//   return Sky::g_Renderer;
// }
// ```
#define METABINARY_LINK_VAR(_type, _name) \
  inline MetaBinaryVariable __var_ ## _name ## __ { #_name }; \
  inline MetaBinaryVariableGetter<_type> _name { &__var_ ## _name ## __ };

// Note: Get the symbol of a MetaBinary linked (member)variable.
#define METABINARY_VAR(_namespace, _name) \
  (_namespace :: __var_ ## _name ## __)

// Note: Declare using MetaBinary to obtain an immediate value in the code
//       or directly write a constant.
//
// Example:
// ```
// METABINARY_LINK_IMM(sizeof_Texture)
// ```
#define METABINARY_LINK_IMM(_name) \
  inline MetaBinaryConstant __imm_ ## _name ## __ { #_name };

// Note: Declare using MetaBinary to obtain an immediate value in the code
//       or directly write a constant.
//
// Example:
// ```
// size_t GetSizeOfTexture() { 
//   return METABINARY_IMM(, sizeof_Texture);
// }
// ```
#define METABINARY_IMM(_namespace, _name) \
  (_namespace :: __imm_ ## _name ## __)

#endif
