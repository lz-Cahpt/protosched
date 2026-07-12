#include <assert.h>
#include <stdio.h>
#include <protosched.h>

typedef struct
{
    task_t task;
    int id;
    int executed;
} prio_frame_t;

TASK_DEFINE(prio_task)
{
    prio_frame_t *frame = (prio_frame_t *)this_task;
    TASK_BEGIN();
    frame->executed = frame->id;
    TASK_RETURN();
    TASK_END();
}

int main(void)
{
    int ret;
    scheduler_t sched;
    prio_frame_t tasks[32];

    scheduler_init(&sched);

    // 创建32个不同优先级的任务，顺序添加：优先0，优先1...，优先31
    // 预期执行顺序：优先31（最高）先执行，然后优先30...，最后优先0
    for (int i = 0; i < 32; i++)
    {
        task_init(&tasks[i].task, i, prio_task); // 优先级 i（31最高）
        tasks[i].id = i;
        tasks[i].executed = -1;
        scheduler_add_task(&sched, &tasks[i].task);
    }

    // 执行调度，从最高优先级31开始执行
    for (int i = 31; i >= 0; i--)
    {
        ret = scheduler_poll(&sched);
        assert(ret == PS_TRUE);
        assert(tasks[i].executed == i);
    }

    // 执行调度32次后，所有任务完成，无就绪任务可调度
    ret = scheduler_poll(&sched);
    assert(ret == PS_FALSE);

    printf("test_priority_schedule passed.\n");

    return 0;
}