from __future__ import annotations

import csv
import math
import struct
from pathlib import Path
from typing import BinaryIO, Iterable

DAQ_STREAM_FRAME_LEN = 36
# Backward-compatible name used by existing session code.
DAQ_FRAME_LEN = DAQ_STREAM_FRAME_LEN
DAQ_LOG_ENABLED_CHANNELS = 6
DAQ_LOG_SAMPLE_LEN = DAQ_LOG_ENABLED_CHANNELS * 4

INT24_MAX = 8388607  # 2^23 - 1
V_REF = 1.2
V_REF_VGAIN = 0.3
CT_V_PEAK = 0.4714
CT_CURRENT_MAX = 100.0
V_DIVIDER = 0.2568
MAINS_VOLTAGE = 240.0


def clamp_int24(adc_value: int) -> int:
    if adc_value > INT24_MAX:
        return INT24_MAX
    if adc_value < -INT24_MAX:
        return -INT24_MAX
    return adc_value


def parse_adc_value_current(adc_value: int) -> float:
    adc_value = clamp_int24(adc_value)
    v_out = (float(adc_value) / float(INT24_MAX)) * V_REF
    v_ct = v_out * (CT_V_PEAK / V_REF)
    current = (v_ct / CT_V_PEAK) * (CT_CURRENT_MAX * 1.414)
    return current


def parse_adc_value_voltage(adc_value: int) -> float:
    adc_value = clamp_int24(adc_value)
    v_adc = (float(adc_value) / float(INT24_MAX)) * V_REF_VGAIN
    v_mains = (v_adc / V_DIVIDER) * (MAINS_VOLTAGE * 1.414)
    return v_mains


def round_significant(value: float, digits: int = 4) -> float:
    if value == 0.0:
        return 0.0
    return round(value, digits - 1 - int(math.floor(math.log10(abs(value)))))


def convert_channels_to_physical(channels: Iterable[int]) -> list[float]:
    converted: list[float] = []
    for index, value in enumerate(channels):
        if index < 3:
            converted.append(round_significant(parse_adc_value_voltage(int(value)), 4))
        else:
            converted.append(round_significant(parse_adc_value_current(int(value)), 4))
    return converted


def decode_daq_stream_frame(frame: bytes) -> list[float]:
    _response, _crc, *channels = struct.unpack("<HH8i", frame)
    return convert_channels_to_physical(channels[:DAQ_LOG_ENABLED_CHANNELS])


def decode_daq_log_sample(sample: bytes) -> list[float]:
    channels = struct.unpack("<6i", sample)
    return convert_channels_to_physical(channels)


# Backward-compatible export name used by the streaming session code.
def decode_daq_frame(frame: bytes) -> list[float]:
    return decode_daq_stream_frame(frame)


def _write_csv_header(writer: csv.writer) -> None:
    writer.writerow(["sample_index", "ch0", "ch1", "ch2", "ch3", "ch4", "ch5"])


def write_daq_stream_rows_from_stream(src: BinaryIO, csv_path: str | Path) -> int:
    csv_path = Path(csv_path)
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    sample_index = 0
    remainder = bytearray()
    with csv_path.open("w", newline="") as csv_file:
        writer = csv.writer(csv_file)
        _write_csv_header(writer)
        while True:
            chunk = src.read(64 * 1024)
            if not chunk:
                break
            remainder.extend(chunk)
            while len(remainder) >= DAQ_STREAM_FRAME_LEN:
                frame = bytes(remainder[:DAQ_STREAM_FRAME_LEN])
                del remainder[:DAQ_STREAM_FRAME_LEN]
                writer.writerow([sample_index, *decode_daq_stream_frame(frame)])
                sample_index += 1
    return sample_index


def write_daq_log_rows_from_stream(src: BinaryIO, csv_path: str | Path) -> int:
    csv_path = Path(csv_path)
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    sample_index = 0
    remainder = bytearray()
    with csv_path.open("w", newline="") as csv_file:
        writer = csv.writer(csv_file)
        _write_csv_header(writer)
        while True:
            chunk = src.read(64 * 1024)
            if not chunk:
                break
            remainder.extend(chunk)
            while len(remainder) >= DAQ_LOG_SAMPLE_LEN:
                sample = bytes(remainder[:DAQ_LOG_SAMPLE_LEN])
                del remainder[:DAQ_LOG_SAMPLE_LEN]
                writer.writerow([sample_index, *decode_daq_log_sample(sample)])
                sample_index += 1
    return sample_index


def convert_daq_stream_bin_to_csv(bin_path: str | Path, csv_path: str | Path) -> int:
    bin_path = Path(bin_path)
    with bin_path.open("rb") as src:
        return write_daq_stream_rows_from_stream(src, csv_path)


def convert_daq_bin_to_csv(bin_path: str | Path, csv_path: str | Path) -> int:
    """
    Convert a DAQ log file produced by command 11 into parsed CSV.

    CM4 firmware writes command 11 eMMC log data as packed little-endian int32
    channel values only, with no response/crc header and no unused channels.
    For the current board build the channel mask is limited to 0x3F, so each
    logged sample is:

    - ch0 int32_le
    - ch1 int32_le
    - ch2 int32_le
    - ch3 int32_le
    - ch4 int32_le
    - ch5 int32_le

    Total: 24 bytes per sample.
    """
    bin_path = Path(bin_path)
    with bin_path.open("rb") as src:
        return write_daq_log_rows_from_stream(src, csv_path)
