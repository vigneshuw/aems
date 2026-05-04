import socket
import struct
import threading
import time
import csv


HOST = "0.0.0.0"
PORT = 10
PACKET_LEN = 128
CONFIG_READ_HEADER_LEN = 24
FILE_STREAM_HEADER_LEN = 18
SERVER_ID = 1
MAX_USEFUL_PAYLOAD_LEN = PACKET_LEN - 15
CONFIG_TEST_PAYLOAD = bytes(((index * 3) + 1) & 0xFF for index in range(MAX_USEFUL_PAYLOAD_LEN))
READ_FILENAME = b"test.dat"
DAQ_FILENAME = b"daq.bin"
DAQ_SAMPLE_RATE_HZ = 2000
DAQ_CHANNEL_MASK = 0x3F
DAQ_BLOCK_SAMPLES = 128
DAQ_STREAM_SAMPLES = 0
DAQ_STREAM_CSV = "stream_data.csv"
DAQ_DISCARD_STREAM_DATA = False
DAQ_FRAME_LEN = 36
DAQ_STREAM_CHANNELS = 6
INT24_MAX = 0x7FFFFF
V_REF_VGAIN = 1.0
V_DIVIDER = 1.0
MAINS_VOLTAGE = 1.0
config_rx_state = {}
file_list_rx_state = {}
expected_config_file = None
transfer_metrics = {}
file_read_in_progress = False
active_file_stream = None
daq_stream_stop_requested = False
daq_stream_control_pending = set()
daq_stream_remainder = bytearray()
daq_stream_sample_index = 0
daq_csv_file = None
daq_csv_writer = None
daq_log_start_time = None
daq_log_stop_time = None
stream_read_offset = 0


def build_packet(command, server_id, epoch_time):
    payload = bytearray(PACKET_LEN)
    useful_payload = b""

    payload[0] = command & 0xFF
    payload[1:5] = struct.pack(">I", server_id)
    payload[5:13] = struct.pack(">Q", epoch_time)

    if command == 1:
        useful_payload = CONFIG_TEST_PAYLOAD
    elif command == 4:
        useful_payload = b""
    elif command == 5:
        useful_payload = READ_FILENAME
    elif command == 7:
        useful_payload = READ_FILENAME
    elif command == 8:
        useful_payload = struct.pack(">I", stream_read_offset) + READ_FILENAME
    elif command in {11, 13}:
        useful_payload = (
            struct.pack(">I", DAQ_SAMPLE_RATE_HZ) +
            struct.pack(">I", DAQ_CHANNEL_MASK) +
            struct.pack(">I", DAQ_BLOCK_SAMPLES) +
            struct.pack(">I", DAQ_STREAM_SAMPLES) +
            DAQ_FILENAME
        )
    elif command in {9, 10, 12, 110, 112}:
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


def parse_delete_ack(packet):
    command = packet[0]
    server_id = struct.unpack(">I", packet[1:5])[0]
    epoch_time = struct.unpack(">Q", packet[5:13])[0]
    system_status = packet[13]
    tcp_connected = packet[14]
    op_status = struct.unpack(">i", packet[15:19])[0]
    deleted_count = struct.unpack(">I", packet[19:23])[0]

    print(
        "RX delete-ack: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}, "
        f"op_status={op_status}, "
        f"deleted={deleted_count}"
    )

    if command == 6:
        if op_status == 0:
            print(f"Delete-all complete: deleted {deleted_count} file(s).")
        else:
            print("Delete-all failed.")
    elif command == 7:
        if op_status != 0:
            print(f"Delete-file failed for: {READ_FILENAME.decode(errors='replace')}")
        elif deleted_count == 0:
            print(f"Delete-file no-op: {READ_FILENAME.decode(errors='replace')} not present.")
        else:
            print(f"Delete-file success: {READ_FILENAME.decode(errors='replace')}")


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
    adc_device_id = struct.unpack(">I", packet[67:71])[0]
    emmc_mount_stage = struct.unpack(">I", packet[71:75])[0]
    emmc_mount_fresult = struct.unpack(">I", packet[75:79])[0]
    emmc_mkfs_fresult = struct.unpack(">I", packet[79:83])[0]
    emmc_post_mount_fresult = struct.unpack(">I", packet[83:87])[0]
    board_ip = struct.unpack(">I", packet[87:91])[0]
    server_ip = struct.unpack(">I", packet[91:95])[0]
    tcp_local_port = struct.unpack(">I", packet[95:99])[0]
    tcp_server_port = struct.unpack(">I", packet[99:103])[0]
    tcp_connect_attempt = struct.unpack(">I", packet[103:107])[0]
    tcp_last_connect_status = struct.unpack(">i", packet[107:111])[0]
    tcp_last_socket_error = struct.unpack(">i", packet[111:115])[0]
    board_mac = packet[115:121]

    def ipv4(value):
        return ".".join(str((value >> shift) & 0xFF) for shift in (24, 16, 8, 0))

    def mac(value):
        return ":".join(f"{byte:02X}" for byte in value[:6])

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
        f"stream_prefetch_len={stream_prefetch_len}, "
        f"adc_device_id=0x{adc_device_id:04X}, "
        f"emmc_mount_stage={emmc_mount_stage}, "
        f"emmc_mount_fresult={emmc_mount_fresult}, "
        f"emmc_mkfs_fresult={emmc_mkfs_fresult}, "
        f"emmc_post_mount_fresult={emmc_post_mount_fresult}, "
        f"board_ip={ipv4(board_ip)}, "
        f"board_mac={mac(board_mac)}, "
        f"server_ip={ipv4(server_ip)}, "
        f"tcp_local_port={tcp_local_port}, "
        f"tcp_server_port={tcp_server_port}, "
        f"tcp_connect_attempt={tcp_connect_attempt}, "
        f"tcp_last_connect_status={tcp_last_connect_status}, "
        f"tcp_last_socket_error={tcp_last_socket_error}"
    )


def parse_daq_status(packet):
    global daq_log_start_time
    global daq_log_stop_time

    command = packet[0]
    server_id = struct.unpack(">I", packet[1:5])[0]
    epoch_time = struct.unpack(">Q", packet[5:13])[0]
    system_status = packet[13]
    tcp_connected = packet[14]
    op_status = struct.unpack(">i", packet[15:19])[0]
    state = struct.unpack(">I", packet[19:23])[0]
    mode = struct.unpack(">I", packet[23:27])[0]
    last_error = struct.unpack(">I", packet[27:31])[0]
    samples_captured = struct.unpack(">I", packet[31:35])[0]
    dropped_buffers = struct.unpack(">I", packet[35:39])[0]
    bytes_written = (
        struct.unpack(">I", packet[43:47])[0] << 32
    ) | struct.unpack(">I", packet[39:43])[0]
    adc_ready_pending = struct.unpack(">I", packet[47:51])[0]

    print(
        "RX daq-status: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}, "
        f"op_status={op_status}, "
        f"state={state}, "
        f"mode={mode}, "
        f"last_error={last_error}, "
        f"samples={samples_captured}, "
        f"dropped={dropped_buffers}, "
        f"bytes_written={bytes_written}, "
        f"adc_pending={adc_ready_pending}"
    )

    if command == 110 and daq_log_start_time is not None:
        end_time = daq_log_stop_time if daq_log_stop_time is not None else time.time()
        elapsed = max(end_time - daq_log_start_time, 0.001)
        mib_s = (bytes_written / (1024.0 * 1024.0)) / elapsed
        frames_s = samples_captured / elapsed
        print(
            "DAQ eMMC metrics: "
            f"elapsed={elapsed:.3f}s, "
            f"frames/s={frames_s:.1f}, "
            f"MiB/s={mib_s:.3f}, "
            f"dropped_blocks={dropped_buffers}"
        )


def parse_daq_ack(packet):
    global daq_log_start_time
    global daq_log_stop_time

    command = packet[0]
    server_id = struct.unpack(">I", packet[1:5])[0]
    epoch_time = struct.unpack(">Q", packet[5:13])[0]
    system_status = packet[13]
    tcp_connected = packet[14]
    op_status = struct.unpack(">i", packet[15:19])[0]
    sample_rate_hz = struct.unpack(">I", packet[19:23])[0]
    channel_mask = struct.unpack(">I", packet[23:27])[0]
    block_samples = struct.unpack(">I", packet[27:31])[0]

    print(
        "RX daq-ack: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}, "
        f"op_status={op_status}, "
        f"sample_rate={sample_rate_hz}, "
        f"channel_mask=0x{channel_mask:08X}, "
        f"block_samples={block_samples}"
    )

    if command == 11 and op_status == 0:
        daq_log_start_time = time.time()
        daq_log_stop_time = None
        print(f"DAQ eMMC logging started: {DAQ_FILENAME.decode(errors='replace')}")
    elif command == 112 and op_status == 0:
        daq_log_stop_time = time.time()
        print("DAQ eMMC logging stopped and file closed. Send 110 for metrics.")

def parse_calibration(packet):
    command = packet[0]
    server_id = struct.unpack(">I", packet[1:5])[0]
    epoch_time = struct.unpack(">Q", packet[5:13])[0]
    system_status = packet[13]
    tcp_connected = packet[14]
    op_status = struct.unpack(">i", packet[15:19])[0]
    offsets = [struct.unpack(">i", packet[offset:offset + 4])[0] for offset in range(19, 43, 4)]
    samples_averaged = struct.unpack(">I", packet[43:47])[0]
    storage_status = struct.unpack(">i", packet[47:51])[0]

    print(
        "RX offset-calibration: "
        f"cmd={command}, "
        f"id={server_id}, "
        f"time={epoch_time}, "
        f"status={system_status}, "
        f"tcp={tcp_connected}, "
        f"op_status={op_status}, "
        f"offsets={offsets}, "
        f"samples_averaged={samples_averaged}, "
        f"storage_status={storage_status}"
    )


def parse_adc_value_voltage(adc_value):
    if adc_value > INT24_MAX:
        adc_value = INT24_MAX
    elif adc_value < -INT24_MAX:
        adc_value = -INT24_MAX

    v_adc = (float(adc_value) / float(INT24_MAX)) * V_REF_VGAIN
    return (v_adc / V_DIVIDER) * (MAINS_VOLTAGE * 1.414)


def open_daq_csv():
    global daq_csv_file
    global daq_csv_writer
    global daq_stream_remainder
    global daq_stream_sample_index

    close_daq_csv()
    daq_stream_remainder = bytearray()
    daq_stream_sample_index = 0
    daq_csv_file = open(DAQ_STREAM_CSV, "w", newline="")
    daq_csv_writer = csv.writer(daq_csv_file)
    daq_csv_writer.writerow(["sample_index", "ch0", "ch1", "ch2", "ch3", "ch4", "ch5"])


def close_daq_csv():
    global daq_csv_file
    global daq_csv_writer
    global daq_stream_remainder
    global daq_stream_sample_index

    if daq_csv_file is not None:
        daq_csv_file.flush()
        daq_csv_file.close()

    daq_csv_file = None
    daq_csv_writer = None
    daq_stream_remainder = bytearray()
    daq_stream_sample_index = 0


def write_daq_stream_csv(data):
    global daq_stream_remainder
    global daq_stream_sample_index

    if daq_csv_writer is None:
        return

    daq_stream_remainder.extend(data)
    while len(daq_stream_remainder) >= DAQ_FRAME_LEN:
        frame = bytes(daq_stream_remainder[:DAQ_FRAME_LEN])
        del daq_stream_remainder[:DAQ_FRAME_LEN]

        _response, _crc, *channels = struct.unpack("<HH8i", frame)
        row = [daq_stream_sample_index]
        row.extend(parse_adc_value_voltage(value) for value in channels[:DAQ_STREAM_CHANNELS])
        daq_csv_writer.writerow(row)
        daq_stream_sample_index += 1

    if daq_csv_file is not None:
        daq_csv_file.flush()


def parse_file_list_packet(packet):
    global file_list_rx_state

    command = packet[0]
    system_status = packet[1]
    server_id = struct.unpack(">I", packet[2:6])[0]
    epoch_time = struct.unpack(">Q", packet[6:14])[0]
    total_size = struct.unpack(">I", packet[14:18])[0]
    offset = struct.unpack(">I", packet[18:22])[0]
    chunk_len = struct.unpack(">H", packet[22:24])[0]
    chunk = packet[24:24 + chunk_len]
    state_key = (command, server_id)

    if (system_status != 0) or (total_size == 0):
        print(
            "RX file-list: "
            f"cmd={command}, id={server_id}, time={epoch_time}, status={system_status}, total_size={total_size}"
        )
        print("File list unavailable or empty.")
        file_list_rx_state.pop(state_key, None)
        return

    state = file_list_rx_state.setdefault(
        state_key,
        {"total_size": total_size, "buffer": bytearray(total_size)}
    )

    if state["total_size"] != total_size:
        state["total_size"] = total_size
        state["buffer"] = bytearray(total_size)

    if chunk_len > 0 and (offset + chunk_len) <= len(state["buffer"]):
        state["buffer"][offset:offset + chunk_len] = chunk

    print(
        "RX file-list: "
        f"cmd={command}, id={server_id}, time={epoch_time}, status={system_status}, total_size={total_size}, offset={offset}, chunk_len={chunk_len}"
    )

    if (offset + chunk_len) >= total_size:
        full_data = bytes(state["buffer"])
        decoded = full_data.decode("utf-8", errors="replace")
        entries = [entry for entry in decoded.splitlines() if entry]
        print(f"File list complete: {len(entries)} file(s)")
        for item in entries:
            if "	" in item:
                name, size_text = item.split("	", 1)
                print(f" - {name} ({size_text.strip()} bytes)")
            else:
                print(f" - {item}")
        file_list_rx_state.pop(state_key, None)


def parse_file_stream_header(packet):
    global active_file_stream
    global file_read_in_progress
    global stream_read_offset
    global daq_stream_stop_requested

    command = packet[0]
    system_status = packet[1]
    server_id = struct.unpack(">I", packet[2:6])[0]
    epoch_time = struct.unpack(">Q", packet[6:14])[0]
    total_size = struct.unpack(">I", packet[14:18])[0]

    if (system_status != 0) or (total_size == 0):
        active_file_stream = None
        file_read_in_progress = False
        if command == 13:
            close_daq_csv()
        if total_size == 0:
            print("Stream is empty or unavailable.")
        return

    print(f"RX stream start: cmd={command}, total_size={total_size}")
    if command == 13:
        daq_stream_stop_requested = False
        if DAQ_DISCARD_STREAM_DATA:
            close_daq_csv()
            print(
                "DAQ discard capture started: "
                f"sample_rate={DAQ_SAMPLE_RATE_HZ}Hz, channel_mask=0x{DAQ_CHANNEL_MASK:02X}"
            )
        else:
            open_daq_csv()
            print(
                f"DAQ CSV capture started: {DAQ_STREAM_CSV}, "
                f"sample_rate={DAQ_SAMPLE_RATE_HZ}Hz, channel_mask=0x{DAQ_CHANNEL_MASK:02X}"
            )

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
        "data_start_time": None,
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
    elif active_file_stream["command"] == 13:
        if not DAQ_DISCARD_STREAM_DATA:
            write_daq_stream_csv(data)

    active_file_stream["bytes_received"] += len(data)

    if (len(data) > 0) and (active_file_stream["first_data_reported"] is False):
        print(f"Read data started: received first {len(data)} bytes")
        active_file_stream["first_data_reported"] = True
        active_file_stream["data_start_time"] = time.time()

    while active_file_stream["bytes_received"] >= active_file_stream["next_progress_mark"]:
        if active_file_stream["command"] == 13:
            frames = active_file_stream["bytes_received"] // DAQ_FRAME_LEN
            print(f"DAQ stream progress: {frames} frames")
        else:
            print(
                f"Read progress: {active_file_stream['bytes_received']}/{active_file_stream['total_size']} bytes"
            )
        active_file_stream["next_progress_mark"] += 16 * 1024

    if active_file_stream["bytes_received"] >= active_file_stream["total_size"]:
        print(f"Named file read complete: received {active_file_stream['bytes_received']} bytes.")
        if active_file_stream["command"] == 8:
            print("Stream pattern verification passed.")
        elif active_file_stream["command"] == 13:
            print(f"DAQ stream complete: received {active_file_stream['bytes_received'] // 36} frames.")
            if not DAQ_DISCARD_STREAM_DATA:
                close_daq_csv()
                print(f"DAQ CSV saved: {DAQ_STREAM_CSV}")

        start_time = transfer_metrics.pop(active_file_stream["server_id"], None)
        if start_time is not None:
            elapsed = max(time.time() - start_time, 1e-6)
            if active_file_stream["command"] == 13:
                frames = active_file_stream["bytes_received"] // DAQ_FRAME_LEN
                print(f"DAQ average rate: {frames / elapsed:.1f} frames/s over {elapsed:.3f} s")
            else:
                throughput_mib_s = (active_file_stream["bytes_received"] / elapsed) / (1024 * 1024)
                print(
                    f"Average throughput: {throughput_mib_s:.2f} MiB/s "
                    f"over {elapsed:.3f} s"
                )

        active_file_stream = None
        file_read_in_progress = False


def finish_daq_stream_from_stop_marker():
    global active_file_stream
    global file_read_in_progress
    global daq_stream_stop_requested

    if active_file_stream is None:
        return

    frames = active_file_stream["bytes_received"] // DAQ_FRAME_LEN
    start_time = transfer_metrics.pop(active_file_stream["server_id"], None)
    if start_time is not None:
        total_elapsed = max(time.time() - start_time, 1e-6)
        data_start_time = active_file_stream.get("data_start_time")
        data_elapsed = max(time.time() - data_start_time, 1e-6) if data_start_time is not None else total_elapsed
        print(
            f"DAQ stream stopped: received {frames} frames, "
            f"avg={frames / total_elapsed:.1f} frames/s over {total_elapsed:.3f} s, "
            f"data_rate={frames / data_elapsed:.1f} frames/s over {data_elapsed:.3f} s"
        )
    else:
        print(f"DAQ stream stopped: received {frames} frames.")

    if not DAQ_DISCARD_STREAM_DATA:
        close_daq_csv()
        print(f"DAQ CSV saved: {DAQ_STREAM_CSV}")
    active_file_stream = None
    file_read_in_progress = False
    daq_stream_stop_requested = False


def find_daq_stream_control_marker(buffer, expected_commands):
    now = int(time.time())

    for index in range(0, max(0, len(buffer) - PACKET_LEN + 1)):
        command = buffer[index]
        if command not in expected_commands:
            continue

        server_id = struct.unpack(">I", buffer[index + 1:index + 5])[0]
        if server_id != SERVER_ID:
            continue

        epoch_time = struct.unpack(">Q", buffer[index + 5:index + 13])[0]
        if epoch_time > (now + 3600):
            continue

        return index

    return -1


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
    elif command == 5:
        parse_file_size(packet)
    elif command in {6, 7}:
        parse_delete_ack(packet)
    elif command in {10, 110}:
        parse_daq_status(packet)
    elif command in {11, 12, 112}:
        parse_daq_ack(packet)
    elif command in {97, 98}:
        parse_calibration(packet)
    elif command == 99:
        parse_openamp_heartbeat(packet)
    else:
        pass


def recv_loop(conn):
    global active_file_stream
    global daq_stream_control_pending
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
                    if active_file_stream["command"] == 13:
                        expected_commands = set(daq_stream_control_pending)
                        if daq_stream_stop_requested:
                            expected_commands.add(12)
                        marker_index = (
                            find_daq_stream_control_marker(rx_buffer, expected_commands)
                            if (expected_commands and len(rx_buffer) >= PACKET_LEN)
                            else -1
                        )
                        if marker_index >= 0:
                            if marker_index > 0:
                                payload = bytes(rx_buffer[:marker_index])
                                del rx_buffer[:marker_index]
                                parse_file_stream_data(payload)

                            command = rx_buffer[0]
                            if command == 12:
                                finish_daq_stream_from_stop_marker()
                            packet = bytes(rx_buffer[:PACKET_LEN])
                            del rx_buffer[:PACKET_LEN]
                            parse_packet(packet)
                            daq_stream_control_pending.discard(command)
                            continue
                        if expected_commands and len(rx_buffer) < PACKET_LEN:
                            break

                        if expected_commands:
                            consume_len = len(rx_buffer) - (PACKET_LEN - 1)
                            payload = bytes(rx_buffer[:consume_len])
                            del rx_buffer[:consume_len]
                            parse_file_stream_data(payload)
                            continue

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

                if command in {8, 9, 13}:
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
                    parse_file_list_packet(packet)
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


def verify_pattern_chunk(offset, chunk):
    for index, value in enumerate(chunk):
        expected = (((offset + index) * 37) + 11) & 0xFF
        if value != expected:
            return False, index, expected, value

    return True, 0, 0, 0


def main():
    global expected_config_file
    global file_read_in_progress
    global stream_read_offset
    global daq_stream_stop_requested
    global daq_stream_control_pending
    global READ_FILENAME
    global DAQ_FILENAME

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
                    user_input = input("Enter command (0=heartbeat, 1=write config, 2=dat count, 3=all file count, 4=file list, 5=file size [asks filename], 6=delete .bin/.dat, 7=delete file [asks filename], 8=stream file, 9=test stream, 10=daq status, 11=daq log [asks filename], 12=daq stop, 13=daq stream [asks filename], 97=get offsets, 98=run offset cal, 110=daq log status, 112=daq log stop/close, 99=openamp heartbeat, q=quit): ").strip()
                except (EOFError, KeyboardInterrupt):
                    print("\nExiting.")
                    break

                if user_input.lower() == "q":
                    close_daq_csv()
                    break

                if user_input not in {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "97", "98", "110", "112", "99"}:
                    print("Only commands 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 97, 98, 110, 112, and 99 are implemented in this test.")
                    continue

                command = int(user_input)

                if command == 5:
                    filename_text = input(f"File size filename [{READ_FILENAME.decode(errors='replace')}]: ").strip()
                    if filename_text:
                        READ_FILENAME = filename_text.encode("ascii", errors="ignore")
                elif command == 7:
                    filename_text = input(f"Delete filename [{READ_FILENAME.decode(errors='replace')}]: ").strip()
                    if filename_text:
                        READ_FILENAME = filename_text.encode("ascii", errors="ignore")
                elif command == 11:
                    filename_text = input(f"DAQ log filename [{DAQ_FILENAME.decode(errors='replace')}]: ").strip()
                    if filename_text:
                        DAQ_FILENAME = filename_text.encode("ascii", errors="ignore")
                elif command == 13:
                    filename_text = input(f"DAQ stream filename [{DAQ_FILENAME.decode(errors='replace')}]: ").strip()
                    if filename_text:
                        DAQ_FILENAME = filename_text.encode("ascii", errors="ignore")

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
                if (command == 13) and file_read_in_progress:
                    print("Stream already in progress. Wait for completion.")
                    continue

                epoch_time = int(time.time())

                if command == 1:
                    expected_config_file = (
                        struct.pack(">I", SERVER_ID) +
                        struct.pack(">Q", epoch_time) +
                        CONFIG_TEST_PAYLOAD
                    )
                    print(f"TX config payload: {CONFIG_TEST_PAYLOAD.hex()}")
                elif command == 4:
                    print("TX file list request")
                elif command == 5:
                    print(f"TX file size request for: {READ_FILENAME.decode()}")
                elif command == 6:
                    print("TX delete .bin/.dat files request")
                elif command == 7:
                    print(f"TX delete file request for: {READ_FILENAME.decode()}")
                elif command == 8:
                    print(f"TX file stream request for: {READ_FILENAME.decode()} offset={stream_read_offset}")
                    file_read_in_progress = True
                elif command == 9:
                    print("TX test stream request")
                    file_read_in_progress = True
                elif command == 7:
                    filename_text = input(f"Delete filename [{READ_FILENAME.decode(errors='replace')}]: ").strip()
                    if filename_text:
                        READ_FILENAME = filename_text.encode("ascii", errors="ignore")
                elif command == 11:
                    print(f"TX DAQ log request for: {DAQ_FILENAME.decode()}")
                elif command == 12:
                    print("TX DAQ stop request")
                    if active_file_stream is not None and active_file_stream["command"] == 13:
                        daq_stream_stop_requested = True
                        daq_stream_control_pending.add(12)
                elif command == 13:
                    print(
                        "TX DAQ stream request: "
                        f"sample_rate={DAQ_SAMPLE_RATE_HZ}Hz, "
                        f"channel_mask=0x{DAQ_CHANNEL_MASK:02X}, "
                        "stop with command 12"
                    )
                    file_read_in_progress = True
                elif command == 97:
                    print("TX offset calibration read request")
                elif command == 98:
                    print("TX offset calibration run request; this takes about 15 seconds")
                elif command == 110:
                    print("TX DAQ eMMC log status request")
                elif command == 112:
                    print("TX DAQ eMMC stop/close request")
                elif command == 99:
                    print("TX OpenAMP heartbeat request")

                if command == 10 and active_file_stream is not None and active_file_stream["command"] == 13:
                    daq_stream_control_pending.add(10)

                send_packet(conn, command, epoch_time)


if __name__ == "__main__":
    main()
