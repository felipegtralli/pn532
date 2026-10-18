# Contributing

UART is currently implemented; I2C and SPI are placeholders.

- Keep pull requests focused. Discuss large or breaking changes first.
- Follow the existing C style and use the repository's `.clang-format` file.
- For hardware-related changes, check the manufacturer documents in
  [references/](references/) and cite the relevant section.
- Build and test the local component. See [Development](development.md).
- In the PR, explain what changed and what you tested. Include the ESP-IDF
  version, board, module, and interface for hardware tests. State anything untested.
- Update API comments and affected examples when changing public behavior.
