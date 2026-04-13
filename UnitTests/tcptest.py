import socket
import struct
import threading
import time


HOST = "0.0.0.0"
PORT = 10
PACKET_LEN = 100
CONFIG_READ_HEADER_LEN = 24
FILE_STREAM_HEADER_LEN = 18
SERVER_ID = 1
MAX_USEFUL_PAYLOAD_LEN = PACKET_LEN - 15
CONFIG_TEST_PAYLOAD = bytes(((index * 3) + 1) & 0xFF for index in range(MAX_USEFUL_PAYLOAD_LEN))
READ_FILENAME = b"test.dat"
config_rx_state = {}
expected_config_file = None
transfer_metrics = {}
file_read_in_progress = False
active_file_stream = None
read_chunk_offset = 0
stream_read_offset = 0
chunk_response_condition = threading.Condition()
chunk_responses = {}


def build_packet(command, server_id, epoch_time):
    payload = bytearray(PACKET_LEN)
    useful_payload = b""

    payload[0] = command & 0xFF
    payload[1:5] = struct.pack(">I", server_id)
    payload[5:13] = struct.pack(">Q", epoch_time)

    if command == 1:
        useful_payload = CONFIG_TEST_PAYLOAD
    elif command == 5:
        useful_payload = READ_FILENAME
    elif command == 7:
        useful_payload = struct.pack(">I", read_chunk_offset) + READ_FILENAME
    elif command == 8:
        useful_payload = struct.pack(">I", stream_read_offset) + READ_FILENAME
    elif command == 9:
        useful_payload = b""
    elif command == 99:
        useful_payload = b""
    else:
        useful_payload = bytes((index & 0xFF) for index in range(MAX_USEFUL_PAYLOAD_LEN))

    useful_len = min(len(useful_payload), MAX_USEFUL_PAYLOAD_LEN)
    payload[13:15] = struct.pack(">H", useful_len)
    payload[15:15 + useful_len] = useful_payload[:useful_len]

    return bytes(payload)


def parse_config_write(packet):
    command = packet[0]
    server_id = struct.unpack(">I", packet[1:5])[0]
    epoch_time = struct.unpack(">Q", packet[5:13])[0]
    system_status = packet[13]
    tcp_connected = packet[14]
    fs_status = struct.unpack(">I", packet[15:19])[0]

    print(
        "RX config-write: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}, "
        f"fs_status=0x{fs_status:08X}"
    )


def parse_heartbeat(packet):
    command = packet[0]
    server_id = struct.unpack(">I", packet[1:5])[0]
    epoch_time = struct.unpack(">Q", packet[5:13])[0]
    system_status = packet[13]
    tcp_connected = packet[14]

    print(
        "RX heartbeat: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}"
    )


def parse_file_count(packet):
    command = packet[0]
    server_id = struct.unpack(">I", packet[1:5])[0]
    epoch_time = struct.unpack(">Q", packet[5:13])[0]
    system_status = packet[13]
    tcp_connected = packet[14]
    dat_file_count = struct.unpack(">I", packet[15:19])[0]
    fs_status = struct.unpack(">I", packet[19:23])[0]

    print(
        "RX file-count: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}, "
        f"dat_count={dat_file_count}, "
        f"fs_status=0x{fs_status:08X}"
    )


def parse_total_file_count(packet):
    command = packet[0]
    server_id = struct.unpack(">I", packet[1:5])[0]
    epoch_time = struct.unpack(">Q", packet[5:13])[0]
    system_status = packet[13]
    tcp_connected = packet[14]
    total_file_count = struct.unpack(">I", packet[15:19])[0]
    fs_status = struct.unpack(">I", packet[19:23])[0]

    print(
        "RX total-file-count: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}, "
        f"total_count={total_file_count}, "
        f"fs_status=0x{fs_status:08X}"
    )


def parse_file_size(packet):
    command = packet[0]
    server_id = struct.unpack(">I", packet[1:5])[0]
    epoch_time = struct.unpack(">Q", packet[5:13])[0]
    system_status = packet[13]
    tcp_connected = packet[14]
    file_size = struct.unpack(">I", packet[15:19])[0]
    fs_status = struct.unpack(">i", packet[19:23])[0]

    print(
        "RX file-size: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}, "
        f"file_size={file_size}, "
        f"fs_status={fs_status}"
    )


def parse_cm4_heartbeat(packet):
    command = packet[0]
    server_id = struct.unpack(">I", packet[1:5])[0]
    epoch_time = struct.unpack(">Q", packet[5:13])[0]
    system_status = packet[13]
    tcp_connected = packet[14]
    ipc_result = struct.unpack(">I", packet[15:19])[0]
    ipc_error = struct.unpack(">I", packet[19:23])[0]
    fs_ready = struct.unpack(">I", packet[23:27])[0]
    emmc_busy = struct.unpack(">I", packet[27:31])[0]
    emmc_init_status = struct.unpack(">i", packet[31:35])[0]
    emmc_mount_status = struct.unpack(">i", packet[35:39])[0]
    emmc_create_status = struct.unpack(">i", packet[39:43])[0]
    emmc_readthrough_status = struct.unpack(">i", packet[43:47])[0]
    cmd_pending = packet[47]
    cmd_seq = struct.unpack(">I", packet[48:52])[0]
    cmd_cmd = packet[52]
    rsp_ready = packet[53]
    rsp_seq = struct.unpack(">I", packet[54:58])[0]
    rsp_cmd = packet[58]

    print(
        "RX cm4-heartbeat: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}, "
        f"ipc_result={ipc_result}, "
        f"ipc_error={ipc_error}, "
        f"fs_ready={fs_ready}, "
        f"emmc_busy={emmc_busy}, "
        f"emmc_init_status={emmc_init_status}, "
        f"emmc_mount_status={emmc_mount_status}, "
        f"emmc_create_status={emmc_create_status}, "
        f"emmc_readthrough_status={emmc_readthrough_status}, "
        f"cmd_pending={cmd_pending}, "
        f"cmd_seq={cmd_seq}, "
        f"cmd_cmd={cmd_cmd}, "
        f"rsp_ready={rsp_ready}, "
        f"rsp_seq={rsp_seq}, "
        f"rsp_cmd={rsp_cmd}"
    )


def parse_openamp_heartbeat(packet):
    command = packet[0]
    server_id = struct.unpack(">I", packet[1:5])[0]
    epoch_time = struct.unpack(">Q", packet[5:13])[0]
    system_status = packet[13]
    tcp_connected = packet[14]
    reply_value = struct.unpack(">I", packet[15:19])[0]
    fs_status = struct.unpack(">i", packet[19:23])[0]
    service_created = struct.unpack(">I", packet[23:27])[0]
    rx_count = struct.unpack(">I", packet[27:31])[0]
    init_status = struct.unpack(">i", packet[31:35])[0]
    remote_init_status = struct.unpack(">i", packet[35:39])[0]
    remote_mount_status = struct.unpack(">i", packet[39:43])[0]
    shmem_probe_status = struct.unpack(">i", packet[43:47])[0]
    shmem_probe_len = struct.unpack(">I", packet[47:51])[0]
    shmem_probe_bad_index = struct.unpack(">I", packet[51:55])[0]
    stream_open_status = struct.unpack(">i", packet[55:59])[0]
    stream_prefetch_status = struct.unpack(">i", packet[59:63])[0]
    stream_prefetch_len = struct.unpack(">I", packet[63:67])[0]

    print(
        "RX openamp-heartbeat: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}, "
        f"reply=0x{reply_value:08X}, "
        f"fs_status={fs_status}, "
        f"service_created={service_created}, "
        f"rx_count={rx_count}, "
        f"init_status={init_status}, "
        f"remote_init_status={remote_init_status}, "
        f"remote_mount_status={remote_mount_status}, "
        f"shmem_probe_status={shmem_probe_status}, "
        f"shmem_probe_len={shmem_probe_len}, "
        f"shmem_probe_bad_index=0x{shmem_probe_bad_index:08X}, "
        f"stream_open_status={stream_open_status}, "
        f"stream_prefetch_status={stream_prefetch_status}, "
        f"stream_prefetch_len={stream_prefetch_len}"
    )


def parse_config_read(packet):
    global expected_config_file
    global file_read_in_progress

    command = packet[0]
    system_status = packet[1]
    server_id = struct.unpack(">I", packet[2:6])[0]
    epoch_time = struct.unpack(">Q", packet[6:14])[0]
    total_size = struct.unpack(">I", packet[14:18])[0]
    offset = struct.unpack(">I", packet[18:22])[0]
    chunk_len = struct.unpack(">H", packet[22:24])[0]
    chunk = packet[24:24 + chunk_len]
    state_key = (command, server_id)

    if command == 4:
        state = config_rx_state.setdefault(
            state_key,
            {"total_size": total_size, "buffer": bytearray(total_size)}
        )
    else:
        state = config_rx_state.setdefault(
            state_key,
            {"total_size": total_size, "bytes_received": 0}
        )

    if command in {5, 9} and offset == 0 and server_id not in transfer_metrics:
        transfer_metrics[server_id] = time.time()

    if state["total_size"] != total_size:
        state["total_size"] = total_size
        if command == 4:
            state["buffer"] = bytearray(total_size)
        else:
            state["bytes_received"] = 0

    if command == 4:
        if chunk_len > 0 and (offset + chunk_len) <= len(state["buffer"]):
            state["buffer"][offset:offset + chunk_len] = chunk
    else:
        state["bytes_received"] += chunk_len
        if (state["bytes_received"] % (256 * 1024) == 0) or ((offset + chunk_len) >= total_size):
            print(
                f"Read progress: {state['bytes_received']}/{total_size} bytes"
            )

    print(
        "RX config-read: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"total_size={total_size}, "
        f"offset={offset}, "
        f"chunk_len={chunk_len}"
    )

    if (system_status == 0) and (total_size == 0):
        print("Config file is empty.")
        config_rx_state.pop(state_key, None)
        transfer_metrics.pop(server_id, None)
        if command in {5, 9}:
            file_read_in_progress = False
    elif (system_status == 0) and ((offset + chunk_len) >= total_size) and (total_size > 0):
        if command == 4:
            full_data = bytes(state["buffer"])
            print(f"Config read complete: {full_data.hex()}")

            if expected_config_file is not None:
                if full_data == expected_config_file:
                    print("Config verification passed.")
                else:
                    print("Config verification FAILED.")
        else:
            print(f"Named file read complete: received {state['bytes_received']} bytes.")

        if command in {5, 9}:
            start_time = transfer_metrics.pop(server_id, None)
            if start_time is not None:
                elapsed = max(time.time() - start_time, 1e-6)
                throughput_mbps = (total_size / elapsed) / (1024 * 1024)
                print(
                    f"Read throughput: {throughput_mbps:.2f} MiB/s "
                    f"for {total_size} bytes in {elapsed:.3f} s"
                )

        config_rx_state.pop(state_key, None)
        if command in {5, 9}:
            file_read_in_progress = False


def parse_file_stream_header(packet):
    global active_file_stream
    global file_read_in_progress
    global stream_read_offset

    command = packet[0]
    system_status = packet[1]
    server_id = struct.unpack(">I", packet[2:6])[0]
    epoch_time = struct.unpack(">Q", packet[6:14])[0]
    total_size = struct.unpack(">I", packet[14:18])[0]

    if (system_status != 0) or (total_size == 0):
        active_file_stream = None
        file_read_in_progress = False
        if total_size == 0:
            print("Stream is empty or unavailable.")
        return

    print(f"RX stream start: cmd={command}, total_size={total_size}")

    transfer_metrics[server_id] = time.time()
    active_file_stream = {
        "command": command,
        "server_id": server_id,
        "epoch_time": epoch_time,
        "total_size": total_size,
        "bytes_received": 0,
        "file_offset": stream_read_offset if command == 8 else 0,
        "next_progress_mark": 16 * 1024,
        "first_data_reported": False,
    }


def parse_file_stream_data(data):
    global active_file_stream
    global file_read_in_progress

    if active_file_stream is None:
        return

    if active_file_stream["command"] == 8:
        absolute_offset = active_file_stream["file_offset"] + active_file_stream["bytes_received"]
        valid, bad_index, expected, actual = verify_pattern_chunk(absolute_offset, data)
        if not valid:
            print(
                "Stream pattern verification FAILED: "
                f"offset={absolute_offset}, index={bad_index}, "
                f"expected=0x{expected:02X}, actual=0x{actual:02X}"
            )
            active_file_stream = None
            file_read_in_progress = False
            return

    active_file_stream["bytes_received"] += len(data)

    if (len(data) > 0) and (active_file_stream["first_data_reported"] is False):
        print(f"Read data started: received first {len(data)} bytes")
        active_file_stream["first_data_reported"] = True

    while active_file_stream["bytes_received"] >= active_file_stream["next_progress_mark"]:
        print(
            f"Read progress: {active_file_stream['bytes_received']}/{active_file_stream['total_size']} bytes"
        )
        active_file_stream["next_progress_mark"] += 16 * 1024

    if active_file_stream["bytes_received"] >= active_file_stream["total_size"]:
        print(f"Named file read complete: received {active_file_stream['bytes_received']} bytes.")
        if active_file_stream["command"] == 8:
            print("Stream pattern verification passed.")

        start_time = transfer_metrics.pop(active_file_stream["server_id"], None)
        if start_time is not None:
            elapsed = max(time.time() - start_time, 1e-6)
            throughput_mib_s = (active_file_stream["bytes_received"] / elapsed) / (1024 * 1024)
            print(
                f"Average throughput: {throughput_mib_s:.2f} MiB/s "
                f"over {elapsed:.3f} s"
            )

        active_file_stream = None
        file_read_in_progress = False


def parse_file_chunk(packet):
    global read_chunk_offset

    command = packet[0]
    system_status = packet[1]
    server_id = struct.unpack(">I", packet[2:6])[0]
    epoch_time = struct.unpack(">Q", packet[6:14])[0]
    total_size = struct.unpack(">I", packet[14:18])[0]
    offset = struct.unpack(">I", packet[18:22])[0]
    chunk_len = struct.unpack(">H", packet[22:24])[0]
    chunk = packet[24:24 + chunk_len]

    print(
        "RX file-chunk: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"total_size={total_size}, "
        f"offset={offset}, "
        f"chunk_len={chunk_len}, "
        f"first16={chunk[:16].hex()}"
    )

    with chunk_response_condition:
        chunk_responses[offset] = {
            "command": command,
            "server_id": server_id,
            "epoch_time": epoch_time,
            "status": system_status,
            "total_size": total_size,
            "offset": offset,
            "chunk_len": chunk_len,
            "chunk": chunk,
        }
        read_chunk_offset = offset + chunk_len
        chunk_response_condition.notify_all()


def parse_packet(packet):
    command = packet[0]

    if command == 0:
        parse_heartbeat(packet)
    elif command == 1:
        parse_config_write(packet)
    elif command == 2:
        parse_file_count(packet)
    elif command == 3:
        parse_total_file_count(packet)
    elif command == 4:
        parse_config_read(packet)
    elif command == 5:
        parse_file_size(packet)
    elif command == 6:
        parse_cm4_heartbeat(packet)
    elif command == 99:
        parse_openamp_heartbeat(packet)
    else:
        pass


def recv_loop(conn):
    global active_file_stream
    rx_buffer = bytearray()

    try:
        while True:
            data = conn.recv(16384)
            if not data:
                print("Client disconnected.")
                break

            rx_buffer.extend(data)

            while rx_buffer:
                if active_file_stream is not None:
                    remaining = active_file_stream["total_size"] - active_file_stream["bytes_received"]
                    if remaining == 0:
                        active_file_stream = None
                        continue

                    if len(rx_buffer) == 0:
                        break

                    consume_len = min(len(rx_buffer), remaining)
                    payload = bytes(rx_buffer[:consume_len])
                    del rx_buffer[:consume_len]
                    parse_file_stream_data(payload)
                    continue

                command = rx_buffer[0]

                if command in {8, 9}:
                    if len(rx_buffer) < FILE_STREAM_HEADER_LEN:
                        break

                    packet = bytes(rx_buffer[:FILE_STREAM_HEADER_LEN])
                    del rx_buffer[:FILE_STREAM_HEADER_LEN]
                    parse_file_stream_header(packet)
                elif command == 4:
                    if len(rx_buffer) < CONFIG_READ_HEADER_LEN:
                        break

                    chunk_len = struct.unpack(">H", rx_buffer[22:24])[0]
                    frame_len = CONFIG_READ_HEADER_LEN + chunk_len
                    if len(rx_buffer) < frame_len:
                        break

                    packet = bytes(rx_buffer[:frame_len])
                    del rx_buffer[:frame_len]
                    parse_packet(packet)
                elif command == 7:
                    if len(rx_buffer) < CONFIG_READ_HEADER_LEN:
                        break

                    chunk_len = struct.unpack(">H", rx_buffer[22:24])[0]
                    frame_len = CONFIG_READ_HEADER_LEN + chunk_len
                    if len(rx_buffer) < frame_len:
                        break

                    packet = bytes(rx_buffer[:frame_len])
                    del rx_buffer[:frame_len]
                    parse_file_chunk(packet)
                else:
                    if len(rx_buffer) < PACKET_LEN:
                        break

                    packet = bytes(rx_buffer[:PACKET_LEN])
                    del rx_buffer[:PACKET_LEN]
                    parse_packet(packet)
    except OSError as exc:
        print(f"Receive loop stopped: {exc}")


def send_packet(conn, command, epoch_time=None):
    if epoch_time is None:
        epoch_time = int(time.time())
    packet = build_packet(command, SERVER_ID, epoch_time)
    conn.sendall(packet)
    print(f"TX: sent {len(packet)} bytes for command {command}")
    return epoch_time


def wait_for_chunk(offset, timeout_s=5.0):
    deadline = time.monotonic() + timeout_s
    with chunk_response_condition:
        while offset not in chunk_responses:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            chunk_response_condition.wait(remaining)

        return chunk_responses.pop(offset)


def verify_pattern_chunk(offset, chunk):
    for index, value in enumerate(chunk):
        expected = (((offset + index) * 37) + 11) & 0xFF
        if value != expected:
            return False, index, expected, value

    return True, 0, 0, 0


def read_file_repeated_chunks(conn):
    global read_chunk_offset

    offset = 0
    total_size = None
    bytes_received = 0
    start_time = time.time()

    with chunk_response_condition:
        chunk_responses.clear()

    print(f"TX repeated chunk read for: {READ_FILENAME.decode()}")

    while True:
        read_chunk_offset = offset
        print(f"TX chunk request offset={offset}")
        send_packet(conn, 7)

        response = wait_for_chunk(offset)
        if response is None:
            print(f"Timed out waiting for chunk at offset {offset}.")
            return

        if response["status"] != 0:
            print(f"Chunk read failed at offset {offset}: status={response['status']}")
            return

        chunk_len = response["chunk_len"]
        if total_size is None:
            total_size = response["total_size"]
            print(f"Repeated read total_size={total_size}")

        if response["total_size"] != total_size:
            print(
                "Total size changed during read: "
                f"old={total_size}, new={response['total_size']}"
            )
            return

        if chunk_len == 0:
            print(f"Zero-length chunk at offset {offset}; stopping.")
            return

        valid, bad_index, expected, actual = verify_pattern_chunk(offset, response["chunk"])
        if not valid:
            print(
                "Pattern verification FAILED: "
                f"offset={offset}, index={bad_index}, "
                f"expected=0x{expected:02X}, actual=0x{actual:02X}"
            )
            return

        bytes_received += chunk_len
        offset += chunk_len

        if (bytes_received % (64 * 1024) == 0) or (offset >= total_size):
            print(f"Repeated read progress: {bytes_received}/{total_size} bytes")

        if offset >= total_size:
            elapsed = max(time.time() - start_time, 1e-6)
            throughput_mib_s = (bytes_received / elapsed) / (1024 * 1024)
            print(
                "Repeated chunk read complete: "
                f"{bytes_received} bytes, "
                f"{throughput_mib_s:.2f} MiB/s over {elapsed:.3f} s"
            )
            read_chunk_offset = 0
            return


def main():
    global expected_config_file
    global file_read_in_progress
    global read_chunk_offset
    global stream_read_offset

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen(1)

        print(f"Listening on {HOST}:{PORT}")
        print("Waiting for board connection...")
        conn, addr = server.accept()

        with conn:
            print(f"Board connected from {addr[0]}:{addr[1]}")

            rx_thread = threading.Thread(target=recv_loop, args=(conn,), daemon=True)
            rx_thread.start()

            while True:
                try:
                    user_input = input("Enter command (0=heartbeat, 1=write config, 2=dat count, 3=all file count, 4=read config, 5=file size, 6=cm4 heartbeat, 7=read one chunk, 8=stream file, 9=test stream, 99=openamp heartbeat, q=quit): ").strip()
                except (EOFError, KeyboardInterrupt):
                    print("\nExiting.")
                    break

                if user_input.lower() == "q":
                    break

                if user_input not in {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "99"}:
                    print("Only commands 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, and 99 are implemented in this test.")
                    continue

                command = int(user_input)

                if command == 7:
                    offset_text = input(f"Offset bytes [{read_chunk_offset}] or all: ").strip()
                    if offset_text.lower() in {"all", "full"}:
                        read_file_repeated_chunks(conn)
                        continue
                    if offset_text:
                        try:
                            read_chunk_offset = int(offset_text, 0)
                        except ValueError:
                            print("Invalid offset. Use decimal or 0x-prefixed hex.")
                            continue
                elif command == 8:
                    offset_text = input(f"Stream offset bytes [{stream_read_offset}]: ").strip()
                    if offset_text:
                        try:
                            stream_read_offset = int(offset_text, 0)
                        except ValueError:
                            print("Invalid offset. Use decimal or 0x-prefixed hex.")
                            continue

                if (command == 5) and file_read_in_progress:
                    print("File read already in progress. Wait for completion.")
                    continue
                if (command == 9) and file_read_in_progress:
                    print("File stream already in progress. Wait for completion.")
                    continue
                if (command == 8) and file_read_in_progress:
                    print("File stream already in progress. Wait for completion.")
                    continue

                epoch_time = int(time.time())

                if command == 1:
                    expected_config_file = (
                        struct.pack(">I", SERVER_ID) +
                        struct.pack(">Q", epoch_time) +
                        CONFIG_TEST_PAYLOAD
                    )
                    print(f"TX config payload: {CONFIG_TEST_PAYLOAD.hex()}")
                elif command == 5:
                    print(f"TX file size request for: {READ_FILENAME.decode()}")
                elif command == 7:
                    print(f"TX one-chunk read request for: {READ_FILENAME.decode()} offset={read_chunk_offset}")
                elif command == 8:
                    print(f"TX file stream request for: {READ_FILENAME.decode()} offset={stream_read_offset}")
                    file_read_in_progress = True
                elif command == 9:
                    print("TX test stream request")
                    file_read_in_progress = True
                elif command == 99:
                    print("TX OpenAMP heartbeat request")

                send_packet(conn, command, epoch_time)


if __name__ == "__main__":
    main()
