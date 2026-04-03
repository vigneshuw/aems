import socket
import struct
import threading
import time


HOST = "0.0.0.0"
PORT = 10
PACKET_LEN = 100
SERVER_ID = 1


def build_packet(command, server_id, epoch_time):
    payload = bytearray(PACKET_LEN)
    payload[0] = command & 0xFF
    payload[1:5] = struct.pack(">I", server_id)
    payload[5:13] = struct.pack(">Q", epoch_time)
    return bytes(payload)


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


def parse_packet(packet):
    command = packet[0]

    if command == 0:
        parse_heartbeat(packet)
    elif command == 2:
        parse_file_count(packet)
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

            while len(rx_buffer) >= PACKET_LEN:
                packet = bytes(rx_buffer[:PACKET_LEN])
                del rx_buffer[:PACKET_LEN]
                parse_packet(packet)
    except OSError as exc:
        print(f"Receive loop stopped: {exc}")


def main():
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
                    user_input = input("Enter command (0=heartbeat, 2=file count, q=quit): ").strip()
                except (EOFError, KeyboardInterrupt):
                    print("\nExiting.")
                    break

                if user_input.lower() == "q":
                    break

                if user_input not in {"0", "2"}:
                    print("Only commands 0 and 2 are implemented in this test.")
                    continue

                command = int(user_input)
                packet = build_packet(command, SERVER_ID, int(time.time()))
                conn.sendall(packet)
                print(f"TX: sent {len(packet)} bytes for command {command}")


if __name__ == "__main__":
    main()
