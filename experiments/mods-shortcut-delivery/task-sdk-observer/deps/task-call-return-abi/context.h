// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
struct TaskValues { std::uint64_t taskno, id; std::uint32_t delta_bits, cumulative_bits; };
struct TaskCallContext {
    TaskValues values;
    std::uint64_t run_id, invocation_id, thread_observation;
    std::uint32_t phase, status;
    std::uint64_t reserved;
};
static_assert(sizeof(TaskValues)==24 && sizeof(TaskCallContext)==64,"fixed scalar context");
static_assert(offsetof(TaskCallContext,run_id)==24 && offsetof(TaskCallContext,phase)==48
              && offsetof(TaskCallContext,reserved)==56,"assembly/context contract");
extern "C" {
extern void *az_task_call_original;
// Inputs are separate opaque scalars. Only pointer-free values may enter ctx.
void az_task_call_before(TaskCallContext *ctx, std::uintptr_t box,
                         std::uintptr_t task, std::uintptr_t original_lr) noexcept;
// Normal return only. No input pointers are supplied, and none may be read later.
void az_task_call_after(const TaskCallContext *ctx, std::uint64_t native_result) noexcept;
}
