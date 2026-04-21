from __future__ import annotations

import struct
import time

from .models import (
    Cm4HeartbeatResponse,
    CommandConfig,
    ConfigWriteResponse,
    DaqAckResponse,
    DaqStatusResponse,
    FileChunkResponse,
    FileCountResponse,
    FileSizeResponse,
    OpenAmpHeartbeatResponse,
    PacketBase,
    TotalFileCountResponse,
)

PACKET_LEN = 100
CONFIG_READ_HEADER_LEN = 24
FILE_STREAM_HEADER_LEN = 18
SERVER_ID = 1
MAX_USEFUL_PAYLOAD_LEN = PACKET_LEN - 15
CONFIG_TEST_PAYLOAD = bytes(((index * 3) + 1) & 0xFF for index in range(MAX_USEFUL_PAYLOAD_LEN))
DAQ_FRAME_LEN = 36
DAQ_STREAM_CHANNELS = 6
INT24_MAX = 0x7FFFFF
V_REF_VGAIN = 1.0
V_DIVIDER = 1.0
MAINS_VOLTAGE = 1.0


def build_packet(command: int, server_id: int = SERVER_ID, epoch_time: int | None = None, config: CommandConfig | None = None) -> bytes:
    if epoch_time is None:
        epoch_time = int(time.time())
    if config is None:
        config = CommandConfig()

    payload = bytearray(PACKET_LEN)
    payload[0] = command & 0xFF
    payload[1:5] = struct.pack(">I", server_id)
    payload[5:13] = struct.pack(">Q", epoch_time)

    read_filename = config.read_filename.encode("ascii", errors="ignore")
    daq_filename = config.daq_filename.encode("ascii", errors="ignore")
    useful_payload = b""

    if command == 1:
        useful_payload = CONFIG_TEST_PAYLOAD
    elif command == 5:
        useful_payload = read_filename
    elif command == 7:
        useful_payload = struct.pack(">I", config.chunk_offset) + read_filename
    elif command == 8:
        useful_payload = struct.pack(">I", config.stream_offset) + read_filename
    elif command in {11, 13}:
        useful_payload = (
            struct.pack(">I", config.daq_sample_rate_hz)
            + struct.pack(">I", config.daq_channel_mask)
            + struct.pack(">I", config.daq_block_samples)
            + struct.pack(">I", config.daq_stream_samples)
            + daq_filename
        )

    useful_len = min(len(useful_payload), MAX_USEFUL_PAYLOAD_LEN)
    payload[13:15] = struct.pack(">H", useful_len)
    payload[15:15 + useful_len] = useful_payload[:useful_len]
    return bytes(payload)


def parse_fixed_packet(packet: bytes) -> PacketBase:
    command = packet[0]

    if command == 0:
        return PacketBase(command, struct.unpack(">I", packet[1:5])[0], struct.unpack(">Q", packet[5:13])[0], packet[13], packet[14])
    if command == 1:
        return ConfigWriteResponse(command, struct.unpack(">I", packet[1:5])[0], struct.unpack(">Q", packet[5:13])[0], packet[13], packet[14], struct.unpack(">I", packet[15:19])[0])
    if command == 2:
        return FileCountResponse(command, struct.unpack(">I", packet[1:5])[0], struct.unpack(">Q", packet[5:13])[0], packet[13], packet[14], struct.unpack(">I", packet[15:19])[0], struct.unpack(">I", packet[19:23])[0])
    if command == 3:
        return TotalFileCountResponse(command, struct.unpack(">I", packet[1:5])[0], struct.unpack(">Q", packet[5:13])[0], packet[13], packet[14], struct.unpack(">I", packet[15:19])[0], struct.unpack(">I", packet[19:23])[0])
    if command == 5:
        return FileSizeResponse(command, struct.unpack(">I", packet[1:5])[0], struct.unpack(">Q", packet[5:13])[0], packet[13], packet[14], struct.unpack(">I", packet[15:19])[0], struct.unpack(">i", packet[19:23])[0])
    if command == 6:
        return Cm4HeartbeatResponse(
            command,
            struct.unpack(">I", packet[1:5])[0],
            struct.unpack(">Q", packet[5:13])[0],
            packet[13],
            packet[14],
            struct.unpack(">I", packet[15:19])[0],
            struct.unpack(">I", packet[19:23])[0],
            struct.unpack(">I", packet[23:27])[0],
            struct.unpack(">I", packet[27:31])[0],
            struct.unpack(">i", packet[31:35])[0],
            struct.unpack(">i", packet[35:39])[0],
            struct.unpack(">i", packet[39:43])[0],
            struct.unpack(">i", packet[43:47])[0],
            packet[47],
            struct.unpack(">I", packet[48:52])[0],
            packet[52],
            packet[53],
            struct.unpack(">I", packet[54:58])[0],
            packet[58],
        )
    if command in {10, 110}:
        low = struct.unpack(">I", packet[39:43])[0]
        high = struct.unpack(">I", packet[43:47])[0]
        return DaqStatusResponse(
            command,
            struct.unpack(">I", packet[1:5])[0],
            struct.unpack(">Q", packet[5:13])[0],
            packet[13],
            packet[14],
            struct.unpack(">i", packet[15:19])[0],
            struct.unpack(">I", packet[19:23])[0],
            struct.unpack(">I", packet[23:27])[0],
            struct.unpack(">I", packet[27:31])[0],
            struct.unpack(">I", packet[31:35])[0],
            struct.unpack(">I", packet[35:39])[0],
            (high << 32) | low,
            struct.unpack(">I", packet[47:51])[0],
        )
    if command in {11, 12, 112}:
        return DaqAckResponse(
            command,
            struct.unpack(">I", packet[1:5])[0],
            struct.unpack(">Q", packet[5:13])[0],
            packet[13],
            packet[14],
            struct.unpack(">i", packet[15:19])[0],
            struct.unpack(">I", packet[19:23])[0],
            struct.unpack(">I", packet[23:27])[0],
            struct.unpack(">I", packet[27:31])[0],
        )
    if command == 99:
        return OpenAmpHeartbeatResponse(
            command,
            struct.unpack(">I", packet[1:5])[0],
            struct.unpack(">Q", packet[5:13])[0],
            packet[13],
            packet[14],
            struct.unpack(">I", packet[15:19])[0],
            struct.unpack(">i", packet[19:23])[0],
            struct.unpack(">I", packet[23:27])[0],
            struct.unpack(">I", packet[27:31])[0],
            struct.unpack(">i", packet[31:35])[0],
            struct.unpack(">i", packet[35:39])[0],
            struct.unpack(">i", packet[39:43])[0],
            struct.unpack(">i", packet[43:47])[0],
            struct.unpack(">I", packet[47:51])[0],
            struct.unpack(">I", packet[51:55])[0],
            struct.unpack(">i", packet[55:59])[0],
            struct.unpack(">i", packet[59:63])[0],
            struct.unpack(">I", packet[63:67])[0],
            struct.unpack(">I", packet[67:71])[0],
        )
    return PacketBase(command, struct.unpack(">I", packet[1:5])[0], struct.unpack(">Q", packet[5:13])[0], packet[13], packet[14])


def parse_variable_packet(packet: bytes) -> FileChunkResponse:
    command = packet[0]
    status = packet[1]
    server_id = struct.unpack(">I", packet[2:6])[0]
    epoch_time = struct.unpack(">Q", packet[6:14])[0]
    total_size = struct.unpack(">I", packet[14:18])[0]
    offset = struct.unpack(">I", packet[18:22])[0]
    chunk_len = struct.unpack(">H", packet[22:24])[0]
    chunk = packet[24:24 + chunk_len]
    return FileChunkResponse(command, server_id, epoch_time, status, total_size, offset, chunk_len, chunk)


def parse_stream_header(packet: bytes) -> tuple[int, int, int, int, int]:
    return (
        packet[0],
        packet[1],
        struct.unpack(">I", packet[2:6])[0],
        struct.unpack(">Q", packet[6:14])[0],
        struct.unpack(">I", packet[14:18])[0],
    )


def verify_pattern_chunk(offset: int, chunk: bytes) -> tuple[bool, int, int, int]:
    for index, value in enumerate(chunk):
        expected = (((offset + index) * 37) + 11) & 0xFF
        if value != expected:
            return False, index, expected, value
    return True, 0, 0, 0
