// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Copyright (C) 2023 Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "support/test_config.h"

#include "oneapi/dpl/dynamic_selection"
#include <iostream>
#include "support/utils.h"
#include <unordered_map>
#include <thread>

template <bool call_select_before_submit, typename Policy, typename UniverseContainer, typename UniverseMapping>
int
test_submit_and_wait_on_group(UniverseContainer u, UniverseMapping map, int best_resource)
{

    using my_policy_t = Policy;
    std::vector<int> result(u.size(), 0);

    // they are cpus so this is ok
    double* v = sycl::malloc_shared<double>(1000000, u[0]);

    my_policy_t p{u};
    auto n_samples = u.size();

    const int N = 10;

    auto func = [&](typename oneapi::dpl::experimental::policy_traits<Policy>::resource_type e, int index) {
        int x = map[e];
        result[x]++;
        auto target = index%u.size();
        if (target==0)
        {
            return e.submit([=](sycl::handler& h) {
                h.parallel_for(
                    1000000, [=](sycl::id<1> idx) {
                        for (int j0 = 0; j0 < 10000; ++j0)
                        {
                            v[idx] += idx;
                        }
                    });
            });
        }
        else
        {
            return e.submit([=](sycl::handler& h){
                            h.single_task([](){});
                         });
        }
    };

    std::vector<std::thread> threads;
    std::atomic<int> counter = 1;
    if(call_select_before_submit){
        auto thread_func = [&p, &func,&counter](){
            for(int i=0;i<3;i++){
                int tmp = counter++;
                auto s = oneapi::dpl::experimental::select(p);

                auto w = oneapi::dpl::experimental::submit(s, func, tmp);
                if (i > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        };

        for(int i=0;i<3;i++){
            threads.push_back(std::thread(thread_func));
        }
    }
    else{

        auto thread_func = [&p, &func, &counter](){
            for(int i=0;i<3;i++){
                int tmp=counter++;
                auto w = oneapi::dpl::experimental::submit(p, func, tmp);
                if (i > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        };

        for(int i=0;i<3;i++){
            threads.push_back(std::thread(thread_func));
        }
    }

    oneapi::dpl::experimental::wait(p.get_submission_group());
    for(auto& thread : threads){
        thread.join();
    }

    auto result_element = std::distance(result.begin(),std::max_element(result.begin(), result.end()));
    EXPECT_TRUE(result_element==best_resource, "ERROR : did not select expected resources\n");
    std::cout<<"Submit and wait on group : OK\n";
    return 0;
}
template <bool call_select_before_submit, typename Policy, typename UniverseContainer, typename UniverseMapping>
int
test_submit_and_wait(UniverseContainer u, UniverseMapping map, int best_resource)
{

    using my_policy_t = Policy;
    std::vector<int> result(u.size(), 0);

    // they are cpus so this is ok
    double* v = sycl::malloc_shared<double>(1000000, u[0]);

    my_policy_t p{u};
    auto n_samples = u.size();

    const int N = 10;

    auto func = [&](typename oneapi::dpl::experimental::policy_traits<Policy>::resource_type e, int index) {
        int x = map[e];
        result[x]++;
        auto target = index%u.size();
        if (target==0)
        {
            return e.submit([=](sycl::handler& h) {
                h.parallel_for(
                    1000000, [=](sycl::id<1> idx) {
                        for (int j0 = 0; j0 < 10000; ++j0)
                        {
                            v[idx] += idx;
                        }
                    });
            });
        }
        else
        {
            return e.submit([=](sycl::handler& h){
                            h.single_task([](){});
                         });
        }
    };

    std::vector<std::thread> threads;
    std::atomic<int> counter = 1;
    if(call_select_before_submit){
        auto thread_func = [&p, &func,&counter](){
            for(int i=0;i<3;i++){
                int tmp = counter++;
                auto s = oneapi::dpl::experimental::select(p);
                oneapi::dpl::experimental::submit_and_wait(s, func, tmp);
            }
        };

        for(int i=0;i<3;i++){
            threads.push_back(std::thread(thread_func));
        }
    }
    else{

        auto thread_func = [&p, &func, &counter](){
            for(int i=0;i<3;i++){
                int tmp=counter++;
                oneapi::dpl::experimental::submit_and_wait(p, func, tmp);
            }
        };

        for(int i=0;i<3;i++){
            threads.push_back(std::thread(thread_func));
        }
    }

    for(auto& thread : threads){
        thread.join();
    }


    auto result_element = std::distance(result.begin(),std::max_element(result.begin(), result.end()));
    EXPECT_TRUE(result_element==best_resource, "ERROR : did not select expected resources\n");
    std::cout<<"Submit and wait : OK\n";
    return 0;
}
template <bool call_select_before_submit, typename Policy, typename UniverseContainer, typename UniverseMapping>
int
test_submit_and_wait_on_event(UniverseContainer u, UniverseMapping map, int best_resource)
{

    using my_policy_t = Policy;
    std::vector<int> result(u.size(), 0);

    // they are cpus so this is ok
    double* v = sycl::malloc_shared<double>(1000000, u[0]);

    my_policy_t p{u};
    auto n_samples = u.size();

    const int N = 10;

    auto func = [&](typename oneapi::dpl::experimental::policy_traits<Policy>::resource_type e, int index) {
        int x = map[e];
        result[x]++;
        auto target = index%u.size();
        if (target==0)
        {
            return e.submit([=](sycl::handler& h) {
                h.parallel_for(
                    1000000, [=](sycl::id<1> idx) {
                        for (int j0 = 0; j0 < 10000; ++j0)
                        {
                            v[idx] += idx;
                        }
                    });
            });
        }
        else
        {
            return e.submit([=](sycl::handler& h){
                            h.single_task([](){});
                         });
        }
    };

    std::vector<std::thread> threads;
    std::atomic<int> counter = 1;
    if(call_select_before_submit){
        auto thread_func = [&p, &func,&counter](){
            for(int i=0;i<3;i++){
                int tmp = counter++;
                auto s = oneapi::dpl::experimental::select(p);

                auto w = oneapi::dpl::experimental::submit(s, func, tmp);
                if (i > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
                oneapi::dpl::experimental::wait(w);
            }
        };

        for(int i=0;i<3;i++){
            threads.push_back(std::thread(thread_func));
        }
    }
    else{

        auto thread_func = [&p, &func, &counter](){
            for(int i=0;i<3;i++){
                int tmp=counter++;
                auto w = oneapi::dpl::experimental::submit(p, func, tmp);
                if (i > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
                oneapi::dpl::experimental::wait(w);
            }
        };

        for(int i=0;i<3;i++){
            threads.push_back(std::thread(thread_func));
        }
    }

    for(auto& thread : threads){
        thread.join();
    }

    auto result_element = std::distance(result.begin(),std::max_element(result.begin(), result.end()));
    EXPECT_TRUE(result_element==best_resource, "ERROR : did not select expected resources\n");
    std::cout<<"Submit and wait on event : OK\n";
    return 0;
}
#if TEST_DYNAMIC_SELECTION_AVAILABLE

static inline void
build_dl_universe(std::vector<sycl::queue>& u, std::unordered_map<sycl::queue, int>& map)
{
    int i=0;
    try
    {
        auto device_cpu1 = sycl::device(sycl::cpu_selector_v);
        sycl::queue cpu1_queue(device_cpu1);
        u.push_back(cpu1_queue);
        map[cpu1_queue]=i++;
    }
    catch (const sycl::exception&)
    {
        std::cout << "SKIPPED: Unable to run with cpu_selector\n";
    }
    try
    {
        auto device_cpu2 = sycl::device(sycl::cpu_selector_v);
        sycl::queue cpu2_queue(device_cpu2);
        u.push_back(cpu2_queue);
        map[cpu2_queue]=i++;
    }
    catch (const sycl::exception&)
    {
        std::cout << "SKIPPED: Unable to run with cpu_selector\n";
    }
}
#endif

int
main()
{
    bool bProcessed = false;

#if TEST_DYNAMIC_SELECTION_AVAILABLE
#if !ONEDPL_FPGA_DEVICE || !ONEDPL_FPGA_EMULATOR
    using policy_t = oneapi::dpl::experimental::dynamic_load_policy<oneapi::dpl::experimental::sycl_backend>;
    std::vector<sycl::queue> u;
    std::unordered_map<sycl::queue, int> map;
    build_dl_universe(u, map);

    auto n = u.size();

    //If building the universe is not a success, return
    if (n != 0)
    {

        constexpr bool just_call_submit = false;
        constexpr bool call_select_before_submit = true;

        auto actual = test_submit_and_wait_on_event<call_select_before_submit, policy_t>(u, map, 0);
        actual = test_submit_and_wait_on_event<call_select_before_submit, policy_t>(u, map, 0);
        actual = test_submit_and_wait_on_group<just_call_submit, policy_t>(u, map, 0);
        actual = test_submit_and_wait_on_group<call_select_before_submit, policy_t>(u, map, 0);
        actual = test_submit_and_wait<just_call_submit, policy_t>(u, map, 0);
        actual = test_submit_and_wait<call_select_before_submit, policy_t>(u, map, 0);

        bProcessed = true;
    }
#endif // Devices available are CPU and GPU
#endif // TEST_DYNAMIC_SELECTION_AVAILABLE

    return TestUtils::done(bProcessed);
}