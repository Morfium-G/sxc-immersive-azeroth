// The read-only Lua functions for addons: ImmersiveAzeroth_* (docs: README, "Lua"). Registered again
// for every script context (the login glue and the game each get a fresh one).
#pragma once

namespace ia::lua {
    void Register();
}
