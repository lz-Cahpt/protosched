#include <assert.h>
#include <stdio.h>
#include <protosched.h>

typedef struct
{
    task_t task;
    int step;
} test_frame_t;

// ----- yield 两次后结束 -----
TASK_DEFINE(yield_task)
{
    test_frame_t *frame = (test_frame_t *)this_task;
    TASK_BEGIN();
    frame->step = 1;
    TASK_YIELD();
    frame->step = 2;
    TASK_YIELD();
    frame->step = 3;
    TASK_RETURN();
    TASK_END();
}

int main(void)
{
    int ret;
    scheduler_t sched;
    test_frame_t frame;

    scheduler_init(&sched);
    task_init(&frame.task, 0, yield_task);
    frame.step = 0;

    scheduler_add_task(&sched, &frame.task);

    // 第一次运行：应执行到第一个 TASK_YIELD
    ret = scheduler_poll(&sched);
    assert(ret == PS_TRUE);
    assert(frame.step == 1);
    assert(task_get_state(&frame.task) == TASK_STATE_READY);

    // 第二次运行：应执行到第二个 TASK_YIELD
    ret = scheduler_poll(&sched);
    assert(ret == PS_TRUE);
    assert(frame.step == 2);
    assert(task_get_state(&frame.task) == TASK_STATE_READY);

    // 第三次运行：执行完 TASK_RETURN，状态变为 DONE
    ret = scheduler_poll(&sched);
    assert(ret == PS_TRUE);
    assert(frame.step == 3);
    assert(task_get_state(&frame.task) == TASK_STATE_DONE);

    // 第四次运行：任务已结束，不应再执行（poll 返回 0）
    ret = scheduler_poll(&sched);
    assert(ret == PS_FALSE);
    assert(frame.step == 3);
    assert(task_get_state(&frame.task) == TASK_STATE_DONE);

    printf("test_task_yield passed.\n");

    return 0;
}