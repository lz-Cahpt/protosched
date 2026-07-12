/**
 * @file protosched.c
 * @author lz-Cahpt (liangzcheng@foxmail.com)
 * @brief Implementation of protosched
 * @version 1.0.0
 * @date 2026-07-09
 * @copyright Copyright (c) 2026
 */

#include "protosched.h"

#include <string.h>

#define TICK_AFTER(a, b)  ((int32_t)(b) - (int32_t)(a) < 0)
#define TICK_BEFORE(a, b) TICK_AFTER(b, a)

#define TASK_HEAP_NODE_TAG_MASK         ((uintptr_t)1)
#define TASK_HEAP_NODE_POINTER_MASK     (~(uintptr_t)1)
#define TASK_STATE_MASK                 ((uint32_t)0x00000003)
#define TASK_STATE_OFFSET               (0)
#define TASK_PRIORRITY_MASK             ((uint32_t)0x0000007C)
#define TASK_PRIORRITY_OFFSET           (2)
#define TASK_WAIT_ALL_EVENT_FLAG_MASK   ((uint32_t)0x00000080)
#define TASK_WAIT_ALL_EVENT_FLAG_OFFSET (7)

#define CHANNEL_WAITING_TASK_LIST_MASK ((uintptr_t)1)
#define CHANNEL_LOCK_FLAG_MASK         ((uintptr_t)1)

static size_t min(size_t x, size_t y)
{
    return (x < y ? x : y);
}

static int clz(uint32_t value)
{
#if defined(__CLZ)
    return __CLZ(value);
#elif (defined(__GNUC__) || defined(__clang__))
    return __builtin_clz(value);
#else
    int count = 0;
    uint32_t mask = 0x80000000;
    while ((value & mask) == 0)
    {
        count += 1;
        mask = mask >> 1;
    }
    return count;
#endif
}

static void task_set_state(task_t *task, task_state_t state)
{
    assert(task);
    task->flags_priority_status = (task->flags_priority_status & ~TASK_STATE_MASK) | state;
}

task_state_t task_get_state(const task_t *task)
{
    assert(task);
    return task->flags_priority_status & TASK_STATE_MASK;
}

static void task_set_priority(task_t *task, int priority)
{
    assert(task);
    assert(0 <= priority && priority < 32);
    task->flags_priority_status =
        (task->flags_priority_status & ~TASK_PRIORRITY_MASK) |
        ((uint32_t)priority << TASK_PRIORRITY_OFFSET);
}

static int task_get_priority(const task_t *task)
{
    assert(task);
    return (task->flags_priority_status & TASK_PRIORRITY_MASK) >> TASK_PRIORRITY_OFFSET;
}

static void task_set_flag(task_t *task, uint32_t mask, int flag)
{
    assert(task);
    task->flags_priority_status =
        flag ? (task->flags_priority_status | mask)
             : (task->flags_priority_status & ~mask);
}

static int task_get_flag(const task_t *task, uint32_t mask)
{
    assert(task);
    return !!(task->flags_priority_status & mask);
}

void task_init(task_t *task, int priority, task_entry_t entry)
{
    assert(task);
    assert(entry);

    task->parent = NULL;
    task->left_ltag = 0;
    task->right_rtag = 0;
    task->prev = task;
    task->next = task;
    task->scheduler = NULL;
    task->wait_list = NULL;
    task->event_mask = 0;
    task->flags_priority_status = 0;
    task_set_state(task, TASK_STATE_READY);
    task_set_priority(task, priority);
    task->entry = entry;
#if (!PS_STRICT_C89 && (defined(__GNUC__) || defined(__clang__)))
    task->resume_point = NULL;
#else
    task->resume_point = 0;
#endif
}

static void task_list_node_link(task_t *prev, task_t *next)
{
    assert(prev);
    assert(next);
    prev->next = next;
    next->prev = prev;
}

static void task_list_node_detach(task_t *task)
{
    assert(task);
    task_list_node_link(task->prev, task->next);
    task_list_node_link(task, task);
}

static void task_list_init(task_list_t *task_list)
{
    assert(task_list);
    task_list->head = NULL;
}

static task_t *task_list_front(const task_list_t *task_list)
{
    assert(task_list);
    return task_list->head;
}

static int task_list_empty(const task_list_t *task_list)
{
    assert(task_list);
    return task_list->head == NULL;
}

/* static */ void task_list_push_front(task_list_t *task_list, task_t *new_task)
{
    assert(task_list);
    assert(new_task);

    /* new_task cannot already be in any task list. */
    assert(new_task->wait_list == NULL);
    assert(new_task->prev == new_task);
    assert(new_task->next == new_task);

    if (!task_list->head)
    {
        task_list->head = new_task;
    }
    else
    {
        task_list_node_link(task_list->head->prev, new_task);
        task_list_node_link(new_task, task_list->head);
        task_list->head = new_task;
    }
}

static void task_list_push_back(task_list_t *task_list, task_t *new_task)
{
    assert(task_list);
    assert(new_task);

    /* new_task cannot already be in any task list. */
    assert(new_task->wait_list == NULL);
    assert(new_task->prev == new_task);
    assert(new_task->next == new_task);

    if (!task_list->head)
    {
        task_list->head = new_task;
    }
    else
    {
        task_list_node_link(task_list->head->prev, new_task);
        task_list_node_link(new_task, task_list->head);
    }
}

/* static */ void task_list_concat_front(task_list_t *task_list, task_list_t *other_task_list)
{
    task_t *task_liat_tail, *other_task_list_tail;

    assert(task_list);
    assert(other_task_list);
    assert(task_list != other_task_list);

    if (other_task_list->head)
    {
        if (task_list->head)
        {
            task_liat_tail = task_list->head->prev;
            other_task_list_tail = other_task_list->head->prev;

            task_list_node_link(task_liat_tail, other_task_list->head);
            task_list_node_link(other_task_list_tail, task_list->head);
        }

        task_list->head = other_task_list->head;
        other_task_list->head = NULL;
    }
}

/**
 * @brief Insert a task into a sorted list by priority.
 *
 * Tasks are ordered by priority (highest first). When priorities are equal,
 * FIFO order is preserved (earlier insertion precedes later ones).
 *
 * @param task_list Target list
 * @param new_task  Task to insert (must not be in any list)
 */
static void task_list_insert_by_priority(task_list_t *task_list, task_t *new_task)
{
    int priority;
    task_t *cursor;

    assert(task_list);
    assert(new_task);

    /* new_task cannot already be in any task list. */
    assert(new_task->wait_list == NULL);
    assert(new_task->prev == new_task);
    assert(new_task->next == new_task);

    if (!task_list->head)
    {
        task_list->head = new_task;
        return;
    }

    priority = task_get_priority(new_task);
    cursor = task_list->head;

    while (1)
    {
        /* If cursor has lower or equal priority, insert before it. */
        /* If priority is equal, we continue to the next node to maintain FIFO. */
        if (priority <= task_get_priority(cursor))
        {
            cursor = cursor->next;

            if (cursor != task_list->head)
            {
                continue;
            }

            /* new_task has the lowest priority: append to tail. */
            task_list_node_link(task_list->head->prev, new_task);
            task_list_node_link(new_task, task_list->head);
        }
        else
        {
            /* New task has higher priority than cursor: insert before cursor. */
            task_list_node_link(cursor->prev, new_task);
            task_list_node_link(new_task, cursor);

            /* If cursor is the head, the new_task becomes the new head. */
            if (cursor == task_list->head)
            {
                task_list->head = new_task;
            }
        }

        break;
    }
}

static void task_list_pop_front(task_list_t *task_list)
{
    task_t *new_head;

    assert(task_list);

    if (!task_list->head)
    {
        return; /* Nothing to pop. */
    }

    if (task_list->head == task_list->head->next)
    {
        /* Only one node in the list. */
        assert(task_list->head == task_list->head->prev);
        task_list->head = NULL;
    }
    else
    {
        new_head = task_list->head->next;
        task_list_node_detach(task_list->head);
        task_list->head = new_head;
    }
}

static void task_list_advance(task_list_t *task_list)
{
    assert(task_list);

    if (task_list->head)
    {
        task_list->head = task_list->head->next;
    }
}

static void task_list_remove(task_list_t *task_list, task_t *task)
{
    assert(task_list);
    assert(task);

    if (!task_list->head)
    {
        /* Empty task list. */
        return;
    }

    if (task_list->head != task)
    {
        task_list_node_detach(task);
        return;
    }

    /* Remove the head node. */
    if (task_list->head->next == task)
    {
        /* Only one node in the list. */
        assert(task_list->head->prev == task);
        task_list->head = NULL;
        return;
    }

    /* More than one node: advance head, then detach old head. */
    task_list->head = task_list->head->next;
    task_list_node_detach(task);
}

static task_t *task_heap_node_get_left(const task_t *task)
{
    assert(task);
    return (task_t *)(task->left_ltag & TASK_HEAP_NODE_POINTER_MASK);
}

static task_t *task_heap_node_get_right(const task_t *task)
{
    assert(task);
    return (task_t *)(task->right_rtag & TASK_HEAP_NODE_POINTER_MASK);
}

static int task_heap_node_get_ltag(const task_t *task)
{
    assert(task);
    return (task->left_ltag & TASK_HEAP_NODE_TAG_MASK);
}

static int task_heap_node_get_rtag(const task_t *task)
{
    assert(task);
    return (task->right_rtag & TASK_HEAP_NODE_TAG_MASK);
}

static void task_heap_node_set_left(task_t *task, task_t *left)
{
    assert(task);
    task->left_ltag =
        ((uintptr_t)left & TASK_HEAP_NODE_POINTER_MASK) |
        (task->left_ltag & TASK_HEAP_NODE_TAG_MASK);
}

static void task_heap_node_set_right(task_t *task, task_t *right)
{
    assert(task);
    task->right_rtag =
        ((uintptr_t)right & TASK_HEAP_NODE_POINTER_MASK) |
        (task->right_rtag & TASK_HEAP_NODE_TAG_MASK);
}

static void task_heap_node_set_left_ltag(task_t *task, task_t *left, int ltag)
{
    assert(task);
    task->left_ltag =
        ((uintptr_t)left & TASK_HEAP_NODE_POINTER_MASK) |
        (!!ltag & TASK_HEAP_NODE_TAG_MASK);
}

static void task_heap_node_set_right_rtag(task_t *task, task_t *right, int rtag)
{
    assert(task);
    task->right_rtag =
        ((uintptr_t)right & TASK_HEAP_NODE_POINTER_MASK) |
        (!!rtag & TASK_HEAP_NODE_TAG_MASK);
}

/**
 * @brief Replace a node in the task heap with another node.
 *
 * This function completely swaps the position of `task` with `other_task` in
 * the heap. All pointers (parent, left, right) and tags are transferred.
 *
 * @param task_heap      Heap containing the node
 * @param replaced_task  Node to be replaced (must be in the heap)
 * @param other_task     Node that takes over the position
 */
static void task_heap_node_replace(task_heap_t *task_heap, task_t *replaced_task, task_t *other_task)
{
    int replaced_task_ltag, replaced_task_rtag;
    task_t *replaced_task_left, *replaced_task_right;

    assert(task_heap);
    assert(replaced_task);
    assert(other_task);

    replaced_task_ltag = task_heap_node_get_ltag(replaced_task);
    replaced_task_rtag = task_heap_node_get_rtag(replaced_task);
    replaced_task_left = task_heap_node_get_left(replaced_task);
    replaced_task_right = task_heap_node_get_right(replaced_task);

    other_task->parent = replaced_task->parent;
    other_task->left_ltag = replaced_task->left_ltag;
    other_task->right_rtag = replaced_task->right_rtag;

    /* Update children: if it's a thread (tagged), update the thread pointer;
     * otherwise, update the child's parent pointer. */
    if (replaced_task_left)
    {
        if (replaced_task_ltag)
        {
            task_heap_node_set_right(replaced_task_left, other_task);
        }
        else
        {
            replaced_task_left->parent = other_task;
        }
    }

    if (replaced_task_right)
    {
        if (replaced_task_rtag)
        {
            task_heap_node_set_left(replaced_task_right, other_task);
        }
        else
        {
            replaced_task_right->parent = other_task;
        }
    }

    /* Update parent: if replaced_task is root, update heap root;
     * otherwise, update the parent's child pointer.
     */
    if (!replaced_task->parent)
    {
        assert(task_heap->root == replaced_task); /* ? */
        task_heap->root = other_task;
    }
    else
    {
        /* Determine whether replaced_task is the left or right child by direct comparison. */
        if (replaced_task == task_heap_node_get_left(replaced_task->parent))
        {
            /* Left child pointer must be real (not a thread). */
            assert(!task_heap_node_get_ltag(replaced_task->parent));
            task_heap_node_set_left(replaced_task->parent, other_task);
        }
        else
        {
            /* Assertions below are used to help detect heap structure errors during
             * development. They can be removed in release builds.
             *
             * If left child exists, replaced_task cannot be the right child in a
             * standard binary heap, as the right child cannot exist without the left.
             * However, if the heap is corrupted, it may happen. These assertions
             * verify that the heap remains a valid binary heap (complete binary tree).
             */
            assert(task_heap_node_get_left(replaced_task->parent));
            assert(!task_heap_node_get_ltag(replaced_task->parent));

            /* Verify replaced_task is indeed the right child and it is valid. */
            assert(replaced_task == task_heap_node_get_right(replaced_task->parent));
            assert(!task_heap_node_get_rtag(replaced_task->parent));

            task_heap_node_set_right(replaced_task->parent, other_task);
        }
    }

    /* Update heap head/tail if they point to the replaced_task */
    if (task_heap->head == replaced_task)
    {
        task_heap->head = other_task;
    }

    if (task_heap->tail == replaced_task)
    {
        task_heap->tail = other_task;
    }
}

/**
 * @brief Swap the positions of two nodes within the same heap.
 *
 * This function exchanges the heap positions of `task` and `other_task` by
 * performing three node replacements. After the swap, each node will be in
 * the other node's original location, with all parent/child pointers and
 * heap metadata (root, head, tail) updated accordingly.
 *
 * @note Both nodes must belong to the same heap. Swapping nodes from
 *       different heaps would corrupt the internal pointers of both heaps
 *       and lead to undefined behavior. The caller is responsible for
 *       ensuring this condition.
 */
static void task_heap_swap_node(task_heap_t *task_heap, task_t *task, task_t *other_task)
{
    task_t temp;

    assert(task_heap);
    assert(task);
    assert(other_task);

    task_heap_node_replace(task_heap, task, &temp);
    task_heap_node_replace(task_heap, other_task, task);
    task_heap_node_replace(task_heap, &temp, other_task);
}

/**
 * @brief Remove the tail node (the last node in level-order) from the heap.
 *
 * The tail node is always the last element in the heap's level-order
 * traversal. In a complete binary heap, it is a leaf, meaning it has
 * no children. This function removes that node and updates all relevant
 * pointers to maintain the heap structure and threaded level-order links.
 *
 * @param task_heap The heap from which to remove the tail.
 */
static void task_heap_remove_tail(task_heap_t *task_heap)
{
    task_t *tail;
    task_t *new_tail;

    assert(task_heap);

    tail = task_heap->tail;
    if (!tail)
    {
        return; /* Nothing to remove. */
    }

    /* In a complete heap, the tail cannot have a right child or thread. */
    assert(!tail->right_rtag);

    if (tail == task_heap->root)
    {
        /* The heap has only one node. */
        assert(tail == task_heap->head);
        assert(!tail->parent);    /* Root cannot have parent node. */
        assert(!tail->left_ltag); /* The only one node cannot have a child or thread. */

        task_heap->root = NULL;
        task_heap->head = NULL;
        task_heap->tail = NULL;
    }
    else
    {
        assert(tail->parent);

        /* Determine whether tail is the left or right child of its parent. */
        if (tail == task_heap_node_get_left(tail->parent))
        {
            /* Tail is the left child: simply clear the left pointer. */
            assert(!task_heap_node_get_ltag(tail->parent));
            task_heap_node_set_left(tail->parent, NULL);
        }
        else
        {
            /* Tail is the right child: the left child must exist (heap property).
             * After removing the right child, it's parent will become the first
             * incomplete node in level-order traversal again.
             */
            assert(task_heap_node_get_left(tail->parent));
            assert(!task_heap_node_get_ltag(tail->parent));

            assert(tail == task_heap_node_get_right(tail->parent));
            assert(!task_heap_node_get_rtag(tail->parent));

            task_heap_node_set_left_ltag(task_heap->head, tail->parent, 1);
            task_heap_node_set_right_rtag(tail->parent, task_heap->head, 1);
            task_heap->head = tail->parent;
        }

        /* new_tail is the level-order predecessor of the removed tail. */
        assert(task_heap_node_get_ltag(tail));
        new_tail = task_heap_node_get_left(tail);

        assert(new_tail);
        assert(tail == task_heap_node_get_right(new_tail));
        assert(task_heap_node_get_rtag(new_tail));

        task_heap_node_set_right_rtag(new_tail, NULL, 0);
        task_heap->tail = new_tail;
    }

    /* Clear the removed node's pointers to detach it from the heap. */
    tail->parent = NULL;
    task_heap_node_set_left_ltag(tail, NULL, 0);
}

/**
 * @brief Move a node upward in the timer heap to restore heap order.
 *
 * @param task_heap The heap containing the node.
 * @param task      The node to swim up. Must already be in the heap.
 */
static void task_heap_swim_node(task_heap_t *task_heap, task_t *task)
{
    task_t *parent;

    assert(task_heap);
    assert(task);

    for (parent = task->parent; parent; parent = task->parent)
    {
        assert(task != parent);
        assert(task == task_heap_node_get_left(parent)
                   ? !task_heap_node_get_ltag(parent)
                   : (task == task_heap_node_get_right(parent)
                          ? !task_heap_node_get_rtag(parent)
                          : PS_FALSE));

        /* If the current node's wakeup tick is not earlier than its parent's,
         * the heap order is satisfied for this path.
         */
        if (!TICK_BEFORE(task->wakeup_tick, parent->wakeup_tick))
        {
            break;
        }

        /* Otherwise, swap the node with its parent and continue. */
        task_heap_swap_node(task_heap, task, parent);
    }

    /* After the loop, the following invariant should hold:
     * - If the node has a parent, it must not be the heap root.
     * - If the node has no parent, it must be the heap root.
     * This assertion validates the root pointer consistency.
     */
    assert((task->parent && (task != task_heap->root)) ||
           (!task->parent && (task == task_heap->root)));
}

/**
 * @brief Move a node downward in the timer heap to restore heap order.
 *
 * @param task_heap The heap containing the node.
 * @param task      The node to sink down. Must already be in the heap.
 */
static void task_heap_sink_node(task_heap_t *task_heap, task_t *task)
{
    int ltag, rtag;
    task_t *left, *right;

    assert(task_heap);
    assert(task);

    while (1)
    {
        ltag = task_heap_node_get_ltag(task);
        rtag = task_heap_node_get_rtag(task);
        left = task_heap_node_get_left(task);
        right = task_heap_node_get_right(task);

        if (ltag || !left)
        {
            break;
        }

        /* Left child exists. */
        assert(task != left);
        assert(task == left->parent);

        if (TICK_AFTER(task->wakeup_tick, left->wakeup_tick))
        {
            task_heap_swap_node(task_heap, task, left);
            continue;
        }

        if (rtag || !right)
        {
            break;
        }

        /* Right child exists. */
        assert(task != right);
        assert(task == right->parent);

        if (TICK_AFTER(task->wakeup_tick, right->wakeup_tick))
        {
            task_heap_swap_node(task_heap, task, right);
            continue;
        }

        /* Neither child is smaller. */
        break;
    }

    /* Verify the heap node of the complete binary tree. */
    assert(!ltag || left);           /* Left child pointer must be non-NULL */
    assert(!rtag || right);          /* Right child pointer must be non-NULL */
    assert(left || rtag || !right);  /* No right child without left child */
    assert(!ltag || rtag || !right); /* If left exists, right is either thread or NULL */
}

static void task_heap_init(task_heap_t *task_heap)
{
    assert(task_heap);
    task_heap->root = NULL;
    task_heap->head = NULL;
    task_heap->tail = NULL;
}

/**
 * @brief Insert a new task into the threaded binary heap.
 *
 * @param task_heap The heap to insert into.
 * @param new_task  The task to insert (must not already be in any heap).
 */
static void task_heap_insert(task_heap_t *task_heap, task_t *new_task)
{
    task_t *new_head;

    assert(task_heap);
    assert(new_task);

    if (!task_heap->root)
    {
        assert(!new_task->parent);
        assert(!new_task->left_ltag);
        assert(!new_task->right_rtag);

        task_heap->root = new_task;
        task_heap->head = new_task;
        task_heap->tail = new_task;

        return;
    }

    /* Heap is not empty, head and tail must exist. */
    assert(task_heap->head);
    assert(task_heap->tail);

    if (!task_heap_node_get_left(task_heap->head))
    {
        /* Head has no left child: insert as left child of head. */
        assert(!task_heap_node_get_ltag(task_heap->head));
        task_heap_node_set_left_ltag(task_heap->head, new_task, 0);
        new_task->parent = task_heap->head;
    }
    else
    {
        /* Head has left child: insert as right child and advance head */
        new_head = task_heap_node_get_right(task_heap->head);

        assert(task_heap_node_get_rtag(task_heap->head) && new_head);
        assert(task_heap->head == task_heap_node_get_left(new_head));
        assert(task_heap_node_get_ltag(new_head));

        task_heap_node_set_right_rtag(task_heap->head, new_task, 0);
        new_task->parent = task_heap->head;
        task_heap->head = new_head;
        task_heap_node_set_left_ltag(new_head, NULL, 0);
    }

    /* Update tail and threaded links */
    task_heap_node_set_left_ltag(new_task, task_heap->tail, 1);
    task_heap_node_set_right_rtag(task_heap->tail, new_task, 1);
    task_heap->tail = new_task;
    task_heap_node_set_right_rtag(new_task, NULL, 0);

    /* Restore heap order by swimming up */
    task_heap_swim_node(task_heap, new_task);
}

static void task_heap_remove(task_heap_t *task_heap, task_t *task)
{
    task_t *tail;

    assert(task_heap);
    assert(task);

    if (task == task_heap->tail)
    {
        task_heap_remove_tail(task_heap);
        return;
    }

    tail = task_heap->tail;
    task_heap_swap_node(task_heap, task, tail);
    task_heap_remove_tail(task_heap);

    task_heap_sink_node(task_heap, tail);
}

static task_t *task_heap_top(const task_heap_t *task_heap)
{
    assert(task_heap);
    return task_heap->root;
}

/* static */ int task_heap_empty(const task_heap_t *task_heap)
{
    assert(task_heap);
    return !task_heap->root;
}

void task_schedule_delay(task_t *task, int32_t delay_ticks)
{
    int priority;
    scheduler_t *scheduler;

    assert(task);
    assert(delay_ticks > 0);

    priority = task_get_priority(task);
    scheduler = task->scheduler;

    assert(scheduler);
    assert(task == task_list_front(&scheduler->ready_task_lists[priority]));

    /* 1. Remove from ready list and update priority bitmap. */
    task_list_pop_front(&scheduler->ready_task_lists[priority]);
    if (task_list_empty(&scheduler->ready_task_lists[priority]))
    {
        scheduler->ready_bitmap &= ~((uint32_t)1 << priority);
    }

    /* 2. Mark as waiting (but not yet blocked!). */
    task_set_state(task, TASK_STATE_WAITING);

    /* 3. Pure timer delay: no sync primitive associated */
    task->wait_list = NULL;

    /* 4. Arm the timer. Execution continues; caller MUST yield immediately. */
    task->wakeup_tick = scheduler->tick + (uint32_t)delay_ticks;
    task_heap_insert(&scheduler->waiting_task_heap, task);
}

int task_schedule_delay_until(task_t *task, uint32_t *last_wakeup_tick, int32_t delay_ticks)
{
    int priority;
    scheduler_t *scheduler;
    uint32_t current_tick;

    assert(task);
    assert(last_wakeup_tick);
    assert(delay_ticks > 0);

    priority = task_get_priority(task);
    scheduler = task->scheduler;
    current_tick = scheduler->tick;

    assert(scheduler);
    assert(task == task_list_front(&scheduler->ready_task_lists[priority]));

    /* 1. Check if the deadline has already passed (missed the phase) */
    if ((int32_t)current_tick - (int32_t)*last_wakeup_tick >= delay_ticks)
    {
        *last_wakeup_tick = current_tick;
        return PS_FALSE;
    }

    /* 2. Remove from ready list and update priority bitmap. */
    task_list_pop_front(&scheduler->ready_task_lists[priority]);
    if (task_list_empty(&scheduler->ready_task_lists[priority]))
    {
        scheduler->ready_bitmap &= ~((uint32_t)1 << priority);
    }

    /* 3. Mark as waiting (but not yet blocked!). */
    task_set_state(task, TASK_STATE_WAITING);

    /* 4. Pure timer delay: no sync primitive associated */
    task->wait_list = NULL;

    /* 5. Calculate the next absolute wakeup tick based on the phase */
    task->wakeup_tick = *last_wakeup_tick + (uint32_t)delay_ticks;
    *last_wakeup_tick = task->wakeup_tick;

    /* 6. Insert into timer heap (task will be woken by scheduler_poll) */
    task_heap_insert(&scheduler->waiting_task_heap, task);

    return PS_TRUE;
}

static void task_detach_wait_list_and_wakeup(task_t *task)
{
    int priority;
    scheduler_t *scheduler;

    assert(task);
    assert(task_get_state(task) == TASK_STATE_WAITING);

    priority = task_get_priority(task);
    scheduler = task->scheduler;

    /* If task is waiting on a synchronization primitive (channel/event/semaphore/barrier),
     * remove it from that primitive's waiting list. For pure timer delay, wait_list is NULL.
     */
    if (task->wait_list)
    {
        task_list_remove(task->wait_list, task);
        task->wait_list = NULL;
    }

    task_set_state(task, TASK_STATE_READY);

    /* Insert into ready list at the tail of its priority level. */
    task_list_push_back(&scheduler->ready_task_lists[priority], task);
    scheduler->ready_bitmap |= 1 << priority;

    /* If the task is still in the timer heap (due to timeout waiting),
     * remove it to prevent duplicate wakeup from timeout.
     */
    if (task->parent || task == task_heap_top(&scheduler->waiting_task_heap))
    {
        task_heap_remove(&scheduler->waiting_task_heap, task);
    }
}

int task_should_yield(const task_t *task)
{
    assert(task);
    assert(task->scheduler);
    return task_get_priority(task) < (31 - clz(task->scheduler->ready_bitmap));
}

/* TODO: Perhaps we could optimize the redundant code in the task_block_if_XXX
 *       function, but I haven't come up with any good ideas.
 */
int task_block_if_event_group_unsatisfied(
    task_t *task,
    event_group_t *event_group,
    uint32_t mask,
    event_group_policy_t policy,
    int32_t timeout_ticks)
{
    int priority;
    scheduler_t *scheduler;

    assert(task);
    assert(event_group);
    assert(timeout_ticks >= 0);

    if (event_group_satisfied(event_group, mask, policy))
    {
        return PS_FALSE;
    }

    priority = task_get_priority(task);
    scheduler = task->scheduler;

    assert(scheduler);
    assert(task == task_list_front(&scheduler->ready_task_lists[priority]));

    task_list_pop_front(&scheduler->ready_task_lists[priority]);
    if (task_list_empty(&scheduler->ready_task_lists[priority]))
    {
        scheduler->ready_bitmap &= ~((uint32_t)1 << priority);
    }

    task->event_mask = mask;
    task_set_state(task, TASK_STATE_WAITING);
    task_set_flag(task, TASK_WAIT_ALL_EVENT_FLAG_MASK, policy == EVENT_GROUP_ALL);

    /* Wake all at once, no need for a specific order. */
    task_list_push_back(&event_group->waiting_task_list, task);
    task->wait_list = &event_group->waiting_task_list;

    /* 0 for never timeout, wait forever. */
    if (timeout_ticks > 0)
    {
        task->wakeup_tick = scheduler->tick + (uint32_t)timeout_ticks;
        task_heap_insert(&scheduler->waiting_task_heap, task);
    }

    return PS_TRUE;
}

int task_block_if_semaphore_untakeable(task_t *task, semaphore_t *semaphore, int32_t timeout_ticks)
{
    int priority;
    scheduler_t *scheduler;

    assert(task);
    assert(semaphore);
    assert(timeout_ticks >= 0);

    if (semaphore_takeable(semaphore))
    {
        return PS_FALSE;
    }

    priority = task_get_priority(task);
    scheduler = task->scheduler;

    assert(scheduler);
    assert(task == task_list_front(&scheduler->ready_task_lists[priority]));

    task_list_pop_front(&scheduler->ready_task_lists[priority]);
    if (task_list_empty(&scheduler->ready_task_lists[priority]))
    {
        scheduler->ready_bitmap &= ~((uint32_t)1 << priority);
    }

    task_set_state(task, TASK_STATE_WAITING);

    /* The highest priority task needs to be awakened first. */
    task_list_insert_by_priority(&semaphore->waiting_task_list, task);
    task->wait_list = &semaphore->waiting_task_list;

    /* 0 for never timeout, wait forever. */
    if (timeout_ticks > 0)
    {
        task->wakeup_tick = scheduler->tick + (uint32_t)timeout_ticks;
        task_heap_insert(&scheduler->waiting_task_heap, task);
    }

    return PS_TRUE;
}

static void barrier_signal(barrier_t *barrier);

int task_block_if_barrier_unpassable(task_t *task, barrier_t *barrier, int32_t timeout_ticks)
{
    int priority;
    scheduler_t *scheduler;

    assert(task);
    assert(barrier);
    assert(timeout_ticks >= 0);

    if (barrier->count >= barrier->threshold)
    {
        return PS_FALSE;
    }

    assert(barrier->count < UINT16_MAX);
    barrier->count++;

    /* TODO: The current design of waking up all waiting tasks here is not optimal,
     *       but at least it functions. I will redesign this part (perhaps).
     */
    if (barrier->count >= barrier->threshold)
    {
        barrier_signal(barrier);
        return PS_FALSE;
    }

    priority = task_get_priority(task);
    scheduler = task->scheduler;

    assert(scheduler);
    assert(task == task_list_front(&scheduler->ready_task_lists[priority]));

    task_list_pop_front(&scheduler->ready_task_lists[priority]);
    if (task_list_empty(&scheduler->ready_task_lists[priority]))
    {
        scheduler->ready_bitmap &= ~((uint32_t)1 << priority);
    }

    task_set_state(task, TASK_STATE_WAITING);

    /* Wake all at once, no need for a specific order. */
    task_list_push_back(&barrier->waiting_task_list, task);
    task->wait_list = &barrier->waiting_task_list;

    /* 0 for never timeout, wait forever. */
    if (timeout_ticks > 0)
    {
        task->wakeup_tick = scheduler->tick + (uint32_t)timeout_ticks;
        task_heap_insert(&scheduler->waiting_task_heap, task);
    }

    return PS_TRUE;
}

static int channel_wakeup_lock(channel_t *channel);
static void channel_wakeup_unlock(channel_t *channel);

int task_block_if_channel_full(task_t *task, channel_t *channel, int32_t timeout_ticks)
{
    int priority;
    scheduler_t *scheduler;

    assert(task);
    assert(channel);
    assert(timeout_ticks >= 0);

    if (!channel_full(channel))
    {
        channel_wakeup_unlock(channel); /* TODO: Need to be redesigned. */
        return PS_FALSE;
    }

    priority = task_get_priority(task);
    scheduler = task->scheduler;

    assert(scheduler);
    assert(task == task_list_front(&scheduler->ready_task_lists[priority]));

    task_list_pop_front(&scheduler->ready_task_lists[priority]);
    if (task_list_empty(&scheduler->ready_task_lists[priority]))
    {
        scheduler->ready_bitmap &= ~((uint32_t)1 << priority);
    }

    task_set_state(task, TASK_STATE_WAITING);

    /* The highest priority task needs to be awakened first. */
    task_list_insert_by_priority(&channel->waiting_task_list, task);
    task->wait_list = &channel->waiting_task_list;

    /* 0 for never timeout, wait forever. */
    if (timeout_ticks > 0)
    {
        task->wakeup_tick = scheduler->tick + (uint32_t)timeout_ticks;
        task_heap_insert(&scheduler->waiting_task_heap, task);
    }

    return PS_TRUE;
}

int task_block_if_channel_empty(task_t *task, channel_t *channel, int32_t timeout_ticks)
{
    int priority;
    scheduler_t *scheduler;

    assert(task);
    assert(channel);
    assert(timeout_ticks >= 0);

    if (!channel_empty(channel))
    {
        channel_wakeup_unlock(channel); /* TODO: Need to be redesigned. */
        return PS_FALSE;
    }

    priority = task_get_priority(task);
    scheduler = task->scheduler;

    assert(scheduler);
    assert(task == task_list_front(&scheduler->ready_task_lists[priority]));

    task_list_pop_front(&scheduler->ready_task_lists[priority]);
    if (task_list_empty(&scheduler->ready_task_lists[priority]))
    {
        scheduler->ready_bitmap &= ~((uint32_t)1 << priority);
    }

    task_set_state(task, TASK_STATE_WAITING);

    /* The highest priority task needs to be awakened first. */
    task_list_insert_by_priority(&channel->waiting_task_list, task);
    task->wait_list = &channel->waiting_task_list;

    /* 0 for never timeout, wait forever. */
    if (timeout_ticks > 0)
    {
        task->wakeup_tick = scheduler->tick + (uint32_t)timeout_ticks;
        task_heap_insert(&scheduler->waiting_task_heap, task);
    }

    return PS_TRUE;
}

void scheduler_init(scheduler_t *scheduler)
{
    int i;

    assert(scheduler);

    scheduler->tick = 0;
    scheduler->active_tasks = 0;
    scheduler->ready_bitmap = 0;

    for (i = 0; i < 32; i++)
    {
        task_list_init(&scheduler->ready_task_lists[i]);
    }

    task_heap_init(&scheduler->waiting_task_heap);
}

uint32_t scheduler_tick(scheduler_t *scheduler)
{
    assert(scheduler);
    return ++scheduler->tick;
}

void scheduler_add_task(scheduler_t *scheduler, task_t *new_task)
{
    int priority;

    assert(scheduler);
    assert(new_task);

    priority = task_get_priority(new_task);

    assert(new_task->scheduler == NULL);
    new_task->scheduler = scheduler;

    task_set_state(new_task, TASK_STATE_READY);

    task_list_push_back(&scheduler->ready_task_lists[priority], new_task);
    scheduler->ready_bitmap |= 1 << priority;

    scheduler->active_tasks++;
}

int scheduler_poll(scheduler_t *scheduler)
{
    int priority;
    task_state_t task_state;
    task_t *waiting_heap_top_task;
    task_t *highest_priority_task;

    assert(scheduler);

    /* Wake up all timeout tasks. */
    while ((waiting_heap_top_task = task_heap_top(&scheduler->waiting_task_heap)))
    {
        if (TICK_BEFORE(scheduler->tick, waiting_heap_top_task->wakeup_tick))
        {
            break;
        }

        task_heap_remove(&scheduler->waiting_task_heap, waiting_heap_top_task);

        /* Tasks that are purely time-delaying and never timeout do not require setting a timeout status. */
        if (waiting_heap_top_task->wait_list)
        {
            task_list_remove(waiting_heap_top_task->wait_list, waiting_heap_top_task);
            waiting_heap_top_task->wait_list = NULL;
            task_set_state(waiting_heap_top_task, TASK_STATE_TIMEOUT);
        }
        else
        {
            task_set_state(waiting_heap_top_task, TASK_STATE_READY);
        }

        priority = task_get_priority(waiting_heap_top_task);

        task_list_push_back(&scheduler->ready_task_lists[priority], waiting_heap_top_task);
        scheduler->ready_bitmap |= 1 << priority;
    }

    if (!scheduler->ready_bitmap)
    {
        return PS_FALSE;
    }

    priority = 31 - clz(scheduler->ready_bitmap);
    highest_priority_task = task_list_front(&scheduler->ready_task_lists[priority]);

    assert(highest_priority_task->scheduler == scheduler);

    /* Execute the task and obtain the next state. */
    if (highest_priority_task->entry)
    {
        task_state = highest_priority_task->entry(highest_priority_task);
    }
    else
    {
        /* Empty task, done directly. */
        task_state = TASK_STATE_DONE;
    }

    /* Update task state. */
    task_set_state(highest_priority_task, task_state);

    assert(task_state == TASK_STATE_READY ||
           task_state == TASK_STATE_WAITING ||
           task_state == TASK_STATE_TIMEOUT ||
           task_state == TASK_STATE_DONE);

    switch (task_state)
    {
        case TASK_STATE_TIMEOUT:
        {
            /* Task timed out while waiting.
             * Reset state to READY so it can be rescheduled later.
             * Fall through to READY case to move it to tail.
             */
            task_set_state(highest_priority_task, TASK_STATE_READY);
        }
        case TASK_STATE_READY:
        {
            /* Task is still ready to run.
             * Move it to the tail of its priority list to implement round-robin
             * scheduling among tasks of the same priority.
             */
            task_list_advance(&scheduler->ready_task_lists[priority]);
            break;
        }
        case TASK_STATE_WAITING:
        {
            /* Task is blocked on a synchronization primitive.
             * Already removed from the ready list in task_block_if_XXX functions.
             * Nothing to do here.
             */
            break;
        }
        case TASK_STATE_DONE:
        {
            /* Task has finished execution.
             * Remove it from the ready list, clear bitmap if priority becomes empty,
             * detach from scheduler, and decrement active task counter.
             */
            task_list_pop_front(&scheduler->ready_task_lists[priority]);
            if (task_list_empty(&scheduler->ready_task_lists[priority]))
            {
                scheduler->ready_bitmap &= ~((uint32_t)1 << priority);
            }
            highest_priority_task->scheduler = NULL;
            assert(scheduler->active_tasks);
            scheduler->active_tasks--;
            break;
        }
    }

    return PS_TRUE;
}

int scheduler_run(scheduler_t *scheduler, scheduler_callback_t on_loop, scheduler_callback_t on_idle)
{
    int busy, task_number;

    assert(scheduler);
    assert(scheduler->active_tasks >= 0);

    task_number = scheduler->active_tasks;

    while (1)
    {
        busy = scheduler_poll(scheduler);

        assert(scheduler->active_tasks >= 0);

        if (scheduler->active_tasks == 0)
        {
            break;
        }

        if (!busy && on_idle)
        {
            on_idle(scheduler);
        }

        if (on_loop)
        {
            on_loop(scheduler);
        }
    }

    return task_number;
}

void event_group_init(event_group_t *event_group)
{
    assert(event_group);
    event_group->flags = 0;
    task_list_init(&event_group->waiting_task_list);
}

int event_group_satisfied(const event_group_t *event_group, uint32_t mask, event_group_policy_t policy)
{
    assert(event_group);
    return policy == EVENT_GROUP_ALL
               ? (event_group->flags & mask) == mask
               : !!(event_group->flags & mask);
}

void event_group_set(event_group_t *event_group, uint32_t mask)
{
    uint32_t old_flags;
    task_t *task, *head, *next;
    int head_processed;

    assert(event_group);

    old_flags = event_group->flags;
    event_group->flags |= mask;

    if ((event_group->flags == old_flags) || task_list_empty(&event_group->waiting_task_list))
    {
        return;
    }

    head_processed = 0;
    head = task_list_front(&event_group->waiting_task_list);
    task = head;

    do
    {
        next = task->next;

        assert(task->wait_list == &event_group->waiting_task_list);

        if (task_get_flag(task, TASK_WAIT_ALL_EVENT_FLAG_MASK)
                ? (event_group->flags & task->event_mask) == task->event_mask
                : !!(event_group->flags & task->event_mask))
        {
            task_detach_wait_list_and_wakeup(task);

            /* The only task was removed. */
            if (task_list_empty(&event_group->waiting_task_list))
            {
                break;
            }

            /* If the removed task was current head, advance head and mark new head as unprocessed. */
            if (task == head)
            {
                head = next;
                head_processed = 0;
            }
        }
        /* Task was not removed: if it is the current head, mark it as processed. */
        else
        {
            if (task == head)
            {
                head_processed = 1;
            }
        }

        task = next;
        assert(task);

        /* Terminate when we circle back to the current head AND it has been processed. */
    } while (task != head || !head_processed);
}

uint32_t event_group_get(const event_group_t *event_group)
{
    assert(event_group);
    return event_group->flags;
}

void event_group_clear(event_group_t *event_group, uint32_t mask)
{
    assert(event_group);
    event_group->flags &= ~mask;
}

void semaphore_init(semaphore_t *semaphore, uint32_t initial_count)
{
    assert(semaphore);
    semaphore->counter = initial_count;
    task_list_init(&semaphore->waiting_task_list);
}

int semaphore_takeable(const semaphore_t *semaphore)
{
    assert(semaphore);
    return semaphore->counter > 0;
}

void semaphore_take(semaphore_t *semaphore)
{
    assert(semaphore);
    assert(semaphore->counter > 0);
    semaphore->counter--;
}

void semaphore_give(semaphore_t *semaphore)
{
    task_t *task;

    assert(semaphore);
    assert(semaphore->counter < UINT32_MAX);

    semaphore->counter++;

    if (!task_list_empty(&semaphore->waiting_task_list))
    {
        task = task_list_front(&semaphore->waiting_task_list);
        assert(task->wait_list == &semaphore->waiting_task_list);
        task_detach_wait_list_and_wakeup(task);
    }
}

void barrier_init(barrier_t *barrier, uint16_t threshold)
{
    assert(barrier);
    barrier->count = 0;
    barrier->threshold = threshold;
    task_list_init(&barrier->waiting_task_list);
}

int barrier_passable(const barrier_t *barrier)
{
    assert(barrier);
    return barrier->count >= barrier->threshold;
}

void barrier_signal(barrier_t *barrier)
{
    task_t *task, *next;

    assert(barrier);

    if (task_list_empty(&barrier->waiting_task_list))
    {
        return;
    }

    task = task_list_front(&barrier->waiting_task_list);

    while (1)
    {
        next = task->next;

        assert(task->wait_list == &barrier->waiting_task_list);

        /* Wakeup all waiting task. */
        task_detach_wait_list_and_wakeup(task);
        if (task_list_empty(&barrier->waiting_task_list))
        {
            break;
        }

        task = next;
        assert(task);
    }
}

void barrier_reset(barrier_t *barrier)
{
    assert(barrier);
    barrier->count = 0;
}

void channel_init(channel_t *channel, void *buffer, size_t size)
{
    assert(channel);
    assert(buffer);
    assert(size);

    channel->buffer = buffer;
    channel->size = size;
    channel->head = 0;
    channel->tail = 0;
    task_list_init(&channel->waiting_task_list);
}

size_t channel_send(channel_t *channel, const void *data, size_t length)
{
    size_t len;
    task_t *receiver;

    assert(channel);
    assert(data);
    assert(length > 0);

    assert(channel->tail >= channel->head);

    if (channel_full(channel))
    {
        return 0;
    }

    length = min(length, channel->size - channel->tail + channel->head);

    /* First put the data starting from channel->tail to buffer end. */
    len = min(length, channel->size - (channel->tail % channel->size));
    memcpy((uint8_t *)channel->buffer + (channel->tail % channel->size), data, len);

    /* Then put the rest (if any) at the beginning of the buffer. */
    memcpy(channel->buffer, (uint8_t *)data + len, length - len);
    channel->tail += length;

    if (!task_list_empty(&channel->waiting_task_list) && channel_wakeup_lock(channel))
    {
        receiver = task_list_front(&channel->waiting_task_list);
        assert(receiver->wait_list == &channel->waiting_task_list);
        task_detach_wait_list_and_wakeup(receiver);
    }

    return length;
}

size_t channel_receive(channel_t *channel, void *buffer, size_t size)
{
    size_t len;
    task_t *sender;

    assert(channel);
    assert(buffer);
    assert(size > 0);

    assert(channel->tail >= channel->head);

    if (channel_empty(channel))
    {
        return 0;
    }

    size = min(size, channel->tail - channel->head);

    /* First get the data from channel->head until the end of the buffer. */
    len = min(size, channel->size - (channel->head % channel->size));
    memcpy(buffer, (uint8_t *)channel->buffer + (channel->head % channel->size), len);

    /* Then get the rest (if any) from the beginning of the buffer. */
    memcpy((uint8_t *)buffer + len, channel->buffer, size - len);
    channel->head += size;

    if (!task_list_empty(&channel->waiting_task_list) && channel_wakeup_lock(channel))
    {
        sender = task_list_front(&channel->waiting_task_list);
        assert(sender->wait_list == &channel->waiting_task_list);
        task_detach_wait_list_and_wakeup(sender);
    }

    return size;
}

/**
 * @brief Lock the channel to prevent concurrent wakeups.
 *
 * Returns PS_TRUE if the lock was acquired (was 0), PS_FALSE if already locked.
 */
int channel_wakeup_lock(channel_t *channel)
{
    assert(channel);
    return channel->wakeup_locked ? PS_FALSE : (channel->wakeup_locked = 1, PS_TRUE);
}

/**
 * @brief Unlock the channel to allow further wakeups.
 */
void channel_wakeup_unlock(channel_t *channel)
{
    assert(channel);
    channel->wakeup_locked = 0;
}

size_t channel_capacity(const channel_t *channel)
{
    assert(channel);
    return channel->size;
}

size_t channel_length(const channel_t *channel)
{
    assert(channel);
    assert(channel->tail >= channel->head);
    return (channel->tail - channel->head);
}

int channel_full(const channel_t *channel)
{
    assert(channel);
    assert(channel->tail >= channel->head);
    return (channel->tail - channel->head == channel->size);
}

int channel_empty(const channel_t *channel)
{
    assert(channel);
    return (channel->head == channel->tail);
}
