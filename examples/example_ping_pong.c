#include <stdio.h>
#include "protosched.h"

/* ========== Define channel buffer ========== */
#define CHANNEL_BUFFER_SIZE 1
static int channel_buffer[CHANNEL_BUFFER_SIZE];

/* ========== Producer task frame ========== */
typedef struct
{
    task_t task;
    channel_t *ch;
    int count;
    int max_count;
} producer_frame_t;

/* ========== Consumer task frame ========== */
typedef struct
{
    task_t task;
    channel_t *ch;
    int count;
    int max_count;
} consumer_frame_t;

/* ========== Producer task ========== */
TASK_DEFINE(producer_task)
{
    producer_frame_t *f = (producer_frame_t *)this_task;

    TASK_BEGIN();

    while (f->count < f->max_count)
    {
        /* Wait until channel has space (macro blocks until available) */
        TASK_YIELD_UNTIL_CHANNEL_HAS_SPACE(f->ch, 100);

        /* Send data */
        int value = f->count * 10;
        channel_send(f->ch, &value, sizeof(value));
        printf("[P] Sent: %d\n", value);

        /* Yield CPU to allow consumer to run */
        TASK_DELAY(1);

        f->count++;
    }

    printf("[P] Producer done.\n");
    TASK_RETURN();
    TASK_END();
}

/* ========== Consumer task ========== */
TASK_DEFINE(consumer_task)
{
    consumer_frame_t *f = (consumer_frame_t *)this_task;

    TASK_BEGIN();

    while (f->count < f->max_count)
    {
        /* Wait until channel has data */
        TASK_YIELD_UNTIL_CHANNEL_HAS_DATA(f->ch, 100);

        /* Receive data */
        int value;
        channel_receive(f->ch, &value, sizeof(value));
        printf("[C] Received: %d\n", value);

        /* Yield CPU to allow producer to continue */
        TASK_YIELD_IF_NEEDED();

        f->count++;
    }

    printf("[C] Consumer done.\n");
    TASK_RETURN();
    TASK_END();
}

/* ========== Helper: Run scheduler until all tasks are done ========== */
static void run_until_done(scheduler_t *sched, int max_ticks)
{
    for (int tick = 0; tick < max_ticks; tick++)
    {
        scheduler_tick(sched);
        printf("\n--- tick %d ---\n", tick);
        int executed = 0;
        while (scheduler_poll(sched) > 0)
        {
            executed++;
            /* printf("  poll #%d\n", executed); */
        }
        if (executed == 0)
        {
            printf("  (no task executed)\n");
        }
        else
        {
            printf("  executed %d task(s)\n", executed);
        }
        printf("  active tasks: %d\n", sched->active_tasks);
        if (sched->active_tasks == 0)
        {
            printf("All tasks done at tick %d\n", tick);
            break;
        }
    }
}

/* ========== Main function ========== */
int main(void)
{
    scheduler_t sched;
    channel_t ch;
    producer_frame_t producer;
    consumer_frame_t consumer;

    /* 1. Initialize scheduler and channel */
    scheduler_init(&sched);
    channel_init(&ch, channel_buffer, sizeof(channel_buffer));

    /* 2. Initialize producer task */
    task_init(&producer.task, 1, producer_task);
    producer.ch = &ch;
    producer.count = 0;
    producer.max_count = 5;

    /* 3. Initialize consumer task */
    task_init(&consumer.task, 0, consumer_task);
    consumer.ch = &ch;
    consumer.count = 0;
    consumer.max_count = 5;

    /* 4. Add tasks to scheduler */
    scheduler_add_task(&sched, &producer.task);
    scheduler_add_task(&sched, &consumer.task);

    /* 5. Run scheduler (max 200 ticks) */
    run_until_done(&sched, 200);

    /* 6. Verify results (optional) */
    if (producer.count == producer.max_count &&
        consumer.count == consumer.max_count)
    {
        printf("Example completed successfully!\n");
    }

    return 0;
}