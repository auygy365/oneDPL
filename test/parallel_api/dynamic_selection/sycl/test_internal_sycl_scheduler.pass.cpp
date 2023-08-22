// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Copyright (C) 2023 Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "oneapi/dpl/dynamic_selection"
#include "oneapi/dpl/internal/dynamic_selection_impl/scoring_policy_defs.h"
#include <atomic>
#include <iostream>

class fake_selection_handle_t {
  sycl::queue q_;
public:
  fake_selection_handle_t(sycl::queue q = sycl::queue(sycl::default_selector{})) : q_(q) {}
  auto unwrap() { return q_; }
};

int test_cout() {
  oneapi::dpl::experimental::sycl_backend s;
  oneapi::dpl::experimental::sycl_backend::execution_resource_t e;
  return 0;
}

int test_submit_and_wait_on_scheduler() {
  const int N = 100;
  oneapi::dpl::experimental::sycl_backend s;
  fake_selection_handle_t h;

  std::atomic<int> ecount = 0;

  for (int i = 1; i <= N; ++i) {
    s.submit(h, [&](sycl::queue q, int i) {
             ecount += i;
             return sycl::event{};
           }, i
    );
  }
  s.wait();
  int count = ecount.load();
  if (count != N*(N+1)/2) {
    std::cout << "ERROR: scheduler did not execute all tasks exactly once\n";
    return 1;
  }
  std::cout << "wait_on_scheduler: OK\n";
  return 0;
}

int test_submit_and_wait_on_scheduler_single_element() {
  const int N = 1;
  oneapi::dpl::experimental::sycl_backend s;
  fake_selection_handle_t h;

  std::atomic<int> ecount = 0;

  for (int i = 1; i <= N; ++i) {
    s.submit(h, [&](sycl::queue q, int i) {
             ecount += i;
             return sycl::event{};
           }, i
    );
  }
  s.wait();
  int count = ecount.load();
  if (count != 1) {
    std::cout << "ERROR: scheduler did not execute all tasks exactly once\n";
    return 1;
  }
  std::cout << "wait_on_scheduler single element: OK\n";
  return 0;
}

int test_submit_and_wait_on_scheduler_empty() {
  const int N = 0;
  oneapi::dpl::experimental::sycl_backend s;
  fake_selection_handle_t h;

  std::atomic<int> ecount = 0;

  for (int i = 1; i <= N; ++i) {
    s.submit(h, [&](sycl::queue q, int i) {
             ecount += i;
             return sycl::event{};
           }, i
    );
  }
  s.wait();
  int count = ecount.load();
  if (count != 0) {
    std::cout << "ERROR: scheduler did not execute all tasks exactly once\n";
    return 1;
  }
  std::cout << "wait_on_scheduler empty list: OK\n";
  return 0;
}

int test_submit_and_wait_on_sync() {
  const int N = 100;
  oneapi::dpl::experimental::sycl_backend s;
  fake_selection_handle_t h;

  std::atomic<int> ecount = 0;

  for (int i = 1; i <= N; ++i) {
    auto w = s.submit(h,
           [&](sycl::queue q, int i) {
             ecount += i;
             return sycl::event{};
           }, i
    );
    w.wait();
    int count = ecount.load();
    if (count != i*(i+1)/2) {
      std::cout << "ERROR: scheduler did not execute all tasks exactly once\n";
      return 1;
    }
  }
  std::cout << "wait_on_sync: OK\n";
  return 0;
}

int test_submit_and_wait_on_sync_single_element() {
  const int N = 1;
  oneapi::dpl::experimental::sycl_backend s;
  fake_selection_handle_t h;

  std::atomic<int> ecount = 0;

  for (int i = 1; i <= N; ++i) {
    auto w = s.submit(h,
           [&](sycl::queue q, int i) {
             ecount += i;
             return sycl::event{};
           }, i
    );
    w.wait();
    int count = ecount.load();
    if (count != 1) {
      std::cout << "ERROR: scheduler did not execute all tasks exactly once\n";
      return 1;
    }
  }
  std::cout << "wait_on_sync single element: OK\n";
  return 0;
}

int test_submit_and_wait_on_sync_empty() {
  const int N = 0;
  oneapi::dpl::experimental::sycl_backend s;
  fake_selection_handle_t h;

  std::atomic<int> ecount = 0;

  for (int i = 1; i <= N; ++i) {
    auto w = s.submit(h,
           [&](sycl::queue q, int i) {
             ecount += i;
             return sycl::event{};
           }, i
    );
    w.wait();
    int count = ecount.load();
    if (count != 0) {
      std::cout << "ERROR: scheduler did not execute all tasks exactly once\n";
      return 1;
    }
  }
  std::cout << "wait_on_sync empty list: OK\n";
  return 0;
}

int test_properties() {
  oneapi::dpl::experimental::sycl_backend s;
  std::vector<sycl::queue> v;
  //= { sycl::queue(sycl::cpu_selector{}), sycl::queue(sycl::gpu_selector{}) };
  try {
    sycl::cpu_selector ds_cpu;
    sycl::queue cpu_queue(ds_cpu);
    v.push_back(cpu_queue);
  } catch (sycl::exception) {
    std::cout << "SKIPPED: Unable to use cpu selector\n";
  }
  try {
    sycl::gpu_selector ds_gpu;
    sycl::queue gpu_queue(ds_gpu);
    v.push_back(gpu_queue);
  } catch (sycl::exception) {
    std::cout << "SKIPPED: Unable to use gpu selector\n";
  }

  std::cout << "UNIVERSE SIZE " << v.size() << std::endl;
  s.initialize(v);
  auto v2 = s.get_resources();
  auto v2s = v2.size();
  if (v2s != v.size()) {
    std::cout << "ERROR: reported universe and queried universe are not equal in size\n";
    return 1;
  }
  for (int i = 0; i < v2s; ++i) {
    if (v[i] != unwrap(v2[i])) {
      std::cout << "ERROR: reported universe and queried universe are not equal\n";
      return 1;
    }
  }
  std::cout << "properties: OK\n";
  return 0;
}

int main() {
  try {
    sycl::queue q;
  } catch (sycl::exception) {
    std::cout << "SKIPPED: Unable to use sycl at all\n";
    return 0;
  }

  if (test_cout()
      || test_submit_and_wait_on_scheduler()
      || test_submit_and_wait_on_scheduler_single_element()
      || test_submit_and_wait_on_scheduler_empty()
      || test_submit_and_wait_on_sync()
      || test_submit_and_wait_on_sync_single_element()
      || test_submit_and_wait_on_sync_empty()
      || test_properties()
   ) {
    std::cout << "FAIL\n";
    return 1;
  } else {
    std::cout << "PASS\n";
    return 0;
  }
}

