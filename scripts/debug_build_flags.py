import os
import re
import sys
from urllib.parse import urlsplit


def debug_url(address):
    if not address or re.search(r"[\s\\\"']", address):
        raise ValueError("DEBUG_ADDRESS must be an HTTP address without whitespace or quotes")
    if "://" not in address:
        address = "http://" + address
    try:
        parsed = urlsplit(address)
        port = parsed.port
    except ValueError as error:
        raise ValueError("DEBUG_ADDRESS has an invalid host or port") from error
    if (parsed.scheme != "http" or not parsed.hostname or parsed.username is not None
            or parsed.password is not None or parsed.path not in ("", "/")
            or parsed.query or parsed.fragment or parsed.netloc.endswith(":")
            or not re.fullmatch(r"[A-Za-z0-9.-]+", parsed.hostname)):
        raise ValueError("DEBUG_ADDRESS must be an HTTP host with an optional port")
    if port is not None and not 1 <= port <= 65535:
        raise ValueError("DEBUG_ADDRESS port must be between 1 and 65535")
    return f"http://{parsed.hostname}:{port or 8080}/"


def main():
    address = os.environ.get("DEBUG_ADDRESS")
    if address is None and "--required" not in sys.argv[1:]:
        return
    try:
        url = debug_url(address)
    except ValueError as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
    print(f'-DNETWORK_DEBUG_ADDRESS=\\"{url}\\"')


if __name__ == "__main__":
    main()
