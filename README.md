# Immersive Azeroth

Makes the world of WoW 3.3.5a feel alive: simulation and data-driven systems for the sky, climate,
fog, ground covers, lights and wind. Successor of WXL's `wxl-seyris-living-azeroth`, rebuilt for
our recompiled client (source + Vulkan). The port plan:
`orchestration/docs/research/immersive-azeroth-port-2026-10-19.md` in the XLClient hub.

A [WarcraftXL](https://github.com/WarcraftXL/sdk) mod. It needs an SXC build of the client: the
client's `sxc.*` services (data tables, settings, the tools overlay), published by the client's
`sxc.bridge` mod.

## State

- Loads as a mod, registers its 17 custom tables (editable in the overlay's Data view), adds an
  "Immersive Azeroth" module with an overview, and a setting `[ImmersiveAzeroth] Enabled`.
- The features follow, in the plan's order: sky clock and bodies first.

## Building

Checkouts beside each other, as in the hub (`client/`, `sdk/`, `mods/immersive-azeroth/`):

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Output: `build/dist/seyri.immersive-azeroth/{mod.json, bin/x64/immersive-azeroth.dll}`. The hub's
`tools/deploy-client.ps1` copies it into the client's `Mods/` with the client.

The mod compiles the client's Dear ImGui (`client/SXC/ThirdParty/imgui`) so its panels share the
overlay's context; rebuild it when the client's ImGui changes.

## Tables

`DBFilesClient\*.cdbc`, documented in WXL's module (`docs/cdbc-tables.md`), schemas in
`src/Tables.cpp`.
