# hmcfgusb repository notes

C utilities for HM-CFG-USB(2), HM-MOD-UART and CUL transports. The code handles
BidCoS traffic, LAN-adapter emulation, packet sniffing and firmware flashing.

## Commands

```bash
make                 # build all binaries
make test            # hardware-free unit tests
make clean
docker build -t hmcfgusb .
dpkg-buildpackage -us -uc
```

Use both GCC and Clang, and exercise the ARM cross-build when changing portable
code or compiler flags. Keep `-Wall -Wextra` clean.

## Non-obvious constraints

- Do not modify the third-party public-domain AES implementation in `aes.c`.
- `Makefile` serves both normal builds and OpenWRT. Preserve both paths.
- `flash-ota.c` shares logic across HM-CFG-USB, CUL and HM-MOD-UART; trace every
  affected transport before changing packet, retry or bootloader behavior.
- `hmuartlgw_close()` and `culfw_close()` restore the saved terminal settings.
- Protocol and AES-signing changes can fail silently on real devices. Unit tests
  cover only pure helpers and firmware parsing; report hardware paths as
  unvalidated unless they were actually exercised.
- If binaries change, keep the Docker copy list and Debian install/symlink files
  aligned. New build artifacts belong in `.gitignore`.
- Hardware access rules live in `hmcfgusb.rules`; treat permission changes as a
  security-sensitive interface change.

Follow the existing C style (tabs, K&R braces, snake_case). Use existing compiler
attributes and `const` conventions instead of introducing a parallel style.
