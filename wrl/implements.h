//
// Copyright (C) Microsoft Corporation
// All rights reserved.
//
// Code in Details namespace is for internal usage within the library code
//
// Backport to Windows 7 SDK by Kurata Sayuri.

#ifndef _WRL_IMPLEMENTS_H_
#define _WRL_IMPLEMENTS_H_

#ifdef _MSC_VER
#pragma once
#endif  // _MSC_VER

#pragma region includes

#include <new.h>
#include <objbase.h>    // IMarshal
#include <cguid.h>      // CLSID_StdGlobalInterfaceTable
#include <intrin.h>

#include "wrl\def.h"
#include "wrl\client.h"

// Set packing
#include <pshpack8.h>

#pragma endregion

#pragma region disable warnings

#pragma warning(push)
#pragma warning(disable: 4584) // 'class1' : base-class 'class2' is already a base-class of 'class3'
#pragma warning(disable: 4481) // nonstandard extension used: override specifier 'override'

#pragma endregion // disable warnings

namespace Microsoft {
namespace WRL {

// Indicator for RuntimeClass,Implements and ChainInterfaces that T interface
// will be not accessible on IID list
// Example:
// struct MyRuntimeClass : RuntimeClass<CloakedIid<IMyCloakedInterface>> {}
template<typename T>
struct CloakedIid : T
{
};

enum RuntimeClassType
{
    ClassicCom              = 0x0002,
    InhibitWeakReference    = 0x0004,
};

template <unsigned int flags>
struct RuntimeClassFlags
{
};

namespace Details
{
// Empty struct used for validating template parameter types in Implements
struct ImplementsBase
{
};

} // namespace Details

// MixIn modifier allows to combine QI from
// a class that doesn't have default constructor on it
template<typename Derived, typename MixInType, bool hasImplements = __is_base_of(Details::ImplementsBase, MixInType)>
struct MixIn
{
};

namespace Details
{

#pragma region helper types
// Empty struct used as default template parameter
class Nil 
{
};

// Used on RuntimeClass to protect it from being constructed with new
class DontUseNewUseMake
{
private:
    void* operator new(size_t) throw()
    {
        __WRL_ASSERT__(false);
        return 0;
    }

public:
    void* operator new(size_t, _In_ void* placement) throw()
    {
        return placement;
    }
};

// RuntimeClassBase is used for detection of RuntimeClass in Make method, and provides helper methods for QI and getting IIDs 
class RuntimeClassBase
{
protected:
    template<typename T>
    static HRESULT AsIID(_In_ T* implements, REFIID riid, _Outptr_result_nullonfailure_ void **ppvObject) throw()
    {
        *ppvObject = nullptr;
#pragma warning(push)
// Conditional expression is constant
#pragma warning(disable: 4127)
// Potential comparison of a constant with another constant
#pragma warning(disable: 6326)
        // Prefer InlineIsEqualGUID over other forms since it's better perf on 4-byte aligned data, which is almost always the case.
        if (InlineIsEqualGUID(riid, __uuidof(IUnknown)))
#pragma warning(pop)
        {
            *ppvObject = implements->CastToUnknown();
            static_cast<IUnknown*>(*ppvObject)->AddRef();
            return S_OK;
        }
        
        HRESULT hr = implements->CanCastTo(riid, ppvObject);        
        if (SUCCEEDED(hr))
        {
            static_cast<IUnknown*>(*ppvObject)->AddRef();
        }

#pragma warning(suppress: 6102) // '*ppvObject' is used but may not be initialized
        _Analysis_assume_(SUCCEEDED(hr) || (*ppvObject == nullptr));

        return hr;
    }

public:
    HRESULT RuntimeClassInitialize() throw()
    {
        return S_OK;
    }
};

// Verifies that I is derived from specified base
template <typename I, bool doStrictCheck = true, bool isImplementsBased = __is_base_of(ImplementsBase, I)>
struct VerifyInterfaceHelper;

// Specialization for ClassicCom interface
template <typename I, bool doStrictCheck>
struct VerifyInterfaceHelper<I, doStrictCheck, false>
{
    static void Verify() throw()
    {
#ifdef __WRL_STRICT__
        // Make sure that your interfaces inherit from IUnknown and are not IUnknown based
        // The IUnknown is allowed only on RuntimeClass as first template parameter
        static_assert(__is_base_of(IUnknown, I) && !(doStrictCheck && IsSame<IUnknown, I>::value),
            "'I' has to derive from 'IUnknown'. 'I' must not be IUnknown.");
#else
        static_assert(__is_base_of(IUnknown, I), "'I' has to derive from 'IUnknown'.");
#endif
    }
};

// Specialization for Implements passed as template parameter
template <typename I>
struct VerifyInterfaceHelper<I, true, true>
{
    static void Verify() throw()
    {
    }
};

// Specialization for Implements passed as first template parameter
template <typename I>
struct VerifyInterfaceHelper<I, false, true>
{
    static void Verify() throw()
    {
#ifdef __WRL_STRICT__
        // Besides make sure that the first interface on Implements meet flags requirement
        VerifyInterfaceHelper<I::FirstInterface, false>::Verify();
#endif
    }
};

// Interface traits provides casting and filling iids methods helpers
template<typename I0>
struct __declspec(novtable) InterfaceTraits
{
    typedef I0 Base;    

    static void Verify() throw()
    {
        VerifyInterfaceHelper<Base>::Verify();
    }

    template<typename T>
    static Base* CastToBase(_In_ T* ptr) throw()
    {
        return static_cast<Base*>(ptr);
    }

    template<typename T>
    static IUnknown* CastToUnknown(_In_ T* ptr) throw()
    {
        return static_cast<IUnknown*>(static_cast<Base*>(ptr));
    }

    template <typename T>
    _Success_(return == true)
    static bool CanCastTo(_In_ T* ptr, REFIID riid, _Outptr_ void **ppv) throw()
    {
        // Prefer InlineIsEqualGUID over other forms since it's better perf on 4-byte aligned data, which is almost always the case.
        if (InlineIsEqualGUID(riid, __uuidof(Base)))
        {
            *ppv = static_cast<Base*>(ptr);
            return true;
        }

        return false;
    }
};

// Specialization of traits for cloaked interface
template<typename CloakedType>
struct __declspec(novtable) InterfaceTraits<CloakedIid<CloakedType>>
{
    typedef CloakedType Base;

    static void Verify() throw()
    {
        VerifyInterfaceHelper<Base>::Verify();
    }

    template<typename T>
    static Base* CastToBase(_In_ T* ptr) throw()
    {
        return static_cast<Base*>(ptr);
    }

    template<typename T>
    static IUnknown* CastToUnknown(_In_ T* ptr) throw()
    {
        return static_cast<IUnknown*>(static_cast<Base*>(ptr));
    }

    template <typename T>
    _Success_(return == true)
    static bool CanCastTo(_In_ T* ptr, REFIID riid, _Outptr_ void **ppv) throw()
    {
        // Prefer InlineIsEqualGUID over other forms since it's better perf on 4-byte aligned data, which is almost always the case.
        if (InlineIsEqualGUID(riid, __uuidof(Base)))
        {
            *ppv = static_cast<Base*>(ptr);
            return true;
        }

        return false;
    }
};

// Specialization for Nil parameter
template<>
struct __declspec(novtable) InterfaceTraits<Nil>
{
    typedef Nil Base;

    static void Verify() throw()
    {
    }

    template <typename T>
    _Success_(return == true)
    static bool CanCastTo(_In_ T*, REFIID, _Outptr_ void **) throw()
    {
        return false;
    }
};

// Verify inheritance
template <typename I, typename Base>
struct VerifyInheritanceHelper
{
    static void Verify() throw()
    {
        static_assert(Details::IsBaseOfStrict<typename InterfaceTraits<Base>::Base, typename InterfaceTraits<I>::Base>::value, "'I' needs to inherit from 'Base'.");
    }
};

template <typename I>
struct VerifyInheritanceHelper<I, Nil>
{
    static void Verify() throw()
    {
    }
};

#pragma endregion //  helper types

} // namespace Details

// ChainInterfaces - template allows specifying a derived COM interface along with its class hierarchy to allow QI for the base interfaces
template <typename I0, typename I1, typename I2 = Details::Nil, typename I3 = Details::Nil, 
        typename I4 = Details::Nil, typename I5 = Details::Nil, typename I6 = Details::Nil,
        typename I7 = Details::Nil, typename I8 = Details::Nil, typename I9 = Details::Nil>
struct ChainInterfaces : I0
{
protected:    
    static void Verify() throw()
    {
        Details::InterfaceTraits<I0>::Verify();
        Details::InterfaceTraits<I1>::Verify();
        Details::InterfaceTraits<I2>::Verify();
        Details::InterfaceTraits<I3>::Verify();
        Details::InterfaceTraits<I4>::Verify();
        Details::InterfaceTraits<I5>::Verify();
        Details::InterfaceTraits<I6>::Verify();
        Details::InterfaceTraits<I7>::Verify();
        Details::InterfaceTraits<I8>::Verify();
        Details::InterfaceTraits<I9>::Verify();

        Details::VerifyInheritanceHelper<I0, I1>::Verify();
        Details::VerifyInheritanceHelper<I0, I2>::Verify();
        Details::VerifyInheritanceHelper<I0, I3>::Verify();
        Details::VerifyInheritanceHelper<I0, I4>::Verify();
        Details::VerifyInheritanceHelper<I0, I5>::Verify();
        Details::VerifyInheritanceHelper<I0, I6>::Verify();
        Details::VerifyInheritanceHelper<I0, I7>::Verify();
        Details::VerifyInheritanceHelper<I0, I8>::Verify();
        Details::VerifyInheritanceHelper<I0, I9>::Verify();
    }

    HRESULT CanCastTo(REFIID riid, _Outptr_ void **ppv) throw()
    {
        typename Details::InterfaceTraits<I0>::Base* ptr = Details::InterfaceTraits<I0>::CastToBase(this);

        return (Details::InterfaceTraits<I0>::CanCastTo(this, riid, ppv) ||
            Details::InterfaceTraits<I1>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I2>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I3>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I4>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I5>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I6>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I7>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I8>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I9>::CanCastTo(ptr, riid, ppv)) ? S_OK : E_NOINTERFACE;
    }

    IUnknown* CastToUnknown() throw()
    {
        return Details::InterfaceTraits<I0>::CastToUnknown(this);
    }
};

template <typename DerivedType, typename BaseType, bool hasImplements, typename I1, typename I2, typename I3, 
        typename I4, typename I5, typename I6,
        typename I7, typename I8, typename I9>
struct ChainInterfaces<MixIn<DerivedType, BaseType, hasImplements>, I1, I2, I3, I4, I5, I6, I7, I8, I9>
{
    static_assert(!hasImplements, "Cannot use ChainInterfaces<MixIn<...>> to Mix a class implementing interfaces using \"Implements\"");

protected:    
    static void Verify() throw()
    {
        Details::InterfaceTraits<BaseType>::Verify();
        Details::InterfaceTraits<I1>::Verify();
        Details::InterfaceTraits<I2>::Verify();
        Details::InterfaceTraits<I3>::Verify();
        Details::InterfaceTraits<I4>::Verify();
        Details::InterfaceTraits<I5>::Verify();
        Details::InterfaceTraits<I6>::Verify();
        Details::InterfaceTraits<I7>::Verify();
        Details::InterfaceTraits<I8>::Verify();
        Details::InterfaceTraits<I9>::Verify();

        Details::VerifyInheritanceHelper<BaseType, I1>::Verify();
        Details::VerifyInheritanceHelper<BaseType, I2>::Verify();
        Details::VerifyInheritanceHelper<BaseType, I3>::Verify();
        Details::VerifyInheritanceHelper<BaseType, I4>::Verify();
        Details::VerifyInheritanceHelper<BaseType, I5>::Verify();
        Details::VerifyInheritanceHelper<BaseType, I6>::Verify();
        Details::VerifyInheritanceHelper<BaseType, I7>::Verify();
        Details::VerifyInheritanceHelper<BaseType, I8>::Verify();
        Details::VerifyInheritanceHelper<BaseType, I9>::Verify();
    }

    HRESULT CanCastTo(REFIID riid, _Outptr_ void **ppv) throw()
    {
        BaseType* ptr = static_cast<BaseType*>(static_cast<DerivedType*>(this));

        return (
            Details::InterfaceTraits<I1>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I2>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I3>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I4>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I5>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I6>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I7>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I8>::CanCastTo(ptr, riid, ppv) ||
            Details::InterfaceTraits<I9>::CanCastTo(ptr, riid, ppv)) ? S_OK : E_NOINTERFACE;
    }

    IUnknown* CastToUnknown() throw()
    {
        // It's not possible to cast to IUnknown when Base interface inherit more interfaces
        // The RuntimeClass is taking always the first interface as IUnknown thus it's required to
        // define your class as follows:
        // struct MyRuntimeClass : RuntimeClass<IInspectable, ChainInterfaces<MixIn<MyRuntimeClass,MyIndependentImplementation>, IFoo, IBar>, MyIndependentImplementation  {}
        static_assert(false, "Cannot cast 'MixInType' to IUnknown interface. Define IInspectable or IUnknown class before MixIn<Derived, MixInType> parameter.");        
        return nullptr;
    }
};

namespace Details
{

#pragma region Implements helper templates

// Helper template used by Implements. This template traverses a list of interfaces and adds them as base class and information
// to enable QI. doStrictCheck is typically false only for the first interface, allowing IInspectable to be explicitly specified 
// only as the first interface.
template <bool doStrictCheck, typename ...TInterfaces>
struct __declspec(novtable) ImplementsHelper;

template <typename T>
struct __declspec(novtable) ImplementsMarker
{};

template <typename I0, bool isImplements>
struct __declspec(novtable) MarkImplements;

template <typename I0>
struct __declspec(novtable) MarkImplements<I0, false>
{
    typedef I0 Type;
};

template <typename I0>
struct __declspec(novtable) MarkImplements<I0, true>
{
    typedef ImplementsMarker<I0> Type;
};

template <typename I0>
struct __declspec(novtable) MarkImplements<CloakedIid<I0>, true>
{
    // Cloaked Implements type will be handled in the nested processing.
    // Applying the ImplementsMarker too early will bypass Cloaked behavior.
    typedef CloakedIid<I0> Type;
};

template <typename DerivedType, typename BaseType, bool hasImplements>
struct __declspec(novtable) MarkImplements<MixIn<DerivedType, BaseType, hasImplements>, true>
{
    // Implements type in mix-ins will be handled in the nested processing.
    typedef MixIn<DerivedType, BaseType, hasImplements> Type;
};

// AdjustImplements pre-processes the type list for more efficient builds.
template <bool doStrictCheck, typename ...Bases>
struct __declspec(novtable) AdjustImplements;

template <bool doStrictCheck, typename I0, typename ...Bases>
struct __declspec(novtable) AdjustImplements<doStrictCheck, I0, Bases...>
{
    typedef ImplementsHelper<doStrictCheck, typename MarkImplements<I0, __is_base_of(ImplementsBase, I0)>::Type, Bases...> Type;
};

// Use AdjustImplements to remove instances of "Details::Nil" from the type list.
template <bool doStrictCheck, typename ...Bases>
struct __declspec(novtable) AdjustImplements<doStrictCheck, typename Details::Nil, Bases...>
{
    typedef typename AdjustImplements<doStrictCheck, Bases...>::Type Type;
};


template <bool doStrictCheck>
struct __declspec(novtable) AdjustImplements<doStrictCheck>
{
    typedef ImplementsHelper<doStrictCheck> Type;
};


// Specialization handles unadorned interfaces
template <bool doStrictCheck, typename I0, typename ...TInterfaces>
struct __declspec(novtable) ImplementsHelper<doStrictCheck, I0, TInterfaces...> :
    I0,
    AdjustImplements<true, TInterfaces...>::Type
{
    template <bool doStrictCheck, typename ...TInterfaces> friend struct ImplementsHelper;
    friend class RuntimeClassBase;

protected:

    HRESULT CanCastTo(REFIID riid, _Outptr_ void **ppv) throw()
    {
        VerifyInterfaceHelper<I0, doStrictCheck>::Verify();
        // Prefer InlineIsEqualGUID over other forms since it's better perf on 4-byte aligned data, which is almost always the case.
        if (InlineIsEqualGUID(riid, __uuidof(I0)))
        {
            *ppv = reinterpret_cast<I0*>(reinterpret_cast<void*>(this));
            return S_OK;
        }
        return AdjustImplements<true, TInterfaces...>::Type::CanCastTo(riid, ppv);
    }

    IUnknown* CastToUnknown() throw()
    {
        return reinterpret_cast<I0*>(reinterpret_cast<void*>(this));
    }
};


// Selector is used to "tag" base interfaces to be used in casting, since a runtime class may indirectly derive from 
// the same interface or Implements<> template multiple times
template <typename base, typename disciminator>
struct  __declspec(novtable) Selector : public base
{
};

// Specialization handles types that derive from ImplementsHelper (e.g. nested Implements).
template <bool doStrictCheck, typename I0, typename ...TInterfaces>
struct __declspec(novtable) ImplementsHelper<doStrictCheck, ImplementsMarker<I0>, TInterfaces...> :
    Selector<I0, ImplementsHelper<doStrictCheck, ImplementsMarker<I0>, TInterfaces...>>,
    Selector<typename AdjustImplements<true, TInterfaces...>::Type, ImplementsHelper<doStrictCheck, ImplementsMarker<I0>, TInterfaces...>>
{
    template <bool doStrictCheck, typename ...TInterfaces> friend struct ImplementsHelper;
    friend class RuntimeClassBase;

protected:
    typedef Selector<I0, ImplementsHelper<doStrictCheck, ImplementsMarker<I0>, TInterfaces...>> CurrentType;
    typedef Selector<typename AdjustImplements<true, TInterfaces...>::Type, ImplementsHelper<doStrictCheck, ImplementsMarker<I0>, TInterfaces...>> BaseType;

    HRESULT CanCastTo(REFIID riid, _Outptr_ void **ppv) throw()
    {
        VerifyInterfaceHelper<I0, doStrictCheck>::Verify();
        HRESULT hr = CurrentType::CanCastTo(riid, ppv);
        if (hr == E_NOINTERFACE)
        {
            hr = BaseType::CanCastTo(riid, ppv);
        }
        return hr;
    }

    IUnknown* CastToUnknown() throw()
    {
        // First in list wins.
        return CurrentType::CastToUnknown();
    }
};

// CloakedIid instance. Since the first "real" interface should be checked against doStrictCheck, 
// pass this through unchanged. Two specializations for cloaked prevent the need to use the Selector
// used in the Implements<> case. The same can't be done there because some type ambiguities are unavoidable.
template <bool doStrictCheck, typename I0, typename I1, typename ...TInterfaces>
struct __declspec(novtable) ImplementsHelper<doStrictCheck, CloakedIid<I0>, I1, TInterfaces...> :
    AdjustImplements<doStrictCheck, I0>::Type,
    AdjustImplements<true, I1, TInterfaces...>::Type
{
    template <bool doStrictCheck, typename ...TInterfaces> friend struct ImplementsHelper;
    friend class RuntimeClassBase;

protected:

    typedef typename AdjustImplements<doStrictCheck, I0>::Type CurrentType;
    typedef typename AdjustImplements<true, I1, TInterfaces...>::Type BaseType;

    HRESULT CanCastTo(REFIID riid, _Outptr_ void **ppv) throw()
    {
        VerifyInterfaceHelper<I0, doStrictCheck>::Verify();

        HRESULT hr = CurrentType::CanCastTo(riid, ppv);
        if (SUCCEEDED(hr))
        {
            return S_OK;
        }
        return BaseType::CanCastTo(riid, ppv);
    }

    IUnknown* CastToUnknown() throw()
    {
        return CurrentType::CastToUnknown();
    }
};

template <bool doStrictCheck, typename I0>
struct __declspec(novtable) ImplementsHelper<doStrictCheck, CloakedIid<I0>> :
    AdjustImplements<doStrictCheck, I0>::Type
{
    template <bool doStrictCheck, typename ...TInterfaces> friend struct ImplementsHelper;
    friend class RuntimeClassBase;

protected:

    typedef typename AdjustImplements<doStrictCheck, I0>::Type CurrentType;

    HRESULT CanCastTo(REFIID riid, _Outptr_ void **ppv) throw()
    {
        VerifyInterfaceHelper<I0, doStrictCheck>::Verify();

        return CurrentType::CanCastTo(riid, ppv);
    }

    IUnknown* CastToUnknown() throw()
    {
        return CurrentType::CastToUnknown();
    }
};


// terminal case specialization.
template <bool doStrictCheck>
struct __declspec(novtable) ImplementsHelper<doStrictCheck>
{
    template <bool doStrictCheck, typename ...TInterfaces> friend struct ImplementsHelper;

protected:
    friend class RuntimeClassBase;    

    HRESULT CanCastTo(_In_ REFIID /*riid*/, _Outptr_ void ** /*ppv*/) throw()
    {
        return E_NOINTERFACE;
    }

    // IUnknown* CastToUnknown() throw(); // not defined for terminal case.
};

// Specialization handles chaining interfaces
template <bool doStrictCheck, typename C0, typename C1, typename C2, typename C3, typename C4, typename C5, typename C6, typename C7, typename C8, typename C9, typename ...TInterfaces>
struct __declspec(novtable) ImplementsHelper<doStrictCheck, ChainInterfaces<C0, C1, C2, C3, C4, C5, C6, C7, C8, C9>, TInterfaces...> :
    ChainInterfaces<C0, C1, C2, C3, C4, C5, C6, C7, C8, C9>,
    AdjustImplements<true, TInterfaces...>::Type
{
    template <bool doStrictCheck, typename ...TInterfaces> friend struct ImplementsHelper;

protected:
    friend class RuntimeClassBase;    
    typedef typename AdjustImplements<true, TInterfaces...>::Type BaseType;

    HRESULT CanCastTo(REFIID riid, _Outptr_ void **ppv) throw()
    {
        ChainInterfaces<C0, C1, C2, C3, C4, C5, C6, C7, C8, C9>::Verify();
        
        HRESULT hr = ChainInterfaces<C0, C1, C2, C3, C4, C5, C6, C7, C8, C9>::CanCastTo(riid, ppv);
        if (FAILED(hr))
        {
            hr = BaseType::CanCastTo(riid, ppv);
        }

        return hr;
    }

    IUnknown* CastToUnknown() throw()
    {
        return ChainInterfaces<C0, C1, C2, C3, C4, C5, C6, C7, C8, C9>::CastToUnknown();
    }
};


// Mixin specialization
template <typename DerivedType, typename BaseMixInType, bool hasImplements, typename ...TInterfaces, bool doStrictCheck>
struct __declspec(novtable) ImplementsHelper<doStrictCheck, MixIn<DerivedType, BaseMixInType, hasImplements>, TInterfaces...> :
    AdjustImplements<true, TInterfaces...>::Type
{
    static_assert(hasImplements, "Cannot use MixIn to with a class not deriving from \"Implements\"");

    template <bool doStrictCheck, typename ...TInterfaces> friend struct ImplementsHelper;

protected:
    friend class RuntimeClassBase;
    typedef typename AdjustImplements<true, TInterfaces...>::Type BaseType;

    HRESULT CanCastTo(REFIID riid, _Outptr_ void **ppv) throw()
    {
        VerifyInterfaceHelper<BaseMixInType, doStrictCheck>::Verify();
        
        HRESULT hr = static_cast<BaseMixInType*>(static_cast<DerivedType*>(this))->CanCastTo(riid, ppv);
        if (FAILED(hr))
        {
            hr = BaseType::CanCastTo(riid, ppv);
        }

        return hr;            
    }

    IUnknown* CastToUnknown() throw()
    {
        return static_cast<BaseMixInType*>(static_cast<DerivedType*>(this))->CastToUnknown();
    }
};

#pragma endregion // Implements helper templates

} // namespace Details

// Implements - template implementing QI using the information provided through its template parameters
// Each template parameter has to be one of the following:
// * COM Interface
// * A class that implements one or more COM interfaces
// * ChainInterfaces template
template <typename I0, typename ...TInterfaces>
struct __declspec(novtable) Implements :
    Details::AdjustImplements<true, I0, TInterfaces...>::Type,
    Details::ImplementsBase
{
public:
    typedef I0 FirstInterface;
protected:

    typedef typename Details::AdjustImplements<true, I0, TInterfaces...>::Type BaseType;
    template <bool doStrictCheck, typename ...TInterfaces> friend struct Details::ImplementsHelper;
    friend class Details::RuntimeClassBase;    

    HRESULT CanCastTo(REFIID riid, _Outptr_ void **ppv) throw()
    {
        return BaseType::CanCastTo(riid, ppv);
    }

    IUnknown* CastToUnknown() throw()
    {
        return BaseType::CastToUnknown();
    }
};

class FtmBase : 
    public Implements< 
        ::Microsoft::WRL::CloakedIid< ::IMarshal> >
{
    // defining type 'Super' for other compilers since '__super' is a VC++-specific language extension
    using Super = Implements< 
      ::Microsoft::WRL::CloakedIid< ::IMarshal> >;
protected:
    template <bool doStrictCheck, typename ...TInterfaces> friend struct Details::ImplementsHelper;

public:
    FtmBase() throw()
    {
        ComPtr<IUnknown> unknown;
        if (SUCCEEDED(::CoCreateFreeThreadedMarshaler(nullptr, &unknown)))
        {
            unknown.As(&marshaller_);
        }
    }

    // IMarshal Methods
#pragma warning(suppress: 6101) // PREFast cannot see through the smart-pointer invocation
    STDMETHOD(GetUnmarshalClass)(_In_ REFIID riid,
                                   _In_opt_ void *pv,
                                   _In_ DWORD dwDestContext,
                                   _Reserved_ void *pvDestContext,
                                   _In_ DWORD mshlflags,
                                   _Out_ CLSID *pCid) override
    {
        if (marshaller_)
        {
            return marshaller_->GetUnmarshalClass(riid, pv, dwDestContext, pvDestContext, mshlflags, pCid);
        }
        return E_OUTOFMEMORY;
    }

#pragma warning(suppress: 6101) // PREFast cannot see through the smart-pointer invocation
    STDMETHOD(GetMarshalSizeMax)(_In_ REFIID riid, _In_opt_ void *pv, _In_ DWORD dwDestContext,
                                   _Reserved_ void *pvDestContext, _In_ DWORD mshlflags, _Out_ DWORD *pSize) override
    {
        if (marshaller_)
        {
            return marshaller_->GetMarshalSizeMax(riid, pv, dwDestContext, pvDestContext, mshlflags, pSize);
        }
        return E_OUTOFMEMORY;
    }

    STDMETHOD(MarshalInterface)(_In_ IStream *pStm, _In_ REFIID riid, _In_opt_ void *pv, _In_ DWORD dwDestContext,
                                  _Reserved_ void *pvDestContext, _In_ DWORD mshlflags) override
    {
        if (marshaller_)
        {
            return marshaller_->MarshalInterface(pStm, riid, pv, dwDestContext, pvDestContext, mshlflags);
        }
        return E_OUTOFMEMORY;
    }

#pragma warning(suppress: 6101) // PREFast cannot see through the smart-pointer invocation
    STDMETHOD(UnmarshalInterface)(_In_ IStream *pStm, _In_ REFIID riid, _Outptr_ void **ppv) override
    {
        if (marshaller_)
        {
            return marshaller_->UnmarshalInterface(pStm, riid, ppv);
        }
        return E_OUTOFMEMORY;
    }

    STDMETHOD(ReleaseMarshalData)(_In_ IStream *pStm) override
    {
        if (marshaller_)
        {
            return marshaller_->ReleaseMarshalData(pStm);
        }
        return E_OUTOFMEMORY;
    }

    STDMETHOD(DisconnectObject)(_In_ DWORD dwReserved) override
    {
        if (marshaller_)
        {
            return marshaller_->DisconnectObject(dwReserved);
        }
        return E_OUTOFMEMORY;
    }

    static HRESULT CreateGlobalInterfaceTable(_Out_ IGlobalInterfaceTable **git) throw()
    {
        *git = nullptr;
        return ::CoCreateInstance(CLSID_StdGlobalInterfaceTable,
            nullptr,
            CLSCTX_INPROC_SERVER,
            __uuidof(IGlobalInterfaceTable),
            reinterpret_cast<void**>(git));
    }

    ::Microsoft::WRL::ComPtr<IMarshal> marshaller_;  // Holds a reference to the free threaded marshaler
};

namespace Details
{

#ifdef _PERF_COUNTERS
class __declspec(novtable) PerfCountersBase
{
public:
    ULONG GetAddRefCount() throw()
    {
        return addRefCount_;
    }

    ULONG GetReleaseCount() throw()
    {
        return releaseCount_;
    }

    ULONG GetQueryInterfaceCount() throw()
    {
        return queryInterfaceCount_;
    }

    void ResetPerfCounters() throw()
    {
        addRefCount_ = 0;
        releaseCount_ = 0;
        queryInterfaceCount_ = 0;
    }

protected:
    PerfCountersBase() throw() : 
        addRefCount_(0),
        releaseCount_(0),
        queryInterfaceCount_(0)
    {
    }

    void IncrementAddRefCount() throw()
    {
        InterlockedIncrement(&addRefCount_);
    }

    void IncrementReleaseCount() throw()
    {
        InterlockedIncrement(&releaseCount_);
    }

    void IncrementQueryInterfaceCount() throw()
    {
        InterlockedIncrement(&queryInterfaceCount_);
    }

private:
    volatile unsigned long addRefCount_;
    volatile unsigned long releaseCount_;
    volatile unsigned long queryInterfaceCount_;
};
#endif

#if defined(_X86_) || defined(_AMD64_)

#define UnknownIncrementReference InterlockedIncrement
#define UnknownDecrementReference InterlockedDecrement
#define UnknownBarrierAfterInterlock() 

#elif defined(_ARM_)

#define UnknownIncrementReference InterlockedIncrementNoFence
#define UnknownDecrementReference InterlockedDecrementRelease
#define UnknownBarrierAfterInterlock() __dmb(_ARM_BARRIER_ISH)

#elif defined(_ARM64_)

#define UnknownIncrementReference InterlockedIncrementNoFence
#define UnknownDecrementReference InterlockedDecrementRelease
#define UnknownBarrierAfterInterlock() __dmb(_ARM64_BARRIER_ISH)

#else

#error Unsupported architecture.

#endif

#pragma warning(push)
// PREFast cannot see through template instantiation for AsIID() 
#pragma warning(disable: 6388)

template <typename ...TInterfaces>
class __declspec(novtable) RuntimeClassImpl :
    public Details::AdjustImplements<false, TInterfaces...>::Type,
    public RuntimeClassBase,
    public DontUseNewUseMake
#ifdef _PERF_COUNTERS
    , public PerfCountersBase
#endif
{
public:
    STDMETHOD(QueryInterface)(REFIID riid, _Outptr_result_nullonfailure_ void **ppvObject)
    {
#ifdef _PERF_COUNTERS
        IncrementQueryInterfaceCount();
#endif
        return Super::AsIID(this, riid, ppvObject);
    }

    STDMETHOD_(ULONG, AddRef)()
    {
        return InternalAddRef();
    }

    STDMETHOD_(ULONG, Release)()    
    {
        ULONG ref = InternalRelease();
        if (ref == 0)
        {
            delete this;
        }

        return ref;
    }

protected:
    using Super = RuntimeClassBase;

    RuntimeClassImpl() throw() : refcount_(1)
    {
    }    

    virtual ~RuntimeClassImpl() throw()
    {
        // Set refcount_ to -(LONG_MAX/2) to protect destruction and
        // also catch mismatched Release in debug builds
        refcount_ = -(LONG_MAX/2);
    }

    unsigned long InternalAddRef() throw()
    {
#ifdef _PERF_COUNTERS
        IncrementAddRefCount();
#endif
        return UnknownIncrementReference(&refcount_);
    }

    unsigned long InternalRelease() throw()
    {
#ifdef _PERF_COUNTERS
        IncrementReleaseCount();
#endif
        // A release fence is required to ensure all guarded memory accesses are
        // complete before any thread can begin destroying the object.
        unsigned long newValue = UnknownDecrementReference(&refcount_);
        if (newValue == 0)
        {
            // An acquire fence is required before object destruction to ensure
            // that the destructor cannot observe values changing on other threads.
            UnknownBarrierAfterInterlock();
        }
        return newValue;
    }

    unsigned long GetRefCount() const throw()
    {
        return refcount_;
    }

private:
    volatile long refcount_;
};

#pragma warning(pop) // C6388

} // namespace Details

// The RuntimeClass IUnknown methods
// It inherits from Details::RuntimeClass that provides helper methods for reference counting and
// collecting IIDs
template <typename ...TInterfaces>
class RuntimeClass;

template <unsigned int classFlags, typename ...TInterfaces>
class RuntimeClass<RuntimeClassFlags<classFlags>, TInterfaces...> :
    public Details::RuntimeClassImpl<TInterfaces...>
{
    RuntimeClass(const RuntimeClass&);    
    RuntimeClass& operator=(const RuntimeClass&);    
protected:
    HRESULT CustomQueryInterface(REFIID /*riid*/, _Outptr_result_nullonfailure_ void** /*ppvObject*/, _Out_ bool *handled)
    {
        *handled = false;
        return S_OK;
    }
public:
	RuntimeClass() = default;
    typedef RuntimeClass RuntimeClassT;
};

namespace Details
{
// Memory allocation for object that doesn't support weak references
// It only allocates memory
template<typename T>
class MakeAllocator
{
public:
    MakeAllocator() throw() : buffer_(nullptr)
    {
    }

    ~MakeAllocator() throw()
    {
        if (buffer_ != nullptr)
        {
            delete buffer_;
        }
    }

    void* Allocate() throw()
    {
        __WRL_ASSERT__(buffer_ == nullptr);
        // Allocate memory with operator new(size, nothrow) only
        // This will allow developer to override one operator only
        // to enable different memory allocation model
        buffer_ = (char*) (operator new (sizeof(T), std::nothrow));
        return buffer_;
    }

    void Detach() throw()
    {
        buffer_ = nullptr;
    }
private:
    char* buffer_;
};

} //Details

#pragma region make overloads

namespace Details {

// Make and MakeAndInitialize functions must not be marked as throw() as the constructor is allowed to throw exceptions.
template <typename T, typename ...TArgs>
ComPtr<T> Make(TArgs&&... args)
{
    static_assert(__is_base_of(RuntimeClassBase, T), "Make can only instantiate types that derive from RuntimeClass");
    ComPtr<T> object;
    Details::MakeAllocator<T> allocator;
    void *buffer = allocator.Allocate();
    if (buffer != nullptr)
    {
        auto ptr = new (buffer)T(Details::Forward<TArgs>(args)...);
        object.Attach(ptr);
        allocator.Detach();
    }
    return object;
}

#pragma warning(push)
#pragma warning(disable:6387 6388 28196) // PREFast does not understand call to ComPtr<T>::CopyTo() is safe here

template <typename T, typename I, typename ...TArgs>
HRESULT MakeAndInitialize(_Outptr_result_nullonfailure_ I** result, TArgs&&... args)
{
    static_assert(__is_base_of(RuntimeClassBase, T), "Make can only instantiate types that derive from RuntimeClass");
    static_assert(__is_base_of(I, T), "The 'T' runtime class doesn't implement 'I' interface");
    *result = nullptr;
    Details::MakeAllocator<T> allocator;
    void *buffer = allocator.Allocate();
    if (buffer == nullptr) { return E_OUTOFMEMORY; }
    auto ptr = new (buffer)T;
    ComPtr<T> object;
    object.Attach(ptr);
    allocator.Detach();
    HRESULT hr = object->RuntimeClassInitialize(Details::Forward<TArgs>(args)...);
    if (FAILED(hr)) { return hr; }
    return object.CopyTo(result);
}

#pragma warning(pop) // C6387 C6388 C28196

template <typename T, typename I, typename ...TArgs>
HRESULT MakeAndInitialize(_Inout_ ComPtrRef<ComPtr<I>> ppvObject, TArgs&&... args)
{    
    return MakeAndInitialize<T>(ppvObject.ReleaseAndGetAddressOf(), Details::Forward<TArgs>(args)...);
}

} //end of Details

using Details::MakeAndInitialize;
using Details::Make;

#pragma endregion // make overloads

#define MixInHelper() \
    public: \
        STDMETHOD(QueryInterface)(REFIID riid, _Outptr_result_nullonfailure_ void **ppvObject) \
        { \
            static_assert(__is_base_of(::Microsoft::WRL::Details::RuntimeClassBase, RuntimeClassT), "'MixInHelper' macro can only be used with ::Windows::WRL::RuntimeClass types"); \
            static_assert(!__is_base_of(IClassFactory, RuntimeClassT), "Incorrect usage of IClassFactory interface. Make sure that your RuntimeClass doesn't implement IClassFactory interface use ::Windows::WRL::ClassFactory instead or 'MixInHelper' macro is not used on ::Windows::WRL::ClassFactory"); \
            return RuntimeClassT::QueryInterface(riid, ppvObject); \
        } \
        STDMETHOD_(ULONG, Release)() \
        { \
            return RuntimeClassT::Release(); \
        } \
        STDMETHOD_(ULONG, AddRef)() \
        { \
            return RuntimeClassT::AddRef(); \
        } \
    private:

#undef UnknownIncrementReference
#undef UnknownDecrementReference
#undef UnknownBarrierAfterInterlock 

}}    // namespace Microsoft::WRL

#pragma warning(pop)

// Restore packing
#include <poppack.h>

#endif // _WRL_IMPLEMENTS_H_
