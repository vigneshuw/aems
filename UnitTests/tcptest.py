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
    elif command == 9:
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
        f"emmc_create_status={emmc_create_status}"
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
        "next_progress_mark": 16 * 1024,
        "first_data_reported": False,
    }


def parse_file_stream_data(data):
    global active_file_stream
    global file_read_in_progress

    if active_file_stream is None:
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
        transfer_metrics.pop(active_file_stream["server_id"], None)

        active_file_stream = None
        file_read_in_progress = False


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
    elif command == 6:
        parse_cm4_heartbeat(packet)
    else:
        pass


def recv_loop(conn):
    global active_file_stream
    rx_buffer = bytearray()

    try:
        while True:
            data = conn.recv(1024)
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

                if command in {5, 9}:
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
                else:
                    if len(rx_buffer) < PACKET_LEN:
                        break

                    packet = bytes(rx_buffer[:PACKET_LEN])
                    del rx_buffer[:PACKET_LEN]
                    parse_packet(packet)
    except OSError as exc:
        print(f"Receive loop stopped: {exc}")


def main():
    global expected_config_file
    global file_read_in_progress

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
                    user_input = input("Enter command (0=heartbeat, 1=write config, 2=dat count, 3=all file count, 4=read config, 5=read named file, 6=cm4 heartbeat, 9=test stream, q=quit): ").strip()
                except (EOFError, KeyboardInterrupt):
                    print("\nExiting.")
                    break

                if user_input.lower() == "q":
                    break

                if user_input not in {"0", "1", "2", "3", "4", "5", "6", "9"}:
                    print("Only commands 0, 1, 2, 3, 4, 5, 6, and 9 are implemented in this test.")
                    continue

                command = int(user_input)

                if (command == 5) and file_read_in_progress:
                    print("File read already in progress. Wait for completion.")
                    continue
                if (command == 9) and file_read_in_progress:
                    print("File stream already in progress. Wait for completion.")
                    continue

                epoch_time = int(time.time())
                packet = build_packet(command, SERVER_ID, epoch_time)

                if command == 1:
                    expected_config_file = (
                        struct.pack(">I", SERVER_ID) +
                        struct.pack(">Q", epoch_time) +
                        CONFIG_TEST_PAYLOAD
                    )
                    print(f"TX config payload: {CONFIG_TEST_PAYLOAD.hex()}")
                elif command in {5, 9}:
                    print(f"TX file read request for: {READ_FILENAME.decode()}")
                    if command == 9:
                        print("TX test stream request")
                    file_read_in_progress = True

                conn.sendall(packet)
                print(f"TX: sent {len(packet)} bytes for command {command}")


if __name__ == "__main__":
    main()
