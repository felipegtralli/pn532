# Development

This guide describes where driver changes belong and what to check before
submitting them. For setup and usage, see the main README and example READMEs.

## Code organization

- `include/pn532.h`: public API, configuration types, and constants.
- `src/pn532.c`: handle lifecycle, command exchanges, and command wrappers.
- `src/pn532_internal.h`: private state, transport callbacks, and shared declarations.
- `src/pn532_internal.c`: shared frame encoding and response validation.
- Transport files: interface-specific initialization, wake-up, I/O, and cleanup.
- `examples/`: applications showing how to use the public API.
- [references/](references/): manufacturer documents for hardware-related changes.

## Making changes

- Keep public functions and command handling independent of the transport.
  Interface-specific behavior belongs in the transport implementation.
- Follow existing command wrappers and reuse the shared exchange and frame
  helpers instead of duplicating them.
- Keep implementation details private. Put declarations in the internal header
  only when they are shared between source files.
- Preserve transaction locking: the core holds the mutex across an exchange;
  transport callbacks must not take it again.
- An interface declaration or a chip capability does not mean the driver
  implements it. Check the implementation before documenting support.

## Checking changes

- Build an ESP-IDF application against the modified component, not a downloaded
  copy of it.
- Check the affected behavior and relevant failure cases, such as invalid inputs,
  small buffers, timeouts, incomplete responses, and cleanup after errors.
- For communication changes, test the affected interface on hardware, including
  repeated operations and startup after reset or a power cycle where relevant.
- State what was tested and what remains unverified. A successful build does not
  verify communication with the PN532.
