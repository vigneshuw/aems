import socket
import struct
import threading
import time


HOST = "0.0.0.0"
PORT = 10
PACKET_LEN = 100
CONFIG_READ_HEADER_LEN = 24
SERVER_ID = 1
MAX_USEFUL_PAYLOAD_LEN = PACKET_LEN - 15
CONFIG_TEST_PAYLOAD = bytes(((index * 3) + 1) & 0xFF for index in range(MAX_USEFUL_PAYLOAD_LEN))
READ_FILENAME = b"config_main.conf"
config_rx_state = {}
expected_config_file = None


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

    print(
        "RX config-write: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}"
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

    print(
        "RX file-count: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}, "
        f"dat_count={dat_file_count}"
    )


def parse_total_file_count(packet):
    command = packet[0]
    server_id = struct.unpack(">I", packet[1:5])[0]
    epoch_time = struct.unpack(">Q", packet[5:13])[0]
    system_status = packet[13]
    tcp_connected = packet[14]
    total_file_count = struct.unpack(">I", packet[15:19])[0]

    print(
        "RX total-file-count: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}, "
        f"total_count={total_file_count}"
    )


def parse_config_read(packet):
    global expected_config_file

    command = packet[0]
    system_status = packet[1]
    server_id = struct.unpack(">I", packet[2:6])[0]
    epoch_time = struct.unpack(">Q", packet[6:14])[0]
    total_size = struct.unpack(">I", packet[14:18])[0]
    offset = struct.unpack(">I", packet[18:22])[0]
    chunk_len = struct.unpack(">H", packet[22:24])[0]
    chunk = packet[24:24 + chunk_len]

    state = config_rx_state.setdefault(
        server_id,
        {"total_size": total_size, "buffer": bytearray(total_size)}
    )

    if state["total_size"] != total_size:
        state["total_size"] = total_size
        state["buffer"] = bytearray(total_size)

    if chunk_len > 0 and (offset + chunk_len) <= len(state["buffer"]):
        state["buffer"][offset:offset + chunk_len] = chunk

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
        config_rx_state.pop(server_id, None)
    elif (system_status == 0) and ((offset + chunk_len) >= total_size) and (total_size > 0):
        full_data = bytes(state["buffer"])
        print(f"Config read complete: {full_data.hex()}")

        if expected_config_file is not None:
            if full_data == expected_config_file:
                print("Config verification passed.")
            else:
                print("Config verification FAILED.")

        config_rx_state.pop(server_id, None)


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
        parse_config_read(packet)
    else:
        print(f"RX unknown packet: cmd={command}, raw={packet.hex()}")


def recv_loop(conn):
    rx_buffer = bytearray()

    try:
        while True:
            data = conn.recv(1024)
            if not data:
                print("Client disconnected.")
                break

            rx_buffer.extend(data)

            while rx_buffer:
                command = rx_buffer[0]

                if command in {4, 5}:
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
                    user_input = input("Enter command (0=heartbeat, 1=write config, 2=dat count, 3=all file count, 4=read config, 5=read named file, q=quit): ").strip()
                except (EOFError, KeyboardInterrupt):
                    print("\nExiting.")
                    break

                if user_input.lower() == "q":
                    break

                if user_input not in {"0", "1", "2", "3", "4", "5"}:
                    print("Only commands 0, 1, 2, 3, 4, and 5 are implemented in this test.")
                    continue

                command = int(user_input)
                epoch_time = int(time.time())
                packet = build_packet(command, SERVER_ID, epoch_time)

                if command == 1:
                    expected_config_file = (
                        struct.pack(">I", SERVER_ID) +
                        struct.pack(">Q", epoch_time) +
                        CONFIG_TEST_PAYLOAD
                    )
                    print(f"TX config payload: {CONFIG_TEST_PAYLOAD.hex()}")
                elif command == 5:
                    print(f"TX file read request for: {READ_FILENAME.decode()}")

                conn.sendall(packet)
                print(f"TX: sent {len(packet)} bytes for command {command}")


if __name__ == "__main__":
    main()
