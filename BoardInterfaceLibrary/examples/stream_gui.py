from __future__ import annotations

import math
import queue
import threading
import tkinter as tk
from tkinter import ttk

from common import build_parser, open_server, wait_for_session


CHANNEL_LABELS = [
    "Voltage-Ph1",
    "Voltage-Ph2",
    "Voltage-Ph3",
    "Current-Ph1",
    "Current-Ph2",
    "Current-Ph3",
]


class RmsAccumulator:
    """Accumulates physical sample values and emits RMS windows."""

    def __init__(self, sample_rate_hz: int, window_seconds: float = 0.25) -> None:
        self.window_samples = max(1, int(sample_rate_hz * window_seconds))
        self.sum_squares = [0.0] * 6
        self.count = 0
        self.total_frames = 0

    def add_sample(self, values: list[float]) -> list[float] | None:
        for index in range(6):
            self.sum_squares[index] += float(values[index]) * float(values[index])
        self.count += 1
        self.total_frames += 1

        if self.count < self.window_samples:
            return None

        rms = [math.sqrt(value / self.count) for value in self.sum_squares]
        self.sum_squares = [0.0] * 6
        self.count = 0
        return rms


class StreamRmsGui:
    def __init__(self, root: tk.Tk, stop_callback) -> None:
        self.root = root
        self.stop_callback = stop_callback
        self.value_vars = [tk.StringVar(value="--") for _ in range(6)]
        self.status_var = tk.StringVar(value="Streaming")
        self.frames_var = tk.StringVar(value="Frames: 0")
        self._stopping = False

        self.root.title("AEMS Live RMS Monitor")
        self.root.geometry("860x520")
        self.root.minsize(760, 460)
        self.root.configure(bg="#eef4f1")

        self._build_styles()
        self._build_layout()
        self.root.protocol("WM_DELETE_WINDOW", self.stop)

    def _build_styles(self) -> None:
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("Title.TLabel", background="#eef4f1", foreground="#17342f", font=("Segoe UI", 22, "bold"))
        style.configure("Subtitle.TLabel", background="#eef4f1", foreground="#55716a", font=("Segoe UI", 10))
        style.configure("Status.TLabel", background="#eef4f1", foreground="#2f5d54", font=("Segoe UI", 11, "bold"))
        style.configure("Stop.TButton", font=("Segoe UI", 12, "bold"), padding=(18, 10))

    def _build_layout(self) -> None:
        header = tk.Frame(self.root, bg="#eef4f1")
        header.pack(fill="x", padx=28, pady=(24, 8))

        ttk.Label(header, text="Live RMS Monitor", style="Title.TLabel").pack(anchor="w")
        ttk.Label(
            header,
            text="RMS is computed independently for each channel over 0.25 second windows.",
            style="Subtitle.TLabel",
        ).pack(anchor="w", pady=(4, 0))

        cards = tk.Frame(self.root, bg="#eef4f1")
        cards.pack(fill="both", expand=True, padx=24, pady=16)
        for column in range(3):
            cards.grid_columnconfigure(column, weight=1)
        for row in range(2):
            cards.grid_rowconfigure(row, weight=1)

        for index, label in enumerate(CHANNEL_LABELS):
            self._make_card(cards, index, label).grid(
                row=index // 3,
                column=index % 3,
                sticky="nsew",
                padx=10,
                pady=10,
            )

        footer = tk.Frame(self.root, bg="#eef4f1")
        footer.pack(fill="x", padx=28, pady=(0, 22))
        ttk.Label(footer, textvariable=self.status_var, style="Status.TLabel").pack(side="left")
        ttk.Label(footer, textvariable=self.frames_var, style="Subtitle.TLabel").pack(side="left", padx=(18, 0))
        ttk.Button(footer, text="Stop Stream", style="Stop.TButton", command=self.stop).pack(side="right")

    def _make_card(self, parent: tk.Widget, index: int, title: str) -> tk.Frame:
        bg = "#ffffff"
        accent = "#0f8f7a" if index < 3 else "#c9791c"
        unit = "V RMS" if index < 3 else "A RMS"

        frame = tk.Frame(parent, bg=bg, highlightbackground="#d7e2de", highlightthickness=1)
        top = tk.Frame(frame, bg=bg)
        top.pack(fill="x", padx=18, pady=(16, 6))
        tk.Label(top, text=title, bg=bg, fg="#31524b", font=("Segoe UI", 12, "bold")).pack(side="left")
        tk.Frame(top, bg=accent, width=10, height=10).pack(side="right", padx=(8, 0))

        tk.Label(
            frame,
            textvariable=self.value_vars[index],
            bg=bg,
            fg="#142c28",
            font=("Segoe UI", 30, "bold"),
        ).pack(anchor="w", padx=18, pady=(4, 0))
        tk.Label(frame, text=unit, bg=bg, fg="#78908a", font=("Segoe UI", 10, "bold")).pack(anchor="w", padx=20, pady=(0, 16))
        return frame

    def update_values(self, rms: list[float], total_frames: int) -> None:
        for index, value in enumerate(rms):
            self.value_vars[index].set(f"{value:.4g}")
        self.frames_var.set(f"Frames: {total_frames}")

    def set_status(self, text: str) -> None:
        self.status_var.set(text)

    def stop(self) -> None:
        if self._stopping:
            return
        self._stopping = True
        self.set_status("Stopping stream...")
        self.stop_callback()


def main() -> None:
    parser = build_parser("Open a Tkinter GUI and display live DAQ RMS values")
    parser.add_argument("--mode", choices=["file", "daq"], default="daq", help="Only --mode daq is supported by this GUI")
    parser.add_argument("--remote-file", default="daq.bin", help="Remote DAQ stream filename/config name")
    parser.add_argument("--output", help="Accepted for CLI compatibility with stream_example.py; not used by the GUI")
    parser.add_argument("--file-output-format", choices=["bin", "csv"], default="bin", help="Accepted for CLI compatibility; not used")
    parser.add_argument("--daq-output-format", choices=["csv", "bin"], default="csv", help="Accepted for CLI compatibility; not used")
    parser.add_argument("--duration", type=float, default=0.0, help="Accepted for CLI compatibility; GUI stops with the Stop button")
    parser.add_argument("--sample-rate", type=int, default=2000, help="DAQ stream sample rate")
    parser.add_argument("--channel-mask", type=lambda value: int(value, 0), default=0x3F, help="DAQ channel mask; current board expects 0x3F")
    parser.add_argument("--block-samples", type=int, default=128, help="DAQ block size")
    args = parser.parse_args()

    if args.mode != "daq":
        raise SystemExit("stream_gui.py supports live DAQ mode only. Use --mode daq.")

    rms_queue: queue.Queue[tuple[list[float], int]] = queue.Queue()
    accumulator = RmsAccumulator(args.sample_rate)

    def on_sample(_sample_index: int, values: list[float]) -> None:
        rms = accumulator.add_sample(values)
        if rms is not None:
            rms_queue.put((rms, accumulator.total_frames))

    server = open_server(args.host, args.port)
    session = wait_for_session(server, args.board_ip, args.timeout)

    print(f"Starting DAQ stream for {args.remote_file}")
    handle = session.start_daq_stream_async(
        filename=args.remote_file,
        sample_rate_hz=args.sample_rate,
        channel_mask=args.channel_mask,
        block_samples=args.block_samples,
        sample_callback=on_sample,
    )

    try:
        session.wait_for_command(13, timeout=2.0)
    except TimeoutError:
        # Some firmware builds go directly to the stream header instead of a fixed command 13 ACK.
        pass

    root = tk.Tk()
    stop_started = threading.Event()

    def stop_stream() -> None:
        if stop_started.is_set():
            return
        stop_started.set()

        def worker() -> None:
            try:
                session.stop_daq(timeout=max(args.timeout, 10.0))
                result = handle.wait(timeout=max(args.timeout, 30.0))
                root.after(0, lambda: gui.set_status(f"Stopped. Frames: {result.frames_received}"))
            except Exception as exc:  # noqa: BLE001 - GUI should surface any stream/stop failure.
                message = str(exc)
                root.after(0, lambda: gui.set_status(f"Stop failed: {message}"))
            finally:
                server.close()
                root.after(0, root.destroy)

        threading.Thread(target=worker, name="StopDaqStream", daemon=True).start()

    gui = StreamRmsGui(root, stop_stream)

    def poll_updates() -> None:
        while True:
            try:
                rms, total_frames = rms_queue.get_nowait()
            except queue.Empty:
                break
            gui.update_values(rms, total_frames)
        if not stop_started.is_set():
            root.after(50, poll_updates)
        else:
            root.after(200, poll_updates)

    root.after(50, poll_updates)
    root.mainloop()

    if not stop_started.is_set():
        stop_stream()


if __name__ == "__main__":
    main()
