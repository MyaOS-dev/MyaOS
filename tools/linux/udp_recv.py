#!/usr/bin/env python3
import argparse
import socket
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser(description="Simple UDP packet receiver for Linux")
    parser.add_argument("--host", default="0.0.0.0", help="bind host (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=9000, help="bind UDP port (default: 9000)")
    parser.add_argument("--hex", action="store_true", help="print payload as hex")
    args = parser.parse_args()

    if args.port < 1 or args.port > 65535:
        print("invalid port", file=sys.stderr)
        return 2

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))

    print(f"listening on {args.host}:{args.port}")

    while True:
        data, addr = sock.recvfrom(65535)
        ts = time.strftime("%Y-%m-%d %H:%M:%S")
        src_ip, src_port = addr[0], addr[1]
        if args.hex:
            payload = data.hex()
        else:
            payload = data.decode("utf-8", errors="replace")
        print(f"[{ts}] from {src_ip}:{src_port} len={len(data)} payload={payload}")


if __name__ == "__main__":
    raise SystemExit(main())
