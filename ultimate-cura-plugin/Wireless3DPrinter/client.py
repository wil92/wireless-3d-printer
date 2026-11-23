"""Discovery and HTTP transport, independent of Cura's UI."""

import http.client
import ipaddress
import json
import math
import os
import re
import socket
import time
from urllib.parse import quote
from dataclasses import dataclass
from pathlib import Path

SERVICE = "_wireless-3dp._tcp.local."
CONNECT_TIMEOUT = 5
UPLOAD_TIMEOUT = 600
FILE_OPERATION_TIMEOUT = 120
MAX_FILE_LIST_BYTES = 2 * 1024 * 1024
STATUS_INTERVAL_MS = 10000


@dataclass(frozen=True)
class Device:
    id: str
    name: str
    address: str
    port: int


def safe_filename(name):
    name = str(name or "untitled_print").replace("\\", "/").rsplit("/", 1)[-1]
    name = re.sub(r"[^A-Za-z0-9._ -]", "_", name).strip(" .")
    if name.lower().endswith(".gcode"):
        name = name[:-6].strip(" .")
    return (name[:100] or "untitled_print") + ".gcode"


def device_at(address, port, timeout=CONNECT_TIMEOUT):
    # mDNS is unauthenticated; reject anything other than a usable IP address.
    ip = ipaddress.ip_address(address)
    if ip.is_unspecified or ip.is_multicast:
        raise ValueError("Invalid device address")
    connection = http.client.HTTPConnection(str(ip), port, timeout=timeout)
    try:
        connection.request("GET", "/api/device", headers={"Connection": "close"})
        response = connection.getresponse()
        if response.status != 200:
            raise ValueError("Device information returned HTTP {}".format(response.status))
        payload = response.read(16385)
        if len(payload) > 16384:
            raise ValueError("Device information is too large")
        data = json.loads(payload)
        if (not isinstance(data, dict) or data.get("type") != "wireless-3d-printer"
                or data.get("api_version") != 1
                or not isinstance(data.get("id"), str) or not data["id"]
                or not isinstance(data.get("name"), str) or not data["name"]):
            raise ValueError("Unsupported device information")
        return Device(data["id"], data["name"], str(ip), port)
    finally:
        connection.close()


def discover(duration=3):
    # Imported here so the output option remains available if a dependency is missing.
    from zeroconf import Zeroconf, ServiceBrowser

    class Listener:
        def __init__(self):
            self.names = set()
            import threading
            self.lock = threading.Lock()

        def add_service(self, zc, service_type, name):
            with self.lock:
                self.names.add(name)

        def update_service(self, zc, service_type, name):
            self.add_service(zc, service_type, name)

        def remove_service(self, zc, service_type, name):
            with self.lock:
                self.names.discard(name)

    listener = Listener()
    zc = Zeroconf()
    browser = None
    try:
        browser = ServiceBrowser(zc, SERVICE, listener)
        time.sleep(duration)
        with listener.lock:
            names = sorted(listener.names)
        devices = {}
        for name in names:
            info = zc.get_service_info(SERVICE, name, timeout=1500)
            if not info or not 1 <= info.port <= 65535:
                continue
            for address in info.parsed_addresses():
                try:
                    device = device_at(address, info.port)
                except (OSError, ValueError, http.client.HTTPException):
                    continue
                devices[device.id] = device
                break
        return sorted(devices.values(), key=lambda device: (device.name, device.id))
    finally:
        if browser:
            browser.cancel()
        zc.close()


def _file_request(device, method, path, limit, body=None, headers=None, timeout=FILE_OPERATION_TIMEOUT):
    connection = http.client.HTTPConnection(device.address, device.port, timeout=timeout)
    try:
        request_headers = {"Connection": "close"}
        if headers:
            request_headers.update(headers)
        connection.request(method, path, body=body, headers=request_headers)
        response = connection.getresponse()
        payload = response.read(limit + 1)
        if len(payload) > limit:
            raise ValueError("Printer response is too large")
        if not 200 <= response.status < 300:
            raise RuntimeError("HTTP {}: {}".format(response.status, payload.decode("utf-8", errors="replace")))
        return payload
    finally:
        connection.close()


def _version_parts(value):
    if not isinstance(value, str) or not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", value):
        raise ValueError("Invalid version; expected major.minor")
    return tuple(int(part) for part in value.split("."))


def version_warning(device):
    with (Path(__file__).parent / "plugin.json").open(encoding="utf-8") as metadata:
        plugin_version = json.load(metadata)["version"]
    plugin_major, _ = _version_parts(plugin_version)
    payload = _file_request(device, "GET", "/api/version", 4096, timeout=CONNECT_TIMEOUT)
    data = json.loads(payload)
    if not isinstance(data, dict):
        raise ValueError("Invalid firmware version response")
    firmware_version = data.get("firmware_version")
    firmware_major, _ = _version_parts(firmware_version)
    if plugin_major != firmware_major:
        return ("WARNING: Cura plugin version {} and firmware version {} have different major "
                "versions and may be incompatible. Update the plugin or firmware."
                .format(plugin_version, firmware_version))
    return ""


def list_files(device):
    payload = _file_request(device, "GET", "/api/files", MAX_FILE_LIST_BYTES)
    files = json.loads(payload)
    if not isinstance(files, list) or len(files) > 256:
        raise ValueError("Invalid printer file list")
    for entry in files:
        if (not isinstance(entry, dict) or not isinstance(entry.get("name"), str)
                or not isinstance(entry.get("path"), str) or not entry["path"]
                or type(entry.get("size")) is not int or entry["size"] < 0):
            raise ValueError("Invalid printer file entry")
    return files


def printer_status(device):
    payload = _file_request(device, "GET", "/api/status", 4096)
    data = json.loads(payload)
    if (not isinstance(data, dict) or not isinstance(data.get("printer_name"), str)
            or not data["printer_name"]
            or data.get("printer_status") not in ("sd_printing", "not_sd_printing")
            or any(value is not None and
                   (type(value) not in (int, float) or not math.isfinite(value))
                   for value in (data.get("nozzle_temperature"), data.get("bed_temperature")))
            or "nozzle_temperature" not in data or "bed_temperature" not in data):
        raise ValueError("Invalid printer status")
    return data


def printer_filename(name):
    name = safe_filename(name)[:-6]
    return "".join(c.upper() if c.isascii() and (c.isalnum() or c in "_-") else "_"
                   for c in name)[:8] + ".GCO"


def matches_model(entry, model_name):
    if not isinstance(model_name, str) or not model_name.strip():
        return False
    expected = safe_filename(model_name)
    return (entry["name"].casefold() == expected.casefold()
            or entry["path"].casefold() == printer_filename(model_name).casefold())


def delete_file(device, path):
    if not isinstance(path, str) or not path:
        raise ValueError("Invalid printer file path")
    _file_request(device, "DELETE", "/api/files?path=" + quote(path, safe=""), 4096)


def start_print(device, path):
    if (not isinstance(path, str) or not re.fullmatch(r"[A-Za-z0-9_.~/-]{1,128}", path)
            or path.startswith("/") or path.endswith("/") or ".." in path or "//" in path):
        raise ValueError("Invalid printer file path")
    try:
        _file_request(device, "POST", "/api/files/print?path=" + quote(path, safe=""), 4096)
    except (OSError, http.client.HTTPException, ValueError, RuntimeError) as error:
        raise RuntimeError("Print request failed: {}. The print may have started; "
                           "check the printer before retrying.".format(error)) from error


def delete_files(device, paths):
    if (not isinstance(paths, list) or not 1 <= len(paths) <= 256
            or any(not isinstance(path, str) or not re.fullmatch(r"[A-Za-z0-9_.~/-]{1,128}", path)
                   or path.startswith("/") or path.endswith("/") or ".." in path or "//" in path
                   for path in paths)
            or len({path.upper() for path in paths}) != len(paths)):
        raise ValueError("Invalid printer file list")
    body = "\n".join(paths).encode("ascii")
    try:
        _file_request(device, "POST", "/api/files/delete", 4096, body,
                      {"Content-Type": "text/plain"}, FILE_OPERATION_TIMEOUT * len(paths))
    except (OSError, http.client.HTTPException, ValueError) as error:
        raise RuntimeError("Bulk delete response failed: {}. Some files may have been deleted; "
                           "refresh before retrying.".format(error)) from error


def upload(device, path, filename, progress=lambda percent: None):
    filename = safe_filename(filename)
    size = os.path.getsize(path)
    if not size:
        raise ValueError("G-code file is empty")
    connection = http.client.HTTPConnection(device.address, device.port, timeout=UPLOAD_TIMEOUT)
    try:
        connection.putrequest("POST", "/upload?name=" + quote(filename, safe=""))
        connection.putheader("Content-Type", "application/octet-stream")
        connection.putheader("Content-Length", str(size))
        connection.putheader("Connection", "close")
        connection.endheaders()
        sent = 0
        with open(path, "rb") as stream:
            while True:
                chunk = stream.read(16384)
                if not chunk:
                    break
                connection.send(chunk)
                sent += len(chunk)
                progress(int(sent * 100 / size))
        response = connection.getresponse()
        detail = response.read(4096).decode("utf-8", errors="replace")
        if not 200 <= response.status < 300:
            raise RuntimeError("Upload failed: HTTP {} {}".format(response.status, detail))
        return detail
    except socket.timeout as error:
        raise RuntimeError("Upload timed out; the device may have a partial file") from error
    finally:
        connection.close()
