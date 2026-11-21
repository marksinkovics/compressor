# Mounting archives with FSKit: spike notes (2026-11-20)

Autonomous run 6, item 3, stopped at the signing step and postponed for separate research (no NFS or macFUSE fallback, by decision).

## What was checked

- **SDK:** Xcode 26.6 on macOS 26.6.2 ships `FSKit.framework` with Swift support (`UnaryFileSystemExtension`, an `ExtensionFoundation.AppExtension`) and, new in FSKit's second API version, `FSPathURLResource`: a file system can be loaded from a path in the file system (`file:` URL, optionally writable), not only from a block device (`FSBlockDeviceResource`). `FSGenericURLResource` covers custom URL schemes (`FSSupportedSchemes` in `Info.plist`). So mounting an `.ingot` file is supported by the API.
- **Signing:** a binary signed ad hoc (`codesign -s -`) with the entitlement `com.apple.developer.fskit.fsmodule` is killed at launch (exit 137, SIGKILL); the same binary without it runs. The entitlement is restricted: it needs a provisioning profile from an Apple developer team, so local ad-hoc signing can't load an FSKit module. The machine has personal "Apple Development" certificates; they were not used, nor the company Developer ID, since the plan was to stop at this point.

## What an implementation needs

- An app with an FSKit module extension (extension point `com.apple.fskit.fsmodule`, `EXAppExtensionAttributes` with `FSShortName` and `FSPersonalities` in the extension's `Info.plist`), signed with automatic signing in Xcode under a developer team, so Xcode can create the profile with the FSKit capability.
- The user enables the extension once in System Settings → General → Login Items & Extensions → File System Extensions.
- In Swift: `FSUnaryFileSystem` + `FSUnaryFileSystemOperations` (`probeResource`, `loadResource` taking the `FSPathURLResource`, `unloadResource`), and an `FSVolume` subclass with `FSVolume.Operations` (`activate`, `mount`, `lookupItem`, `enumerateDirectory`, `getAttributes`, `reclaimItem`, `readSymbolicLink`, `deactivate`, `unmount`, `synchronize`, path-conf limits; the create/remove/rename/set-attribute calls fail with read-only errors), `FSVolume.OpenCloseOperations` and `FSVolume.ReadWriteOperations` (`read` into an `FSMutableFileDataBuffer`). The signatures can be printed with `xcrun swift-synthesize-interface -module-name FSKit -include-submodules -sdk "$(xcrun --show-sdk-path)" -target arm64-apple-macos26.0`.
- The archive side exists: `read_archive_index()` gives the entries (paths, types, modes, times, sizes, symlink targets), and `extract_range()` reads any byte range of a file decoding only its blocks (run 6, item 1), which is what `read` needs. Swift's C++ interoperability can call them directly; a small C++ facade with plain types would keep the boundary simple.
- Mounting from the command line: `mount -F` with the module's short name and the archive path, to be verified once a signed module exists.

## Open questions for the research

- Whether a personal team's development profile is enough to load the module locally, or distribution needs a Developer ID with the FSKit capability.
- How the module gets access to the archive file (security-scoped URL from the mounting client, or a user-granted location).
- Caching decoded blocks across `read` calls (blocks are up to 1 MiB; reads come in pages).
