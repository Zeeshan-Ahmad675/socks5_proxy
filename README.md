# SOCKS5 Proxy

This project builds a local SOCKS5 proxy that listens on `127.0.0.1:1080`.

It is meant to be used from a browser on the same machine, for example Firefox, Chrome, or Chromium.

## Build

```bash
cmake -S . -B build
cmake --build build
```

The executable is named `socks5_proxy`.

## Run

```bash
./build/socks5_proxy
```

When it starts successfully, it binds to port `1080` on the local machine.

## Use From A Browser

### Firefox

1. Open `Settings`.
2. Scroll to `Network Settings` and click `Settings...`.
3. Select `Manual proxy configuration`.
4. Set `SOCKS Host` to `127.0.0.1` and `Port` to `1080`.
5. Select `SOCKS v5`.
6. Make sure local addresses are not bypassed if you want traffic to go through the proxy.

### Chrome / Chromium / Other Chromium-based Browsers

These browsers usually use the system proxy settings on Linux.

1. Open your desktop or system network proxy settings.
2. Set the SOCKS proxy host to `127.0.0.1`.
3. Set the SOCKS proxy port to `1080`.
4. Choose SOCKS version 5 if the settings dialog asks for it.

If your browser has a direct proxy configuration page, use the same values:

- Host: `127.0.0.1`
- Port: `1080`
- Type: `SOCKS5`

## Notes

- The proxy currently supports SOCKS5 without authentication.
- It binds on the local machine using port `1080`.
- If a site does not load, check that the proxy is running and that the browser is pointed at `127.0.0.1:1080`.
- Any bug report is highly appreciated.