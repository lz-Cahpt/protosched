#include <assert.h>
#include <stdio.h>
#include <protosched.h>

#define EVT_A 0x01
#define EVT_B 0x02

typedef struct
{
    task_t task;
    event_group_t *eg;
    uint32_t mask;
    int policy;
    int result; // 0=未唤醒, 1=唤醒
} evt_frame_t;

TASK_DEFINE(evt_task)
{
    evt_frame_t *frame = (evt_frame_t *)this_task;
    TASK_BEGIN();
    TASK_YIELD_UNTIL_EVENT_GROUP_SATISFIED(frame->eg, frame->mask, frame->policy, 10);
    if (task_get_state(this_task) == TASK_STATE_TIMEOUT)
    {
        frame->result = 0;
    }
    else
    {
        frame->result = 1;
    }
    TASK_RETURN();
    TASK_END();
}

int main(void)
{
    scheduler_t sched;
    event_group_t eg;
    evt_frame_t tasks[2];
    int i;

    scheduler_init(&sched);
    event_group_init(&eg);

    // 任务1：等待 EVENT_ANY (A or B)
    task_init(&tasks[0].task, 0, evt_task);
    tasks[0].eg = &eg;
    tasks[0].mask = EVT_A | EVT_B;
    tasks[0].policy = EVENT_GROUP_ANY;
    tasks[0].result = -1;
    scheduler_add_task(&sched, &tasks[0].task);

    // 任务2：等待 EVENT_ALL (A and B)
    task_init(&tasks[1].task, 0, evt_task);
    tasks[1].eg = &eg;
    tasks[1].mask = EVT_A | EVT_B;
    tasks[1].policy = EVENT_GROUP_ALL;
    tasks[1].result = -1;
    scheduler_add_task(&sched, &tasks[1].task);

    // 先运行一轮：两个任务都应阻塞
    scheduler_poll(&sched); // 任务1
    scheduler_poll(&sched); // 任务2
    assert(task_get_state(&tasks[0].task) == TASK_STATE_WAITING);
    assert(task_get_state(&tasks[1].task) == TASK_STATE_WAITING);

    // 设置事件 A
    event_group_set(&eg, EVT_A);
    // 运行调度：任务1（ANY）应被唤醒，任务2（ALL）继续等待
    scheduler_poll(&sched);
    assert(tasks[0].result == 1);
    assert(task_get_state(&tasks[0].task) == TASK_STATE_DONE);
    assert(tasks[1].result == -1); // 未唤醒
    assert(task_get_state(&tasks[1].task) == TASK_STATE_WAITING);

    // 设置事件 B
    event_group_set(&eg, EVT_B);
    // 运行调度：任务2（ALL）应被唤醒
    scheduler_poll(&sched);
    assert(tasks[1].result == 1);
    assert(task_get_state(&tasks[1].task) == TASK_STATE_DONE);

    printf("test_event_group passed.\n");

    return 0;
}