from __future__ import annotations

import argparse
import json
import socket
import threading
import time
from typing import Any

from .cli.main import DEFAULT_SOCKET


def _local_request(socket_path: str, action: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.connect(socket_path)
        client.sendall((json.dumps({"action": action, "params": params or {}}) + "\n").encode("utf-8"))
        response = b""
        while not response.endswith(b"\n"):
            chunk = client.recv(65536)
            if not chunk:
                break
            response += chunk
    return json.loads(response.decode("utf-8"))


def _command_to_local_action(payload: dict[str, Any]) -> tuple[str, dict[str, Any]]:
    """Translate a cloud command payload into the daemon's local API.

    Expected command shape:
      {"command": "daq.stream.start", "params": {"board": "all", ...}}
    """

    command = str(payload.get("command", ""))
    params = dict(payload.get("params") or {})
    allowed = {
        "health.get",
        "health.shadow",
        "daq.stream.start",
        "daq.stream.stop",
        "daq.log.start",
        "daq.log.stop",
        "daq.log.run",
        "transfer.start",
        "schedule.add",
        "schedule.remove",
        "schedule.enable",
        "boards.list",
        "jobs.list",
        "transfers.list",
    }
    if command not in allowed:
        raise ValueError(f"Unsupported cloud command: {command}")
    return command, params


def main() -> None:
    parser = argparse.ArgumentParser(description="Optional AWS IoT Core bridge for aems-boardd")
    parser.add_argument("--socket", default=DEFAULT_SOCKET, help="Local aems-boardd Unix socket")
    parser.add_argument("--endpoint", required=True, help="AWS IoT Core ATS endpoint")
    parser.add_argument("--thing-name", required=True, help="AWS IoT thing name for this Pi")
    parser.add_argument("--client-id", default=None, help="MQTT client ID; defaults to thing name")
    parser.add_argument("--cert", required=True, help="Device certificate PEM")
    parser.add_argument("--key", required=True, help="Private key PEM")
    parser.add_argument("--ca", required=True, help="Amazon Root CA PEM")
    parser.add_argument("--topic-prefix", default=None, help="Command topic prefix; default aems/<thing-name>")
    parser.add_argument("--shadow-interval", type=float, default=30.0, help="Seconds between shadow reported-state updates")
    args = parser.parse_args()

    try:
        from awscrt import mqtt  # type: ignore[import-not-found]
        from awsiot import mqtt_connection_builder  # type: ignore[import-not-found]
    except ImportError as exc:
        raise SystemExit("AWS IoT support requires awsiotsdk; install .[cloud] into the active environment") from exc

    topic_prefix = (args.topic_prefix or f"aems/{args.thing_name}").strip("/")
    command_topic = f"{topic_prefix}/commands/#"
    response_topic = f"{topic_prefix}/responses"
    shadow_update_topic = f"$aws/things/{args.thing_name}/shadow/update"

    mqtt_connection = mqtt_connection_builder.mtls_from_path(
        endpoint=args.endpoint,
        cert_filepath=args.cert,
        pri_key_filepath=args.key,
        ca_filepath=args.ca,
        client_id=args.client_id or args.thing_name,
        clean_session=False,
        keep_alive_secs=30,
    )
    mqtt_connection.connect().result()
    stop_event = threading.Event()

    def publish_response(payload: dict[str, Any]) -> None:
        mqtt_connection.publish(topic=response_topic, payload=json.dumps(payload), qos=mqtt.QoS.AT_LEAST_ONCE)

    def on_message(topic: str, payload: bytes, **_kwargs: Any) -> None:
        try:
            incoming = json.loads(payload.decode("utf-8"))
            action, params = _command_to_local_action(incoming)
            result = _local_request(args.socket, action, params)
            publish_response({"topic": topic, "request": incoming, "result": result})
        except Exception as exc:
            publish_response({"topic": topic, "ok": False, "error": str(exc)})

    mqtt_connection.subscribe(topic=command_topic, qos=mqtt.QoS.AT_LEAST_ONCE, callback=on_message).result()

    try:
        while not stop_event.wait(args.shadow_interval):
            shadow = _local_request(args.socket, "health.shadow", {"poll": True})
            reported = shadow.get("data", shadow)
            mqtt_connection.publish(topic=shadow_update_topic, payload=json.dumps(reported), qos=mqtt.QoS.AT_LEAST_ONCE)
    except KeyboardInterrupt:
        pass
    finally:
        mqtt_connection.disconnect().result()


if __name__ == "__main__":
    main()
