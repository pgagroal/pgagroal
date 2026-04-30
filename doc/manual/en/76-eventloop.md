\newpage

## Event Loop

Here you find a concise developer-focused description of how the event loop implemented in `ev.c` and `ev.h` operates. This includes architecture, watcher mechanics, lifecycle, role separation, callback pipelines, and planned enhancements.

**High-Level Architecture**

The event loop sits in a continuous wait cycle, monitoring sources of events (I/O readiness, timer expirations, or delivered signals), dispatching the appropriate handler when an event occurs, and calling an user registered callback.

* Backends abstract the OS-specific wait or I/O mechanism used. The backend can be configured in the configuration file with the variable `ev_backend`.
* Watchers encapsulate interest in one type of event (I/O, signal, or periodic) and carry the callback and context required.
* Lifecycle routines manage setup, execution, interruption, and teardown of the loop.
* **Each process has its own event loop**: There is a **clear distinction** between **main** (accepting connections) and **worker** (handling established connections) event loops.

**Data Structures**

* `struct event_loop` centralizes all loop state, holding:

  * `running` flag governs the main loop.
  * `sigset` tracks which signals the loop intercepts.
  * An array of generic `event_watcher_t*` pointers represents active watchers.
  * Backend handles (`io_uring` ring, `epollfd`, or `kqueuefd`) interface directly with the kernel.
  * A scratch `buffer` or `io_uring` buffer ring is used to stage data transfers efficiently (the memory is defined elsewhere in pgagroal and used here as the buffer).

**Watcher Types and Responsibilities**

Every watcher embeds a small **common header** containing its type, enabling the loop to iterate over mixed watcher arrays.

1. **I/O Watchers** monitor one or two file descriptors.
   * *Main* watchers listen for new client connections and accept them.
   * *Worker* watchers handle serial request/response flows, blocking on receive then send.

2. **Signal Watchers** wrap POSIX signals into file descriptors. The loop unblocks these signals globally, then watches the FD for delivery events, invoking the registered callback.

3. **Periodic Watchers** fire at fixed millisecond intervals.

**Event Loop Lifecycle**

1. **Initialization** (`pgagroal_event_loop_init`):
2. **Running** (`pgagroal_event_loop_run`):
3. **Breaking** (`pgagroal_event_loop_break`):
4. **Destruction** (`pgagroal_event_loop_destroy`):
5. **Fork Handling** (`pgagroal_event_loop_fork`):

**Main vs Worker I/O watchers**

To simplify connection handling, the code forks a **Worker** process for each accepted client. Both processes run the same loop, but with different watchers registered:

* **Main Process**:

  1. Watches `listen_fd` for new connections.
  2. On accept, forks a Worker and continues listening.

* **Worker Process**:

  1. Registers I/O watchers on `rcv_fd` and `snd_fd`.
  2. Waits for `rcv_fd` to signal incoming data, then invokes a pipeline callback to process it.
  3. Sends responses on `snd_fd`.

**Pipelines & Callback Flow**

The loop’s generic I/O `handler` delegates to a **pipeline** based on watcher type and context. A typical flow:

1. **I/O event**: `backend -> loop -> io_watcher.handler`
2. **Dispatch**: Handler inspects messages in buffer and selects the next pipeline stage function.
3. **Processing and Returning**: Pipeline stage validates the message payload and gets back to the loop.

This approach separates generic loop mechanics from application-specific message handling.

**Enhancements**

First, the main enhancement we could do is improve initial connection time. This could happen by initially caching the event loops beforehand and allowing for a connection to pick up one. Further examination of ftrace here is required.

Second, a series of compile-time flags mark areas for performance tuning. In my experience, none of these have been able to greatly improve performance (**haven't tested with iovecs**), but these may still require correct implementation and evaluation:

* **Zero Copy** (`MSG_ZEROCOPY` via io\_uring) — reduce CPU overhead by skipping buffer copies.
* **Fast Poll** (`EPOLLET`) — edge-triggered epoll mode for high-throughput scenarios.
* **Huge Pages** (`IORING_SETUP_NO_MMAP`) — leverage large page mappings for buffer rings.
* **Multishot Recv** — one SQE to deliver multiple receive completions.
* **IOVecs** — scatter/gather I/O arrays for fewer system calls.
