# Network discovery

After a configured device connects to its Wi-Fi network, it advertises the
DNS-SD service `_wireless-3dp._tcp.local.` over mDNS on TCP port 80. The
service name is shortened to fit DNS-SD's 15-character service-name limit.
The setup access point does not advertise this service. Each device uses the
hostname `wireless-3d-printer-<mac>.local`, where `<mac>` is its Wi-Fi MAC
address in lowercase hexadecimal without separators, for example
`wireless-3d-printer-001122334455.local`.
The hostname and device ID are stable as long as the Wi-Fi MAC address remains
the same; they are not secrets.

The DHCP hostname sent to the router is the fixed name `wireless-3d-printer`; it is
independent of this unique mDNS hostname and the configurable display name.

The renamed service, hostname, device ID, and API type are incompatible with
clients using the previous identifiers. Update the firmware and clients
together; saved custom device names are not renamed.

A Cura plugin or other client can browse `_wireless-3dp._tcp.local.`, resolve
each service's SRV target, port and addresses, and request `/api/device` from
that address and port. Use the `id` to deduplicate devices across addresses
and repeated announcements. Remove expired services and handle devices
disappearing or changing IP addresses. Apply a request timeout; a device may be
busy serving another request. A manual IP address is a useful fallback where
multicast DNS is unavailable or blocked between network segments.

For example, on a machine with Avahi:

```sh
avahi-browse -rt _wireless-3dp._tcp
curl http://wireless-3d-printer-001122334455.local/api/device
```

## Device information endpoint

`GET /api/device` returns HTTP 200 with `Content-Type: application/json`,
`Cache-Control: no-store`, and a connection-close-delimited body:

```json
{
  "type": "wireless-3d-printer",
  "id": "wireless-3d-printer-001122334455",
  "name": "wireless-3d-printer",
  "api_version": 1
}
```

`type` identifies this project, `id` identifies the device, `name` is the
display label saved during setup (default `wireless-3d-printer`), and
`api_version` is the integer version of this response contract. The endpoint does not return
Wi-Fi credentials. In setup mode it returns HTTP 404. If mDNS initialization or
service registration fails, the serial log reports the failure and the HTTP
server remains available by IP address.

HTTP and mDNS are unauthenticated and unencrypted. Discovery identifies a
device but does not authenticate it; use this service only on a trusted local
network. mDNS multicast may be blocked by client isolation, firewalls, or
routers, and is not generally routed between subnets.
