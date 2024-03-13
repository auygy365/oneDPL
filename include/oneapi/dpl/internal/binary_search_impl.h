/*
 *  Copyright (c) Intel Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef _ONEDPL_BINARY_SEARCH_IMPL_H
#define _ONEDPL_BINARY_SEARCH_IMPL_H

#include "function.h"
#include "binary_search_extension_defs.h"
#include "../pstl/iterator_impl.h"

namespace oneapi
{
namespace dpl
{

namespace internal
{

enum class search_algorithm
{
    lower_bound,
    upper_bound,
    binary_search
};

template <typename Comp, typename T, search_algorithm func>
struct custom_brick
{
    Comp comp;
    T size;

    template <typename _ItemId, typename _Acc1>
    void
    operator()(_ItemId idx, _Acc1 acc) const
    {
        T start_orig = 0;
        auto end_orig = size;
        using ::std::get;
        if constexpr (func == search_algorithm::lower_bound)
        {
            get<2>(acc[idx]) = oneapi::dpl::__internal::__pstl_lower_bound(get<0>(acc.tuple()), start_orig, end_orig,
                                                                           get<1>(acc[idx]), comp);
        }
        else if constexpr (func == search_algorithm::upper_bound)
        {
            get<2>(acc[idx]) = oneapi::dpl::__internal::__pstl_upper_bound(get<0>(acc.tuple()), start_orig, end_orig,
                                                                           get<1>(acc[idx]), comp);
        }
        else
        {
            auto value = oneapi::dpl::__internal::__pstl_lower_bound(get<0>(acc.tuple()), start_orig, end_orig,
                                                                     get<1>(acc[idx]), comp);
            get<2>(acc[idx]) = (value != end_orig) && (get<1>(acc[idx]) == get<0>(acc[value]));
        }
    }
};

template <typename Policy, typename InputIterator1, typename InputIterator2, typename OutputIterator,
          typename StrictWeakOrdering>
oneapi::dpl::__internal::__enable_if_host_execution_policy<Policy, OutputIterator>
lower_bound_impl(Policy&& policy, InputIterator1 start, InputIterator1 end, InputIterator2 value_start,
                 InputIterator2 value_end, OutputIterator result, StrictWeakOrdering comp)
{
    return oneapi::dpl::transform(policy, value_start, value_end, result,
                                  [=](typename ::std::iterator_traits<InputIterator2>::reference val) {
                                      return ::std::lower_bound(start, end, val, comp) - start;
                                  });
}

template <typename Policy, typename InputIterator1, typename InputIterator2, typename OutputIterator,
          typename StrictWeakOrdering>
oneapi::dpl::__internal::__enable_if_host_execution_policy<Policy, OutputIterator>
upper_bound_impl(Policy&& policy, InputIterator1 start, InputIterator1 end, InputIterator2 value_start,
                 InputIterator2 value_end, OutputIterator result, StrictWeakOrdering comp)
{
    return oneapi::dpl::transform(policy, value_start, value_end, result,
                                  [=](typename ::std::iterator_traits<InputIterator2>::reference val) {
                                      return ::std::upper_bound(start, end, val, comp) - start;
                                  });
}

template <typename Policy, typename InputIterator1, typename InputIterator2, typename OutputIterator,
          typename StrictWeakOrdering>
oneapi::dpl::__internal::__enable_if_host_execution_policy<Policy, OutputIterator>
binary_search_impl(Policy&& policy, InputIterator1 start, InputIterator1 end, InputIterator2 value_start,
                   InputIterator2 value_end, OutputIterator result, StrictWeakOrdering comp)
{
    return oneapi::dpl::transform(policy, value_start, value_end, result,
                                  [=](typename ::std::iterator_traits<InputIterator2>::reference val) {
                                      return ::std::binary_search(start, end, val, comp);
                                  });
}

#if _ONEDPL_BACKEND_SYCL
template <typename T>
class lower_bound_fallback;

template <typename Policy, typename InputIterator1, typename InputIterator2, typename OutputIterator,
          typename StrictWeakOrdering>
oneapi::dpl::__internal::__enable_if_hetero_execution_policy<Policy, OutputIterator>
lower_bound_impl(Policy&& policy, InputIterator1 start, InputIterator1 end, InputIterator2 value_start,
                 InputIterator2 value_end, OutputIterator result, StrictWeakOrdering comp)
{
    namespace __bknd = __par_backend_hetero;
    const auto size = ::std::distance(start, end);

    if (size <= 0)
        return result;

    const auto value_size = ::std::distance(value_start, value_end);

    auto keep_input = oneapi::dpl::__ranges::__get_sycl_range<__bknd::access_mode::read, InputIterator1>();
    auto input_buf = keep_input(start, end);

    auto keep_values = oneapi::dpl::__ranges::__get_sycl_range<__bknd::access_mode::read, InputIterator2>();
    auto value_buf = keep_values(value_start, value_end);

    auto keep_result = oneapi::dpl::__ranges::__get_sycl_range<__bknd::access_mode::read_write, OutputIterator>();
    auto result_buf = keep_result(result, result + value_size);
    auto zip_vw = make_zip_view(input_buf.all_view(), value_buf.all_view(), result_buf.all_view());
    // Enable index calculation to proceed with uint32_t if input range is small enough.
    if (size <= ::std::numeric_limits<::std::uint32_t>::max())
    {
        __bknd::__parallel_for(::std::forward<Policy>(policy),
                               custom_brick<StrictWeakOrdering, ::std::uint32_t, search_algorithm::lower_bound>{
                                   comp, static_cast<::std::uint32_t>(size)},
                               value_size, zip_vw)
            .wait();
    }
    else
    {
        auto fallback_policy = __bknd::make_wrapped_policy<lower_bound_fallback>(::std::forward<Policy>(policy));
        __bknd::__parallel_for(::std::move(fallback_policy),
                               custom_brick<StrictWeakOrdering, ::std::size_t, search_algorithm::lower_bound>{
                                   comp, static_cast<::std::size_t>(size)},
                               value_size, zip_vw)
            .wait();
    }
    return result + value_size;
}

template <typename T>
class upper_bound_fallback;

template <typename Policy, typename InputIterator1, typename InputIterator2, typename OutputIterator,
          typename StrictWeakOrdering>
oneapi::dpl::__internal::__enable_if_hetero_execution_policy<Policy, OutputIterator>
upper_bound_impl(Policy&& policy, InputIterator1 start, InputIterator1 end, InputIterator2 value_start,
                 InputIterator2 value_end, OutputIterator result, StrictWeakOrdering comp)
{
    namespace __bknd = __par_backend_hetero;
    const auto size = ::std::distance(start, end);

    if (size <= 0)
        return result;

    const auto value_size = ::std::distance(value_start, value_end);

    auto keep_input = oneapi::dpl::__ranges::__get_sycl_range<__bknd::access_mode::read, InputIterator1>();
    auto input_buf = keep_input(start, end);

    auto keep_values = oneapi::dpl::__ranges::__get_sycl_range<__bknd::access_mode::read, InputIterator2>();
    auto value_buf = keep_values(value_start, value_end);

    auto keep_result = oneapi::dpl::__ranges::__get_sycl_range<__bknd::access_mode::read_write, OutputIterator>();
    auto result_buf = keep_result(result, result + value_size);
    auto zip_vw = make_zip_view(input_buf.all_view(), value_buf.all_view(), result_buf.all_view());
    // Enable index calculation to proceed with uint32_t if input range is small enough.
    if (size <= ::std::numeric_limits<::std::uint32_t>::max())
    {
        __bknd::__parallel_for(::std::forward<Policy>(policy),
                               custom_brick<StrictWeakOrdering, ::std::uint32_t, search_algorithm::upper_bound>{
                                   comp, static_cast<::std::uint32_t>(size)},
                               value_size, zip_vw)
            .wait();
    }
    else
    {
        auto fallback_policy = __bknd::make_wrapped_policy<upper_bound_fallback>(::std::forward<Policy>(policy));
        __bknd::__parallel_for(::std::move(fallback_policy),
                               custom_brick<StrictWeakOrdering, ::std::size_t, search_algorithm::upper_bound>{
                                   comp, static_cast<::std::size_t>(size)},
                               value_size, zip_vw)
            .wait();
    }
    return result + value_size;
}

template <typename T>
class binary_search_fallback;

template <typename Policy, typename InputIterator1, typename InputIterator2, typename OutputIterator,
          typename StrictWeakOrdering>
oneapi::dpl::__internal::__enable_if_hetero_execution_policy<Policy, OutputIterator>
binary_search_impl(Policy&& policy, InputIterator1 start, InputIterator1 end, InputIterator2 value_start,
                   InputIterator2 value_end, OutputIterator result, StrictWeakOrdering comp)
{
    namespace __bknd = __par_backend_hetero;
    const auto size = ::std::distance(start, end);

    if (size <= 0)
        return result;

    const auto value_size = ::std::distance(value_start, value_end);

    auto keep_input = oneapi::dpl::__ranges::__get_sycl_range<__bknd::access_mode::read, InputIterator1>();
    auto input_buf = keep_input(start, end);

    auto keep_values = oneapi::dpl::__ranges::__get_sycl_range<__bknd::access_mode::read, InputIterator2>();
    auto value_buf = keep_values(value_start, value_end);

    auto keep_result = oneapi::dpl::__ranges::__get_sycl_range<__bknd::access_mode::read_write, OutputIterator>();
    auto result_buf = keep_result(result, result + value_size);
    auto zip_vw = make_zip_view(input_buf.all_view(), value_buf.all_view(), result_buf.all_view());

    // Enable index calculation to proceed with uint32_t if input range is small enough.
    if (size <= ::std::numeric_limits<::std::uint32_t>::max())
    {
        __bknd::__parallel_for(::std::forward<Policy>(policy),
                               custom_brick<StrictWeakOrdering, ::std::uint32_t, search_algorithm::binary_search>{
                                   comp, static_cast<::std::uint32_t>(size)},
                               value_size, zip_vw)
            .wait();
    }
    else
    {
        auto fallback_policy = __bknd::make_wrapped_policy<binary_search_fallback>(::std::forward<Policy>(policy));
        __bknd::__parallel_for(::std::move(fallback_policy),
                               custom_brick<StrictWeakOrdering, ::std::size_t, search_algorithm::binary_search>{
                                   comp, static_cast<::std::size_t>(size)},
                               value_size, zip_vw)
            .wait();
    }
    return result + value_size;
}

#endif
} // namespace internal

//Lower Bound start
template <typename Policy, typename InputIterator1, typename InputIterator2, typename OutputIterator>
oneapi::dpl::__internal::__enable_if_execution_policy<Policy, OutputIterator>
lower_bound(Policy&& policy, InputIterator1 start, InputIterator1 end, InputIterator2 value_start,
            InputIterator2 value_end, OutputIterator result)
{
    return internal::lower_bound_impl(::std::forward<Policy>(policy), start, end, value_start, value_end, result,
                                      oneapi::dpl::__internal::__pstl_less());
}

template <typename Policy, typename InputIterator1, typename InputIterator2, typename OutputIterator,
          typename StrictWeakOrdering>
oneapi::dpl::__internal::__enable_if_execution_policy<Policy, OutputIterator>
lower_bound(Policy&& policy, InputIterator1 start, InputIterator1 end, InputIterator2 value_start,
            InputIterator2 value_end, OutputIterator result, StrictWeakOrdering comp)
{
    return internal::lower_bound_impl(::std::forward<Policy>(policy), start, end, value_start, value_end, result, comp);
}
//Lower Bound end

//Upper Bound start

template <typename Policy, typename InputIterator1, typename InputIterator2, typename OutputIterator>
oneapi::dpl::__internal::__enable_if_execution_policy<Policy, OutputIterator>
upper_bound(Policy&& policy, InputIterator1 start, InputIterator1 end, InputIterator2 value_start,
            InputIterator2 value_end, OutputIterator result)
{
    return internal::upper_bound_impl(::std::forward<Policy>(policy), start, end, value_start, value_end, result,
                                      oneapi::dpl::__internal::__pstl_less());
}

template <typename Policy, typename InputIterator1, typename InputIterator2, typename OutputIterator,
          typename StrictWeakOrdering>
oneapi::dpl::__internal::__enable_if_execution_policy<Policy, OutputIterator>
upper_bound(Policy&& policy, InputIterator1 start, InputIterator1 end, InputIterator2 value_start,
            InputIterator2 value_end, OutputIterator result, StrictWeakOrdering comp)
{
    return internal::upper_bound_impl(::std::forward<Policy>(policy), start, end, value_start, value_end, result, comp);
}

//Upper Bound end

//Binary Search start

template <typename Policy, typename InputIterator1, typename InputIterator2, typename OutputIterator>
oneapi::dpl::__internal::__enable_if_execution_policy<Policy, OutputIterator>
binary_search(Policy&& policy, InputIterator1 start, InputIterator1 end, InputIterator2 value_start,
              InputIterator2 value_end, OutputIterator result)
{
    return internal::binary_search_impl(::std::forward<Policy>(policy), start, end, value_start, value_end, result,
                                        oneapi::dpl::__internal::__pstl_less());
}

template <typename Policy, typename InputIterator1, typename InputIterator2, typename OutputIterator,
          typename StrictWeakOrdering>
oneapi::dpl::__internal::__enable_if_execution_policy<Policy, OutputIterator>
binary_search(Policy&& policy, InputIterator1 start, InputIterator1 end, InputIterator2 value_start,
              InputIterator2 value_end, OutputIterator result, StrictWeakOrdering comp)
{
    return internal::binary_search_impl(::std::forward<Policy>(policy), start, end, value_start, value_end, result,
                                        comp);
}

//Binary search end
} // end namespace dpl
} // end namespace oneapi

#endif // _ONEDPL_BINARY_SEARCH_IMPL_H
