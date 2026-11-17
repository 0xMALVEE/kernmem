# Apex Legends

ESP client for `r5apex.exe`, built on the shared framework in `sdk/client`.

## Layout

| file | what it is |
| --- | --- |
| `offsets.txt` | the offset dump. Source of truth for every value in `client/offsets.h`. |
| `client/offsets.h` | the dump's values as constants, each with the struct/global it came from. |
| `client/provider.h` | the `esp::Provider` subclass declaration. |
| `client/provider.cpp` | process/module resolve, matrix, local entity, player walk. |
| `client/main.cpp` | three lines: construct the provider, call `esp::Run`. |
| `client/build.ps1` | builds `build/kernmem-apex.exe`. |

Everything else - overlay window, box and health drawing, attach retry, stop
key, driver handshake - comes from `sdk/client` and is compiled in.

## How the walk works

Apex has no entity system object and no chunk table. `cl_entitylist` is a flat
array of entity pointers, stride `0x20`, and a handle's low 16 bits are the slot
index directly. `EntityAt` is therefore one read, not three.

The local entity is a pointer global (`LocalPlayer`), with the handle global
(`LocalEntityHandle`) through the list as the fallback. `SignonState` gates the
read: anything below 6 is still connecting, and a stale pointer in a menu
transition reads as a plausible address full of garbage.

Only entities with `m_iHealth` in 1..200, `m_lifeState == 0` and a team id in
1..64 are kept. That filter is what removes the world, props and deployables
that share the same array and have no health at all.

`m_vecAbsOrigin` is the feet, so the box top is a fixed `HeroHeight` above it.
The view offset is the eye, which sits inside the head and would give a box with
no height.

## Building

```
powershell -ExecutionPolicy Bypass -File apex-legends/client/build.ps1
```

Or run the root `build.ps1`, which builds the driver, loader and every client
and drops them in `dist/`.

## When a game update breaks it

The client still runs and shows `0 tracked`. The status line carries the walk
counters (`slots ent dead noorg loc1 loc2 signon`) so the failing step is named
instead of guessed:

- `slots 0` - `cl_entitylist` moved, or the list pointer is bad.
- `slots` high, `ent 0` - the health/team/life offsets moved, or the stride is
  no longer `0x20`.
- `loc1 0 loc2 0` - both local routes are dead; `LocalPlayer` and
  `LocalEntityHandle` moved.
- `signon` never 6 - the `SignonState` offset moved, and the client is gating
  itself out of every frame.

Re-dump, diff against `client/offsets.h`, and change only what moved. Every
constant in that header names the struct it came from so the diff is mechanical.
