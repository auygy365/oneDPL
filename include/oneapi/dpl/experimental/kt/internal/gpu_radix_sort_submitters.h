// -*- C++ -*-
//===-- gpu_radix_sort_submitters.h --------------------------------===//
//
// Copyright (C) 2023 Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===------------------------------------------------------===//

#ifndef _ONEDPL_KT_GPU_RADIX_SORT_SUBMITTERS_H
#define _ONEDPL_KT_GPU_RADIX_SORT_SUBMITTERS_H

#if __has_include(<sycl/sycl.hpp>)
#    include <sycl/sycl.hpp>
#else
#    include <CL/sycl.hpp>
#endif

#include <cstdint>

#include "gpu_radix_sort_kernels.h"
#include "../../../pstl/hetero/dpcpp/parallel_backend_sycl_utils.h"
#include "../../../pstl/hetero/dpcpp/utils_ranges_sycl.h"

namespace oneapi::dpl::experimental::kt::gpu::__impl
{

static constexpr ::std::uint32_t SUBGROUP_SIZE = 32;

//------------------------------------------------------------------------
// Please see the comment for __parallel_for_submitter for optional kernel name explanation
//------------------------------------------------------------------------

template <typename _KeyT, ::std::uint8_t __radix_bits, ::std::uint32_t __stage_count,
          ::std::uint16_t __data_per_work_item, ::std::uint16_t __work_group_size, bool __is_ascending,
          typename _KernelName>
struct __radix_sort_onesweep_histogram_submitter;

template <typename _KeyT, ::std::uint32_t __radix_bits, ::std::uint32_t __stage_count,
          ::std::uint16_t __data_per_work_item, ::std::uint16_t __work_group_size, bool __is_ascending,
          typename... _Name>
struct __radix_sort_onesweep_histogram_submitter<
    _KeyT, __radix_bits, __stage_count, __data_per_work_item, __work_group_size, __is_ascending,
    oneapi::dpl::__par_backend_hetero::__internal::__optional_kernel_name<_Name...>>
{
    template <typename _KeysRng, typename _GlobalOffsetData>
    sycl::event
    operator()(sycl::queue& __q, _KeysRng&& __keys_rng, _GlobalOffsetData * const __global_offset_data,
               ::std::size_t __n, const sycl::event& __e) const
    {
        const ::std::uint32_t __hist_work_group_count =
            oneapi::dpl::__internal::__dpl_ceiling_div(__n, __data_per_work_item * __work_group_size);
        sycl::nd_range<1> __nd_range(__hist_work_group_count * __work_group_size, __work_group_size);
        return __q.submit([&](sycl::handler& __cgh) {
            oneapi::dpl::__ranges::__require_access(__cgh, __keys_rng);
            __cgh.depends_on(__e);
            auto __data = __keys_rng.data();
            static constexpr std::uint32_t __bin_count = 1 << __radix_bits;
            __dpl_sycl::__local_accessor<_GlobalOffsetData> __lacc(__stage_count * __bin_count, __cgh);
            __cgh.parallel_for<_Name...>(__nd_range, [=](sycl::nd_item<1> __nd_item) {
                RadixSortHistogram<__work_group_size, __data_per_work_item, _KeyT,
                                   _GlobalOffsetData, __radix_bits, __stage_count,
                                   __is_ascending>
                    kernel(__global_offset_data, __dpl_sycl::__get_accessor_ptr(__lacc), __data, __n);
                kernel.process(__nd_item);
            });
        });
    }
};

template <::std::uint32_t __stage_count, ::std::uint32_t __bin_count, typename _KernelName>
struct __radix_sort_onesweep_scan_submitter;

template <::std::uint32_t __stage_count, ::std::uint32_t __bin_count, typename... _Name>
struct __radix_sort_onesweep_scan_submitter<
    __stage_count, __bin_count, oneapi::dpl::__par_backend_hetero::__internal::__optional_kernel_name<_Name...>>
{
    template <typename _GlobalOffsetData>
    sycl::event
    operator()(sycl::queue& __q, const _GlobalOffsetData& __global_offset_data, ::std::size_t __n,
               const sycl::event& __e) const
    {
        sycl::nd_range<1> __nd_range(__stage_count * __bin_count, __bin_count);
        return __q.submit([&](sycl::handler& __cgh) {
            __cgh.depends_on(__e);
            __cgh.parallel_for<_Name...>(__nd_range, [=](sycl::nd_item<1> __nd_item) {
                ::std::uint32_t __offset = __nd_item.get_global_id(0);
                const auto __g = __nd_item.get_group();
                ::std::uint32_t __count = __global_offset_data[__offset];
                ::std::uint32_t __presum = sycl::exclusive_scan_over_group(__g, __count, sycl::plus<::std::uint32_t>());
                __global_offset_data[__offset] = __presum;
            });
        });
    }
};

template <bool __is_ascending, ::std::uint8_t __radix_bits, ::std::uint16_t __data_per_work_item,
          ::std::uint16_t __work_group_size, typename _KeyT, typename _KernelName>
struct __radix_sort_onesweep_submitter;

template <bool __is_ascending, ::std::uint8_t __radix_bits, ::std::uint16_t __data_per_work_item,
          ::std::uint16_t __work_group_size, typename _KeyT, typename... _Name>
struct __radix_sort_onesweep_submitter<__is_ascending, __radix_bits, __data_per_work_item, __work_group_size, _KeyT,
                                       oneapi::dpl::__par_backend_hetero::__internal::__optional_kernel_name<_Name...>>
{
    template <typename _InKeysRng, typename _OutKeysRng, typename _GlobalHistT, typename _WgCounterT>
    sycl::event
    operator()(sycl::queue& __q, _InKeysRng& __in_keys_rng, _OutKeysRng& __out_keys_rng, _GlobalHistT* __p_global_hist,
               _GlobalHistT* __p_group_hists, _WgCounterT* __p_dynamic_id, ::std::uint32_t __sweep_work_group_count, ::std::size_t __n,
               ::std::uint32_t __stage, const sycl::event& __e) const
    {
        sycl::nd_range<1> __nd_range(__sweep_work_group_count * __work_group_size, __work_group_size);
        return __q.submit([&](sycl::handler& __cgh) {
            oneapi::dpl::__ranges::__require_access(__cgh, __in_keys_rng, __out_keys_rng);
            auto __in_data = __in_keys_rng.data();
            auto __out_data = __out_keys_rng.data();
            __cgh.depends_on(__e);

            static constexpr std::uint32_t __bin_count = 1 << __radix_bits;
            __dpl_sycl::__local_accessor<OneSweepSharedData<__bin_count, __work_group_size/SUBGROUP_SIZE, __data_per_work_item, __work_group_size, _KeyT>>
                __lacc(1, __cgh);

            __cgh.parallel_for<_Name...>(
                __nd_range, [=](sycl::nd_item<1> __nd_item) [[intel::reqd_sub_group_size(SUBGROUP_SIZE)]] {
                    OneSweepRadixSort<__radix_bits, __work_group_size, __data_per_work_item, true /* USE_DYNAMIC_ID */,
                                      SUBGROUP_SIZE, _KeyT, decltype(__in_data), decltype(__out_data),
                                      __is_ascending>
                        kernel(__stage, __in_data, __out_data, __p_global_hist, __p_group_hists,
                               __p_dynamic_id + __stage, __n, *__dpl_sycl::__get_accessor_ptr(__lacc));
                    kernel.process(__nd_item);
                });
        });
    }
};

template <typename _KeyT, typename _KernelName>
struct __radix_sort_copyback_submitter;

template <typename _KeyT, typename... _Name>
struct __radix_sort_copyback_submitter<_KeyT,
                                       oneapi::dpl::__par_backend_hetero::__internal::__optional_kernel_name<_Name...>>
{
    template <typename _KeysTmpRng, typename _KeysRng>
    sycl::event
    operator()(sycl::queue& __q, _KeysTmpRng& __keys_tmp_rng, _KeysRng& __keys_rng, ::std::uint32_t __n,
               const sycl::event& __e) const
    {
        return __q.submit([&](sycl::handler& __cgh) {
            oneapi::dpl::__ranges::__require_access(__cgh, __keys_tmp_rng, __keys_rng);
            // TODO: make sure that access is read_only for __keys_tmp_rng  and is write_only for __keys_rng
            auto __tmp_data = __keys_tmp_rng.data();
            auto __out_data = __keys_rng.data();
            __cgh.depends_on(__e);
            __cgh.parallel_for<_Name...>(sycl::range<1>{__n}, [=](sycl::item<1> __item) {
                auto __global_id = __item.get_linear_id();
                __out_data[__global_id] = __tmp_data[__global_id];
            });
        });
    }
};

} // namespace oneapi::dpl::experimental::kt::gpu::__impl

#endif // _ONEDPL_KT_GPU_RADIX_SORT_SUBMITTERS_H