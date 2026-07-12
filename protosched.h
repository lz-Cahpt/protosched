/**
 * @file protosched.h
 * @author lz-Cahpt (liangzcheng@foxmail.com)
 * @brief A minimal, priority-based cooperative scheduler for bare-metal embedded systems
 *        and as Complement for RTOS.
 * @version 1.0.0
 * @date 2026-07-09
 * @copyright Copyright (c) 2026
 *
 * protosched is a lightweight cooperative scheduler written in C89.
 * It provides coroutine-based tasking with priority scheduling and
 * synchronization primitives (channel, semaphore, event group, barrier).
 *
 * Key features:
 * - 32-level bitmap-based priority queue.
 * - Binary heap timer queue for delays and timeouts
 * - Shared-stack coroutines (protothreads), minimum 48 bytes per task (32-bit machine)
 * - GNU extension support (`&&` label addresses), with portable fallback
 * - Zero dynamic memory allocation
 */

#ifndef PROTOSCHED_H
#define PROTOSCHED_H

#include <assert.h>
#include <stddef.h>

#if (PS_STRICT_C89)
/* Strict C89: no <stdint.h>, use manual type definitions.
 * The PS_USE_CUSTOM_DATA_TYPE macro triggers the hand-coded types
 * defined later in this header. */
#include <limits.h>
#define PS_USE_CUSTOM_DATA_TYPE 1
#elif (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L))
/* C99 or later: <stdint.h> is part of the standard library. */
#include <stdint.h>
#elif (defined(__MINGW32__) || defined(__MINGW64__))
/* MinGW provides <stdint.h> even when not strictly C90 compliant. */
#include <stdint.h>
#else
/* Unknown compiler/environment: fall back to custom definitions.
 * This covers older compilers (e.g., Keil C51, IAR, some embedded toolchains)
 * that may not provide <stdint.h> but can still be used with manual types. */
#include <limits.h>
#define PS_USE_CUSTOM_DATA_TYPE 1
#endif /* (PS_STRICT_C89) */

/* ============================================================================
 *  Preprocessing Tools
 * ============================================================================ */

/**
 * @brief Concatenate two tokens.
 * @internal
 */
#define PS_MACRO_CONCAT_IMPL(a, b) a##b
#define PS_MACRO_CONCAT(a, b)      PS_MACRO_CONCAT_IMPL(a, b)

/**
 * @brief Internal macro to generate a unique ID for each expansion.
 * @internal
 *
 * Uses `__COUNTER__` if available (GCC, Clang) and not in strict C89 mode,
 * otherwise falls back to `__LINE__`.
 *
 * @note Internal use only.
 */
#if (!PS_STRICT_C89 && defined(__COUNTER__))
#define PS_UNIQUE_ID __COUNTER__
#else
#define PS_UNIQUE_ID __LINE__
#endif

/* ============================================================================
 *  Macro constant
 * ============================================================================ */

#define PS_TRUE  1
#define PS_FALSE 0

/**
 * @brief Special value for timeout parameters indicating infinite wait.
 *
 * Pass this value to TASK_YIELD_UNTIL_* or TASK_DELAY_* macros to wait
 * forever until the condition is satisfied.
 *
 * @warning This is NOT the same as "do not wait". In this library, 0 means
 *          infinite wait, not immediate return. To check state without
 *          blocking, use the dedicated state functions instead.
 */
#define TASK_NEVER_TIMEOUT (0)

#ifdef __cplusplus
extern "C"
{
#endif

/* ============================================================================
 *  Custom Type Definitions (Optional)
 *
 *  When PS_USE_CUSTOM_DATA_TYPE is defined, the library defines its own
 *  fixed-width integer types instead of relying on <stdint.h>.
 *  This is useful for compilers that do not fully support C99 (e.g., strict C89)
 *  or when you need to ensure exact type sizes.
 *
 *  To use this feature, define PS_USE_CUSTOM_DATA_TYPE before including this header.
 *  You may also define PS_SIZEOF_POINTER if __SIZEOF_POINTER__ is not available.
 * ============================================================================ */

#if (PS_USE_CUSTOM_DATA_TYPE)

/* ----- 8-bit type ----- */
#if (UCHAR_MAX == 255)
typedef unsigned char uint8_t; /**< 8-bit unsigned integer */
#else
#error "No 8-bit unsigned type found."
#endif /* (UCHAR_MAX == 255) */

/* ----- 16-bit type ----- */
#if (USHRT_MAX == 65535)
typedef unsigned short uint16_t; /**< 16-bit unsigned integer */
#define UINT16_MAX USHRT_MAX
#elif (UINT_MAX == 65535)
typedef unsigned int uint16_t; /**< 16-bit unsigned integer */
#define UINT16_MAX UINT_MAX
#else
#error "No 16-bit unsigned integer type found."
#endif /* USHRT_MAX == 65535 */

/* ----- 32-bit type ----- */
#if (INT_MAX == 2147483647)
typedef signed int int32_t;    /**< 32-bit signed integer */
typedef unsigned int uint32_t; /**< 32-bit unsigned integer */
#define UINT32_MAX UINT_MAX
#elif (LONG_MAX == 2147483647)
typedef signed long int32_t;    /**< 32-bit signed integer */
typedef unsigned long uint32_t; /**< 32-bit unsigned integer */
#define UINT32_MAX ULONG_MAX
#else
#error "No 32-bit integer type found."
#endif /* (INT_MAX == 2147483647) */

/* ----- Pointer size detection ----- */
#if (defined(PS_SIZEOF_POINTER) && defined(__SIZEOF_POINTER__))
#if (PS_SIZEOF_POINTER != __SIZEOF_POINTER__)
#error "PS_SIZEOF_POINTER does not match compiler's __SIZEOF_POINTER__. Please correct the definition."
#endif
#elif (!defined(PS_SIZEOF_POINTER) && defined(__SIZEOF_POINTER__))
#define PS_SIZEOF_POINTER __SIZEOF_POINTER__
#elif (!defined(PS_SIZEOF_POINTER) && !defined(__SIZEOF_POINTER__))
#error "PS_SIZEOF_POINTER must be defined."
#endif /* (defined(PS_SIZEOF_POINTER) && defined(__SIZEOF_POINTER__)) */

/* ----- uintptr_t type (pointer-sized unsigned integer) ----- */
#if (defined(_UINTPTR_T_DEFINED) || defined(__uintptr_t_defined))
/* uintptr_t already defined by the system; do nothing */
#else
#if (PS_SIZEOF_POINTER == 4)
typedef unsigned long uintptr_t; /**< Pointer-sized unsigned integer (4-byte) */
#elif (PS_SIZEOF_POINTER == 8)
#if (PS_STRICT_C89)
#error "64-bit pointer requires 'long long' which is not available in strict C89."
#else
typedef unsigned long long uintptr_t;
#endif /* (PS_STRICT_C89) */
#else
#error "Unsupported pointer size."
#endif /* (PS_SIZEOF_POINTER == 4) */
#endif /* !(defined(_UINTPTR_T_DEFINED) || defined(__uintptr_t_defined)) */

#endif /* (PS_USE_CUSTOM_DATA_TYPE) */

/* ============================================================================
 *  Enumerations
 * ============================================================================ */

/**
 * @brief Task execution states.
 */
enum task_state
{
    TASK_STATE_READY = 0x00,   /**< Task is ready to run */
    TASK_STATE_WAITING = 0x01, /**< Task is blocked on a wait condition */
    TASK_STATE_TIMEOUT = 0x02, /**< Task was blocked and timed out */
    TASK_STATE_DONE = 0x03     /**< Task has completed */
};

/**
 * @brief Event group wait policy.
 *
 * Determines whether waiting tasks are awakened when:
 * - EVENT_GROUP_ANY: any bit in the mask is set
 * - EVENT_GROUP_ALL: all bits in the mask are set
 */
enum event_group_policy
{
    EVENT_GROUP_ANY = 0, /**< Wake if any bit matches */
    EVENT_GROUP_ALL = 1  /**< Wake if all bits match */
};

/* ============================================================================
 *  Type Forward Declarations
 * ============================================================================ */

typedef enum task_state task_state_t;
typedef enum event_group_policy event_group_policy_t;

typedef struct task task_t;
typedef struct task_list task_list_t;
typedef struct task_heap task_heap_t;
typedef struct scheduler scheduler_t;
typedef struct semaphore semaphore_t;
typedef struct event_group event_group_t;
typedef struct barrier barrier_t;
typedef struct channel channel_t;

typedef task_state_t (*task_entry_t)(task_t *const this_task);

/**
 * @brief Task resume point storage type.
 *
 * Under GNU extensions (`&&` label addresses), this is a `void*`.
 * In strict C89 mode, this is an `int` used as a switch case index.
 */
#if (!PS_STRICT_C89 && (defined(__GNUC__) || defined(__clang__)))
typedef void *task_resume_point_t;
#else
typedef int task_resume_point_t;
#endif

/**
 * @brief Scheduler callback function type.
 *
 * Used for hooks like `on_loop` and `on_idle`.
 */
typedef void (*scheduler_callback_t)(scheduler_t *scheduler);

/* ============================================================================
 *  Core Data Structures
 * ============================================================================ */

/**
 * @brief Intrusive doubly-linked list head.
 */
struct task_list
{
    task_t *head; /**< First node in the list, or NULL if empty */
};

/**
 * @brief Binary heap for timer management.
 *
 * Implements a min-heap keyed by `wakeup_tick`. Uses intrusive pointers
 * (`parent`, `left_ltag`, `right_rtag`) embedded in `task_t`.
 */
struct task_heap
{
    task_t *root; /**< Root node (earliest wakeup) */
    task_t *head; /**< The first incomplete node in level-order traversal,
                       and the newly inserted node will first become it's child */
    task_t *tail; /**< The last node in level-order traversal */
};

/**
 * @brief Task structure.
 *
 * Each task occupies approximately 32 bytes. All fields are managed by the
 * scheduler; users should not modify them directly.
 */
struct task
{
    /* ----- Timer heap intrusive pointers ----- */
    task_t *parent;       /**< Parent in timer heap */
    uintptr_t left_ltag;  /**< Left pointer [31:1] + tag bit */
                          /**< if tag bit = 0, the left pointer indicates the child. */
                          /**< if tag bit = 1, the left pointer indicates the thread. */
    uintptr_t right_rtag; /**< Right pointer [31:1] + tag bit, same as left_ltag */

    /* ----- take list intrusive pointers ----- */
    task_t *prev; /**< Previous task in list */
    task_t *next; /**< Next task in list */

    /* ----- Scheduler context ----- */
    scheduler_t *scheduler; /**< Owning scheduler instance */
    task_list_t *wait_list; /**< Waiting list this task is blocked on */

    /* ----- Event group waiting ----- */
    uint32_t event_mask; /**< Mask to wait for on event group */

    /* ----- Scheduling attributes ----- */
    uint32_t flags_priority_status; /**< Bit fields: [31:7] flags, [6:2] priority, [1:0] status */

    /* ----- Task execution context ----- */
    task_entry_t entry;               /**< Task entry function */
    task_resume_point_t resume_point; /**< Resume position after YIELD */

    /* ----- Wakeup timer management ----- */
    uint32_t wakeup_tick; /**< Absolute tick when task should wake */
};

/**
 * @brief Scheduler instance.
 *
 * Manages all tasks, timer heap, and priority scheduling.
 */
struct scheduler
{
    uint32_t tick;    /**< Current schedule tick counter */
    int active_tasks; /**< Number of tasks not in DONE state */

    uint32_t ready_bitmap;            /**< Bitmap of non-empty priority levels */
    task_list_t ready_task_lists[32]; /**< Ready lists by priority (0=lowest) */

    task_heap_t waiting_task_heap; /**< Timer heap for delayed/waiting tasks */
};

/**
 * @brief Event group synchronization primitive.
 *
 * Allows tasks to wait for one or more event bits to be set.
 */
struct event_group
{
    uint32_t flags;                /**< Current event flags */
    task_list_t waiting_task_list; /**< Tasks blocked on this event group */
};

/**
 * @brief Counting semaphore.
 *
 * A non-negative counter that supports task blocking and waking.
 */
struct semaphore
{
    uint32_t counter;              /**< Available resource count */
    task_list_t waiting_task_list; /**< Tasks blocked on this semaphore */
};

/**
 * @brief Barrier synchronization primitive.
 *
 * Blocks tasks until `threshold` tasks have arrived at the barrier.
 */
struct barrier
{
    uint16_t count;                /**< Number of tasks that have arrived */
    uint16_t threshold;            /**< Number of tasks needed to release */
    task_list_t waiting_task_list; /**< Tasks blocked on this barrier */
};

/**
 * @brief Channel for data exchange between tasks.
 *
 * Implements a ring buffer with blocking send/receive operations.
 */
struct channel
{
    void *buffer;                  /**< Pointer to ring buffer storage */
    size_t size;                   /**< Total buffer size in bytes */
    size_t head;                   /**< Read position in buffer */
    size_t tail;                   /**< Write position in buffer */
    task_list_t waiting_task_list; /**< Tasks blocked on this channel */
    uint8_t wakeup_locked;         /**< 1 indicates that new wake-up operations are prohibited. */
};

/* ============================================================================
 *  Task API
 * ============================================================================ */

/**
 * @brief Initialize a task structure.
 *
 * @param task     Pointer to task structure
 * @param priority 0 (lowest) to 31 (highest)
 * @param entry    Task entry function (must be defined using the TASK_DEFINE macro)
 */
void task_init(task_t *task, int priority, task_entry_t entry);

/**
 * @brief Get the state of a task.
 *
 * @param task Task structure
 * @return task state
 */
task_state_t task_get_state(const task_t *task);

/**
 * @brief Schedule a task to wake after `ticks` ticks.
 * @internal
 *
 * Must be immediately followed by TASK_YIELD; failing to do so will cause
 * unexpected behavior.
 *
 * @param task  The current task (must be the task currently being executed)
 * @param ticks Number of ticks to delay (must be > 0)
 * @note Use TASK_DELAY(ticks) macro instead for safe usage.
 *       This function is only valid after scheduler_add_task() has been called
 *       for the task. Calling it before then triggers an assertion failure in
 *       debug builds.
 */
void task_schedule_delay(task_t *task, int32_t ticks);

/**
 * @brief Delay until a fixed phase is reached.
 * @internal
 *
 * Similar to `vTaskDelayUntil` in FreeRTOS. Useful for fixed-rate tasks.
 * The task must call TASK_YIELD only when the return value is 1.
 *
 * @param task              The current task (must be the task currently being executed)
 * @param last_wakeup_tick  Pointer to the previous wakeup tick (must be
 *                          initialized with TASK_GET_TICK() before first use)
 * @param delay_ticks       Interval between wakeups (must be > 0)
 * @return PS_TRUE if task was delayed, PS_FALSE if not (if already past deadline)
 *
 * @note Use TASK_DELAY_UNTIL(last_wakeup_tick, delay_ticks) macro instead for
 *       safe usage.
 *       The caller is responsible for initializing `last_wakeup_tick` before
 *       the first call.
 *       This function is only valid after scheduler_add_task() has been called
 *       for the task. Calling it before then triggers an assertion failure in
 *       debug builds.
 */
int task_schedule_delay_until(task_t *task, uint32_t *last_wakeup_tick, int32_t delay_ticks);

/**
 * @brief Check if the scheduler has higher-priority tasks ready.
 *
 * Used by `TASK_YIELD_IF_NEEDED` to determine if yielding is beneficial.
 *
 * @param task The current task (must be the task currently being executed)
 * @return PS_TRUE if higher-priority tasks are ready, PS_FALSE otherwise
 *
 * @note This function is only valid after scheduler_add_task() has been called
 *       for the task. Calling it before then triggers an assertion failure in
 *       debug builds.
 */
int task_should_yield(const task_t *task);

/* ============================================================================
 *  Task Blocking Functions (Internal)
 *
 * These functions are used by the `TASK_YIELD_UNTIL_*` macros.
 * They check a condition and, if unmet, suspend the current task.
 * ============================================================================ */

/**
 * @brief Block if event group conditions are not satisfied.
 * @internal
 *
 * @param task          The current task (must be the task currently being executed)
 * @param event_group   Event group to check
 * @param mask          Event mask to wait for
 * @param policy        EVENT_GROUP_ANY or EVENT_GROUP_ALL
 * @param timeout_ticks Timeout in ticks, or 0 for never timeout
 * @return PS_TRUE if blocked, PS_FALSE if condition already satisfied
 *
 * @note Do not call it directly. Use TASK_YIELD_UNTIL_EVENT_GROUP_SATISFIED macro instead.
 * @note O(log M) if timeout_ticks > 0 (heap insertion), plus O(1) for wait-list insertion.
 */
int task_block_if_event_group_unsatisfied(
    task_t *task,
    event_group_t *event_group,
    uint32_t mask,
    event_group_policy_t policy,
    int32_t timeout_ticks);

/**
 * @brief Block if semaphore is not takeable (counter == 0).
 * @internal
 *
 * @param task          The current task (must be the task currently being executed)
 * @param semaphore     Semaphore to check
 * @param timeout_ticks Timeout in ticks, or 0 for never timeout (wait forever)
 * @return PS_TRUE if blocked, PS_FALSE if semaphore is takeable
 *
 * @note Do not call it directly. Use TASK_YIELD_UNTIL_SEMAPHORE_TAKEABLE macro instead.
 * @note O(N) for priority-ordered wait-list insertion, plus O(log M) if timeout_ticks > 0 (heap insertion).
 */
int task_block_if_semaphore_untakeable(
    task_t *task,
    semaphore_t *semaphore,
    int32_t timeout_ticks);

/**
 * @brief Block if barrier is not passable (count < threshold).
 * @internal
 *
 * @param task          The current task (must be the task currently being executed)
 * @param barrier       Barrier to check
 * @param timeout_ticks Timeout in ticks, or 0 for never timeout (wait forever)
 * @return PS_TRUE if blocked, PS_FALSE if barrier is passable
 *
 * @note Do not call it directly. Use TASK_YIELD_UNTIL_BARRIER_PASSABLE macro instead.
 * @note O(N * log M) when waking all waiting tasks (due to heap removals),
 *       plus O(log M) if timeout_ticks > 0 for heap insertion of the current task.
 *       If no wake-up occurs, O(1) for wait-list insertion plus O(log M) for timeout heap.
 */
int task_block_if_barrier_unpassable(
    task_t *task,
    barrier_t *barrier,
    int32_t timeout_ticks);

/**
 * @brief Block if channel is full.
 * @internal
 *
 * @param task          The current task (must be the task currently being executed)
 * @param channel       Channel to check
 * @param timeout_ticks Timeout in ticks, or 0 for never timeout (wait forever)
 * @return PS_TRUE if blocked, PS_FALSE if channel has space
 *
 * @note Do not call it directly. Use TASK_YIELD_UNTIL_CHANNEL_HAS_SPACE macro instead.
 * @note If channel has space, the wake lock is automatically released.
 * @note O(N) for priority-ordered wait-list insertion, plus O(log M) if timeout_ticks > 0 (heap insertion).
 */
int task_block_if_channel_full(
    task_t *task,
    channel_t *channel,
    int32_t timeout_ticks);

/**
 * @brief Block if channel is empty.
 * @internal
 *
 * @param task          The current task (must be the task currently being executed)
 * @param channel       Channel to check
 * @param timeout_ticks Timeout in ticks, or 0 for never timeout (wait forever)
 * @return PS_TRUE if blocked, PS_FALSE if channel has data
 *
 * @note Do not call it directly. Use TASK_YIELD_UNTIL_CHANNEL_HAS_DATA macro instead.
 * @note If channel has data, the wake lock is automatically released.
 * @note O(N) for priority-ordered wait-list insertion, plus O(log M) if timeout_ticks > 0 (heap insertion).
 */
int task_block_if_channel_empty(
    task_t *task,
    channel_t *channel,
    int32_t timeout_ticks);

/* ============================================================================
 *  Scheduler API
 * ============================================================================ */

/**
 * @brief Initialize a scheduler instance.
 *
 * @param scheduler Scheduler to initialize
 */
void scheduler_init(scheduler_t *scheduler);

/**
 * @brief Advance the system tick counter by one.
 *
 * Should be called periodically (e.g., from a SysTick interrupt).
 *
 * @param scheduler Scheduler instance
 * @return New tick value
 */
uint32_t scheduler_tick(scheduler_t *scheduler);

/**
 * @brief Add a task to the scheduler.
 *
 * @param scheduler Scheduler instance
 * @param task      Task to add (must be initialized)
 */
void scheduler_add_task(scheduler_t *scheduler, task_t *task);

/**
 * @brief Execute one scheduling step.
 *
 * Handles timeout processing, priority selection, and task execution.
 *
 * @param scheduler Scheduler instance
 * @return PS_TRUE if a task was executed, PS_FLASE if no task was ready
 *
 * @note Complexity: O(K * log M) where K is the number of expired tasks
 *       (up to the total number of tasks) and M is the size of the timer
 *       heap, due to heap pops. The task execution itself is O(1).
 */
int scheduler_poll(scheduler_t *scheduler);

/**
 * @brief Run the scheduler until all tasks are done.
 *
 * This function repeatedly calls scheduler_poll() in a loop until the active
 * task count drops to zero. After each poll:
 * - If no task was executed and on_idle is provided, it is called.
 * - Then, if on_loop is provided, it is called (regardless of whether a task
 *   was executed or not).
 *
 * The loop exits when all tasks have reached TASK_STATE_DONE.
 *
 * @param scheduler Scheduler instance
 * @param on_loop   Called at the start of each loop iteration (may be NULL)
 * @param on_idle   Called when no tasks are ready (may be NULL)
 * @return Number of tasks completed
 */
int scheduler_run(
    scheduler_t *scheduler,
    scheduler_callback_t on_loop,
    scheduler_callback_t on_idle);

/* ============================================================================
 *  Event Group API
 * ============================================================================ */

/**
 * @brief Initialize an event group.
 *
 * @param event_group Event group to initialize
 */
void event_group_init(event_group_t *event_group);

/**
 * @brief Check if event group satisfies a mask condition.
 *
 * @param event_group Event group to check
 * @param mask        Event mask
 * @param policy      EVENT_GROUP_ANY or EVENT_GROUP_ALL
 * @return 1 if satisfied, 0 otherwise
 */
int event_group_satisfied(
    const event_group_t *event_group,
    uint32_t mask,
    event_group_policy_t policy);

/**
 * @brief Set event bits in an event group.
 *
 * This function sets the specified bits in the event group's flag field.
 * It then scans the waiting task list and wakes up all tasks whose wait
 * conditions are now satisfied (based on their mask and policy).
 *
 * Each awakened task is unblocked (removed from the waiting list and from
 * the timer heap if it had a timeout), and inserted into the ready queue
 * at the appropriate priority level.
 *
 * The time complexity is O(N log M), where N is the number of waiting tasks
 * and M is the size of the timer heap (due to heap removal operations).
 *
 * The caller is responsible for yielding (TASK_YIELD or TASK_YIELD_IF_NEEDED)
 * to allow woken tasks to execute.
 *
 * @param event_group Event group
 * @param mask        Bits to set
 */
void event_group_set(event_group_t *event_group, uint32_t mask);

/**
 * @brief Get the current event flags.
 *
 * @param event_group Event group
 * @return Current flags value
 */
uint32_t event_group_get(const event_group_t *event_group);

/**
 * @brief Clear event bits in an event group.
 *
 * @param event_group Event group
 * @param mask        Bits to clear
 */
void event_group_clear(event_group_t *event_group, uint32_t mask);

/* ============================================================================
 *  Semaphore API
 * ============================================================================ */

/**
 * @brief Initialize a semaphore.
 *
 * @param semaphore     Semaphore to initialize
 * @param initial_count Initial counter value
 */
void semaphore_init(semaphore_t *semaphore, uint32_t initial_count);

/**
 * @brief Check if the semaphore is takeable (counter > 0).
 *
 * @param semaphore Semaphore to check
 * @return 1 if takeable, 0 otherwise
 */
int semaphore_takeable(const semaphore_t *semaphore);

/**
 * @brief Take a semaphore (decrement counter).
 *
 * @param semaphore Semaphore to take
 *
 * @note This function does not block. It returns immediately.
 *       Use `TASK_YIELD_UNTIL_SEMAPHORE_TAKEABLE` for blocking waits.
 */
void semaphore_take(semaphore_t *semaphore);

/**
 * @brief Give a semaphore (increment counter).
 *
 * If there are tasks waiting on this semaphore, the highest-priority task
 * (FIFO among equal priorities) is woken. The woken task is removed from
 * the timer heap if it had a pending timeout, resulting in O(log N)
 * time complexity, where N is the number of tasks in the timer heap.
 *
 * The caller is responsible for yielding (TASK_YIELD or
 * TASK_YIELD_IF_NEEDED) to allow the woken task to execute.
 *
 * @param semaphore Semaphore to give
 */
void semaphore_give(semaphore_t *semaphore);

/* ============================================================================
 *  Barrier API
 * ============================================================================ */

/**
 * @brief Initialize a barrier.
 *
 * @param barrier   Barrier to initialize
 * @param threshold Number of tasks required to release the barrier
 */
void barrier_init(barrier_t *barrier, uint16_t threshold);

/**
 * @brief Check if the barrier is passable (count >= threshold).
 *
 * @param barrier Barrier to check
 * @return 1 if passable, 0 otherwise
 */
int barrier_passable(const barrier_t *barrier);

/**
 * @brief Reset the barrier to 0.
 *
 * @param barrier Barrier to reset
 */
void barrier_reset(barrier_t *barrier);

/* ============================================================================
 *  Channel API
 * ============================================================================ */

/**
 * @brief Initialize a channel.
 *
 * @param channel Channel to initialize
 * @param buffer  Pointer to ring buffer storage
 * @param size    Total buffer size in bytes
 */
void channel_init(channel_t *channel, void *buffer, size_t size);

/**
 * @brief Send data to the channel.
 *
 * Attempts to write data to the channel. If space is available, the data is
 * copied and the highest-priority waiting receiver task is woken up
 * (FIFO among tasks with equal priority). If the channel is full, no data
 * is copied and no receiver task is woken up.
 *
 * The wakeup is guarded by a wakeup lock to prevent concurrent wakeups:
 * - If the lock is already held, the wakeup is skipped (the lock holder will
 *   handle the wakeup after completing its operation).
 * - If the lock is acquired, the task is woken and the lock is released by
 *   the woken task when it starts executing.
 *
 * Time complexity:
 * - Data copy: O(length) (linear in the number of bytes sent)
 * - Task wakeup: O(log M) if a receiver task is woken, where M is the
 *   number of tasks in the timer heap (for timeout heap removal).
 *
 * @param channel Channel to send to
 * @param data    Pointer to data to send
 * @param length  Number of bytes to send
 * @return Number of bytes sent (0 if full and no wakeup occurs)
 */
size_t channel_send(channel_t *channel, const void *data, size_t length);

/**
 * @brief Receive data from the channel.
 *
 * Attempts to read data from the channel. If data is available, it is copied
 * to the buffer and the highest-priority waiting sender task is woken up
 * (FIFO among tasks with equal priority). If the channel is empty, no data
 * is copied and no sender task is woken up.
 *
 * The wakeup is guarded by a wakeup lock to prevent concurrent wakeups:
 * - If the lock is already held, the wakeup is skipped (the lock holder will
 *   handle the wakeup after completing its operation).
 * - If the lock is acquired, the task is woken and the lock is released by
 *   the woken task when it starts executing.
 *
 * Time complexity:
 * - Data copy: O(size) (linear in the number of bytes received)
 * - Task wakeup: O(log M) if a sender task is woken, where M is the
 *   number of tasks in the timer heap (for timeout heap removal).
 *
 * @param channel Channel to receive from
 * @param buffer  Pointer to receive buffer
 * @param size    Buffer size in bytes
 * @return Number of bytes received (0 if empty and no wakeup occurs)
 */
size_t channel_receive(channel_t *channel, void *buffer, size_t size);

/**
 * @brief Get the total capacity of the channel buffer.
 *
 * @param channel Channel to query
 * @return Buffer size in bytes
 */
size_t channel_capacity(const channel_t *channel);

/**
 * @brief Get the amount of data currently in the channel.
 *
 * @param channel Channel to query
 * @return Data length in bytes
 */
size_t channel_length(const channel_t *channel);

/**
 * @brief Check if the channel is full.
 *
 * @param channel Channel to check
 * @return PS_TRUE if full, PS_FLASE otherwise
 */
int channel_full(const channel_t *channel);

/**
 * @brief Check if the channel is empty.
 *
 * @param channel Channel to check
 * @return PS_TRUE if empty, PS_FALSE otherwise
 */
int channel_empty(const channel_t *channel);

#ifdef __cplusplus
}
#endif

/* ============================================================================
 *  Task Definition Macros
 *
 *  These macros define the coroutine task model.
 *
 *  Usage:
 *  @code
 *  TASK_DEFINE(my_task)
 *  {
 *      TASK_BEGIN();
 *      // ... task body ...
 *      TASK_YIELD();
 *      // ... resume later ...
 *      TASK_RETURN();
 *      TASK_END();
 *  }
 *  @endcode
 *
 *  @warning Local variables are NOT preserved across TASK_YIELD, unless they are
 *           defined before TASK_BEGIN.
 *           Use a task frame structure (e.g., `my_frame_t`) for persistent data.
 *
 *  @warning Do NOT use `break` or `continue` inside the task body, as they may
 *           interfere with the control flow of the underlying macros.
 *           Avoid using `goto` to jump outside the task body.
 * ============================================================================ */

/**
 * @brief Define a task function.
 *
 * @param func Function name
 */
#define TASK_DEFINE(func) task_state_t func(task_t *const this_task)

/**
 * @name GNU Extension Implementation (GCC/Clang)
 *
 * Uses `&&` label address for fast resume.
 * @{
 */
#if (!PS_STRICT_C89 && (defined(__GNUC__) || defined(__clang__)))

/**
 * @brief Start a task.
 *
 * Must be the first statement in the task function.
 */
#define TASK_BEGIN()                                          \
    assert(task_get_state(this_task) != TASK_STATE_DONE);     \
    if (this_task->resume_point)                              \
    {                                                         \
        goto * this_task->resume_point;                       \
    }                                                         \
    /* If you see a error about this label, you likely forgot \
     * TASK_BEGIN or TASK_END, or duplicated them.            \
     */                                                       \
    TASK_BEGIN_END_MISMATCH_LABEL:                            \
    (void)&&TASK_BEGIN_END_MISMATCH_LABEL /* avoid no address-of-label warning */

/**
 * @brief Yield from a task.
 *
 * Suspends the current task and returns control to the scheduler.
 * The task will resume from the next statement after TASK_YIELD.
 */
#define TASK_YIELD()                                                           \
    this_task->resume_point = &&PS_MACRO_CONCAT(TASK_RESUME_POINT_, __LINE__); \
    return task_get_state(this_task);                                          \
    PS_MACRO_CONCAT(TASK_RESUME_POINT_, __LINE__) : (void)0 /* avoid warning */

/**
 * @brief Return from a task, marking it as DONE.
 */
#define TASK_RETURN() return TASK_STATE_DONE

/**
 * @brief End a task.
 *
 * Must be the last statement in the task function.
 */
#define TASK_END()                                            \
    /* If you see a error about this label, you likely forgot \
     * TASK_BEGIN or TASK_END, or duplicated them.            \
     */                                                       \
    (void)&&TASK_BEGIN_END_MISMATCH_LABEL;                    \
    TASK_RETURN()

/** @} */

/**
 * @name Portable Implementation
 *
 * Uses `switch` statement for resume.
 * @{
 */
#else

#if (PS_STRICT_C89)

/**
 * @internal
 * @brief Implementation of TASK_BEGIN for strict C89 compilers.
 *
 * Initializes the resume point and enters a switch statement.
 * The first case (`resume_index`) corresponds to the task's entry point.
 *
 * @param resume_index A unique compile-time index
 */
#define TASK_BEGIN_IMPL(resume_index)                     \
    static const int resume_base = resume_index;          \
    assert(task_get_state(this_task) != TASK_STATE_DONE); \
    switch (this_task->resume_point + resume_index)       \
    {                                                     \
        case resume_index: (void)0

/**
 * @internal
 * @brief Implementation of TASK_YIELD for strict C89 compilers.
 *
 * Saves the current resume point and returns to the scheduler.
 * The resume point is stored as `(resume_index - resume_base)`,
 * and the corresponding case label is `resume_index`.
 *
 * @param resume_index A unique compile-time index.
 */
#define TASK_YIELD_IMPL(resume_index)                       \
    this_task->resume_point = (resume_index - resume_base); \
    return task_get_state(this_task);                       \
    case resume_index: (void)0

#else

/**
 * @internal
 * @brief Implementation of TASK_BEGIN for compilers with loose C89 version.
 *
 * Similar to the strict C89 version, but the switch cases are optimized
 * for offset-based resume points (starting from 0).
 *
 * @param resume_index A unique compile-time index.
 */
#define TASK_BEGIN_IMPL(resume_index)                     \
    static const int resume_base = resume_index;          \
    assert(task_get_state(this_task) != TASK_STATE_DONE); \
    switch (this_task->resume_point)                      \
    {                                                     \
        case 0: (void)0

/**
 * @internal
 * @brief Implementation of TASK_YIELD for compilers with GNU extensions.
 *
 * Saves the resume point as an offset `(resume_index - resume_base)`,
 * and the case label is that offset value.
 *
 * @param resume_index A unique compile-time index.
 */
#define TASK_YIELD_IMPL(resume_index)                       \
    this_task->resume_point = (resume_index - resume_base); \
    return task_get_state(this_task);                       \
    case (resume_index - resume_base): (void)0

#endif

/**
 * @brief Start a task.
 */
#define TASK_BEGIN()  TASK_BEGIN_IMPL(PS_UNIQUE_ID)

/**
 * @brief Yield from a task.
 */
#define TASK_YIELD()  TASK_YIELD_IMPL(PS_UNIQUE_ID)

/**
 * @brief Return from a task, marking it as DONE.
 */
#define TASK_RETURN() return TASK_STATE_DONE

/**
 * @brief End a task.
 */
#define TASK_END() \
    }              \
    TASK_RETURN()

#endif

/** @} */

/* ============================================================================
 *  Control Flow Macros
 * ============================================================================ */

#define TASK_GET_TICK() (this_task->scheduler->tick)

/**
 * @brief Yield if a higher-priority task is ready.
 */
#define TASK_YIELD_IF_NEEDED()            \
    do                                    \
    {                                     \
        if (task_should_yield(this_task)) \
        {                                 \
            TASK_YIELD();                 \
        }                                 \
    } while (0)

/**
 * @brief Delay the current task for a given number of ticks.
 *
 * @param ticks Number of ticks to delay
 */
#define TASK_DELAY(ticks)                      \
    do                                         \
    {                                          \
        task_schedule_delay(this_task, ticks); \
        TASK_YIELD();                          \
    } while (0)

/**
 * @brief Delay until a fixed phase is reached.
 *
 * @param last_wakeup_tick Pointer to the previous wakeup tick
 * @param delay_ticks      Interval between wakeups
 */
#define TASK_DELAY_UNTIL(last_wakeup_tick, delay_ticks)                          \
    do                                                                           \
    {                                                                            \
        if (task_schedule_delay_until(this_task, last_wakeup_tick, delay_ticks)) \
        {                                                                        \
            TASK_YIELD();                                                        \
        }                                                                        \
    } while (0)

/**
 * @brief Generic blocking wait macro.
 *
 * Waits until the blocking function returns 0 (condition satisfied).
 * If a timeout occurs, jumps to a finish label.
 *
 * @param block_func Blocking function (e.g., task_block_if_channel_full)
 * @param args       Arguments to the blocking function
 */
#define TASK_YIELD_UNTIL(block_func, args)                                                 \
    do                                                                                     \
    {                                                                                      \
        while (block_func args)                                                            \
        {                                                                                  \
            TASK_YIELD();                                                                  \
            if (task_get_state(this_task) == TASK_STATE_TIMEOUT)                           \
            {                                                                              \
                goto PS_MACRO_CONCAT(TASK_YIELD_UNTIL_FINISH_, __LINE__);                  \
            }                                                                              \
        }                                                                                  \
        PS_MACRO_CONCAT(TASK_YIELD_UNTIL_FINISH_, __LINE__) : (void)0; /* avoid warning */ \
    } while (0)

/**
 * @brief Wait until an event group is satisfied.
 *
 * @param event_group Event group to wait on
 * @param mask        Event mask
 * @param policy      EVENT_GROUP_ANY or EVENT_GROUP_ALL
 * @param timeout     Timeout in ticks (0 = never timeout)
 */
#define TASK_YIELD_UNTIL_EVENT_GROUP_SATISFIED(event_group, mask, policy, timeout) \
    TASK_YIELD_UNTIL(task_block_if_event_group_unsatisfied,                        \
                     (this_task, event_group, mask, policy, timeout))

/**
 * @brief Wait until a semaphore is takeable (counter > 0).
 *
 * @param semaphore Semaphore to wait on
 * @param timeout   Timeout in ticks (0 = never timeout)
 */
#define TASK_YIELD_UNTIL_SEMAPHORE_TAKEABLE(semaphore, timeout) \
    TASK_YIELD_UNTIL(task_block_if_semaphore_untakeable, (this_task, semaphore, timeout))

/**
 * @brief Wait until a barrier is passable.
 *
 * @param barrier Barrier to wait on
 * @param timeout Timeout in ticks (0 = never timeout)
 */
#define TASK_YIELD_UNTIL_BARRIER_PASSABLE(barrier, timeout) \
    TASK_YIELD_UNTIL(task_block_if_barrier_unpassable, (this_task, barrier, timeout))

/**
 * @brief Wait until a channel has space (not full).
 *
 * @param channel Channel to wait on
 * @param timeout Timeout in ticks (0 = never timeout)
 */
#define TASK_YIELD_UNTIL_CHANNEL_HAS_SPACE(channel, timeout) \
    TASK_YIELD_UNTIL(task_block_if_channel_full, (this_task, channel, timeout))

/**
 * @brief Wait until a channel has data (not empty).
 *
 * @param channel Channel to wait on
 * @param timeout Timeout in ticks (0 = never timeout)
 */
#define TASK_YIELD_UNTIL_CHANNEL_HAS_DATA(channel, timeout) \
    TASK_YIELD_UNTIL(task_block_if_channel_empty, (this_task, channel, timeout))

#endif /* PROTOSCHED_H */
