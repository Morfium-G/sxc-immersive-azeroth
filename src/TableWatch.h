// A table this mod derives something from: rebuild when it changed (an edit in the Data view, a
// reload, the file hot-reloaded). Cheap to ask every frame.
#pragma once

#include <cstdint>
#include "SXC/Api/sxc_data.hpp"

namespace ia {
    class TableWatch {
    public:
        explicit TableWatch(const char* name) : m_name(name) {}

        // True once after every change (and the first time the table is there).
        bool Changed() {
            if (!m_table) {
                m_table = sxc::data::Table::Get(m_name);
            }

            const uint32_t generation = m_table.Generation();

            if (generation == m_generation) {
                return false;
            }

            m_generation = generation;
            return true;
        }

        const sxc::data::Table& Table() const { return m_table; }

    private:
        const char* m_name;
        sxc::data::Table m_table;
        uint32_t m_generation = 0;
    };
}
