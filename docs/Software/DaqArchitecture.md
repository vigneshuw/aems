# Dual-Core STM32H7 DAQ and Ethernet Manager Architecture Specification

## 1. Purpose

This document defines the software architecture for a dual-core STM32H7 system in which:

- **CM7** acts as the system manager and Ethernet-facing controller
- **CM4** acts as the deterministic data acquisition engine
- **CM4** acquires ADC data and writes it to eMMC
- **CM7** receives commands over Ethernet, triggers the actions in CM4, read data back from eMMC for transfer
- **both CM7 and CM4 are capable of accessing eMMC**, but access must be coordinated safely

This specification is intended to provide a robust, maintainable, and deterministic implementation plan.

---

## 2. System Summary

### 2.1 Core roles

#### CM7
CM7 runs FreeRTOS v1 and is responsible for:

- Ethernet stack
- command server
- protocol parsing
- supervisory control
- system health reporting
- file transfer coordination
- inter-core communication with CM4

#### CM4
CM4 does not run an RTOS. It is responsible for:

- ADC configuration
- DMA-driven acquisition
- DAQ buffering
- writing DAQ data to eMMC
- responding to manager requests from CM7

---

## 3. Design Goals

The architecture shall satisfy the following goals:

1. **Deterministic acquisition**
   ADC sampling and buffering on CM4 shall not be disrupted by Ethernet traffic or noncritical control logic.

2. **Safe eMMC/filesystem access**
   Concurrent dual-core access to eMMC and FatFs shall not corrupt the filesystem or create undefined behavior.

3. **Clear ownership boundaries**
   Peripheral, protocol, and storage responsibilities shall be explicitly defined.

4. **Simple inter-core coordination**
   CM7 and CM4 shall communicate using lightweight shared-memory IPC.

5. **Scalable data transfer**
   Data shall be returned over Ethernet using chunked transfers rather than monolithic file copies.

6. **Recoverability**
   The file format and control logic shall support graceful stop, fault handling, and interrupted-run diagnosis.

---

## 4. Architectural Principles

### 4.1 Control plane and data plane split

The system shall be divided logically into:

- **control plane on CM7**
- **data plane on CM4**

CM7 handles supervisory logic and networking. CM4 handles time-sensitive acquisition and storage.

### 4.2 Single active writer model

Even though both cores can access eMMC, **CM4 shall be the only writer for active DAQ files**.

This is a core design rule.

CM7 may access the filesystem for limited nonconflicting operations, but it shall not write to the currently active DAQ file.

### 4.3 Transaction-based filesystem locking

All filesystem operations that access the shared volume shall be protected using a **global filesystem transaction lock** implemented with HSEM.

Locking only startup functions such as `FATFS_LinkDriver()` or `f_mkfs()` is not sufficient.

### 4.4 Bare-metal CM4 event-driven execution

CM4 shall be structured as:

- interrupt-assisted
- state-machine-driven
- cooperative superloop-based

CM4 shall not implement blocking, long-running code inside ISRs.

---

## 5. High-Level Software Architecture

## 5.1 CM7 software blocks

CM7 shall contain the following major blocks:

- Ethernet stack
- command parser
- system manager task
- file transfer task
- IPC manager
- diagnostics/status service

## 5.2 CM4 software blocks

CM4 shall contain the following major blocks:

- ADC/DMA driver control
- DAQ engine state machine
- buffer manager
- storage engine
- IPC responder
- status publisher

## 5.3 Shared blocks

Shared cross-core resources shall include:

- command mailbox
- response mailbox
- status block
- optional shared transfer buffers
- HSEM-based locking
- shared-memory regions

---

## 6. Responsibilities by Core

## 6.1 CM7 responsibilities

CM7 shall own:

- LwIP Ethernet stack
- TCP/UDP or application protocol server
- parsing incoming host commands
- validating requests
- issuing commands to CM4
- presenting DAQ status to host
- orchestrating file transfers
- global system state management

CM7 should be the only core that interacts directly with the host over Ethernet.

## 6.2 CM4 responsibilities

CM4 shall own:

- ADC sampling setup
- DMA interrupts and buffer service
- DAQ state machine
- active DAQ file creation and write operations
- acquisition stop/flush/finalize
- status updates for DAQ progress
- responding to IPC commands from CM7

CM4 should remain highly deterministic and minimal.

---

## 7. eMMC and FatFs Access Policy

## 7.1 Policy summary

Although both cores can access eMMC, filesystem access shall follow these rules:

1. **CM4 is the sole owner of active DAQ file writes**
2. **CM7 shall not directly modify the active DAQ file**
3. **all FatFs transactions on the shared volume shall be serialized**
4. **all low-level eMMC access paths shall be non-reentrant across cores**
5. **simultaneous read of an actively growing file is discouraged unless explicitly supported by policy**

## 7.2 Why global locking is required

FatFs operations modify shared volume state, including:

- FAT allocation structures
- directory entries
- cluster chains
- sector caches
- file size metadata

Therefore, both cores shall not call FatFs APIs concurrently on the same volume without serialization.

## 7.3 Filesystem transaction lock

A global HSEM lock shall protect each complete logical filesystem transaction.

Examples of a transaction include:

- `f_open + f_write + f_sync + f_close`
- `f_open + f_read + f_close`
- `f_opendir + repeated f_readdir + f_closedir`
- `f_unlink`
- `f_stat`
- `f_rename`

Locking only one API call at a time is insufficient.

---

## 8. Recommended Active File Policy

## 8.1 Preferred policy

The preferred policy is:

- CM4 writes the active DAQ file
- CM7 does not read or modify the active DAQ file directly
- CM7 requests file data only from finalized or closed files

This is the safest and simplest mode.

## 8.2 Optional chunk-rotation policy

If near-live readback is required, CM4 should write chunked DAQ files such as:

- `run001_part0001.bin`
- `run001_part0002.bin`
- `run001_part0003.bin`

Under this policy:

- only the currently open chunk is active
- CM7 may read completed chunks
- CM7 shall not access the currently open chunk

This provides safer concurrent operation than allowing reads from a growing file.

---

## 9. Inter-Core Communication Specification

## 9.1 IPC transport

IPC shall use:

- shared memory
- hardware semaphore coordination where needed
- interrupt or event notification between cores

A full RPMsg/OpenAMP stack is not required for this use case.

## 9.2 IPC channels

The following shared channels shall be implemented:

- **CM7 to CM4 command mailbox**
- **CM4 to CM7 response/event mailbox**
- **shared status block**
- optional **shared chunk buffer** for file transfer payloads

## 9.3 IPC design requirements

IPC messages shall be:

- fixed-size where possible
- sequence-numbered
- versioned if future expansion is expected
- ownership-defined

CM7 shall generally act as the command initiator.

---

## 10. IPC Message Definitions

## 10.1 Command message

```c
typedef enum
{
    IpcCmdNone = 0,
    IpcCmdPing,
    IpcCmdGetStatus,
    IpcCmdStartAcq,
    IpcCmdStopAcq,
    IpcCmdReadFileChunk,
    IpcCmdDeleteFile,
    IpcCmdListFiles,
    IpcCmdStatFile
} IpcCmdType_t;

typedef struct
{
    uint32_t Magic;
    uint32_t Version;
    uint32_t Seq;
    uint32_t Cmd;
    uint32_t Arg0;
    uint32_t Arg1;
    uint32_t Arg2;
    char Filename[64];
} IpcCommand_t;
```

## 10.2 Response message

```c
typedef enum
{
    IpcRspNone = 0,
    IpcRspAck,
    IpcRspError,
    IpcRspStatus,
    IpcRspChunkReady,
    IpcRspChunkEof,
    IpcRspFileInfo,
    IpcRspFileList
} IpcRspType_t;

typedef struct
{
    uint32_t Magic;
    uint32_t Version;
    uint32_t Seq;
    uint32_t Rsp;
    uint32_t Status;
    uint32_t DataLength;
    uint32_t FileOffset;
    uint32_t ErrorCode;
} IpcResponse_t;
```

## 10.3 Shared status block

```c
typedef enum
{
    DaqStateIdle = 0,
    DaqStatePreparing,
    DaqStateAcquiring,
    DaqStateStopping,
    DaqStateFinalizing,
    DaqStateReading,
    DaqStateError
} DaqState_t;

typedef struct
{
    uint32_t Magic;
    uint32_t Version;
    uint32_t State;
    uint32_t LastError;
    uint32_t SampleRateHz;
    uint32_t ChannelCount;
    uint64_t SamplesCaptured;
    uint64_t BytesWritten;
    uint32_t DroppedBuffers;
    uint32_t ActiveFileSize;
    char ActiveFilename[64];
} SharedStatus_t;
```

---

## 11. Memory Map and Shared Regions

## 11.1 Memory classes

The system shall define at least three memory classes:

### Private CM4 acquisition memory
Used for:

- ADC DMA buffers
- DAQ block assembly buffers
- CM4 local working state

### Shared IPC memory
Used for:

- command mailbox
- response mailbox
- status block
- optional chunk transfer buffer

### Private CM7 networking memory
Used for:

- LwIP state
- pbufs
- protocol context
- FreeRTOS objects

## 11.2 Cache coherency

On STM32H7 dual-core systems, the design shall explicitly address:

- cacheable versus non-cacheable shared memory regions
- DMA-safe memory placement
- cache clean before DMA transmit/write
- cache invalidate after DMA receive/read

Failure to define cache policy will cause intermittent corruption or stale-data bugs.

## 11.3 Recommendation

Shared IPC structures should preferably be placed in a shared non-cacheable region if practical.

If cache remains enabled for shared regions, explicit cache maintenance shall be performed at each ownership handoff.

---

## 12. HSEM Usage Plan

## 12.1 Semaphore allocation

The following HSEM IDs are recommended:

- `HSEM_BOOT_INIT` for startup coordination
- `HSEM_FS_GLOBAL` for filesystem transaction locking
- `HSEM_MMC_HW` for low-level hardware access if required
- `HSEM_IPC_CMD` for mailbox signaling if implemented that way

## 12.2 Boot initialization semaphore

`HSEM_BOOT_INIT` shall be used to coordinate:

- one-time driver linkage
- initial format if needed
- initial mount sequencing
- shared startup readiness signaling

## 12.3 Global filesystem semaphore

`HSEM_FS_GLOBAL` shall protect all complete filesystem transactions across both cores.

## 12.4 Hardware semaphore for MMC peripheral

If the low-level eMMC driver path is not guaranteed safe under filesystem-level serialization alone, `HSEM_MMC_HW` may be used internally in the disk I/O layer.

This depends on implementation details of the HAL and disk driver.

---

## 13. Startup and Initialization Sequence

## 13.1 Boot goals

Startup shall ensure:

- one-time initialization is not duplicated unsafely
- formatting is performed only when explicitly intended
- both cores agree on filesystem readiness
- DAQ cannot start until storage is ready

## 13.2 Recommended startup sequence

1. CM7 and CM4 boot
2. one designated core acquires `HSEM_BOOT_INIT`
3. that core performs:
   - driver linkage if needed
   - media detect
   - optional format if required
   - mount or readiness preparation
4. startup status is written into shared status/boot block
5. semaphore is released
6. the other core waits until storage-ready flag is set
7. normal operation begins

## 13.3 Important note on formatting

`f_mkfs()` shall not be called routinely at boot.

Formatting must be a deliberate maintenance action only.

---

## 14. CM7 FreeRTOS Task Architecture

## 14.1 Task list

CM7 shall implement the following core tasks.

### EthernetCommandTask
Responsibilities:

- receive network commands
- parse request frames
- validate syntax
- forward valid requests to SystemManagerTask

### SystemManagerTask
Responsibilities:

- main supervisory state machine
- issue IPC commands to CM4
- await responses
- handle timeouts and error propagation
- coordinate acquisition lifecycle

This should be the only task that sends direct management commands to CM4.

### EthernetDataTransferTask
Responsibilities:

- coordinate chunked file transfer
- request data from CM4 or direct filesystem reads per policy
- send chunks over Ethernet
- report transfer status

### StatusTask
Responsibilities:

- expose shared status to remote host
- monitor watchdog counters and fault flags
- support diagnostics

### Optional LoggingTask
Responsibilities:

- emit debug traces
- maintain software logs
- support development visibility

## 14.2 CM7 priority guidance

Suggested relative priority order:

1. SystemManagerTask
2. EthernetCommandTask
3. EthernetDataTransferTask
4. StatusTask
5. LoggingTask

Exact values may be tuned during integration.

---

## 15. CM4 Bare-Metal Architecture

## 15.1 Execution model

CM4 shall execute as:

- ISR-assisted
- event-driven
- superloop-based
- state-machine-controlled

## 15.2 CM4 modules

Recommended modules:

- `IpcCm4.c`
- `DaqEngine.c`
- `BufferManager.c`
- `StorageEngine.c`
- `StatusManager.c`
- `Main.c`

## 15.3 CM4 main loop model

Conceptually:

```c
while (1)
{
    ProcessIncomingCommand();
    ProcessHighPriorityControl();
    ProcessDmaReadyBuffers();
    ProcessPendingStorageWrites();
    ProcessReadbackRequests();
    PublishStatus();
}
```

The actual structure may be bitmask- or queue-driven.

## 15.4 CM4 event sources

CM4 shall react to:

- command arrival from CM7
- ADC DMA half-complete
- ADC DMA full-complete
- stop request
- storage completion
- readback request
- fault condition

---

## 16. CM4 State Machine

## 16.1 States

CM4 DAQ state shall include at minimum:

- `Idle`
- `Preparing`
- `Acquiring`
- `Stopping`
- `Finalizing`
- `Reading`
- `Error`

## 16.2 State intent

### Idle
No active acquisition. Await commands.

### Preparing
Create file, configure ADC/DMA, initialize counters, prepare buffers.

### Acquiring
ADC/DMA running. Buffers are aggregated and written to eMMC.

### Stopping
Stop has been requested. ADC/DMA is being halted cleanly.

### Finalizing
Buffered data is flushed and file metadata is finalized.

### Reading
CM4 is servicing a file readback request.

### Error
A recoverable or nonrecoverable fault has occurred.

---

## 17. ADC and DMA Strategy

## 17.1 Acquisition method

ADC sampling shall use DMA, preferably with:

- circular mode plus half/full callbacks
- or double-buffer mode if supported and preferred

## 17.2 ISR rules

DMA/ADC interrupt handlers shall:

- set flags or enqueue events
- update counters if needed
- notify the main loop

ISRs shall not:

- perform file writes
- do complex parsing
- block
- call heavy filesystem routines

## 17.3 Buffer hierarchy

CM4 shall use a staged buffering model:

### Level 1: DMA buffer
Small and timing-critical

### Level 2: aggregation buffer
Intermediate buffer to combine DMA fragments into storage-sized records

### Level 3: write queue
Ready-to-write blocks awaiting storage commit

This structure decouples ADC timing from storage latency.

---

## 18. Storage Engine Design

## 18.1 General requirements

The storage engine shall:

- create and open DAQ files
- append acquisition blocks
- periodically flush data
- finalize files on stop
- report bytes written and errors

## 18.2 Write strategy

The design should avoid writing one tiny DMA fragment at a time.

Instead, it should:

- collect data into larger aligned blocks
- write blocks of practical size such as 4 KB, 8 KB, or 16 KB
- periodically `f_sync()` according to performance and integrity needs

## 18.3 Flush strategy

The flush policy shall balance:

- throughput
- power-loss exposure
- metadata update cost

A flush may occur:

- every N blocks
- every T milliseconds
- on stop request
- on fault handling

---

## 19. File Transfer Strategy

## 19.1 General model

All data returned over Ethernet shall use chunked transfer.

The system shall not attempt to load an entire file into RAM.

## 19.2 Chunk transaction model

A typical transfer shall proceed as:

1. host requests file or range
2. CM7 validates request
3. CM7 requests chunk from CM4 or directly reads per approved policy
4. data is placed into buffer
5. CM7 sends chunk over Ethernet
6. repeat until complete

## 19.3 Chunk size

Chunk size shall be chosen based on:

- available shared RAM
- Ethernet throughput
- storage throughput
- latency requirements

A practical starting point is 4 KB to 16 KB.

## 19.4 Preferred responsibility

Preferred design:

- CM4 reads DAQ files for transfer
- CM7 sends them over Ethernet

This preserves the single-owner model for DAQ files.

---

## 20. File Format Specification

## 20.1 General recommendation

DAQ data shall be stored in a structured binary format, not as uncontrolled raw byte streams.

## 20.2 File layout

A DAQ file should contain:

### File header
- magic number
- format version
- session ID or run number
- start timestamp
- sample rate
- channel count
- ADC resolution
- scaling metadata if needed

### Repeated data blocks
Each block should contain:
- block magic or tag
- block sequence number
- sample index or timestamp
- payload length
- payload bytes
- optional CRC

### Finalization info
- completion flag or footer
- stop timestamp
- total block count
- abort indication if incomplete

## 20.3 Benefits

This format improves:

- offline parsing
- corruption detection
- partial file recovery
- debug visibility
- future compatibility

---

## 21. Host Protocol Recommendation

## 21.1 Suggested commands

The Ethernet-facing protocol on CM7 should support commands such as:

- `PING`
- `GET_STATUS`
- `START_ACQ`
- `STOP_ACQ`
- `LIST_FILES`
- `STAT_FILE`
- `READ_FILE`
- `DELETE_FILE`

## 21.2 Example command semantics

### START_ACQ
Parameters:
- filename
- sample rate
- channel mask or channel count
- optional duration or sample limit

### STOP_ACQ
Request orderly stop and finalize active file.

### GET_STATUS
Return DAQ state, bytes written, counters, errors.

### READ_FILE
Request chunked transfer of a file or file range.

## 21.3 Transport

TCP is recommended for simplicity and reliability.

UDP should only be used if a strong low-latency or specialized streaming requirement justifies its complexity.

---

## 22. Filesystem Locking Rules

## 22.1 Mandatory rules

The following rules are mandatory.

1. Every FatFs transaction shall be guarded by `FsLock()` / `FsUnlock()`
2. The lock shall span the complete logical transaction
3. No core shall hold the filesystem lock longer than necessary
4. The active DAQ file shall not be modified by CM7
5. Directory mutation during active DAQ shall be limited and controlled

## 22.2 Wrapper API recommendation

Both cores should use a shared wrapper pattern such as:

```c
void FsLock(void);
void FsUnlock(void);
```

Filesystem access code shall not directly use HSEM everywhere inline.

This reduces mistakes and centralizes policy.

## 22.3 Example transaction

```c
FsLock();

res = f_open(&fil, Filename, FA_WRITE | FA_OPEN_APPEND);
if (res == FR_OK)
{
    res = f_write(&fil, Buffer, Length, &BytesWritten);
    if (res == FR_OK)
    {
        res = f_sync(&fil);
    }
    f_close(&fil);
}

FsUnlock();
```

---

## 23. Error Handling Strategy

## 23.1 Error classes

Errors shall be classified at least as:

- IPC errors
- DAQ errors
- ADC/DMA errors
- filesystem errors
- eMMC driver errors
- protocol errors
- timeout errors

## 23.2 Error propagation

CM4 shall publish errors to CM7 via:

- response mailbox
- shared status block
- fault counters

CM7 shall translate errors into host-visible responses.

## 23.3 Error response rules

Examples:

- acquisition buffer overrun should increment a counter and may force stop
- filesystem write failure should move CM4 to `Error`
- invalid host command should not affect DAQ state
- timeout awaiting CM4 response should be handled by CM7 manager task

---

## 24. Watchdog and Health Monitoring

## 24.1 Watchdog coverage

The system should implement watchdog coverage for:

- CM7 task liveness
- CM4 main-loop liveness
- IPC response timeout
- acquisition buffer overrun detection

## 24.2 Health metrics

The status block should expose at least:

- DAQ state
- active filename
- sample count
- bytes written
- dropped buffers
- last error
- free buffer count if relevant
- readback activity if relevant

---

## 25. Concurrency Restrictions

## 25.1 Unsupported operations

The following shall be considered unsupported unless specifically implemented and validated:

- CM7 reading the active DAQ file while CM4 is appending it
- CM7 deleting or renaming an active DAQ file
- both cores independently mounting, opening, and modifying the same file concurrently
- direct concurrent low-level MMC transactions from both cores without serialization

## 25.2 Controlled operations

The following may be supported with care:

- CM7 listing directory contents while CM4 is idle or between write transactions
- CM7 reading finalized DAQ files
- CM7 performing maintenance operations when no acquisition is active

---

## 26. Recommended Development Sequence

## 26.1 Phase 1: IPC bring-up
Implement:

- shared mailboxes
- simple `PING`
- `GET_STATUS`

## 26.2 Phase 2: CM4 RAM-only acquisition
Implement:

- ADC + DMA
- buffer handling
- acquisition state machine
- no filesystem writes yet

## 26.3 Phase 3: eMMC single-core write
Implement:

- CM4 file creation
- append writes
- finalize on stop

Keep CM7 away from filesystem except for status.

## 26.4 Phase 4: file transfer
Implement:

- chunked `READ_FILE`
- CM4 reads file
- CM7 transmits over Ethernet

## 26.5 Phase 5: limited CM7 filesystem access
Only if needed, implement:

- directory listing
- stat operations
- maintenance functions

All protected by filesystem lock.

## 26.6 Phase 6: optimization and hardening
Implement and validate:

- cache maintenance
- watchdogs
- fault injection tests
- power interruption tests
- throughput tuning

---

## 27. Validation and Test Plan

## 27.1 IPC tests
Verify:

- command delivery
- response delivery
- sequence number integrity
- timeout detection

## 27.2 Acquisition tests
Verify:

- correct sample rate
- no dropped buffers under expected load
- clean stop behavior
- file integrity

## 27.3 Filesystem concurrency tests
Verify:

- CM4 DAQ writes remain valid
- CM7 maintenance operations do not corrupt volume
- filesystem lock is always released
- no deadlocks occur

## 27.4 Transfer tests
Verify:

- large file transfers succeed
- partial read requests work
- chunk boundaries are correct
- network disconnect recovery is acceptable

## 27.5 Fault tests
Inject:

- eMMC write failures
- IPC timeouts
- DMA overrun conditions
- forced resets during acquisition

---

## 28. Coding Rules and Implementation Notes

## 28.1 General rules

- avoid dynamic allocation in timing-critical paths
- keep ISRs short
- use explicit state machines
- define ownership of every shared object
- prefer static buffers
- centralize filesystem locking

## 28.2 CM4 rules

- no file writes inside ISR
- no blocking loops that starve buffer service
- check stop requests at high priority
- publish health regularly

## 28.3 CM7 rules

- only SystemManagerTask may issue DAQ lifecycle commands
- network tasks shall not bypass manager logic
- file transfers shall be chunked
- avoid direct manipulation of active DAQ files

---

## 29. Recommended Final Architecture Statement

The recommended system architecture is:

- **CM7 runs FreeRTOS and acts as the Ethernet-facing system manager**
- **CM4 runs bare-metal and acts as the deterministic DAQ and active-file writer**
- **shared eMMC access is permitted only under strict transaction-level filesystem serialization**
- **CM4 is the sole writer of active DAQ files**
- **CM7 coordinates acquisition and file transfer through lightweight shared-memory IPC**
- **data is returned over Ethernet using chunked transfers**
- **filesystem access policy forbids CM7 from modifying the active DAQ file**
- **all shared-memory and DMA regions must be designed with explicit cache coherency rules**

This architecture gives the best balance of determinism, robustness, simplicity, and maintainability for the described system.

---

## 30. Appendix A: Minimal Filesystem Lock Abstraction

```c
void FsLock(void)
{
    LOCK_HSEM(HSEM_FS_GLOBAL);
}

void FsUnlock(void)
{
    UNLOCK_HSEM(HSEM_FS_GLOBAL);
}
```

Usage:

```c
FsLock();

res = f_open(&fil, "data.bin", FA_READ);
if (res == FR_OK)
{
    res = f_read(&fil, Buffer, Length, &BytesRead);
    f_close(&fil);
}

FsUnlock();
```

---

## 31. Appendix B: Minimal CM4 Event Flags Example

```c
#define EVT_NONE              0x00000000u
#define EVT_CMD_RECEIVED      0x00000001u
#define EVT_DMA_HALF_READY    0x00000002u
#define EVT_DMA_FULL_READY    0x00000004u
#define EVT_STOP_REQUESTED    0x00000008u
#define EVT_READ_REQUEST      0x00000010u
#define EVT_STORAGE_ERROR     0x00000020u
```

Conceptual main loop:

```c
for (;;)
{
    if (Events & EVT_CMD_RECEIVED)
    {
        HandleCommand();
    }

    if (Events & EVT_STOP_REQUESTED)
    {
        HandleStopRequest();
    }

    if (Events & EVT_DMA_HALF_READY)
    {
        ProcessDmaHalf();
    }

    if (Events & EVT_DMA_FULL_READY)
    {
        ProcessDmaFull();
    }

    ServicePendingWrites();
    ServiceReadRequests();
    UpdateStatus();
}
```

---

## 32. Appendix C: Recommended Next-Step Deliverables

The next useful documents to create from this specification are:

1. a shared-memory map
2. IPC header file definitions
3. CM7 task-to-task command flow
4. CM4 DAQ state diagram
5. filesystem locking wrapper implementation
6. DAQ binary file format definition
7. Ethernet command protocol definition

I can turn this into a more formal design package next, including:
a header/API spec, state diagrams, and example source skeletons for CM7 and CM4.

