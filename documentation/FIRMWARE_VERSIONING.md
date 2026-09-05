# Firmware versioning

## Version source

The firmware version has one authoritative definition in the current reference build:

```cmake
set(HUB_FIRMWARE_VERSION "0.2.20")
```

CMake uses this value to:

- generate `firmware_version.h` for the C source;
- populate reference-platform binary metadata;
- provide the version printed at startup and by the `version` command.

Do not duplicate the version in source files, web assets, or documentation.

## Version policy

Zap Hub uses semantic-versioning principles:

- **patch** — compatible bug fixes, diagnostics, and small implementation corrections;
- **minor** — a substantial compatible capability, completed framework feature, or new supported module contract;
- **major** — an incompatible change to public module, driver, command, configuration, or platform interfaces.

Before 1.0, a minor release may still contain architectural changes. Such changes should be called out clearly in release notes and migration instructions.

## What should trigger a version change

Change the firmware version when a build intended for hardware testing, release, or comparison changes observable behaviour, including:

- module startup or shutdown behaviour;
- bus transaction semantics;
- public command syntax;
- persistent configuration format;
- fault classification;
- default output state;
- driver conversion or calibration behaviour;
- networking or authentication behaviour;
- platform support.

Pure comments or documentation-only changes do not normally require a firmware version change unless the project release process packages documentation with a tagged firmware release.

## Release notes

Keep release notes outside this policy document. They should describe changes by architectural effect rather than only by the individual bench problem that prompted them.

Useful categories are:

- Core and platform
- Managed buses
- Module framework
- Drivers
- Diagnostics and networking
- Safety and supervisor
- Compatibility and migration
- Known limitations

## Build identity

For reproducible diagnosis, a production build should eventually expose more than the semantic version:

- source revision or commit identifier;
- dirty-tree indicator;
- target platform and board configuration;
- build type;
- build timestamp where reproducibility policy permits;
- configuration schema version.

The semantic version remains the human-facing release identity. Build metadata identifies the exact binary.
