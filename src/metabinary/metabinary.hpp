#ifndef __API_MOD_ASSEMBLY_METABINARY_HH__
#define __API_MOD_ASSEMBLY_METABINARY_HH__

#include <type_traits>
#include <utility>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "utils/types.hpp"
#include "utils/abiMisc.hpp"
#include "utils/assert.hpp"
#include "metabinary/platform/lowLevel.hpp"
#include "metabinary/binaryDescriptor.hpp"

// ----------------------------------------------------------------------------
// [SECTION] PseudoClass
// ----------------------------------------------------------------------------

// A base class for creating hooks on class member functions. Hook classes can
// be created by inheriting from this class:
// 
// class hook_Game: public PseudoClass<hook_Game> {
// public:
//   void Initialize() { }
// };
//
// Subclasses can also be used as type placeholders, provided that the distinction
// between the detour and the original member function is clearly defined.
//
// This class is only used for hooking member functions where the object layout
// is incomplete or uncertain (in most cases). If a complete memory layout is
// available, you can create hooks by inheriting from a complete class and declaring
// functions in the derived class.
//
// Note that detours declared with this method can only be non-virtual functions.
// Passing a pointer to a virtual member function as the detour results in undefined
// behavior. Since MetaBinary erases the type traits of the detour function, it is
// possible to use member functions with entirely unrelated types as detours, as
// long as their addresses are valid (i.e., non-virtual).
template<typename Hook>
class PseudoClass {
  uintptr_t Cast() { return reinterpret_cast<uintptr_t>(static_cast<Hook>(this)); }
};

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
    const Derived *d = static_cast<const Derived *>(this);
    ASSERTMSG(d->Function().IsValid(),
      "Try to call uninitialized function %s", d->GetName());
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
    ASSERT(!IsValid());
    ASSERT(address);
    m_address = address;
  }

  // Terminate is safe to call even when Hook() was never invoked.
  inline void Terminate() {
    if (IsHookEnabled())
      Unhook();
    m_address = 0;
  }

  // Give a chance to redirect the detour function.
  inline void Redirect(FuncPtr detour) { ASSERT(!IsValid()); m_detour = reinterpret_cast<uintptr_t>(detour); }
  inline void Redirect(MemberFuncPtr detour) { ASSERT(!IsValid()); m_detour = detour == nullptr ? 0 : AbiMemFuncPtr(detour)->GetAddress(); }

  // Create and enable hook. Name is passed from the owning MetaObject.
  inline void Hook(cstring tag) {
    if (!Detour()) return;

    ASSERT(IsValid() && !IsHookEnabled());

    m_hook = CreateHook(tag, m_address, m_detour, &m_trampoline);
    ASSERT(m_hook);

    EnableHook(m_hook);

    m_hookEnabled = true;
  }

  // Remove hook.
  inline void Unhook() {
    if (!Detour()) return;

    ASSERT(IsValid() && IsHookEnabled());

    DisableHook(m_hook);
    DeleteHook(m_hook);

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
  inline void Hook() { m_func.Hook(GetName()); }
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
  inline void Initialize(i32 vindex) { ASSERT(m_vindex == -1); m_vindex = vindex; }
  inline void Terminate() { m_func.Terminate(); }

  // User must call this function to cache the vftable of any object.
  template<typename Class>
  inline void Cache(const Class *object) {
    static_assert(std::is_polymorphic<Class>::value,
      "MetaBinary: Object is not a polymorphic class.");
    ASSERT(object);
    Cache(*reinterpret_cast<const uintptr_t *const *>(object));
  }

  inline void Cache(const uintptr_t *vftable) {
    ASSERT(vftable && m_vindex != -1);
    m_cache = vftable;
    m_func.Initialize(vftable[m_vindex]);
  }

  inline void Hook() { m_func.Hook(GetName()); }
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

  void Initialize(BinaryDescriptorBarn *binaryDescriptorBarn);
  void Terminate();

  void HotLoad();

private:
  void m_LogDescriptors(BinaryDescriptorBarn *binaryDescriptorBarn);
  void m_AssignDescriptors(BinaryDescriptorBarn *binaryDescriptorBarn);

  std::unordered_map<TgcString, MetaBinaryFunction *> m_functions = {};
  std::unordered_map<TgcString, MetaBinaryVirtualFunction *> m_virtualFunctions = {};
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
//   FL_METABINARY_LINK_MFN_DECL(void, Game, Alloc, 0, ())
// };
//
// class Test {
// public:
//   FL_METABINARY_LINK_MFN_DECL(void, Test, Func, 0, (int))
//   FL_METABINARY_LINK_MFN_DECL(void, Test, Func, 1, (float))
// };
// ```
#define FL_METABINARY_LINK_MFN_DECL(_return, _namespace, _name, _index, _params) \
  static MetaBinaryFunctionImpl<_return (_namespace::*) _params> sfn_ ## _name ## _ ## _index; \
  _return _name _params;

// Note: Actually define the member function link declared in the class. Place
//       this macro in the cpp file. The parameter list and overload index must
//       match those written in the declaration macro.
//
// Example:
// ```
// FL_METABINARY_LINK_MFN_DEF(void, Game, Alloc, 0, (), ())
// FL_METABINARY_LINK_MFN_DEF(void, Test, Func, 0, (int i0), (i0))
// FL_METABINARY_LINK_MFN_DEF(void, Test, Func, 1, (float f0), (f0))
// ```
#define FL_METABINARY_LINK_MFN_DEF(_return, _namespace, _name, _index, _params, _args) \
  MetaBinaryFunctionImpl<_return (_namespace::*) _params> _namespace::sfn_ ## _name ## _ ## _index \
    (#_namespace "::" #_name "$" #_index);\
  _return _namespace::_name _params { \
    return (this ->* _namespace :: sfn_ ## _name ## _ ## _index) _args; \
  }

// Note: Declare linking a virtual function as an ordinary member function via
//       MetaBinary. Usage is similar to `FL_METABINARY_LINK_MFN_DECL`.
#define FL_METABINARY_LINK_MVFN_DECL(_return, _namespace, _name, _index, _params) \
  static MetaBinaryVirtualFunctionImpl<_return (_namespace::*) _params> sfn_ ## _name ## _ ## _index; \
  _return _name _params;

// Note: Define linking a virtual function as an ordinary member function via
//       MetaBinary. Usage is similar to `FL_METABINARY_LINK_MFN_DEF`.
#define FL_METABINARY_LINK_MVFN_DEF(_return, _namespace, _name, _index, _params, _args) \
  MetaBinaryVirtualFunctionImpl<_return (_namespace::*) _params> _namespace::sfn_ ## _name  ## _ ## _index \
    (#_namespace "::" #_name "$" #_index); \
  _return _namespace::_name _params { \
    return (this ->* _namespace::sfn_ ## _name  ## _ ## _index) _args; \
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
// FL_METABINARY_HOOK_MFN(MetaSystem, Initialize, 0, &HOOK_MetaSystem::Initialize)
// ```
#define FL_METABINARY_HOOK_MFN(_namespace, _name, _index, _detour) \
  static MetaBinaryHookRedirect hook_ ## _namespace ## _ ## _name ## _ ## _index = { \
    _namespace::sfn_ ## _name  ## _ ## _index, \
    _detour \
  };

// Note: Define a global function linked via MetaBinary. This macro must be
//       placed within the namespace where the function's declaration resides;
//       afterward, it can be called just like a normal member function.
//
// Example:
// ```
// namespace Hook { PCMetaClass GetMetaClassById(u32 id); }
// namespace Hook {
// FL_METABINARY_LINK_FN_DEF(PCMetaClass, GetMetaClassById, 0, (u32 id), (id))
// }
// ```
#define FL_METABINARY_LINK_FN_DEF(_return, _name, _index, _params, _args) \
  static MetaBinaryFunctionImpl<decltype(&_name)> sfn_ ## _name ## _ ## _index \
    (#_name "$" #_index);\
  _return _name _params {\
    return sfn_ ## _name ## _ ## _index _args; \
  }

// Note: Define a hook function for a global function linked via MetaBinary. This
//       macro must be placed in the same namespace as the hooked function.
//
// Example:
// ```
// namespace Hook {
// FL_METABINARY_HOOK_FN(PCMetaClass, GetMetaClassById, 0, (u32 id)) {
//   if (!GetOverride())
//     return GetMetaClassById(id);
//   return ::GetMetaClassById(id);
// }
// }
// ```
#define FL_METABINARY_HOOK_FN(_return, _name, _index, _params) \
  static _return hfn ## _ ## _name ## _ ## _index _params;\
  static MetaBinaryHookRedirect hook_ ## _name ## _ ## _index = { \
    sfn_ ## _name ## _ ## _index, \
    reinterpret_cast<uintptr_t>(hfn ## _ ## _name ## _ ## _index) \
  };\
  static _return hfn ## _ ## _name ## _ ## _index _params

// Note: Get the symbol of a MetaBinary linked function.
//
// Example:
// FL_METABINARY_FN(BaseHooks, GetMetaClassById, 0).Hook();
#define FL_METABINARY_FN(_namespace, _name, _index) \
  _namespace :: sfn_ ## _name ## _ ## _index

#endif
