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


def recv_loop(conn):
    try:
        while True:
            data = conn.recv(1024)
            if not data:
                print("Client disconnected.")
                break
            print(f"RX: {data.decode('utf-8', errors='replace').rstrip()}")
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
