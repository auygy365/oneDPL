// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Copyright (C) 2023 Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include "oneapi/dpl/dynamic_selection"
#include "support/test_dynamic_selection_utils.h"
#include "support/inline_backend.h"

int main() {
  using policy_t = oneapi::dpl::experimental::dynamic_load_policy<TestUtils::int_inline_backend_t>;
  std::vector<int> u{4, 5, 6, 7};

  // should be similar to round_robin when waiting on policy
  auto f = [u](int i, int offset=0) { return u[(i+offset-1)%4]; };

  // should always pick first when waiting on sync in each iteration
  auto f2 = [u](int i, int offset=0) { return u[offset]; };

  constexpr bool just_call_submit = false;
  constexpr bool call_select_before_submit = true;
  if ( test_initialization<policy_t, int>(u)
       || test_select<policy_t, decltype(u), decltype(f2)&, false>(u, f2)
       || test_submit_and_wait_on_event<just_call_submit, policy_t>(u, f2)
       || test_submit_and_wait_on_event<just_call_submit, policy_t>(u, f2, 1)
       || test_submit_and_wait_on_event<just_call_submit, policy_t>(u, f2, 2)
       || test_submit_and_wait_on_event<just_call_submit, policy_t>(u, f2, 3)
       || test_submit_and_wait_on_event<call_select_before_submit, policy_t>(u, f2)
       || test_submit_and_wait_on_event<call_select_before_submit, policy_t>(u, f2, 1)
       || test_submit_and_wait_on_event<call_select_before_submit, policy_t>(u, f2, 2)
       || test_submit_and_wait_on_event<call_select_before_submit, policy_t>(u, f2, 3)
       || test_submit_and_wait<just_call_submit, policy_t>(u, f2)
       || test_submit_and_wait<just_call_submit, policy_t>(u, f2, 1)
       || test_submit_and_wait<just_call_submit, policy_t>(u, f2, 2)
       || test_submit_and_wait<just_call_submit, policy_t>(u, f2, 3)
       || test_submit_and_wait<call_select_before_submit, policy_t>(u, f2)
       || test_submit_and_wait<call_select_before_submit, policy_t>(u, f2, 1)
       || test_submit_and_wait<call_select_before_submit, policy_t>(u, f2, 2)
       || test_submit_and_wait<call_select_before_submit, policy_t>(u, f2, 3)
       || test_submit_and_wait_on_group<just_call_submit, policy_t>(u, f)
       || test_submit_and_wait_on_group<just_call_submit, policy_t>(u, f, 1)
       || test_submit_and_wait_on_group<just_call_submit, policy_t>(u, f, 2)
       || test_submit_and_wait_on_group<just_call_submit, policy_t>(u, f, 3)
       || test_submit_and_wait_on_group<call_select_before_submit, policy_t>(u, f)
       || test_submit_and_wait_on_group<call_select_before_submit, policy_t>(u, f, 1)
       || test_submit_and_wait_on_group<call_select_before_submit, policy_t>(u, f, 2)
       || test_submit_and_wait_on_group<call_select_before_submit, policy_t>(u, f, 3)
     ) {
    std::cout << "FAIL\n";
    return 1;
  } else {
    std::cout << "PASS\n";
    return 0;
  }
}
