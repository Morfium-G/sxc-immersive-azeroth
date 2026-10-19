#pragma once

// The custom tables (sxc.data). Schemas from WXL's wxl-seyris-living-azeroth docs/cdbc-tables.md;
// column order as its loaders read them. Registered at WXL_STAGE_READY.
namespace ia {
    void RegisterTables();
}
