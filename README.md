# PrimeGram Desktop

Fork of Telegram Desktop for Windows x64. Includes an embedded Model Context Protocol (MCP) server for local AI assistant integration, DPAPI-backed biometric authentication via Windows Hello, segmented cache management, and GitHub Releases auto-update synchronization.

---

## Capabilities

### Model Context Protocol (MCP) Server
- Embedded JSON-RPC 2.0 / SSE server listening on `127.0.0.1:28888`.
- Exposes Telegram client operations to external AI tools (Claude Desktop, Cursor, local agents).
- Supported tools:
  - `telegram_get_me`: retrieve current user profile details.
  - `telegram_list_chats`: list active dialogs, channels, and groups.
  - `telegram_read_history`: fetch recent messages from specified peer.
  - `telegram_search_messages`: full-text search across active chats.
  - `telegram_send_message`: dispatch outbound messages (guarded by permission toggle).
- Server lifecycle and permissions are managed in `Settings -> PrimeGram -> MCP Server`.

### Biometric Authentication
- Native Windows Hello integration (fingerprint, facial recognition, PIN).
- Cold-start session unlocking using Windows Data Protection API (DPAPI).
- Automatic biometric challenge on process launch and lock events.

### Storage and Cache Audit
- Segmented disk usage calculation by category: media, stickers/GIFs, voice notes, documents, database indices.
- Granular cache purging without resetting authenticated sessions.

### Media Attachments
- Windows System Media Transport Controls integration for current audio track sharing.

### Update Mechanism
- Direct integration with GitHub Releases API (`Primeevokak/PrimeGram-desktop`).
- Independent of upstream Telegram distribution servers and channel feeds.

---

## Build Instructions

### Prerequisites
- Windows 10 (version 21H2+) or Windows 11 x64
- Visual Studio 2022 (MSVC v143, C++ x64 toolset)
- Python 3.11+
- CMake 3.25+ and Ninja
- Pre-built dependencies in `Libraries/win64/` (Qt 5.15.19, OpenSSL 3, WebRTC, FFmpeg)

### Build Steps

1. Configure CMake project:
   ```cmd
   build_telegram.bat configure
   ```

2. Compile executable:
   - Debug configuration:
     ```cmd
     build_telegram.bat build
     ```
   - Release configuration:
     ```cmd
     build_telegram.bat release
     ```

3. Build and package release:
   ```cmd
   build_telegram.bat package
   ```
   Compiles Release target, applies digital signature via `sign_exe.bat`, and outputs:
   - `releases/PrimeGram-v7.4-x64-portable.zip`
   - `releases/SHA256SUMS.txt`

4. Manual signing:
   ```cmd
   build_telegram.bat sign
   ```

---

## Repository Structure

PrimeGram extensions are isolated in `Telegram/SourceFiles/prime/`:

```
Telegram/SourceFiles/
├── prime/
│   ├── mcp/               # Model Context Protocol server (JSON-RPC / SSE)
│   ├── cache/             # Segmented cache accounting and cleanup
│   ├── privacy/           # Windows Hello and DPAPI integration
│   ├── nowplaying/        # System media session querying
│   ├── updates/           # GitHub Releases client
│   ├── tgws/              # Transport protocol adapters
│   └── prime_prefs.h      # Persistent configuration store
└── settings/sections/
    ├── settings_prime.cpp           # PrimeGram root settings
    └── settings_prime_storage.cpp   # Storage management view
```

---

## License

GNU General Public License v3.0 with OpenSSL Exception. See `LEGAL` for details.
