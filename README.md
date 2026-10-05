# PS4 PKG Bridge - Phase 1 (phone-only build route)

Target: PS4 Firmware 9.00 / OpenOrbis

Phase 1 is a network benchmark prototype. It is intended to test phone -> PS4 streaming and measure sustained throughput before adding PKG installation/RPI/BGFT logic.

This repository includes a GitHub Actions workflow so the project can be built from an Android phone without a local computer. The workflow uses OpenOrbis's documented GitHub Actions toolchain flow and the official `samples/net_http` build files.

The source currently implements a small HTTP server on port 8080 with `/api/state` and `/api/upload`, writing received data incrementally to `/data/PS4PKGBridge/upload.bin` and calculating average MB/s.

Important limitations of this Phase 1 prototype:
- It is not a production-ready installer.
- The current prototype does not yet render the final TV UI or fully initialize all PS4 network subsystems in the production-safe way we want.
- It must be tested on the target PS4 before any speed claim is accepted.
- The 30 MB/s threshold is a test requirement, not a guaranteed rate.
- Use only files/content you are authorized to transfer/install.
