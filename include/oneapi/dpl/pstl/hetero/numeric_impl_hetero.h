// -*- C++ -*-
//===-- numeric_impl_hetero.h ---------------------------------------------===//
//
// Copyright (C) Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// This file incorporates work covered by the following copyright and permission
// notice:
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#ifndef _ONEDPL_NUMERIC_IMPL_HETERO_H
#define _ONEDPL_NUMERIC_IMPL_HETERO_H

#include <iterator>
#include "../parallel_backend.h"
#if _ONEDPL_BACKEND_SYCL
#    include "dpcpp/execution_sycl_defs.h"
#    include "algorithm_impl_hetero.h" // to use __pattern_walk2_brick
#    include "dpcpp/parallel_backend_sycl_utils.h"
#    include "dpcpp/unseq_backend_sycl.h"
#endif

namespace oneapi
{
namespace dpl
{
namespace __par_backend_hetero
{

template <typename... _Name>
class __scan_single_wg_kernel;

template <typename... _Name>
class __scan_single_wg_dynamic_kernel;

} // namespace __par_backend_hetero
namespace __internal
{

//------------------------------------------------------------------------
// transform_reduce (version with two binary functions)
//------------------------------------------------------------------------

template <typename _ExecutionPolicy, typename _RandomAccessIterator1, typename _RandomAccessIterator2, typename _Tp,
          typename _BinaryOperation1, typename _BinaryOperation2>
oneapi::dpl::__internal::__enable_if_hetero_execution_policy<_ExecutionPolicy, _Tp>
__pattern_transform_reduce(_ExecutionPolicy&& __exec, _RandomAccessIterator1 __first1, _RandomAccessIterator1 __last1,
                           _RandomAccessIterator2 __first2, _Tp __init, _BinaryOperation1 __binary_op1,
                           _BinaryOperation2 __binary_op2, /*vector=*/::std::true_type, /*parallel=*/::std::true_type)
{
    if (__first1 == __last1)
        return __init;

    using _Policy = _ExecutionPolicy;
    using _Functor = unseq_backend::walk_n<_Policy, _BinaryOperation2>;
    using _RepackedTp = __par_backend_hetero::__repacked_tuple_t<_Tp>;
    using _NoOpFunctor = unseq_backend::walk_n<_ExecutionPolicy, oneapi::dpl::__internal::__no_op>;

    auto __n = __last1 - __first1;
    auto __keep1 =
        oneapi::dpl::__ranges::__get_sycl_range<__par_backend_hetero::access_mode::read, _RandomAccessIterator1>();
    auto __buf1 = __keep1(__first1, __last1);
    auto __keep2 =
        oneapi::dpl::__ranges::__get_sycl_range<__par_backend_hetero::access_mode::read, _RandomAccessIterator2>();
    auto __buf2 = __keep2(__first2, __first2 + __n);

    auto __res =
        oneapi::dpl::__par_backend_hetero::__parallel_transform_reduce<_RepackedTp>(
            ::std::forward<_ExecutionPolicy>(__exec),
            unseq_backend::transform_init<_Policy, _BinaryOperation1, _Functor>{__binary_op1,
                                                                                _Functor{__binary_op2}}, // transform
            unseq_backend::transform_init<_Policy, _BinaryOperation1, _NoOpFunctor>{__binary_op1, _NoOpFunctor{}},
            unseq_backend::reduce<_Policy, _BinaryOperation1, _RepackedTp>{__binary_op1}, // reduce
            unseq_backend::__init_value<_RepackedTp>{__init},                             //initial value
            __buf1.all_view(), __buf2.all_view())
            .get();

    return __res;
}

//------------------------------------------------------------------------
// transform_reduce (with unary and binary functions)
//------------------------------------------------------------------------

template <typename _ExecutionPolicy, typename _ForwardIterator, typename _Tp, typename _BinaryOperation,
          typename _UnaryOperation>
oneapi::dpl::__internal::__enable_if_hetero_execution_policy<_ExecutionPolicy, _Tp>
__pattern_transform_reduce(_ExecutionPolicy&& __exec, _ForwardIterator __first, _ForwardIterator __last, _Tp __init,
                           _BinaryOperation __binary_op, _UnaryOperation __unary_op, /*vector=*/::std::true_type,
                           /*parallel=*/::std::true_type)
{
    if (__first == __last)
        return __init;

    using _Policy = _ExecutionPolicy;
    using _Functor = unseq_backend::walk_n<_Policy, _UnaryOperation>;
    using _RepackedTp = __par_backend_hetero::__repacked_tuple_t<_Tp>;
    using _NoOpFunctor = unseq_backend::walk_n<_ExecutionPolicy, oneapi::dpl::__internal::__no_op>;

    auto __keep = oneapi::dpl::__ranges::__get_sycl_range<__par_backend_hetero::access_mode::read, _ForwardIterator>();
    auto __buf = __keep(__first, __last);
    auto __res =
        oneapi::dpl::__par_backend_hetero::__parallel_transform_reduce<_RepackedTp>(
            ::std::forward<_ExecutionPolicy>(__exec),
            unseq_backend::transform_init<_Policy, _BinaryOperation, _Functor>{__binary_op,
                                                                               _Functor{__unary_op}}, // transform
            unseq_backend::transform_init<_Policy, _BinaryOperation, _NoOpFunctor>{__binary_op, _NoOpFunctor{}},
            unseq_backend::reduce<_Policy, _BinaryOperation, _RepackedTp>{__binary_op}, // reduce
            unseq_backend::__init_value<_RepackedTp>{__init},                           //initial value
            __buf.all_view())
            .get();

    return __res;
}

template< bool _Inclusive>
struct __single_group_scan
{
    template<typename _Policy, typename _InRng, typename _OutRng, typename _InitType, typename _BinaryOperation, typename _UnaryOp>
    static void __launch_dynamic_bounds_scan(const _Policy& __policy, _InRng __in, _OutRng __out, ::std::size_t __n, _InitType __init, _BinaryOperation __bin_op, _UnaryOp __unary_op, ::std::uint16_t __wg_size)
    {
        using _ValueType = typename _InitType::__value_type;
        using _CustomName = typename _Policy::kernel_name;
        using _GroupScanKernel =
                     oneapi::dpl::__par_backend_hetero::__internal::__kernel_name_generator<__par_backend_hetero::__scan_single_wg_dynamic_kernel, _CustomName, _BinaryOperation, _InRng, _OutRng>;

        const ::std::uint16_t __elems_per_item = __par_backend_hetero::__ceiling_div(__n, __wg_size);
        const ::std::uint16_t __elems_per_wg = __elems_per_item * __wg_size;

        auto __event = __policy.queue().submit([&](sycl::handler& __hdl) {
            oneapi::dpl::__ranges::__require_access(__hdl, __in, __out);

            auto __lacc =  __dpl_sycl::__local_accessor<_ValueType>(sycl::range<1>{__elems_per_wg}, __hdl);

            __hdl.parallel_for<_GroupScanKernel>(sycl::nd_range<1>(__wg_size, __wg_size), [=](sycl::nd_item<1> __self_item) {
                const auto& __group = __self_item.get_group();
                // This kernel is only launched for sizes less than 2^16
                const ::std::uint16_t __item_id = __self_item.get_local_linear_id();

                for (::std::uint16_t __idx = __item_id; __idx < __n; __idx += __wg_size)
                {
                   __lacc[__idx] = __unary_op(__in[__idx]);
                }

                __group_scan<_ValueType>(__group, __lacc.get_pointer(), __lacc.get_pointer() + __n, __bin_op, __init);

                for (::std::uint16_t __idx = __item_id; __idx < __n; __idx += __wg_size)
                {
                   __out[__idx] = __lacc[__idx];
                }

                const ::std::uint16_t __residual = __n % __wg_size;
                const ::std::uint16_t __residual_start = __n - __residual;
                if (__residual > 0 && __item_id < __residual)
                {
                    auto __idx = __residual_start + __item_id;
                   __out[__idx] = __lacc[__idx];
                }
            });
        });
        __event.wait();
    }

    template<::std::uint16_t _ElemsPerItem, ::std::uint16_t _WGSize, bool _IsFullGroup, typename _Policy, typename _InRng, typename _OutRng, typename _InitType, typename _BinaryOperation, typename _UnaryOp>
    static void __launch_static_bounds_scan(_Policy const & __policy, _InRng&& __in, _OutRng&& __out, std::size_t __n, _InitType __init, _BinaryOperation __bin_op, _UnaryOp __unary_op)
    {
        using _ValueType = typename _InitType::__value_type;
        using _CustomName = typename _Policy::kernel_name;
        using _GroupScanKernel =
                     oneapi::dpl::__par_backend_hetero::__internal::__kernel_name_generator<__par_backend_hetero::__scan_single_wg_kernel, _CustomName, _BinaryOperation, std::integral_constant<bool, _Inclusive>, std::integral_constant<bool, _IsFullGroup>, _InRng, _OutRng, std::integral_constant<::std::size_t, _ElemsPerItem>, std::integral_constant<std::size_t, _WGSize>>;

        constexpr ::uint32_t __elems_per_wg = _ElemsPerItem * _WGSize;

        auto __event = __policy.queue().submit([&](sycl::handler& __hdl) {
            oneapi::dpl::__ranges::__require_access(__hdl, __in, __out);

            auto __lacc = __dpl_sycl::__local_accessor<_ValueType>(sycl::range<1>{__elems_per_wg}, __hdl);

            __hdl.parallel_for<__par_backend_hetero::__i_kernel_name<_GroupScanKernel, __elems_per_wg, _IsFullGroup, _Inclusive>>(sycl::nd_range<1>(_WGSize, _WGSize), [=](sycl::nd_item<1> __self_item) {
                const auto& __group = __self_item.get_group();
                const auto& __subgroup = __self_item.get_sub_group();
                // This kernel is only launched for sizes less than 2^16
                const ::std::uint16_t __item_id = __self_item.get_local_linear_id();
                const ::std::uint16_t __subgroup_id = __subgroup.get_group_id();
                const ::std::uint16_t __id_in_subgroup = __subgroup.get_local_id();
                const ::std::uint16_t __subgroup_size = __subgroup.get_local_linear_range();

                if constexpr (_IsFullGroup)
                {
                    _ONEDPL_PRAGMA_UNROLL
                    for (::std::uint16_t __i = 0; __i < _ElemsPerItem; ++__i)
                    {
                       auto __idx = __i * _WGSize + __subgroup_id * __subgroup_size;
                       auto __val = __unary_op(__subgroup.load(__in.begin() + __idx));
                       __subgroup.store(__lacc.get_pointer() + __idx, __val);
                    }
                }
                else
                {
                    _ONEDPL_PRAGMA_UNROLL
                    for (::std::uint16_t __idx = __item_id; __idx < __n; __idx += _WGSize)
                    {
                       __lacc[__idx] = __unary_op(__in[__idx]);
                    }
                }

                __group_scan<_ValueType>(__group, __lacc.get_pointer(), __lacc.get_pointer() + __n, __bin_op, __init);

                if constexpr (_IsFullGroup)
                {
                    _ONEDPL_PRAGMA_UNROLL
                    for (::std::uint16_t __i = 0; __i < _ElemsPerItem; ++__i)
                    {
                       auto __idx = __i * _WGSize + __subgroup_id * __subgroup_size;
                       auto __val = __subgroup.load(__lacc.get_pointer() + __idx);
                       __subgroup.store(__out.begin() + __idx, __val);
                    }
                }
                else
                {
                    _ONEDPL_PRAGMA_UNROLL
                    for (::std::uint16_t __idx = __item_id; __idx < __n; __idx += _WGSize)
                    {
                       __out[__idx] = __lacc[__idx];
                    }

                    const ::std::uint16_t __residual = __n % _WGSize;
                    const ::std::uint16_t __residual_start = __n - __residual;
                    if (__item_id < __residual)
                    {
                        auto __idx = __residual_start + __item_id;
                       __out[__idx] = __lacc[__idx];
                    }
                }
            });
        });
        __event.wait();
    }

    template<typename _ValueType, typename _Group, typename _Begin, typename _End, typename _BinaryOperation>
    static void __group_scan(const _Group& __group, _Begin __begin, _End __end, _BinaryOperation __bin_op, unseq_backend::__no_init_value<_ValueType>)
    {
        if constexpr (_Inclusive)
            __dpl_sycl::__joint_inclusive_scan(__group, __begin, __end, __begin, __bin_op);
        else
            __dpl_sycl::__joint_exclusive_scan(__group, __begin, __end, __begin, __bin_op);
    }

    template<typename _ValueType, typename _Group, typename _Begin, typename _End, typename _BinaryOperation>
    static void __group_scan(const _Group& __group, _Begin __begin, _End __end, _BinaryOperation __bin_op, unseq_backend::__init_value<_ValueType> __init)
    {
        if constexpr (_Inclusive)
            __dpl_sycl::__joint_inclusive_scan(__group, __begin, __end, __begin, __bin_op, __init.__value);
        else
            __dpl_sycl::__joint_exclusive_scan(__group, __begin, __end, __begin, __init.__value, __bin_op);
    }
};


//------------------------------------------------------------------------
// transform_scan
//------------------------------------------------------------------------
template <typename T>
struct ExecutionPolicyWrapper;

template <typename _ExecutionPolicy, typename _InRng, typename _OutRng, typename _UnaryOperation,
          typename _InitType, typename _BinaryOperation, typename _Inclusive>
void
__pattern_transform_scan_single_group(_ExecutionPolicy&& __exec, _InRng&& __in_rng, _OutRng __out_rng, ::std::size_t __n,
                              _UnaryOperation __unary_op, _InitType __init, _BinaryOperation __binary_op, _Inclusive)
{

    ::std::size_t __max_wg_size = oneapi::dpl::__internal::__max_work_group_size(__exec);

    // Specialization for devices that have a max work-group size of 1024
    constexpr ::std::uint16_t __targeted_wg_size = 1024;

    if (__max_wg_size >= __targeted_wg_size)
    {
        auto __single_group_scan_f = [&](auto __size_constant) {
            constexpr ::std::uint16_t __size = decltype(__size_constant)::value;
            constexpr ::std::uint16_t __wg_size = std::min(__size, __targeted_wg_size);
            constexpr ::std::uint16_t __num_elems_per_item = __par_backend_hetero::__ceiling_div(__size, __wg_size);
            const bool __is_full_group = __n == __wg_size;

            if (__is_full_group)
                __single_group_scan<_Inclusive::value>::template __launch_static_bounds_scan<__num_elems_per_item, __wg_size, true>(__exec, __in_rng.all_view(), __out_rng.all_view(), __n, __init, __binary_op, __unary_op);
            else
                __single_group_scan<_Inclusive::value>::template __launch_static_bounds_scan<__num_elems_per_item, __wg_size, false>(__exec, __in_rng.all_view(), __out_rng.all_view(), __n, __init, __binary_op, __unary_op);
        };
        if (__n <= 16)
            __single_group_scan_f(std::integral_constant<::std::uint16_t, 16>{});
        else if (__n <= 32)
            __single_group_scan_f(std::integral_constant<::std::uint16_t, 32>{});
        else if (__n <= 64)
            __single_group_scan_f(std::integral_constant<::std::uint16_t, 64>{});
        else if (__n <= 128)
            __single_group_scan_f(std::integral_constant<::std::uint16_t, 128>{});
        else if (__n <= 256)
            __single_group_scan_f(std::integral_constant<::std::uint16_t, 256>{});
        else if (__n <= 512)
            __single_group_scan_f(std::integral_constant<::std::uint16_t, 512>{});
        else if (__n <= 1024)
            __single_group_scan_f(std::integral_constant<::std::uint16_t, 1024>{});
        else if (__n <= 2048)
            __single_group_scan_f(std::integral_constant<::std::uint16_t, 2048>{});
        else if (__n <= 4096)
            __single_group_scan_f(std::integral_constant<::std::uint16_t, 4096>{});
        else if (__n <= 8192)
            __single_group_scan_f(std::integral_constant<::std::uint16_t, 8192>{});
        else
            __single_group_scan_f(std::integral_constant<::std::uint16_t, 16384>{});
    }
    else
    {
        __single_group_scan<_Inclusive::value>::__launch_dynamic_bounds_scan(__exec, __in_rng.all_view(), __out_rng.all_view(), __n, __init, __binary_op, __unary_op, __max_wg_size);
    }
}

template <typename _ExecutionPolicy, typename _InRng, typename _OutRng, typename _UnaryOperation,
          typename _InitType, typename _BinaryOperation, typename _Inclusive>
void
__pattern_transform_scan_multi_group(_ExecutionPolicy&& __exec, _InRng&& __in_rng, _OutRng __out_rng,
                              _UnaryOperation __unary_op, _InitType __init, _BinaryOperation __binary_op, _Inclusive)
{
    using _Type = typename _InitType::__value_type;
    using _Assigner = unseq_backend::__scan_assigner;
    using _NoAssign = unseq_backend::__scan_no_assign;
    using _UnaryFunctor = unseq_backend::walk_n<_ExecutionPolicy, _UnaryOperation>;
    using _NoOpFunctor = unseq_backend::walk_n<_ExecutionPolicy, oneapi::dpl::__internal::__no_op>;

    _Assigner __assign_op;
    _NoAssign __no_assign_op;
    _NoOpFunctor __get_data_op;

    oneapi::dpl::__par_backend_hetero::__parallel_transform_scan(
        ::std::forward<_ExecutionPolicy>(__exec), __in_rng.all_view(), __out_rng.all_view(), __binary_op, __init,
        // local scan
        unseq_backend::__scan<_Inclusive, _ExecutionPolicy, _BinaryOperation, _UnaryFunctor, _Assigner, _Assigner,
                              _NoOpFunctor, _InitType>{__binary_op, _UnaryFunctor{__unary_op}, __assign_op, __assign_op,
                                                       __get_data_op},
        // scan between groups
        unseq_backend::__scan</*inclusive=*/::std::true_type, _ExecutionPolicy, _BinaryOperation, _NoOpFunctor,
                              _NoAssign, _Assigner, _NoOpFunctor, unseq_backend::__no_init_value<_Type>>{
            __binary_op, _NoOpFunctor{}, __no_assign_op, __assign_op, __get_data_op},
        // global scan
        unseq_backend::__global_scan_functor<_Inclusive, _BinaryOperation, _InitType>{__binary_op, __init})
        .wait();
}

template <typename _ExecutionPolicy, typename _Iterator1, typename _Iterator2, typename _UnaryOperation,
          typename _InitType, typename _BinaryOperation, typename _Inclusive>
void
__pattern_transform_scan_base_impl(_ExecutionPolicy&& __exec, _Iterator1 __first, _Iterator1 __last,
                                   _Iterator2 __result, _UnaryOperation __unary_op, _InitType __init,
                                   _BinaryOperation __binary_op, _Inclusive)
{
    const auto __n = __last - __first;
    assert(__n > 0);

    using _Type = typename _InitType::__value_type;

    auto __keep1 = oneapi::dpl::__ranges::__get_sycl_range<__par_backend_hetero::access_mode::read, _Iterator1>();
    auto __buf1 = __keep1(__first, __last);
    auto __keep2 = oneapi::dpl::__ranges::__get_sycl_range<__par_backend_hetero::access_mode::write, _Iterator2>();
    auto __buf2 = __keep2(__result, __result + __n);

    constexpr int __single_group_upper_limit = 16384;

    const auto __max_slm_size = __exec.queue().get_device().template get_info<sycl::info::device::local_mem_size>();
    const auto __n_uniform = 1 << (::std::uint32_t(log2(__n - 1)) + 1);
    const auto __req_slm_size = sizeof(_Type) * __n_uniform;

    if (__n <= __single_group_upper_limit && __max_slm_size >= __req_slm_size)
    {
        constexpr bool __can_use_group_scan = unseq_backend::__has_known_identity<_BinaryOperation, _Type>::value;
        if constexpr (__can_use_group_scan)
        {
            __pattern_transform_scan_single_group(std::forward<_ExecutionPolicy>(__exec), __buf1, __buf2, __n, __unary_op, __init, __binary_op, _Inclusive{});
        }
        else
        {
            __pattern_transform_scan_multi_group(std::forward<_ExecutionPolicy>(__exec), __buf1, __buf2, __unary_op, __init, __binary_op, _Inclusive{});
        }
    }
    else
    {
        __pattern_transform_scan_multi_group(std::forward<_ExecutionPolicy>(__exec), __buf1, __buf2, __unary_op, __init, __binary_op, _Inclusive{});
    }
}

template <typename _Iterator1, typename _Iterator2>
constexpr bool
__check_equal_iterators(_Iterator1 __it1, _Iterator2 __it2)
{
    // In-place exclusive scan works correctly only if an input and an output iterators are the same type.
    // Otherwise, there is no way to check an in-place case and a workaround below is not applied.
    if constexpr (::std::is_same_v<::std::decay_t<_Iterator1>, ::std::decay_t<_Iterator2>>)
    {
        return __it1 == __it2;
    }

    return false;
}

template <typename _ExecutionPolicy, typename _Iterator1, typename _Iterator2, typename _UnaryOperation,
          typename _InitType, typename _BinaryOperation, typename _Inclusive>
oneapi::dpl::__internal::__enable_if_hetero_execution_policy<_ExecutionPolicy, _Iterator2>
__pattern_transform_scan_base(_ExecutionPolicy&& __exec, _Iterator1 __first, _Iterator1 __last, _Iterator2 __result,
                              _UnaryOperation __unary_op, _InitType __init, _BinaryOperation __binary_op, _Inclusive)
{
    if (__first == __last)
        return __result;

    const auto __n = __last - __first;

    // This is a temporary workaround for an in-place exclusive scan while the SYCL backend scan pattern is not fixed.
    const bool __is_scan_inplace_exclusive = __n > 1 && !_Inclusive{} && __check_equal_iterators(__first, __result);
    if (!__is_scan_inplace_exclusive)
    {
        __pattern_transform_scan_base_impl(__exec, __first, __last, __result, __unary_op, __init, __binary_op,
                                           _Inclusive{});
    }
    else
    {
        assert(__n > 1);
        assert(!_Inclusive{});
        assert(__check_equal_iterators(__first, __result));

        using _Type = typename _InitType::__value_type;

        auto __policy =
            __par_backend_hetero::make_wrapped_policy<ExecutionPolicyWrapper>(::std::forward<_ExecutionPolicy>(__exec));
        using _NewExecutionPolicy = decltype(__policy);

        // Create temporary buffer
        oneapi::dpl::__par_backend_hetero::__internal::__buffer<_NewExecutionPolicy, _Type> __tmp_buf(__policy, __n);
        auto __first_tmp = __tmp_buf.get();
        auto __last_tmp = __first_tmp + __n;

        // Run main algorithm and save data into temporary buffer
        __pattern_transform_scan_base_impl(__policy, __first, __last, __first_tmp, __unary_op, __init, __binary_op,
                                           _Inclusive{});

        // Move data from temporary buffer into results
        oneapi::dpl::__internal::__pattern_walk2_brick(
            ::std::forward<_NewExecutionPolicy>(__policy), __first_tmp, __last_tmp, __result,
            oneapi::dpl::__internal::__brick_move<_NewExecutionPolicy>{}, ::std::true_type{});

        //TODO: optimize copy back depending on Iterator, i.e. set_final_data for host iterator/pointer
    }

    return __result + __n;
}

template <typename _ExecutionPolicy, typename _Iterator1, typename _Iterator2, typename _UnaryOperation, typename _Type,
          typename _BinaryOperation, typename _Inclusive>
oneapi::dpl::__internal::__enable_if_hetero_execution_policy<_ExecutionPolicy, _Iterator2>
__pattern_transform_scan(_ExecutionPolicy&& __exec, _Iterator1 __first, _Iterator1 __last, _Iterator2 __result,
                         _UnaryOperation __unary_op, _Type __init, _BinaryOperation __binary_op, _Inclusive,
                         /*vector=*/::std::true_type, /*parallel=*/::std::true_type)
{
    using _RepackedType = __par_backend_hetero::__repacked_tuple_t<_Type>;
    using _InitType = unseq_backend::__init_value<_RepackedType>;

    return __pattern_transform_scan_base(::std::forward<_ExecutionPolicy>(__exec), __first, __last, __result,
                                         __unary_op, _InitType{__init}, __binary_op, _Inclusive{});
}

// scan without initial element
template <typename _ExecutionPolicy, typename _Iterator1, typename _Iterator2, typename _UnaryOperation,
          typename _BinaryOperation, typename _Inclusive>
oneapi::dpl::__internal::__enable_if_hetero_execution_policy<_ExecutionPolicy, _Iterator2>
__pattern_transform_scan(_ExecutionPolicy&& __exec, _Iterator1 __first, _Iterator1 __last, _Iterator2 __result,
                         _UnaryOperation __unary_op, _BinaryOperation __binary_op, _Inclusive,
                         /*vector=*/::std::true_type, /*parallel=*/::std::true_type)
{
    using _Type = typename ::std::iterator_traits<_Iterator1>::value_type;
    using _RepackedType = __par_backend_hetero::__repacked_tuple_t<_Type>;
    using _InitType = unseq_backend::__no_init_value<_RepackedType>;

    return __pattern_transform_scan_base(::std::forward<_ExecutionPolicy>(__exec), __first, __last, __result,
                                         __unary_op, _InitType{}, __binary_op, _Inclusive{});
}

//------------------------------------------------------------------------
// adjacent_difference
//------------------------------------------------------------------------

// a wrapper for the policy is required to avoid the kernel naming issue
template <typename Name>
struct adjacent_difference_wrapper
{
};

template <typename _ExecutionPolicy, typename _ForwardIterator1, typename _ForwardIterator2, typename _BinaryOperation>
oneapi::dpl::__internal::__enable_if_hetero_execution_policy<_ExecutionPolicy, _ForwardIterator2>
__pattern_adjacent_difference(_ExecutionPolicy&& __exec, _ForwardIterator1 __first, _ForwardIterator1 __last,
                              _ForwardIterator2 __d_first, _BinaryOperation __op, /*vector*/ ::std::true_type,
                              /*parallel*/ ::std::true_type)
{
    auto __n = __last - __first;
    if (__n <= 0)
        return __d_first;

    using _It1ValueT = typename ::std::iterator_traits<_ForwardIterator1>::value_type;
    using _It2ValueTRef = typename ::std::iterator_traits<_ForwardIterator2>::reference;

    _ForwardIterator2 __d_last = __d_first + __n;

#if !__SYCL_UNNAMED_LAMBDA__
    // if we have the only element, just copy it according to the specification
    if (__n == 1)
    {
        return __internal::__except_handler([&__exec, __first, __last, __d_first, __d_last, &__op]() {
            auto __wrapped_policy = __par_backend_hetero::make_wrapped_policy<adjacent_difference_wrapper>(
                ::std::forward<_ExecutionPolicy>(__exec));

            __internal::__pattern_walk2_brick(__wrapped_policy, __first, __last, __d_first,
                                              __internal::__brick_copy<decltype(__wrapped_policy)>{},
                                              ::std::true_type{});

            return __d_last;
        });
    }
    else
#endif
    {
        return __internal::__except_handler([&__exec, __first, __last, __d_first, __d_last, &__op, __n]() {
            auto __fn = [__op](_It1ValueT __in1, _It1ValueT __in2, _It2ValueTRef __out1) {
                __out1 = __op(__in2, __in1); // This move assignment is allowed by the C++ standard draft N4810
            };

            auto __keep1 =
                oneapi::dpl::__ranges::__get_sycl_range<__par_backend_hetero::access_mode::read, _ForwardIterator1>();
            auto __buf1 = __keep1(__first, __last);
            auto __keep2 =
                oneapi::dpl::__ranges::__get_sycl_range<__par_backend_hetero::access_mode::write, _ForwardIterator2>();
            auto __buf2 = __keep2(__d_first, __d_last);

            using _Function = unseq_backend::walk_adjacent_difference<_ExecutionPolicy, decltype(__fn)>;

            oneapi::dpl::__par_backend_hetero::__parallel_for(__exec, _Function{__fn}, __n, __buf1.all_view(),
                                                              __buf2.all_view())
                .wait();

            return __d_last;
        });
    }
}

} // namespace __internal
} // namespace dpl
} // namespace oneapi

#endif // _ONEDPL_NUMERIC_IMPL_HETERO_H
