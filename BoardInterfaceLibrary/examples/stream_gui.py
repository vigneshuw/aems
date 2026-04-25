from __future__ import annotations

import math
import queue
import threading
import tkinter as tk
from tkinter import ttk
from collections import deque

from common import build_parser, open_server, wait_for_session


CHANNEL_LABELS = [
    "Voltage-Ph1",
    "Voltage-Ph2",
    "Voltage-Ph3",
    "Current-Ph1",
    "Current-Ph2",
    "Current-Ph3",
]

PHASE_LABELS = ["Phase-1", "Phase-2", "Phase-3"]
PHASE_COLORS = ["#000000", "#C62828", "#1565C0"]  # US 3-phase convention: black, red, blue
PLOT_BG = "#fbfdfc"
PLOT_GRID = "#d9e4df"
PLOT_BORDER = "#c7d6d1"
MAX_HISTORY_POINTS = 240


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
        self.voltage_phase_enabled = [tk.BooleanVar(value=True) for _ in range(3)]
        self.current_phase_enabled = [tk.BooleanVar(value=True) for _ in range(3)]
        self.voltage_history = [deque(maxlen=MAX_HISTORY_POINTS) for _ in range(3)]
        self.current_history = [deque(maxlen=MAX_HISTORY_POINTS) for _ in range(3)]
        self._stopping = False

        self.root.title("AEMS Live RMS Monitor")
        self.root.geometry("1080x820")
        self.root.minsize(960, 720)
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
        style.configure("PlotTitle.TLabel", background="#eef4f1", foreground="#244640", font=("Segoe UI", 12, "bold"))
        style.configure("PlotCheck.TCheckbutton", background="#eef4f1", foreground="#355a52", font=("Segoe UI", 10))

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
        cards.pack(fill="x", padx=24, pady=16)
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

        plots = tk.Frame(self.root, bg="#eef4f1")
        plots.pack(fill="both", expand=True, padx=24, pady=(4, 16))
        plots.grid_columnconfigure(0, weight=1)
        plots.grid_columnconfigure(1, weight=1)
        plots.grid_rowconfigure(0, weight=1)

        self.voltage_canvas = self._make_plot_panel(
            plots,
            column=0,
            title="Voltage RMS History",
            phase_vars=self.voltage_phase_enabled,
        )
        self.current_canvas = self._make_plot_panel(
            plots,
            column=1,
            title="Current RMS History",
            phase_vars=self.current_phase_enabled,
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

    def _make_plot_panel(self, parent: tk.Widget, column: int, title: str, phase_vars: list[tk.BooleanVar]) -> tk.Canvas:
        panel = tk.Frame(parent, bg="#eef4f1")
        panel.grid(row=0, column=column, sticky="nsew", padx=10, pady=10)
        panel.grid_rowconfigure(1, weight=1)
        panel.grid_columnconfigure(0, weight=1)

        header = tk.Frame(panel, bg="#eef4f1")
        header.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        ttk.Label(header, text=title, style="PlotTitle.TLabel").pack(anchor="w")

        checks = tk.Frame(header, bg="#eef4f1")
        checks.pack(anchor="w", pady=(6, 0))
        for index, label in enumerate(PHASE_LABELS):
            ttk.Checkbutton(
                checks,
                text=label,
                variable=phase_vars[index],
                style="PlotCheck.TCheckbutton",
                command=self._redraw_plots,
            ).pack(side="left", padx=(0, 16))

        canvas = tk.Canvas(panel, bg=PLOT_BG, highlightbackground=PLOT_BORDER, highlightthickness=1, height=280)
        canvas.grid(row=1, column=0, sticky="nsew")
        canvas.bind("<Configure>", lambda _event: self._redraw_plots())
        return canvas

    def update_values(self, rms: list[float], total_frames: int) -> None:
        for index, value in enumerate(rms):
            self.value_vars[index].set(f"{value:.4g}")
        self.frames_var.set(f"Frames: {total_frames}")
        for index in range(3):
            self.voltage_history[index].append(float(rms[index]))
            self.current_history[index].append(float(rms[index + 3]))
        self._redraw_plots()

    def set_status(self, text: str) -> None:
        self.status_var.set(text)

    def _redraw_plots(self) -> None:
        self._draw_plot(
            self.voltage_canvas,
            self.voltage_history,
            self.voltage_phase_enabled,
            "V RMS",
        )
        self._draw_plot(
            self.current_canvas,
            self.current_history,
            self.current_phase_enabled,
            "A RMS",
        )

    def _draw_plot(
        self,
        canvas: tk.Canvas,
        histories: list[deque[float]],
        phase_vars: list[tk.BooleanVar],
        unit_label: str,
    ) -> None:
        canvas.delete("all")
        width = max(canvas.winfo_width(), 200)
        height = max(canvas.winfo_height(), 200)

        left = 68
        right = width - 26
        top = 52
        bottom = height - 52
        plot_width = max(1, right - left)
        plot_height = max(1, bottom - top)

        canvas.create_rectangle(left, top, right, bottom, outline=PLOT_BORDER, width=1)

        active_values: list[float] = []
        for index in range(3):
            if phase_vars[index].get():
                active_values.extend(histories[index])

        y_max = max(active_values) if active_values else 1.0
        if y_max <= 0.0:
            y_max = 1.0
        y_max *= 1.10

        for grid_index in range(5):
            y = top + (plot_height * grid_index / 4.0)
            canvas.create_line(left, y, right, y, fill=PLOT_GRID, width=1)
            value = y_max * (1.0 - grid_index / 4.0)
            canvas.create_text(left - 8, y, text=f"{value:.4g}", anchor="e", fill="#5f7771", font=("Segoe UI", 9))

        canvas.create_text((left + right) / 2, bottom + 26, text="History", anchor="center", fill="#5f7771", font=("Segoe UI", 9, "bold"))
        canvas.create_text(14, top - 16, text=unit_label, anchor="w", fill="#5f7771", font=("Segoe UI", 9, "bold"))

        legend_x = left
        legend_y = 18
        for index, label in enumerate(PHASE_LABELS):
            if not phase_vars[index].get():
                continue
            color = PHASE_COLORS[index]
            canvas.create_line(legend_x, legend_y + 7, legend_x + 20, legend_y + 7, fill=color, width=3)
            canvas.create_text(legend_x + 26, legend_y + 7, text=label, anchor="w", fill="#28463f", font=("Segoe UI", 9))
            legend_x += 110

        for index in range(3):
            if not phase_vars[index].get():
                continue
            values = list(histories[index])
            if len(values) < 2:
                continue
            x_step = plot_width / max(1, MAX_HISTORY_POINTS - 1)
            points: list[float] = []
            start_x = right - x_step * (len(values) - 1)
            for value_index, value in enumerate(values):
                x = start_x + value_index * x_step
                y = bottom - (float(value) / y_max) * plot_height
                points.extend((x, y))
            canvas.create_line(*points, fill=PHASE_COLORS[index], width=2, smooth=True)

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
