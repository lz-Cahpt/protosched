#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <protosched.h>

#define BUF_SIZE 4
int buffer[BUF_SIZE];

typedef struct
{
    task_t task;
    channel_t *ch;
    int sent[10];
    int send_count;
    int idx;          /* 循环索引，跨 YIELD 保留 */
} prod_frame_t;

typedef struct
{
    task_t task;
    channel_t *ch;
    int recv[10];
    int recv_count;
    int idx;          /* 循环索引，跨 YIELD 保留 */
} cons_frame_t;

TASK_DEFINE(producer)
{
    prod_frame_t *frame = (prod_frame_t *)this_task;
    TASK_BEGIN();

    for (frame->idx = 0; frame->idx < 5; frame->idx++)
    {
        TASK_YIELD_UNTIL_CHANNEL_HAS_SPACE(frame->ch, 0);
        int val = frame->idx * 10;
        channel_send(frame->ch, &val, sizeof(val));
        frame->sent[frame->send_count++] = val;
    }

    TASK_RETURN();
    TASK_END();
}

TASK_DEFINE(consumer)
{
    cons_frame_t *frame = (cons_frame_t *)this_task;
    TASK_BEGIN();

    for (frame->idx = 0; frame->idx < 5; frame->idx++)
    {
        TASK_YIELD_UNTIL_CHANNEL_HAS_DATA(frame->ch, 0);
        int val;
        size_t n = channel_receive(frame->ch, &val, sizeof(val));
        assert(n == sizeof(val));
        frame->recv[frame->recv_count++] = val;
    }

    TASK_RETURN();
    TASK_END();
}

int main(void)
{
    scheduler_t sched;
    channel_t ch;

    scheduler_init(&sched);
    channel_init(&ch, buffer, sizeof(buffer));

    prod_frame_t prod;
    cons_frame_t cons;

    task_init(&prod.task, 0, producer);
    prod.ch = &ch;
    prod.send_count = 0;
    prod.idx = 0;
    memset(prod.sent, 0, sizeof(prod.sent));

    task_init(&cons.task, 0, consumer);
    cons.ch = &ch;
    cons.recv_count = 0;
    cons.idx = 0;
    memset(cons.recv, 0, sizeof(cons.recv));

    scheduler_add_task(&sched, &prod.task);
    scheduler_add_task(&sched, &cons.task);

    /* 运行直到所有任务完成 */
    while (scheduler_poll(&sched) > 0)
    {
    }

    /* 验证数据完整性 */
    assert(prod.send_count == 5);
    assert(cons.recv_count == 5);
    for (int i = 0; i < 5; i++)
    {
        assert(prod.sent[i] == i * 10);
        assert(cons.recv[i] == i * 10);
    }

    printf("test_channel passed.\n");

    return 0;
}