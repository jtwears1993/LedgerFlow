#!/usr/bin/env python3
"""
LedgerFlow TCP event feeder — sends protobuf-framed ingress events to the C++ service.

Requires Python protobuf bindings generated from proto/ingress.proto.
Generate them with one of:

    mkdir -p generated/proto/v1/python
    protoc -I proto --python_out=generated/proto/v1/python proto/ingress.proto

or, if you have grpc_tools installed:

    python -m grpc_tools.protoc -I proto --python_out=generated/proto/v1/python proto/ingress.proto

Then run:

    python scripts/position_event_feeder.py --duration-sec 60 --md-interval-ms 250
"""

import argparse
import math
import os
import signal
import socket
import struct
import sys
import threading
import time
import logging

# ---------------------------------------------------------------------------
# Proto bindings
# ---------------------------------------------------------------------------
_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
_PROTO_PYTHON_DIR = os.path.join(_REPO_ROOT, "generated", "proto", "v1", "python")
sys.path.insert(0, _PROTO_PYTHON_DIR)

try:
    import ingress_pb2
except ImportError:
    print(
        "ERROR: Python protobuf bindings not found.\n"
        "Generate them first:\n"
        "\n"
        "    mkdir -p generated/proto/v1/python\n"
        "    protoc -I proto --python_out=generated/proto/v1/python proto/ingress.proto\n"
        "\n"
        "or with grpc_tools:\n"
        "\n"
        "    python -m grpc_tools.protoc -I proto "
        "--python_out=generated/proto/v1/python proto/ingress.proto\n"
    )
    sys.exit(1)

# ---------------------------------------------------------------------------
# Wire-protocol helpers
# ---------------------------------------------------------------------------
_HEADER_FMT = "<QII"   # uint64 timestampNS, uint32 proto_size, uint32 idempotencyKey
_HEADER_SIZE = struct.calcsize(_HEADER_FMT)  # 16 bytes


def recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError(f"Connection closed after {len(buf)}/{n} bytes")
        buf.extend(chunk)
    return bytes(buf)


def read_response(sock: socket.socket):
    raw_header = recv_exact(sock, _HEADER_SIZE)
    _ts, proto_size, _key = struct.unpack(_HEADER_FMT, raw_header)
    if proto_size == 0:
        return None
    payload = recv_exact(sock, proto_size)
    return ingress_pb2.IngressResponse.FromString(payload)


def send_request(sock: socket.socket, req: ingress_pb2.IngressRequest, idempotency_key: int):
    payload = req.SerializeToString()
    header = struct.pack(_HEADER_FMT, time.time_ns(), len(payload), idempotency_key)
    sock.sendall(header + payload)
    return read_response(sock)


# ---------------------------------------------------------------------------
# Monotonic key counter
# ---------------------------------------------------------------------------
_key_lock = threading.Lock()
_next_key_val = 1


def next_key() -> int:
    global _next_key_val
    with _key_lock:
        k = _next_key_val
        _next_key_val += 1
        return k


# ---------------------------------------------------------------------------
# Execution events
# ---------------------------------------------------------------------------
_BASE_PRICES: dict[str, int] = {"AAPL": 10000, "MSFT": 25000}


def _make_execution_req(symbol: str, order_id: int, qty: int, price: int) -> ingress_pb2.IngressRequest:
    hdr = ingress_pb2.EventHeader(
        ingest_ts_ns=time.time_ns(),
        type=ingress_pb2.EVENT_TYPE_ORDER,
    )
    ev = ingress_pb2.ExecutionEvent(
        symbol=symbol,
        hdr=hdr,
        order_id=order_id,
        side=ingress_pb2.SIDE_BUY if qty > 0 else ingress_pb2.SIDE_SELL,
        status=ingress_pb2.ORDER_STATUS_ACCEPTED,
        signed_order_qty=qty,
        limit_price=price,
        avg_entry_price=price,
    )
    return ingress_pb2.IngressRequest(
        type=ingress_pb2.REQUEST_TYPE_EXECUTION_EVENT,
        execution_event=ev,
    )


def send_initial_positions(sock: socket.socket) -> None:
    fills = [
        ("AAPL",  1, 100, 10000),
        ("AAPL",  2,  50, 10200),  # second fill — exercises avg-entry recalculation
        ("MSFT",  3, 200, 25000),
    ]
    for symbol, order_id, qty, price in fills:
        req = _make_execution_req(symbol, order_id, qty, price)
        resp = send_request(sock, req, next_key())
        logging.info(
            "[exec] %s order_id=%d qty=%+d price=%d  response=%s",
            symbol, order_id, qty, price,
            "ok" if resp is None else resp.WhichOneof("response"),
        )


# ---------------------------------------------------------------------------
# Market-data thread
# ---------------------------------------------------------------------------
def md_thread_fn(
    host: str,
    port: int,
    symbols: list[str],
    md_interval_ms: int,
    stop_event: threading.Event,
) -> None:
    sleep_sec = md_interval_ms / 1000.0
    with socket.create_connection((host, port)) as md_sock:
        logging.info("[md] connected to %s:%d", host, port)
        tick_count = 0
        while not stop_event.is_set():
            t = time.monotonic()
            for sym in symbols:
                base = _BASE_PRICES.get(sym, 10000)
                mid = base + int(5 * math.sin(t * 0.3))
                bid = mid - 5
                ask = mid + 5
                if bid <= 0:
                    bid, ask = 1, 11

                hdr = ingress_pb2.EventHeader(
                    ingest_ts_ns=time.time_ns(),
                    type=ingress_pb2.EVENT_TYPE_MARKET_DATA,
                )
                tob = ingress_pb2.TopOfBook(
                    symbol=sym,
                    hdr=hdr,
                    best_bid_price=bid,
                    best_ask_price=ask,
                )
                req = ingress_pb2.IngressRequest(
                    type=ingress_pb2.REQUEST_TYPE_TOP_OF_BOOK,
                    top_of_book=tob,
                )
                send_request(md_sock, req, next_key())

            tick_count += 1
            if tick_count % 20 == 0:
                logging.info("[md] %d ticks sent", tick_count)

            stop_event.wait(timeout=sleep_sec)

    logging.info("[md] thread exiting, %d ticks total", tick_count)


# ---------------------------------------------------------------------------
# Query loop
# ---------------------------------------------------------------------------
def run_query_loop(
    sock: socket.socket,
    query_interval_sec: float,
    stop_event: threading.Event,
    start_time: float,
    duration_sec: float,
) -> None:
    while not stop_event.is_set():
        elapsed = time.monotonic() - start_time
        if elapsed >= duration_sec:
            break

        req = ingress_pb2.IngressRequest(
            type=ingress_pb2.REQUEST_TYPE_GET_ALL_POSITIONS,
            get_all_positions_request=ingress_pb2.GetAllPositionsRequest(),
        )
        resp = send_request(sock, req, next_key())
        if resp is not None and resp.HasField("all_positions_response"):
            apr = resp.all_positions_response
            for pos in apr.positions:
                logging.info(
                    "[positions] %s  qty=%d  avg_entry=%d  unrealized_pnl=%d  realized_pnl=%d",
                    pos.sym, pos.quantity, pos.average_entry_price,
                    pos.unrealized_pnl, pos.realized_pnl,
                )
            if not apr.positions:
                logging.info("[positions] (no open positions yet)")

        stop_event.wait(timeout=query_interval_sec)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="LedgerFlow position event feeder")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=4000)
    p.add_argument("--symbols", default="AAPL,MSFT")
    p.add_argument("--duration-sec", type=float, default=60.0)
    p.add_argument("--md-interval-ms", type=int, default=250)
    p.add_argument("--query-interval-sec", type=float, default=2.0)
    p.add_argument("--no-query", action="store_true", help="Disable position polling")
    return p.parse_args()


def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(message)s",
        datefmt="%H:%M:%S",
    )
    args = parse_args()
    symbols = [s.strip() for s in args.symbols.split(",") if s.strip()]

    stop_event = threading.Event()

    def _handle_sigint(_sig, _frame):
        logging.info("Interrupted — shutting down...")
        stop_event.set()

    signal.signal(signal.SIGINT, _handle_sigint)

    # Main socket — execution events + optional queries
    main_sock = socket.create_connection((args.host, args.port))
    logging.info("[main] connected to %s:%d", args.host, args.port)

    send_initial_positions(main_sock)

    # Market-data thread
    md = threading.Thread(
        target=md_thread_fn,
        args=(args.host, args.port, symbols, args.md_interval_ms, stop_event),
        daemon=True,
    )
    md.start()

    start_time = time.monotonic()

    if not args.no_query:
        run_query_loop(main_sock, args.query_interval_sec, stop_event, start_time, args.duration_sec)
    else:
        # Just wait for duration / Ctrl-C
        remaining = args.duration_sec - (time.monotonic() - start_time)
        if remaining > 0:
            stop_event.wait(timeout=remaining)

    stop_event.set()
    md.join(timeout=5)
    main_sock.close()
    logging.info("[main] done")


if __name__ == "__main__":
    main()
