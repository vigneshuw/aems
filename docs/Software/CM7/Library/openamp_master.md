# CM7 Library: OpenAMP Master Service

## Scope

This document describes the CM7-side OpenAMP master wrapper implemented in:

- `CM7/Core/Src/openamp_fs.c`
- `CM7/Core/Inc/openamp_fs.h`

This layer is the CM7 RPC client for CM4-owned services.

## High-level role

The CM7 OpenAMP wrapper provides blocking helper APIs that let `ControllerTask()` treat CM4 capabilities as function calls.

Examples:

- file counts
- file list and file size
- file stream open/read/close
- DAQ start / stop / status

Internally, each helper:

1. builds an `OpenAmpPingRequest_t`
2. sends it to CM4 over RPMsg
3. waits for the reply
4. optionally exposes shared-memory metadata or copies RPMsg chunk payload data

OpenAMP operation IDs are internal RPC IDs. They do not always match host TCP command numbers; `ControllerTask()` performs that mapping in `CM7/Core/Src/freertos.c`.

## Why this layer exists

CM7 owns host networking but does not own eMMC or DAQ sampling. So many host commands require a synchronous CM7-to-CM4 transaction before TCP can respond.

## Initialization API

### `int32_t OpenAmpFs_MasterInit(void)`

Initializes the CM7 OpenAMP master side:

- mailbox/HSEM setup
- RPMsg/OpenAMP stack init
- endpoint creation/wait-for-ready

Returns:
- `0` on success
- nonzero on failure

## Public API groups

### Health / diagnostics

| API | Purpose |
|---|---|
| `OpenAmpFs_Ping()` | round-trip ping to CM4 |
| `OpenAmpFs_GetServiceCreated()` | whether RPMsg service endpoint exists |
| `OpenAmpFs_GetRxCount()` | CM7-side RX count |
| `OpenAmpFs_GetInitStatus()` | CM7 init status |
| `OpenAmpFs_GetRemoteInitStatus()` | CM4 init status |
| `OpenAmpFs_GetRemoteMountStatus()` | CM4 eMMC mount status |
| `OpenAmpFs_GetRemoteMountDiagStage()` | CM4 eMMC mount lifecycle stage |
| `OpenAmpFs_GetRemoteMountDiagMountFresult()` | FatFs result from CM4 first `f_mount()` |
| `OpenAmpFs_GetRemoteMountDiagMkfsFresult()` | FatFs result from CM4 `f_mkfs()` |
| `OpenAmpFs_GetRemoteMountDiagPostMountFresult()` | FatFs result from CM4 post-format `f_mount()` |
| `OpenAmpFs_GetRemoteAdcDeviceId()` | ADC device ID reported by CM4 |

### Filesystem metadata

| API | Purpose |
|---|---|
| `OpenAmpFs_CountDatFiles()` | count `.dat` files |
| `OpenAmpFs_CountAllFiles()` | count all files |
| `OpenAmpFs_ListFilesShared()` | ask CM4 to place file list text in shared memory |
| `OpenAmpFs_GetFileSize()` | get one file size |
| `OpenAmpFs_DeleteLogFiles()` | delete log files |
| `OpenAmpFs_DeleteFile()` | delete one file |

### File data access

| API | Purpose |
|---|---|
| `OpenAmpFs_ReadFileChunk()` | direct RPMsg chunk read |
| `OpenAmpFs_OpenFileStream()` | open persistent stream on CM4 |
| `OpenAmpFs_ReadFileStream()` | direct RPMsg stream chunk read |
| `OpenAmpFs_ReadFileStreamShared()` | read next file chunk into shared SRAM |
| `OpenAmpFs_CloseFileStream()` | close file stream |

### DAQ control

| API | Purpose |
|---|---|
| `OpenAmpFs_DaqGetStatus()` | read current DAQ status |
| `OpenAmpFs_DaqStartLog()` | start command `11` DAQ log on CM4 |
| `OpenAmpFs_DaqStartStream()` | start command `13` DAQ stream on CM4 |
| `OpenAmpFs_DaqReadStreamShared()` | read next DAQ block into shared SRAM |
| `OpenAmpFs_DaqStop()` | request DAQ stop |
| `OpenAmpFs_DaqStopAndClose()` | stop and close DAQ log |

## Function-level notes

### Diagnostic getters used by command `99`

The latest OpenAMP response is cached in `g_last_response`. After `OpenAmpFs_Ping()` and `OpenAmpFs_ProbeSharedMemory()` complete, CM7 reads cached remote diagnostics through getter functions and places them into the command `99` TCP response.

Important command `99` storage fields:

| TCP offset | Getter | Meaning |
|---:|---|---|
| `39` | `OpenAmpFs_GetRemoteMountStatus()` | public CM4 mount status |
| `71` | `OpenAmpFs_GetRemoteMountDiagStage()` | mount lifecycle stage |
| `75` | `OpenAmpFs_GetRemoteMountDiagMountFresult()` | first `f_mount()` result |
| `79` | `OpenAmpFs_GetRemoteMountDiagMkfsFresult()` | `f_mkfs()` result |
| `83` | `OpenAmpFs_GetRemoteMountDiagPostMountFresult()` | post-format `f_mount()` result |

### `OpenAmpFs_ListFilesShared(uint8_t **buffer, uint32_t *bytes_read)`

Arguments:
- `buffer`: output pointer to `FILE_SHMEM_DATA_PTR`
- `bytes_read`: number of valid bytes in shared memory

This avoids copying long directory text through RPMsg payloads.

### `OpenAmpFs_OpenFileStream(const char *filename, uint32_t offset, uint32_t *file_size)`

Arguments:
- `filename`: remote file to stream
- `offset`: initial byte offset
- `file_size`: returned total file size

This is the first call in the command `8` streaming path.

### `OpenAmpFs_ReadFileStreamShared(...)`

```c
int32_t OpenAmpFs_ReadFileStreamShared(uint8_t **buffer,
                                       uint16_t buffer_size,
                                       uint16_t *bytes_read,
                                       uint32_t *offset,
                                       uint32_t *total_size);
```

Arguments:
- `buffer`: pointer to shared SRAM region
- `buffer_size`: maximum bytes CM7 is willing to consume
- `bytes_read`: returned valid chunk length
- `offset`: returned file offset for this chunk
- `total_size`: full file size

CM7 uses this operation in the command `8` path to hand shared SRAM directly to `TcpClient_StartStreamPtr()`.

### `OpenAmpFs_DaqStartLog(const DaqConfig_t *config)`

Starts command `11` DAQ logging on CM4.

Arguments:
- `config`: DAQ configuration passed through to CM4

### `OpenAmpFs_DaqStartStream(const DaqConfig_t *config)`

Starts command `13` live DAQ streaming on CM4.

Arguments:
- `config`: DAQ configuration for stream mode

### `OpenAmpFs_DaqReadStreamShared(...)`

```c
int32_t OpenAmpFs_DaqReadStreamShared(uint8_t **buffer,
                                      uint16_t buffer_size,
                                      uint16_t *bytes_read,
                                      uint32_t *samples_read,
                                      uint32_t *samples_captured);
```

Arguments:
- `buffer`: pointer to shared SRAM DAQ block
- `buffer_size`: maximum bytes acceptable to CM7
- `bytes_read`: returned payload size
- `samples_read`: number of frames in this block
- `samples_captured`: total samples captured on CM4

This is the core data path behind command `13`.

## Transaction model

Most public helpers call one of the request helpers:

- `OpenAmpFs_SendRequest()`
- `OpenAmpFs_SendRequestEx()`
- `OpenAmpFs_SendRequestEx2()`

Those functions:

1. clear the response-ready flag
2. populate request fields
3. call `OPENAMP_send()`
4. wait for the response callback to set ready state
5. decode the response into `g_last_response`

## Sequence diagram: host command to CM4 service

```plantuml
@startuml
skinparam monochrome true
skinparam shadowing false
participant CTRL as ControllerTask
participant OA as "CM7 OpenAmpFs"
participant RP as RPMsg/OpenAMP
participant CM4 as "CM4 OpenAmpFs"

CTRL -> OA : OpenAmpFs_* API
OA -> RP : send OpenAmpPingRequest_t
RP -> CM4 : deliver request
CM4 -> CM4 : execute EmmcFs_* or DAQ_* work
CM4 --> RP : OpenAmpSmallResponse_t / chunk response
RP --> OA : callback stores g_last_response
OA --> CTRL : return status / metadata
@enduml
```

## Shared-memory role

The CM7 OpenAMP wrapper is the layer that translates CM4 shared-memory semantics into CM7-friendly APIs. It is what turns:

- a CM4-shared file chunk
- or a CM4-shared DAQ block

into something that `ControllerTask()` can feed directly into `tcpclient`.

## Important implementation note

The CM7 OpenAMP wrapper is one of the most architecture-critical modules in the firmware. Any change here affects:

- command IDs
- file streaming
- DAQ log control
- DAQ live streaming
- Python host-side parsing assumptions
