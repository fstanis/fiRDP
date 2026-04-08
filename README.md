# fiRDP

A lightweight RDP client built on FreeRDP with hardware-accelerated decoding.

Connects to RDP servers using SDL3 for rendering on Wayland, with VAAPI/FFmpeg hardware video decoding. Passwords are stored in the system keyring via libsecret.

## Building

**Requirements:** clang, lld, cmake, SDL3, libsecret, and FreeRDP's build dependencies (openssl, ffmpeg, libusb, cups, jansson, etc.).

```sh
./build.sh
```

Or manually:

```sh
git submodule update --init --recursive
cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release
cmake --build build --target fiRDP -j$(nproc)
```

The binary is at `build/fiRDP`.

When making changes, run `clang-format -i src/*.cpp src/*.hpp` to apply formatting.

## Usage

```
fiRDP [options] <file.rdp>

Options:
  -c, --connect    Connect immediately (skip confirmation)
  -q, --quiet      Suppress connection info output
  -h, --help       Show this help
```

Settings are loaded from the `.rdp` file. The password is retrieved from the system keyring if previously saved, otherwise prompted on the terminal. After a successful connection, the password is stored in the keyring for next time.

Embedded passwords in `.rdp` files are rejected as insecure.

```sh
# Interactive (shows settings, prompts for password, waits for Enter)
fiRDP session.rdp

# Auto-connect (no confirmation prompt)
fiRDP -c session.rdp

# Quiet auto-connect
fiRDP -qc session.rdp
```

## Keyboard Shortcuts

| Linux | macOS | Action |
|---|---|---|
| Ctrl+Alt+F11 | Cmd+Alt+F11 | Toggle fullscreen |
| Ctrl+Alt+F4 | Cmd+Alt+F4 | Disconnect |

## Configuration

Config file: `~/.config/freerdp/sdl-freerdp.json` (created automatically on first run).

### Host keys

By default, all key presses are forwarded to the remote session. The `host_keys` option lets you specify key combinations that should be kept for the host OS instead:

```json
{
    "SDL_KeyModMask": ["KMOD_NONE"],
    "host_keys": ["Super+Q", "Super+Tab", "Alt+F4"]
}
```

Supported modifiers: `Ctrl`, `Alt`, `Shift`, `Super` (aliases: `Win`, `Cmd`, `Gui`). Key names use SDL naming (e.g. `Q`, `Tab`, `F4`, `Escape`, `Return`, `Space`).

### FreeRDP hotkeys

The config also controls FreeRDP's SDL hotkey system. By default, FreeRDP hotkeys are disabled (only the hardcoded shortcuts above work). To enable additional FreeRDP hotkeys:

```json
{
    "SDL_KeyModMask": ["KMOD_SHIFT"]
}
```

## Architecture

fiRDP reuses FreeRDP's SDL3 client code (`client/SDL/SDL3/`) for connection management, rendering, input, clipboard, and display handling. The SDL dialog system is replaced with stub implementations since all user interaction happens before connecting.

```
src/
  main.cpp              CLI, argument parsing, password prompt, config
  rdp_file.hpp/cpp      .rdp file parser and validator
  password_store.hpp/cpp  libsecret/Keychain keyring integration
  rdp_connection.hpp/cpp  SDL/FreeRDP session manager
  host_keys.hpp/cpp       Host key passthrough config and matching
  fi_dialogs.cpp          No-op dialog callbacks
  fi_dialog_stubs.cpp     Stub connection dialog wrapper
  sdl_config.hpp          Client metadata constants
```

FreeRDP is built as a static subdirectory via cmake. The `FreeRDP/` directory is only needed at build time.

## License

This project is licensed under the GNU General Public License v3.0 or later - see the LICENSE file for details.

### Third-Party Code

This software includes code from FreeRDP, which is licensed under the Apache License 2.0. See [FreeRDP's LICENSE](FreeRDP/LICENSE) for the Apache 2.0 license terms and attributions.
