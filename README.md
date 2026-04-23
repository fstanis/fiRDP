# fiRDP

A lightweight RDP client for Linux and macOS, built on FreeRDP and SDL3.

GPU-accelerated rendering via SDL3 (Metal on macOS, GPU-accelerated on Linux), hardware video decoding (VAAPI/FFmpeg on Linux, VideoToolbox on macOS), and system keyring integration for password storage.

## Building

Requirements: clang, lld, cmake, SDL3, and FreeRDP's build dependencies (openssl, ffmpeg, libusb, cups, jansson). On Linux: libsecret. On macOS: Xcode command line tools.

```sh
git submodule update --init --recursive
cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release
cmake --build build --target fiRDP -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

On macOS, the build produces `build/fiRDP.app`. Run it with:

```sh
open build/fiRDP.app -- <file.rdp>
# or directly:
./build/fiRDP.app/Contents/MacOS/fiRDP [options] <file.rdp>
```

## Usage

```
fiRDP [options] <file.rdp>

Options:
  -c, --connect            Connect immediately (skip confirmation)
  -q, --quiet              Suppress connection info output
  -g, --grab-keyboard      Grab keyboard (requires Accessibility on macOS)
  -s, --native-scale       Override desktop scale factor with local display scale
      --native-resolution  Use display's native panel resolution (macOS only)
      --prefer-h264        Hint server to prefer H.264
      --low-latency        Send QoE feedback and suspend per-frame acks
  -h, --help               Show this help
```

Settings are loaded from the `.rdp` file. Passwords are retrieved from the system keyring (libsecret on Linux, Keychain on macOS) and stored after successful connections.

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| Shift+F11 | Toggle fullscreen |
| Shift+F12 | Disconnect |

## Configuration

Config file: `~/.config/freerdp/sdl-freerdp.json` (created on first run).

The `host_keys` option specifies key combinations kept for the host OS instead of forwarded to the remote session:

```json
{
    "SDL_KeyModMask": ["KMOD_NONE"],
    "host_keys": ["Super+Q", "Super+Tab", "Alt+F4"]
}
```

Modifiers: `Ctrl`, `Alt`, `Shift`, `Super` (aliases: `Win`, `Cmd`, `Gui`). Key names use SDL naming (e.g. `Q`, `Tab`, `F4`).

## License

GPLv3 or later. Includes code from [FreeRDP](FreeRDP/LICENSE) (Apache 2.0).
