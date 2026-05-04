from __future__ import annotations

import argparse
import logging
import signal
import threading
import time

from .api import LocalApiServer
from .config import load_config
from .database import AemsDatabase
from .manager import AemsManager


def main() -> None:
    parser = argparse.ArgumentParser(description="AEMS Raspberry Pi board server daemon")
    parser.add_argument("--config", default="/etc/aems-server/config.toml", help="Path to aems-server TOML config")
    parser.add_argument("--host", help="Override TCP listen host")
    parser.add_argument("--port", type=int, help="Override TCP listen port")
    parser.add_argument("--log-level", default="INFO", help="Python logging level")
    args = parser.parse_args()

    logging.basicConfig(level=getattr(logging, args.log_level.upper(), logging.INFO), format="%(asctime)s %(levelname)s %(message)s")
    config = load_config(args.config)
    if args.host is not None:
        config.host = args.host
    if args.port is not None:
        config.port = args.port
    config.ensure_directories()

    database = AemsDatabase(config.database_path)
    manager = AemsManager(config=config, database=database)
    api = LocalApiServer(config.api_socket, manager.handle_request)
    stop_event = threading.Event()

    def _stop(_signum: int, _frame: object) -> None:
        stop_event.set()

    signal.signal(signal.SIGTERM, _stop)
    signal.signal(signal.SIGINT, _stop)

    logging.info("Starting AEMS board daemon on %s:%s", config.host, config.port)
    manager.start()
    api.start()
    logging.info("Local control socket: %s", config.api_socket)
    try:
        while not stop_event.is_set():
            time.sleep(0.5)
    finally:
        logging.info("Stopping AEMS board daemon")
        api.close()
        manager.stop()
        database.close()


if __name__ == "__main__":
    main()
