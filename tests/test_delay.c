#include <assert.h>
#include <stdio.h>
#include <protosched.h>

typedef struct
{
    task_t task;
    int step;
} delay_frame_t;

TASK_DEFINE(delay_task)
{
    delay_frame_t *frame = (delay_frame_t *)this_task;
    TASK_BEGIN();
    frame->step = 1;
    TASK_DELAY(5);
    frame->step = 2;
    TASK_RETURN();
    TASK_END();
}

int main(void)
{
    int ret;
    scheduler_t sched;
    delay_frame_t frame;

    scheduler_init(&sched);
    task_init(&frame.task, 0, delay_task);
    frame.step = 0;

    scheduler_add_task(&sched, &frame.task);

    // 第一次 poll：执行到 TASK_DELAY，进入 WAITING
    scheduler_poll(&sched);
    assert(frame.step == 1);
    assert(task_get_state(&frame.task) == TASK_STATE_WAITING);

    // 推进 4 个 tick（未到 5），任务不应唤醒
    for (int i = 0; i < 4; i++)
    {
        scheduler_tick(&sched);
        // 检查是否有任务就绪（poll 应返回 PS_FALSE）
        ret = scheduler_poll(&sched);
        assert(ret == PS_FALSE);
        assert(frame.step == 1);
    }

    // 第 5 个 tick，任务超时唤醒，应继续执行TASK_RETURN，变为 DONE
    scheduler_tick(&sched);
    ret = scheduler_poll(&sched);
    assert(ret == 1);
    assert(frame.step == 2);
    assert(task_get_state(&frame.task) == TASK_STATE_DONE);

    // 再次 poll，无就绪任务
    ret = scheduler_poll(&sched);
    assert(ret == PS_FALSE);

    printf("test_delay passed.\n");

    return 0;
}