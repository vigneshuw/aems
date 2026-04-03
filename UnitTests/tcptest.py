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
                parse_heartbeat(packet)
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
                    user_input = input("Enter command (0 to send heartbeat request, q to quit): ").strip()
                except (EOFError, KeyboardInterrupt):
                    print("\nExiting.")
                    break

                if user_input.lower() == "q":
                    break

                if user_input != "0":
                    print("Only command 0 is implemented in this test.")
                    continue

                packet = build_packet(0, SERVER_ID, int(time.time()))
                conn.sendall(packet)
                print(f"TX: sent {len(packet)} bytes for command 0")


if __name__ == "__main__":
    main()
