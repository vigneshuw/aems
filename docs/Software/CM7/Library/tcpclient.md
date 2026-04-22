# CM7 Library: tcpclient

## Scope

`tcpclient` is the CM7 library that maintains the Ethernet/TCP connection to the host computer. It is the transport engine behind fixed-size command responses and streamed payload delivery.

This library is not a generic server. In the current architecture, the board is a **TCP client** that connects outward to a host machine running the Python tooling.

## Source files

| File | Role |
|---|---|
| `CM7/Library/eth/tcpclient.h` | public API and callback types |
| `CM7/Library/eth/tcpclient.c` | socket loop, TX queueing, and stream engine |

## High-level responsibilities

`tcpclient` provides:

- TCP socket connect/reconnect logic
- fixed response transmission
- receive callback dispatch to command parser
- queued TX buffering for ordinary command responses
- stream-header + raw-payload transfer for file and DAQ streaming

## Internal model

The library contains a single process-wide state structure (`TcpClientState_t`) that owns:

- socket descriptor
- connection state
- transmit mailbox and mutex
- callback configuration
- active stream state

## Public callback and config types

### `TcpClientRxHandler_t`

```c
typedef void (*TcpClientRxHandler_t)(const char *data, uint16_t length);
```

Called when command bytes arrive from the host.

Arguments:
- `data`: received bytes
- `length`: received length

In current firmware this points at `ProcessTcpData()` in `freertos.c`.

### `TcpClientStreamReadFn`

```c
typedef int32_t (*TcpClientStreamReadFn)(void *context,
                                         uint8_t *buffer,
                                         uint16_t max_len,
                                         uint16_t *out_len);
```

Copy-style stream callback.

Arguments:
- `context`: stream context
- `buffer`: destination owned by `tcpclient`
- `max_len`: buffer capacity
- `out_len`: returned valid byte count

Used by command `9` test stream.

### `TcpClientStreamReadPtrFn`

```c
typedef int32_t (*TcpClientStreamReadPtrFn)(void *context,
                                            const uint8_t **out_data,
                                            uint16_t max_len,
                                            uint16_t *out_len);
```

Pointer-style stream callback.

Arguments:
- `context`: stream context
- `out_data`: returned pointer to bytes to send
- `max_len`: maximum allowed chunk size this iteration
- `out_len`: returned byte count

Used for high-throughput file and DAQ streaming from shared SRAM.

### `TcpClientStreamDoneFn`

```c
typedef void (*TcpClientStreamDoneFn)(void *context);
```

Completion or abort callback for streams.

### `TcpClientConfig_t`

```c
typedef struct
{
    ip_addr_t ServerIp;
    uint16_t ServerPort;
    struct netif *Netif;
    TcpClientRxHandler_t RxHandler;
} TcpClientConfig_t;
```

Fields:
- `ServerIp`: host server IP
- `ServerPort`: host server port
- `Netif`: LwIP network interface
- `RxHandler`: callback used for incoming command bytes

## Public API reference

### `void TcpClient_BuildConfig(...)`

Populates a `TcpClientConfig_t`.

Arguments:
- `config`: destination configuration
- `serverIp`: host server IP
- `serverPort`: TCP port
- `netif`: LwIP network interface
- `rxHandler`: host command receive callback

### `int32_t TcpClient_Init(const TcpClientConfig_t *config)`

Initializes the TCP client task resources.

Arguments:
- `config`: validated configuration

Returns:
- `0` on success
- negative values for config, mailbox, mutex, or task init failures

### `int32_t TcpClient_SendBuffer(const uint8_t *data, uint16_t length)`

Queues a raw fixed-size or variable-size message for transmission.

Arguments:
- `data`: source bytes
- `length`: length to queue

Used by `ControllerTask()` for fixed 100-byte replies and file-list chunks.

### `int32_t TcpClient_StartStream(...)`

Starts a stream using the copy callback interface.

Arguments:
- `header`: stream header bytes
- `header_len`: header length
- `total_size`: total payload bytes after header
- `read_fn`: copy-style callback
- `done_fn`: completion callback
- `context`: callback context

Used by command `9` test stream.

### `int32_t TcpClient_StartStreamPtr(...)`

Starts a stream using the pointer callback interface.

Arguments:
- `header`: stream header bytes
- `header_len`: header length
- `total_size`: total payload bytes after header
- `read_ptr_fn`: pointer-style callback
- `done_fn`: completion callback
- `context`: callback context

Used by:
- command `8` file stream
- command `13` live DAQ stream

### `int32_t TcpClient_Send(const char *text)`

Convenience wrapper to queue a null-terminated string.

### `uint8_t TcpClient_IsConnected(void)`

Returns `1` when socket is connected, otherwise `0`.

### `void TcpClient_RequestReconnect(void)`

Requests the socket loop to reconnect.

## Stream framing used by CM7

### Fixed 100-byte replies

Most commands use a fixed 100-byte reply format:

| Offset | Size | Meaning |
|---:|---:|---|
| `0` | 1 | command |
| `1` | 4 | server ID, big-endian |
| `5` | 8 | epoch time, big-endian |
| `13` | 1 | status |
| `14` | 1 | TCP connected flag |
| `15+` | command-specific fields |

### 18-byte stream header

Commands `8`, `9`, and `13` use an 18-byte stream header before raw payload bytes:

| Offset | Size | Meaning |
|---:|---:|---|
| `0` | 1 | command |
| `1` | 1 | stream status |
| `2` | 4 | server ID |
| `6` | 8 | epoch time |
| `14` | 4 | total payload bytes following |

## File stream path

```plantuml
@startuml
skinparam monochrome true
skinparam shadowing false
participant CTRL as ControllerTask
participant OA as CM7 OpenAmpFs
participant TCP as tcpclient
participant CM4 as CM4
participant SHM as Shared SRAM
participant Host

CTRL -> OA : OpenFileStream(filename, offset)
OA -> CM4 : STREAM_OPEN
CM4 --> OA : file_size
CTRL -> TCP : StartStreamPtr(header)
TCP --> Host : 18-byte stream header
loop each chunk
  TCP -> CTRL : read_ptr callback
  CTRL -> OA : ReadFileStreamShared()
  OA -> CM4 : STREAM_READ_SHMEM
  CM4 -> SHM : fill shared chunk
  CM4 --> OA : offset/length
  CTRL --> TCP : pointer to SHM bytes
  TCP --> Host : raw file bytes
end
TCP -> CTRL : done callback
CTRL -> OA : CloseFileStream()
@enduml
```

## Live DAQ stream path

```plantuml
@startuml
skinparam monochrome true
skinparam shadowing false
participant CTRL as ControllerTask
participant OA as CM7 OpenAmpFs
participant TCP as tcpclient
participant CM4 as CM4
participant SHM as Shared SRAM
participant Host

CTRL -> OA : DaqStartStream(config)
OA -> CM4 : DAQ_START_STREAM
CM4 --> OA : start status
CTRL -> TCP : StartStreamPtr(header, total_size)
TCP --> Host : 18-byte stream header
loop each DAQ block
  TCP -> CTRL : read_ptr callback
  CTRL -> OA : DaqReadStreamShared()
  OA -> CM4 : DAQ_READ_SHMEM
  CM4 -> SHM : copy full DaqSampleFrame_t block
  CM4 --> OA : bytes_read / samples_read
  CTRL --> TCP : pointer to SHM bytes
  TCP --> Host : raw DAQ bytes
end
TCP -> CTRL : done callback
CTRL -> OA : DaqStop()
@enduml
```

## Important implementation note

The library supports **pointer-based streaming** because that is how CM7 avoids extra copies when moving data from shared SRAM to the host socket. This is a key performance feature for commands `8` and `13`.
