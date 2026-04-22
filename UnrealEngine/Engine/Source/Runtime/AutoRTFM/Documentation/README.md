# AutoRTFM User Guide

**Author**: [John Stiles](mailto:john.stiles@epicgames.com)  
**Team**: Verse - AutoRTFM  

# Context

A core Verse language feature is the ability to “roll back” changes to the state of execution when
failures are encountered, making it appear as if nothing has changed. Verse code is allowed to wrap
almost any operation within a failure scope, and runtime failures can occur at any point, so very
little in Verse is truly exempt from rollback. dSTM will also use this ability to abort and retry a
transaction whenever a remote `TObjectPtr` is dereferenced.

Before AutoRTFM was enabled, rollback was handled using explicit callbacks in 
[Verse::Stm](https://docs.google.com/document/d/1gQlVTJigO4AdpljegaRe9hDGIeaLfqgJLq1p6z8QBNE/edit?tab=t.0)
and this is still used on the client. However, servers now use AutoRTFM instead, and automatically
support most forms of rollback without any programmer involvement. However, AutoRTFM has limitations
around threading and resource sharing which sometimes require very careful thought.

# AutoRTFM Rollback

## Live in Production

The goal of AutoRTFM is to automatically implement rollback for single-threaded code. The basics of
this approach are covered in Phil Pizlo’s blog post,
[Bringing Verse Transactional Memory Semantics to C++](https://www.unrealengine.com/en-US/tech-blog/bringing-verse-transactional-memory-semantics-to-c).
Note that this post discusses AutoRTFM as being compiled into the binary as of Release 28.10
(January 2024), and that’s technically correct, but it wasn't actually *enabled* on live servers at
that time. AutoRTFM was disabled shortly after launch due to server stability issues.

After the initial launch attempt, the team regrouped and devised a more robust launch plan. AutoRTFM
was then successfully enabled for a small percentage of VkPlay games on public servers in Release
34.10 (March 2025). As of Release 36.20 (July 2025), AutoRTFM is fully enabled for all VkPlay and
VkEdit sessions. In Release 38.00 (November 2025), we plan to deprecate the ability to disable
AutoRTFM in production. We are no longer testing Valkyrie in the AutoRTFM-off path, and features
like the new VM rely on AutoRTFM to function.

When AutoRTFM is enabled, almost all game logic on the main thread automatically supports rollback
without manual intervention. Unfortunately, some things have side effects that simply can’t be
undone automatically—for instance, you can’t un-send a network packet after it’s been sent!—so more
complex or multi-threaded code will need to be aware of the implementation details of AutoRTFM.
Fortunately, most of the time, your existing code will work as-is, and no additional work will be
necessary.

## Open and Closed Code

AutoRTFM is designed around an Epic-internal fork of the Clang compiler. This version 
of Clang is responsible for adding transactional instrumentation to our code. For performance 
reasons, though, we don’t always run with this instrumentation enabled; instead, AutoRTFM 
is designed to compile all of our code twice. One version is the typical, uninstrumented 
form that you would expect from a normal Clang build, more or less—by convention, we refer 
to this form as “open.” The other version is compiled with our extra transactional logic 
mixed in—our convention is to call this instrumented form “closed.” Open and closed versions 
of almost all functions coexist in our binaries. 

Programs start off in the “open” state; in situations where rollback is needed, we transition 
into the “closed” state via `AutoRTFM::Transact`. This function is responsible for teleporting 
us to the matching, “closed” form of the currently-running code. 

## Instrumentation

In transactional mode, writes to heap memory occur in real-time, but they are also closely 
tracked so that they can be undone if necessary. We also wrap some low-level APIs like 
`malloc` and `free` to make them transactionally safe. Function pointers to “open” functions 
are dynamically rerouted to their “closed” equivalent so that we don’t accidentally escape 
from “closed” code into its “open” form. Finally, we maintain a list of deferred tasks 
to perform at the conclusion of the transaction.

## Committing

If the transaction reaches the end of its `Transact` block without being aborted, it has 
succeeded and will be committed. To do this, we execute our list of `CommitTasks` which 
are responsible for handling all deferred operations. For instance, we always defer `free(MyData)` 
to commit time, via the `CommitTasks` list, instead of calling `free` immediately; this 
allows us to resuscitate the block of heap memory if the transaction is aborted. Once 
the commit tasks are complete, we return to the “open” state and discard any heap tracking 
information. Users can also defer work to the end of a transaction by calling `AutoRTFM::OnCommit` 
to add their own callbacks to the commit task list. Commit tasks are run in first-in, 
first-out order.

## Aborting

Conversely, if `AutoRTFM::AbortTransaction` is called anywhere within a `Transact` block, 
the transaction is considered to be aborted and must be rolled back. In other words, AutoRTFM 
is now responsible for undoing all changes made within the transaction; we must teleport 
execution straight to the end of the `Transact` block, while maintaining the illusion 
that nothing at all has changed. Our heap-write tracking data is used to undo all transactional 
changes to the heap, and we additionally have a list of `AbortTasks` to execute. As you 
might expect, these `AbortTasks` are the parallel opposite of the `CommitTasks`. For instance, 
we need to ensure that memory isn’t just leaked if the user calls `malloc(Size)` and then 
aborts the transaction. This is handled by generating an `AbortTask` inside of `malloc` 
which frees the allocated data; in the event of an abort, this saves us from a leak. Users 
can also call `OnAbort` to add work to the abort task list. Abort tasks are run in first-in, 
last-out order.

## Hazards

Some APIs are inherently multi-threaded; these are considered hazards in AutoRTFM, since 
our transactional model only has a single-threaded view of the world. These APIs intentionally 
trigger a language failure and will need to be manually fixed up by a programmer. For 
instance, `std::atomic<>` or `FThreadSafeCounter` are considered unsafe because they are 
designed to communicate state across threads; it would be possible to automatically roll 
back an atomic increment with an atomic decrement, but in general this wouldn’t be sufficient 
to guarantee correct behavior of the entire program. The AutoRTFM team has designed transactionally 
safe primitives like critical sections (`FTransactionallySafeCriticalSection`) and mutexes 
(`FTransactionallySafeMutex`), but these must be manually replaced in the code because 
they are a little more complex, and take more memory, than a plain `FCriticalSection` 
or `FMutex`. 

## Resource Locking

The approach for resource locking within a transaction is that, once a lock is taken or 
a resource is acquired, that lock or resource is always held until the end of the transaction. 
That is, calling `FTransactionallySafeCriticalSection::Unlock` within a transaction will 
not immediately release the lock; instead, it enqueues a `CommitTask` entry responsible 
for unlocking the resource. Because the AutoRTFM thread maintains its lock for the duration 
of the transaction, it is free to re-acquire or re-lock the resource within the same transaction 
at will; this is important, because otherwise, we would quickly deadlock.

Holding resource locks is an important principle, because it prevents the transactional 
state from being exposed to other threads prematurely. In other words, transactional changes 
shouldn’t be visible to other threads while the transaction is still in flight. This wouldn’t 
be safe, because the transaction is still in a provisional state and might be rolled back—we 
don’t want other threads to see or act on those changes until the transaction is fully 
committed. (For that matter, immediate unlocks would also make rollback *itself* a threading 
hazard, since the heap rollback code doesn’t know about critical sections or mutexes, 
and won’t take any locks while it is undoing heap changes.)

## Nesting

It is legal to nest a sub-transaction inside of a transaction, and it is safe to abort 
the sub-transaction within the outer transaction. Aborting an outer transaction will roll 
back all the work performed in the sub-transaction as well, even if those inner transactions 
succeeded and were committed. In general, aborting a transaction should always roll back 
*everything* that happened inside of that transaction—even if that includes an inner 
sub-transaction.

## Missing Closed Code

In some cases, we don’t have the “closed” version of a function at all. For instance, 
Unreal Engine includes some libraries like [Oodle](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-oodle-in-unreal-engine) 
and [EOS](https://dev.epicgames.com/documentation/en-us/unreal-engine/online-subsystem-eos-plugin-in-unreal-engine) 
as precompiled binaries; we also compile some code with specialized compilers like [Intel 
ISPC](https://ispc.github.io/ispc.html) that don’t support instrumentation. In these cases, 
we are required to manually go into the “open” state via `UE_AUTORTFM_OPEN` or `AutoRTFM::Open` 
before calling these functions. However, for an abort to properly undo these changes to 
the heap, callers are required to *manually* inform the AutoRTFM runtime about any heap 
memory that the code will write to—crucially, before the write occurs!—via 
`AutoRTFM::RecordOpenWrite`. 

Obviously, this extra work is complex and undesirable. If calling an open function from 
closed code cannot be avoided, consider wrapping the work in a helper function. If it 
is feasible, design the code so that heap writes do not need to be undone at all—e.g., 
only write to a short-lived scratch buffer that will be destroyed before the transaction 
is complete, or maintain a persistent scratch buffer where the contents are always assumed 
to be clobbered across transactions.

## Mixed Open and Closed Code

When trying to resolve AutoRTFM incompatibilities, it is tempting to consider invoking 
large, complex functions in the “open”—thereby dodging all hazards—and then registering 
an `OnAbort` handler to undo its effects. However, this is dangerous and should be avoided. 
This approach can lead to new, subtle transactional hazards:

- Locks will be released before the transaction is finished. This exposes transactional 
  changes to other threads while the transaction is still in a provisional state. Also, 
  if a rollback occurs, this becomes a second thread hazard, because locks won’t be taken 
  at all during the rollback.  
- An “open” block cannot free or reallocate memory that was allocated in a “closed” block.
  (Reason: When allocating, the closed code will also have created an abort handler to 
  free its memory. Those callbacks will still run if the transaction is aborted, but the 
  pointer passed to Free will already have been deallocated; in short, it will lead to a 
  double-free.)  
- The above-mentioned reallocation hazard also extends to classes which implicitly reallocate 
  memory on your behalf. In particular, innocuous methods like `TArray::Add()` can become 
  very dangerous when both open and closed code add items to the same array.  
- Passing ownership of non-trivial objects across the open-closed boundary can lead to 
  memory handling errors. For instance, if you have an `FString` that was created in “closed” 
  code, and assign a new value to it from an “open” block, this is unsafe (discussed in 
  depth [here](https://jira.it.epicgames.com/browse/SOL-6991). We have a special mechanism 
  which is designed to allow returning a non-trivial object from `AutoRTFM::Open` into closed 
  code by making a copy; this must be handled on a type-by-type basis.  
- Altering the same bytes of memory from both “open” and “closed” code in the same transaction 
  can lead to hard-to-diagnose silent rollback failures. 
  (Reason: heap changes made in the open are not tracked and can’t be undone automatically, 
  as you probably know. But if we subsequently make a write to the same range of heap memory 
  from “closed” code, the instrumentation will log the write so that it can be undone. 
  Unfortunately, we already changed the data, so the true “original” value is already gone, and we
  log the already-changed value. If an abort occurs, this behavior is sometimes harmless, and 
  other times very wrong.)

These hazards can be extraordinarily difficult to debug once they have occurred.

To help identify writes made in the closed, and then within the same transaction, in the 
open, you can enable a memory validator with the `AutoRTFMMemoryValidationLevel` flag 
(see below).

## Command line flags

AutoRTFM can be controlled with the following `dpcvars`, which can be combined with a 
comma. 

NOTE: Be aware that these settings are ignored in Shipping builds! A Test build should 
be used to run the memory validator.

| AutoRTFM mode  | Server Flags                                                                        |
| :------------- | :---------------------------------------------------------------------------------- |
| Disable        | `-dpcvars=AutoRTFMRuntimeEnabled=off`      *or* `-dpcvars=AutoRTFMRuntimeEnabled=0` |
| Enabled        | `-dpcvars=AutoRTFMRuntimeEnabled=on`       *or* `-dpcvars=AutoRTFMRuntimeEnabled=1` |
| Force-disabled | `-dpcvars=AutoRTFMRuntimeEnabled=forceoff` *or* `-dpcvars=AutoRTFMRuntimeEnabled=2` |
| Force-enabled  | `-dpcvars=AutoRTFMRuntimeEnabled=forceon`  *or* `-dpcvars=AutoRTFMRuntimeEnabled=3` |

| Retry Validation Mode | Server Flags                           |
| :-------------------- | :------------------------------------- |
| Disable               | `-dpcvars=AutoRTFMRetryTransactions=0` |
| Retry non-nested      | `-dpcvars=AutoRTFMRetryTransactions=1` |
| Retry nested too      | `-dpcvars=AutoRTFMRetryTransactions=2` |

| Memory Validation Mode | Server Flags                                                                                                 |
| :--------------------- | :----------------------------------------------------------------------------------------------------------- |
| Disable                | `-dpcvars=AutoRTFMRuntimeEnabled=1` *or* `-dpcvars=AutoRTFMRuntimeEnabled=1,AutoRTFMMemoryValidationLevel=1` |
| Warn and continue      | `-dpcvars=AutoRTFMRuntimeEnabled=1,AutoRTFMMemoryValidationLevel=2`                                          |
| Hard error             | `-dpcvars=AutoRTFMRuntimeEnabled=1,AutoRTFMMemoryValidationLevel=3`                                          |

| AutoRTFM Enable Probability    | Server Flags                               |
| :----------------------------- | :----------------------------------------- |
| 0.1% chance to enable AutoRTFM | `-dpcvars=AutoRTFMEnabledProbability=0.1`  |
| 5% chance to enable AutoRTFM   | `-dpcvars=AutoRTFMEnabledProbability=5.0`  |
| 50% chance to enable AutoRTFM  | `-dpcvars=AutoRTFMEnabledProbability=50.0` |
