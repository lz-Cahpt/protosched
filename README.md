# protosched

**A Minimal, Priority-Based Cooperative Scheduler for Bare-Metal Embedded Systems and as Complement for RTOS**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C Standard](https://img.shields.io/badge/C-C89-blue.svg)](https://en.wikipedia.org/wiki/ANSI_C)
[![Platform](https://img.shields.io/badge/platform-embedded-lightgrey.svg)](https://github.com)

`protosched` is a lightweight cooperative scheduler written in C89, designed for resource-constrained microcontrollers (MCUs) with limited RAM (e.g., STM32F0 series). It provides a coroutine-based tasking model with priority scheduling and a set of synchronization primitives, enabling you to write complex concurrent logic without the overhead of a full RTOS.

> **⚠️ Production Readiness Notice**
>
> This library is **not yet production-proven**. While simple tests have been conducted, they do not cover all edge cases. Use it at your own risk, and always validate thoroughly in your specific application. Contributions and bug reports are welcome to help improve its reliability.

---

## Features

- **Cooperative Coroutines** — Shared-stack tasks using `goto`-based protothreads, with only a small `task_t` structure overhead (48 bytes in 32-bit machine).
- **Priority Scheduling** — 32-level bitmap-based priority queue.
- **Synchronization Primitives** — Full set of IPC mechanisms:
  - **Channel** — Lock-free ring buffer for data exchange.
  - **Semaphore** — Counting semaphore for resource management.
  - **Event Group** — Synchronization based on event masks (`ANY`/`ALL`).
  - **Barrier** — Task rendezvous point.
- **Timer Support** — Binary heap-based timer queue for delayed execution (`TASK_DELAY`) and wait timeouts.
- **Portable** — Minimal dependency on GNU extensions (fallback to portable `switch`-based implementation).
- **Low Footprint** — ~1k lines of core code, zero dynamic memory allocation.

---

## Quick Start

### Minimal Example

```c
#include <protosched.h>

scheduler_t sched;
task_t task;

TASK_DEFINE(blink_task) {
    TASK_BEGIN();
    while (1) {
        printf("LED ON\n");
        TASK_DELAY(100);
        printf("LED OFF\n");
        TASK_DELAY(100);
    }
    TASK_END();
}

int main() {
    scheduler_init(&sched);
    task_init(&task, 0, blink_task);
    scheduler_add_task(&sched, &task);
    scheduler_run(&sched, NULL, NULL);
    return 0;
}