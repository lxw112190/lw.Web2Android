# External Content Demo

This local-only sample consumes the fixed `lw:external-content` CustomEvent and the
`window.__lwExternalContentQueue` startup queue. The Android Runtime only supplies a
read-only text copy explicitly shared or opened by the user; it does not expose a
general Native Bridge or file-system API.
