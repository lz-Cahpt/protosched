#include <assert.h>
#include <stdio.h>
#include <protosched.h>

typedef struct
{
    task_t task;
    barrier_t *bar;
    int arrived;
} bar_frame_t;

TASK_DEFINE(bar_task)
{
    bar_frame_t *frame = (bar_frame_t *)this_task;
    TASK_BEGIN();
    frame->arrived = 1;
    TASK_YIELD_UNTIL_BARRIER_PASSABLE(frame->bar, 0);
    frame->arrived = 2;
    TASK_RETURN();
    TASK_END();
}

int main(void)
{
    int i;
    scheduler_t sched;
    barrier_t bar;
    bar_frame_t tasks[3];

    scheduler_init(&sched);
    barrier_init(&bar, 3);

    for (i = 0; i < 3; i++)
    {
        task_init(&tasks[i].task, 0, bar_task);
        tasks[i].bar = &bar;
        tasks[i].arrived = 0;
        scheduler_add_task(&sched, &tasks[i].task);
    }

    // 运行三个任务：前两个阻塞，第三个触发屏障并通过
    // 前两个任务应阻塞
    scheduler_poll(&sched);
    assert(tasks[0].arrived == 1);
    assert(task_get_state(&tasks[0].task) == TASK_STATE_WAITING);
    scheduler_poll(&sched);
    assert(tasks[1].arrived == 1);
    assert(task_get_state(&tasks[1].task) == TASK_STATE_WAITING);
    // 第三个不应阻塞
    scheduler_poll(&sched);
    assert(tasks[2].arrived == 2);
    assert(task_get_state(&tasks[2].task) == TASK_STATE_DONE);

    // 现在两个任务（A 和 B）已就绪，继续调度它们完成
    for (int i = 0; i < 2; i++)
    {
        scheduler_poll(&sched);
        assert(tasks[i].arrived == 2);
        assert(task_get_state(&tasks[i].task) == TASK_STATE_DONE);
    }

    printf("test_barrier passed.\n");

    return 0;
}