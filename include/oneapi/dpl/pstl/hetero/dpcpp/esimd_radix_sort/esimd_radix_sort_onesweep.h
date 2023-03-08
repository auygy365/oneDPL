// -*- C++ -*-
//===-- esimd_radix_sort_onesweep.h --------------------------------===//
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

#ifndef _ONEDPL_esimd_radix_sort_onesweep_H
#define _ONEDPL_esimd_radix_sort_onesweep_H


#include <ext/intel/esimd.hpp>
#include "../sycl_defs.h"
#include <cstdint>

namespace oneapi::dpl::experimental::esimd::impl
{

template <typename KeyT, typename InputT, uint32_t RADIX_BITS, uint32_t TG_COUNT, uint32_t THREAD_PER_TG>
void global_histogram(auto idx, size_t n, const InputT& input, uint32_t *p_global_offset, uint32_t *p_sync_buffer) {
    using bin_t = uint16_t;
    using hist_t = uint32_t;
    using global_hist_t = uint32_t;

    using namespace sycl;
    using namespace __ESIMD_NS;
    using namespace __ESIMD_ENS;

    using device_addr_t = uint32_t;

    slm_init(16384);
    constexpr uint32_t BINCOUNT = 1 << RADIX_BITS;
    constexpr uint32_t NBITS =  sizeof(KeyT) * 8;
    constexpr uint32_t STAGES = oneapi::dpl::__internal::__dpl_ceiling_div(NBITS, RADIX_BITS);
    constexpr uint32_t PROCESS_SIZE = 128;
    constexpr uint32_t addr_step = TG_COUNT * THREAD_PER_TG * PROCESS_SIZE;

    uint32_t local_tid = idx.get_local_linear_id();
    uint32_t tid = idx.get_global_linear_id();

    constexpr uint32_t SYNC_SEGMENT_COUNT = 64;
    constexpr uint32_t SYNC_SEGMENT_SIZE_DW = 128;
    if (tid < SYNC_SEGMENT_COUNT) {
        simd<uint32_t, SYNC_SEGMENT_SIZE_DW> sync_init = 0;
        sync_init.copy_to(p_sync_buffer + SYNC_SEGMENT_SIZE_DW * tid);
    }

    if ((tid - local_tid) * PROCESS_SIZE > n) {
        //no work for this tg;
        return;
    }

    // cooperative fill 0
    {
        constexpr uint32_t BUFFER_SIZE = STAGES * BINCOUNT;
        constexpr uint32_t THREAD_SIZE = BUFFER_SIZE / THREAD_PER_TG;
        slm_block_store<global_hist_t, THREAD_SIZE>(local_tid*THREAD_SIZE*sizeof(global_hist_t), 0);
    }
    barrier();


    simd<KeyT, PROCESS_SIZE> keys;
    simd<bin_t, PROCESS_SIZE> bins;
    simd<global_hist_t, BINCOUNT * STAGES> state_hist_grf(0);
    bin_t MASK = BINCOUNT-1;

    device_addr_t read_addr;
    for (read_addr = tid * PROCESS_SIZE; read_addr < n; read_addr += addr_step) {
        if (read_addr+PROCESS_SIZE < n) {
            utils::copy_from(keys, input, read_addr);
        }
        else
        {
            simd<uint32_t, 16> lane_id(0, 1);
            #pragma unroll
            for (uint32_t s = 0; s<PROCESS_SIZE; s+=16) {
                simd_mask<16> m = (s+lane_id)<(n-read_addr);
                // simd<KeyT, 16> source = lsc_gather<KeyT, 1,
                //         lsc_data_size::default_size, cache_hint::cached, cache_hint::cached, 16>(input+read_addr+s, lane_id*sizeof(KeyT), m);
                sycl::ext::intel::esimd::simd offset((read_addr + s + lane_id)*sizeof(KeyT));
                simd<KeyT, 16> source = lsc_gather<KeyT, 1,
                    lsc_data_size::default_size, cache_hint::cached, cache_hint::cached, 16>(input, offset, m);
                keys.template select<16, 1>(s) = merge(source, simd<KeyT, 16>(-1), m);
            }
        }
        #pragma unroll
        for (uint32_t i = 0; i < STAGES; i++) //4*3 = 12 instr
        {
            bins = (keys >> (i * RADIX_BITS))&MASK;
            #pragma unroll
            for (uint32_t s = 0; s < PROCESS_SIZE; s++)
            {
                state_hist_grf[i * BINCOUNT + bins[s]]++;// 256K * 4 * 1.25 = 1310720 instr for grf indirect addr
            }
        }
    }

    //atomic add to the state counter in slm
    #pragma unroll
    for (uint32_t s = 0; s < BINCOUNT * STAGES; s+=16) {
        simd<uint32_t, 16> offset(0, sizeof(global_hist_t));
        lsc_slm_atomic_update<atomic_op::add, global_hist_t, 16>(s*sizeof(global_hist_t)+offset, state_hist_grf.template select<16, 1>(s), 1);
    }

    barrier();

    {
        // bin count 256, 4 stages, 1K uint32_t, by 64 threads, happen to by 16-wide each thread. will not work for other config.
        constexpr uint32_t BUFFER_SIZE = STAGES * BINCOUNT;
        constexpr uint32_t THREAD_SIZE = BUFFER_SIZE / THREAD_PER_TG;
        simd<global_hist_t, THREAD_SIZE> group_hist = slm_block_load<global_hist_t, THREAD_SIZE>(local_tid*THREAD_SIZE*sizeof(global_hist_t));
        simd<uint32_t, THREAD_SIZE> offset(0, 4);
        lsc_atomic_update<atomic_op::add>(p_global_offset + local_tid*THREAD_SIZE, offset, group_hist, simd_mask<THREAD_SIZE>(1));
    }
}

void inline global_wait(uint32_t *psync, uint32_t sync_id, uint32_t count, uint32_t gid, uint32_t tid) {
    using namespace __ESIMD_NS;
    using namespace __ESIMD_ENS;
    //assume initial is 1, do inc, then repeat load until count is met, then the first one atomic reduce by count to reset to 1, do not use store to 1, because second round might started.
    psync += sync_id;
    uint32_t current = -1;
    uint32_t try_count = 0;
    while (current != count) {
        // current=lsc_atomic_update<atomic_op::load, uint32_t, 1>(psync, 0, 1)[0];
        current=lsc_atomic_update<atomic_op::load, uint32_t, 1>(psync, simd<uint32_t, 1>(0), 1)[0];
        if (try_count++ > 5120) break;
    }
}

template <typename KeyT, typename InputT, typename OutputT, uint32_t RADIX_BITS, uint32_t THREAD_PER_TG, uint32_t PROCESS_SIZE>
void onesweep_kernel(auto idx, uint32_t n, uint32_t stage, const InputT& input, const OutputT& output, uint8_t *p_global_buffer) {
    using namespace sycl;
    using namespace __ESIMD_NS;
    using namespace __ESIMD_ENS;

    using bin_t = uint16_t;
    using hist_t = uint32_t;
    using global_hist_t = uint32_t;
    using device_addr_t = uint32_t;

    uint32_t tg_id = idx.get_group(0);
    uint32_t tg_count = idx.get_group_range(0);

    uint32_t local_tid = idx.get_local_linear_id();
    constexpr uint32_t BIN_COUNT = 1 << RADIX_BITS;
    constexpr uint32_t NBITS =  sizeof(KeyT) * 8;
    constexpr uint32_t STAGES = oneapi::dpl::__internal::__dpl_ceiling_div(NBITS, RADIX_BITS);
    constexpr uint32_t TG_PROCESS_SIZE = PROCESS_SIZE * THREAD_PER_TG;
    constexpr bin_t MASK = BIN_COUNT - 1;

    constexpr uint32_t BIN_HIST_SLM_SIZE = BIN_COUNT * sizeof(hist_t) * THREAD_PER_TG;  // bin hist working buffer, 64K for DW hist
    constexpr uint32_t INCOMING_OFFSET_SLM_SIZE = (BIN_COUNT+16)*sizeof(global_hist_t); // incoming offset buffer
    constexpr uint32_t GLOBAL_SCAN_SIZE = (BIN_COUNT+16)*sizeof(global_hist_t);

    slm_init( BIN_HIST_SLM_SIZE + INCOMING_OFFSET_SLM_SIZE + GLOBAL_SCAN_SIZE);
    uint32_t slm_bin_hist_start = 0;
    uint32_t slm_incoming_start = slm_bin_hist_start + BIN_HIST_SLM_SIZE;
    uint32_t slm_global_scan_start = slm_incoming_start + INCOMING_OFFSET_SLM_SIZE;


    uint32_t slm_bin_hist_this_thread = slm_bin_hist_start + local_tid * BIN_COUNT * sizeof(hist_t);

    size_t temp_io_size = n * sizeof(KeyT);

    global_hist_t *p_global_bin_start_buffer_allstages = reinterpret_cast<global_hist_t*>(p_global_buffer);
    global_hist_t *p_global_bin_start_buffer = p_global_bin_start_buffer_allstages + BIN_COUNT * stage;
    uint32_t *p_sync_buffer = reinterpret_cast<uint32_t*>(p_global_bin_start_buffer_allstages + BIN_COUNT * STAGES);

    constexpr uint32_t SYNC_SEGMENT_COUNT = 64;
    constexpr uint32_t SYNC_SEGMENT_SIZE_DW = 128;


    simd<hist_t, BIN_COUNT> bin_offset;
    simd<device_addr_t, 16> lane_id(0, 1);

    device_addr_t io_offset = tg_id * TG_PROCESS_SIZE + PROCESS_SIZE * local_tid;

    constexpr uint32_t BIN_GROUPS = 8;
    constexpr uint32_t THREAD_PER_BIN_GROUP = THREAD_PER_TG / BIN_GROUPS;
    constexpr uint32_t BIN_WIDTH = BIN_COUNT / BIN_GROUPS;
    constexpr uint32_t BIN_WIDTH_UD = BIN_COUNT / BIN_GROUPS * sizeof(hist_t) / sizeof(uint32_t);
    constexpr uint32_t BIN_HEIGHT = THREAD_PER_TG / THREAD_PER_BIN_GROUP;

    {
        simd<KeyT, PROCESS_SIZE> keys;
        simd<bin_t, PROCESS_SIZE> bins;
        if (io_offset+PROCESS_SIZE < n) {
            utils::copy_from(keys, input, io_offset);
        }
        else if (io_offset >= n) {
            keys = -1;
        }
        else {
            #pragma unroll
            for (uint32_t s = 0; s<PROCESS_SIZE; s+=16) {
                simd_mask<16> m = (io_offset+lane_id+s)<n;
                simd<KeyT, 16> source = lsc_gather<KeyT, 1,
                        lsc_data_size::default_size, cache_hint::cached, cache_hint::cached, 16>(input+io_offset+s, lane_id*uint32_t(sizeof(KeyT)), m);
                keys.template select<16, 1>(s) = merge(source, simd<KeyT, 16>(-1), m);
            }
        }
        bins = (keys >> (stage * RADIX_BITS)) & MASK;
        bin_offset = 0;

        #pragma unroll
        for (uint32_t s = 0; s<PROCESS_SIZE; s+=1) {
            bin_offset[bins[s]] += 1;
        }
    }
    {
        /*
        first write to slm,
        then do column scan by group, each thread to 32c*8r,
        then last row do exclusive scan as incoming offset
        then every thread add local sum with sum of previous group and incoming offset
        */
        {
            // put local hist to slm
            #pragma unroll
            for (uint32_t s = 0; s<BIN_COUNT; s+=64) {
                lsc_slm_block_store<uint32_t, 64>(slm_bin_hist_this_thread + s*sizeof(hist_t), bin_offset.template select<64, 1>(s).template bit_cast_view<uint32_t>());
            }
            barrier();
            // small group sum for local hist
            constexpr uint32_t BIN_GROUPS = 8;
            constexpr uint32_t THREAD_PER_BIN_GROUP = THREAD_PER_TG / BIN_GROUPS;
            constexpr uint32_t BIN_WIDTH = BIN_COUNT / BIN_GROUPS;
            constexpr uint32_t BIN_HEIGHT = THREAD_PER_TG / THREAD_PER_BIN_GROUP;
            constexpr uint32_t HIST_STRIDE = BIN_COUNT * sizeof(hist_t);
            uint32_t THREAD_GID = local_tid / THREAD_PER_BIN_GROUP;
            uint32_t THREAD_LTID = local_tid % THREAD_PER_BIN_GROUP;
            {
                uint32_t slm_bin_hist_ingroup_offset = slm_bin_hist_start + THREAD_GID * BIN_WIDTH * sizeof(hist_t) + THREAD_LTID * BIN_HEIGHT * HIST_STRIDE;
                utils::simd2d<hist_t, BIN_HEIGHT, BIN_WIDTH> thread_grf_hist;
                #pragma unroll
                for (uint32_t s = 0; s<BIN_HEIGHT; s++) {
                    thread_grf_hist.row(s).template bit_cast_view<uint32_t>() = lsc_slm_block_load<uint32_t, BIN_WIDTH_UD>(slm_bin_hist_ingroup_offset + s * HIST_STRIDE);
                }
                #pragma unroll
                for (uint32_t s = 1; s<BIN_HEIGHT; s++) {
                    thread_grf_hist.row(s) += thread_grf_hist.row(s-1);
                }
                #pragma unroll
                for (uint32_t s = 1; s<BIN_HEIGHT; s++) {
                    lsc_slm_block_store<uint32_t, BIN_WIDTH_UD>(slm_bin_hist_ingroup_offset + s * HIST_STRIDE, thread_grf_hist.row(s).template bit_cast_view<uint32_t>());
                }
            }

            barrier();
            // thread group sum for groups
            simd<global_hist_t, BIN_WIDTH> group_hist_sum;
            if (THREAD_LTID == 0) {
                uint32_t slm_bin_hist_group_summary_offset = slm_bin_hist_start + THREAD_GID * BIN_WIDTH * sizeof(hist_t) +  (BIN_HEIGHT-1) * HIST_STRIDE;
                utils::simd2d<hist_t, THREAD_PER_BIN_GROUP, BIN_WIDTH> thread_grf_hist_summary;
                #pragma unroll
                for (uint32_t s = 0; s<THREAD_PER_BIN_GROUP; s++) {
                    thread_grf_hist_summary.row(s).template bit_cast_view<uint32_t>() = lsc_slm_block_load<uint32_t, BIN_WIDTH_UD>(slm_bin_hist_group_summary_offset + s * BIN_HEIGHT * HIST_STRIDE);
                }
                #pragma unroll
                for (uint32_t s = 1; s<THREAD_PER_BIN_GROUP; s++) {
                    thread_grf_hist_summary.row(s) += thread_grf_hist_summary.row(s-1);
                }
                #pragma unroll
                for (uint32_t s = 1; s<THREAD_PER_BIN_GROUP-1; s++) {
                    lsc_slm_block_store<uint32_t, BIN_WIDTH_UD>(slm_bin_hist_group_summary_offset + s * BIN_HEIGHT * HIST_STRIDE, thread_grf_hist_summary.row(s).template bit_cast_view<uint32_t>());
                }
                group_hist_sum = thread_grf_hist_summary.row(THREAD_PER_BIN_GROUP-1);
            }
            // wait for previous TG pass over starting point
            // atomic add starting point with group_hist_sum
            // and pass to next
            {
                //sync offset will be distributed to 64 banks to reduce latency
                //for each tg, sync segment id is (tg_id % 64), sync target is ((tg_id+63) / 64) * BIN_GROUPS,
                if (local_tid == 0) {
                    uint32_t sync_segment_id = tg_id % SYNC_SEGMENT_COUNT;
                    uint32_t sync_segment_target = ((tg_id + SYNC_SEGMENT_COUNT-1) / SYNC_SEGMENT_COUNT) * BIN_GROUPS;
                    uint32_t sync_offset = sync_segment_id * SYNC_SEGMENT_SIZE_DW + stage;
                    global_wait(p_sync_buffer, sync_offset, sync_segment_target, tg_id, local_tid);
                }
                barrier();
                if (THREAD_LTID == 0) {
                    uint32_t pass_over_sync_segment_id = (tg_id + 1) % SYNC_SEGMENT_COUNT;
                    uint32_t pass_over_sync_offset = pass_over_sync_segment_id * SYNC_SEGMENT_SIZE_DW + stage;
                    simd<uint32_t, BIN_WIDTH> update_lanes(0, sizeof(global_hist_t));
                    group_hist_sum = lsc_atomic_update<atomic_op::add, uint32_t, BIN_WIDTH>(p_global_bin_start_buffer+THREAD_GID*BIN_WIDTH, update_lanes, group_hist_sum, 1);
                    lsc_slm_block_store<uint32_t, BIN_WIDTH>(slm_incoming_start + THREAD_GID * BIN_WIDTH * sizeof(global_hist_t), group_hist_sum);
                    // lsc_atomic_update<atomic_op::inc, uint32_t, 1>(p_sync_buffer+pass_over_sync_offset, 0, 1);
                    lsc_atomic_update<atomic_op::inc, uint32_t, 1>(p_sync_buffer+pass_over_sync_offset, simd<uint32_t, 1>(0), 1);

                }
            }
            // calculate each thread starting offset
            barrier();
            {
                #pragma unroll
                for (uint32_t s = 0; s<BIN_COUNT; s+=64) {
                    bin_offset.template select<64, 1>(s).template bit_cast_view<uint32_t>() = lsc_slm_block_load<uint32_t, 64>(slm_incoming_start + s*sizeof(hist_t));
                }
                if (local_tid>0) {
                    #pragma unroll
                    for (uint32_t s = 0; s<BIN_COUNT; s+=64) {
                        simd<hist_t, 64> group_local_sum;
                        group_local_sum.template bit_cast_view<uint32_t>() = lsc_slm_block_load<uint32_t, 64>(slm_bin_hist_start + (local_tid-1)*HIST_STRIDE + s*sizeof(hist_t));
                        bin_offset.template select<64, 1>(s) += group_local_sum;
                    }
                }
                if ((local_tid > THREAD_PER_BIN_GROUP) && (local_tid % BIN_HEIGHT != 0)) {
                    uint32_t prev_cum_rowid = (local_tid-1) / BIN_HEIGHT * BIN_HEIGHT - 1;
                    #pragma unroll
                    for (uint32_t s = 0; s<BIN_COUNT; s+=64) {
                        simd<hist_t, 64> group_sum;
                        group_sum.template bit_cast_view<uint32_t>() = lsc_slm_block_load<uint32_t, 64>(slm_bin_hist_start + prev_cum_rowid*HIST_STRIDE + s*sizeof(hist_t));
                        bin_offset.template select<64, 1>(s) += group_sum;
                    }
                }
            }
        }
    }
    {
        constexpr uint32_t BLOCK_SIZE = 32;
        simd<device_addr_t, BLOCK_SIZE> write_addr;
        simd<KeyT, BLOCK_SIZE> keys;
        simd<bin_t, BLOCK_SIZE> bins;
        auto p_read = input + io_offset;
        if (io_offset < n) {
            for (uint32_t i=0; i<PROCESS_SIZE; i+=BLOCK_SIZE) {
                simd<device_addr_t, BLOCK_SIZE> lane_id_block(0, 1);
                simd_mask<BLOCK_SIZE> m = (io_offset+lane_id_block+i)<n;
                simd<KeyT, BLOCK_SIZE> source = lsc_gather<KeyT, 1,
                        lsc_data_size::default_size, cache_hint::cached, cache_hint::cached, BLOCK_SIZE>(p_read+i, lane_id_block*uint32_t(sizeof(KeyT)), m);
                keys = merge(source, simd<KeyT, BLOCK_SIZE>(-1), m);

                bins = (keys >> (stage * RADIX_BITS)) & MASK;
                #pragma unroll
                for (uint32_t s = 0; s<BLOCK_SIZE; s+=1) {
                    write_addr[s] = bin_offset[bins[s]];
                    bin_offset[bins[s]] += 1;
                }
                lsc_scatter<KeyT, 1, lsc_data_size::default_size, cache_hint::write_back, cache_hint::write_back, BLOCK_SIZE>(
                    output,
                    write_addr*sizeof(KeyT),
                    keys, write_addr<n);
            }
        }
    }
}

template<typename KeyT, typename InputT, std::uint32_t RadixBits /* not used*/>
void onesweep(sycl::queue &q, const InputT& input, size_t n, uint32_t process_size) {
    using namespace sycl;
    using namespace __ESIMD_NS;

    using global_hist_t = uint32_t;
    constexpr uint32_t RADIX_BITS = 8;
    constexpr uint32_t BINCOUNT = 1 << RADIX_BITS;
    const uint32_t HW_TG_COUNT = 64;
    constexpr uint32_t THREAD_PER_TG = 64;
    uint32_t SWEEP_PROCESSING_SIZE = process_size;
    const uint32_t sweep_tg_count = oneapi::dpl::__internal::__dpl_ceiling_div(n, THREAD_PER_TG*SWEEP_PROCESSING_SIZE);
    const uint32_t sweep_threads = sweep_tg_count * THREAD_PER_TG;
    constexpr uint32_t NBITS =  sizeof(KeyT) * 8;
    constexpr uint32_t STAGES = oneapi::dpl::__internal::__dpl_ceiling_div(NBITS, RADIX_BITS);

    assert((SWEEP_PROCESSING_SIZE == 256) || (SWEEP_PROCESSING_SIZE == 512) || (SWEEP_PROCESSING_SIZE == 1024) || (SWEEP_PROCESSING_SIZE == 1536));

    constexpr uint32_t SYNC_SEGMENT_COUNT = 64;
    constexpr uint32_t SYNC_SEGMENT_SIZE_DW = 128;
    constexpr uint32_t SYNC_BUFFER_SIZE = SYNC_SEGMENT_COUNT * SYNC_SEGMENT_SIZE_DW * sizeof(uint32_t); //bytes
    constexpr uint32_t GLOBAL_OFFSET_SIZE = BINCOUNT * STAGES * sizeof(global_hist_t);
    size_t temp_buffer_size = GLOBAL_OFFSET_SIZE + // global offset
                              SYNC_BUFFER_SIZE;  // sync buffer

    uint8_t *tmp_buffer = reinterpret_cast<uint8_t*>(sycl::malloc_device(temp_buffer_size, q));
    auto p_global_offset = reinterpret_cast<uint32_t*>(tmp_buffer);
    auto p_sync_buffer = reinterpret_cast<uint32_t*>(tmp_buffer + GLOBAL_OFFSET_SIZE);
    auto output = sycl::malloc_device<uint32_t>(n, q);
    using OutputT = decltype(output);

    {
        nd_range<1> Range( (range<1>( HW_TG_COUNT * THREAD_PER_TG)), (range<1>(THREAD_PER_TG)) );
        auto e = q.submit([&](handler &cgh) {
            cgh.parallel_for<class kernel_global_histogram>(
                    Range, [=](nd_item<1> idx) [[intel::sycl_explicit_simd]] {
                        global_histogram<KeyT, InputT, RADIX_BITS, HW_TG_COUNT, THREAD_PER_TG> (idx, n, input, p_global_offset, p_sync_buffer);
                    });
        });
    }
    {
        auto e = q.submit([&](handler &cgh) {
            cgh.parallel_for<class kernel_global_scan>(
                nd_range<1>({STAGES * BINCOUNT}, {BINCOUNT}), [=](nd_item<1> idx) {
                    uint32_t offset = idx.get_global_id(0);
                    auto g = idx.get_group();
                    uint32_t count = p_global_offset[offset];
                    uint32_t presum = sycl::exclusive_scan_over_group(g, count, sycl::plus<uint32_t>());
                    p_global_offset[offset] = presum;
                });
            });
    }
    {
        /*
        sycl::event e;
        for (uint32_t stage = 0; stage < STAGES; stage++) {
            nd_range<1> Range( (range<1>( sweep_tg_count * THREAD_PER_TG)), (range<1>(THREAD_PER_TG)) );
            if (SWEEP_PROCESSING_SIZE == 256) {
                e = q.submit([&](handler &cgh) {
                    cgh.parallel_for<class kernel_radix_sort_onesweep_256>(
                            Range, [=](nd_item<1> idx) [[intel::sycl_explicit_simd]] {
                                onesweep_kernel<KeyT, InputT, RADIX_BITS, THREAD_PER_TG, 256> (idx, n, stage, input, output, tmp_buffer);
                            });
                });
            } else if (SWEEP_PROCESSING_SIZE == 512) {
                e = q.submit([&](handler &cgh) {
                    cgh.parallel_for<class kernel_radix_sort_onesweep_512>(
                            Range, [=](nd_item<1> idx) [[intel::sycl_explicit_simd]] {
                                onesweep_kernel<KeyT, InputT, RADIX_BITS, THREAD_PER_TG, 512> (idx, n, stage, input, output, tmp_buffer);
                            });
                });
            } else if (SWEEP_PROCESSING_SIZE == 1024) {
                e = q.submit([&](handler &cgh) {
                    cgh.parallel_for<class kernel_radix_sort_onesweep_1024>(
                            Range, [=](nd_item<1> idx) [[intel::sycl_explicit_simd]] {
                                onesweep_kernel<KeyT, InputT, RADIX_BITS, THREAD_PER_TG, 1024> (idx, n, stage, input, output, tmp_buffer);
                            });
                });
            } else if (SWEEP_PROCESSING_SIZE == 1536) {
                e = q.submit([&](handler &cgh) {
                    cgh.parallel_for<class kernel_radix_sort_onesweep_1536>(
                            Range, [=](nd_item<1> idx) [[intel::sycl_explicit_simd]] {
                                onesweep_kernel<KeyT, InputT, RADIX_BITS, THREAD_PER_TG, 1536> (idx, n, stage, input, output, tmp_buffer);
                            });
                });
            }
            std::swap(input, output);

        }
        */
        sycl::event e;
        for (uint32_t stage = 0; stage < STAGES; stage++) {
            nd_range<1> Range( (range<1>( sweep_tg_count * THREAD_PER_TG)), (range<1>(THREAD_PER_TG)) );
            if((stage & 1) == 0)
            {
                if (SWEEP_PROCESSING_SIZE == 256) {
                    e = q.submit([&](handler &cgh) {
                        cgh.parallel_for<class kernel_radix_sort_onesweep_256>(
                                Range, [=](nd_item<1> idx) [[intel::sycl_explicit_simd]] {
                                    onesweep_kernel<KeyT, InputT, OutputT, RADIX_BITS, THREAD_PER_TG, 256> (idx, n, stage, input, output, tmp_buffer);
                                });
                    });
                } else if (SWEEP_PROCESSING_SIZE == 512) {
                    e = q.submit([&](handler &cgh) {
                        cgh.parallel_for<class kernel_radix_sort_onesweep_512>(
                                Range, [=](nd_item<1> idx) [[intel::sycl_explicit_simd]] {
                                    onesweep_kernel<KeyT, InputT, OutputT, RADIX_BITS, THREAD_PER_TG, 512> (idx, n, stage, input, output, tmp_buffer);
                                });
                    });
                } else if (SWEEP_PROCESSING_SIZE == 1024) {
                    e = q.submit([&](handler &cgh) {
                        cgh.parallel_for<class kernel_radix_sort_onesweep_1024>(
                                Range, [=](nd_item<1> idx) [[intel::sycl_explicit_simd]] {
                                    onesweep_kernel<KeyT, InputT, OutputT, RADIX_BITS, THREAD_PER_TG, 1024> (idx, n, stage, input, output, tmp_buffer);
                                });
                    });
                } else if (SWEEP_PROCESSING_SIZE == 1536) {
                    e = q.submit([&](handler &cgh) {
                        cgh.parallel_for<class kernel_radix_sort_onesweep_1536>(
                                Range, [=](nd_item<1> idx) [[intel::sycl_explicit_simd]] {
                                    onesweep_kernel<KeyT, InputT, OutputT, RADIX_BITS, THREAD_PER_TG, 1536> (idx, n, stage, input, output, tmp_buffer);
                                });
                    });
                }
            }
            else
            {
                if (SWEEP_PROCESSING_SIZE == 256) {
                    e = q.submit([&](handler &cgh) {
                        cgh.parallel_for<class kernel_radix_sort_onesweep_256_odd>(
                                Range, [=](nd_item<1> idx) [[intel::sycl_explicit_simd]] {
                                    onesweep_kernel<KeyT, OutputT, InputT, RADIX_BITS, THREAD_PER_TG, 256> (idx, n, stage, output, input, tmp_buffer);
                                });
                    });
                } else if (SWEEP_PROCESSING_SIZE == 512) {
                    e = q.submit([&](handler &cgh) {
                        cgh.parallel_for<class kernel_radix_sort_onesweep_512_odd>(
                                Range, [=](nd_item<1> idx) [[intel::sycl_explicit_simd]] {
                                    onesweep_kernel<KeyT, OutputT, InputT, RADIX_BITS, THREAD_PER_TG, 512> (idx, n, stage, output, input, tmp_buffer);
                                });
                    });
                } else if (SWEEP_PROCESSING_SIZE == 1024) {
                    e = q.submit([&](handler &cgh) {
                        cgh.parallel_for<class kernel_radix_sort_onesweep_1024_odd>(
                                Range, [=](nd_item<1> idx) [[intel::sycl_explicit_simd]] {
                                    onesweep_kernel<KeyT, OutputT, InputT, RADIX_BITS, THREAD_PER_TG, 1024> (idx, n, stage, output, input, tmp_buffer);
                                });
                    });
                } else if (SWEEP_PROCESSING_SIZE == 1536) {
                    e = q.submit([&](handler &cgh) {
                        cgh.parallel_for<class kernel_radix_sort_onesweep_1536_odd>(
                                Range, [=](nd_item<1> idx) [[intel::sycl_explicit_simd]] {
                                    onesweep_kernel<KeyT, OutputT, InputT, RADIX_BITS, THREAD_PER_TG, 1536> (idx, n, stage, output, input, tmp_buffer);
                                });
                    });
                }
            }
        }
        e.wait();
    }
    sycl::free(tmp_buffer, q);
    sycl::free(output, q);
}

} // oneapi::dpl::experimental::esimd::impl

#endif // _ONEDPL_esimd_radix_sort_onesweep_H