# Bearing Live

Existing v0.19 source for the Bearing Live iPad music app (JUCE 8 / C++17).

- `app/` — CMake project. Build notes: `app/BUILD_ON_MAC.md`. What the code does today: `app/STATUS.md`.
- `seat-service/` — church-seat worker. Deploy notes: `seat-service/DEPLOY.md`.
- `docs/` — launch and design notes.

JUCE 8.0.4 is not stored in this repository. Place that checkout at `app/JUCE` (see `app/BUILD_ON_MAC.md`). Local Xcode projects and `app/build*` folders stay on the machine that generates them.
