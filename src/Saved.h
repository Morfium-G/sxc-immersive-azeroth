// A panel's settings struct kept in WTF\SXC.ini (sxc.settings), section by section. The panel edits the
// struct; once a frame (Sync) a changed field goes to its setting, and a change made in the overlay's
// Settings view comes back into the struct. Declared at the first Sync (after WXL_STAGE_READY): until
// then the struct's own defaults stand, and they are what a missing key falls back to.
#pragma once

#include <cstddef>
#include <memory>
#include <vector>
#include "SXC/Api/sxc_settings.hpp"

namespace ia::saved {
    // One field: a switch, a number in min..max, or a whole number in min..max (exactly one member set).
    template <class T>
    struct Field {
        const char* key;
        const char* label;   // the Settings view's; null keeps it out of the view
        const char* help;
        bool T::*flag;
        float T::*value;
        int T::*whole;
        float min, max;
    };

    template <class T>
    Field<T> Flag(const char* key, const char* label, const char* help, bool T::*m) { return { key, label, help, m, nullptr, nullptr, 0.0f, 1.0f }; }

    template <class T>
    Field<T> Value(const char* key, const char* label, const char* help, float T::*m, float min, float max) { return { key, label, help, nullptr, m, nullptr, min, max }; }

    template <class T>
    Field<T> Whole(const char* key, const char* label, const char* help, int T::*m, int min, int max) {
        return { key, label, help, nullptr, nullptr, m, static_cast<float>(min), static_cast<float>(max) };
    }

    template <class T>
    class Block {
    public:
        Block(const char* section, std::vector<Field<T>> fields) : m_section(section), m_fields(std::move(fields)) {}

        // Once a frame. True when a field of `target` changed from outside the panel (the Settings view),
        // or was read from the file at the first call.
        bool Sync(T& target) {
            if (!sxc::settings::Api()) {
                return false;
            }

            if (m_handles.empty()) {
                const T defaults;
                m_handles.resize(m_fields.size());

                for (size_t i = 0; i < m_fields.size(); i++) {
                    const Field<T>& f = m_fields[i];
                    Handle& h = m_handles[i];

                    if (f.flag) {
                        h.flag = std::make_unique<sxc::settings::Bool>(m_section, f.key, defaults.*f.flag, f.label, f.help);
                    } else if (f.value) {
                        h.value = std::make_unique<sxc::settings::Float>(m_section, f.key, defaults.*f.value, f.min, f.max, f.label, f.help);
                    } else {
                        h.whole = std::make_unique<sxc::settings::Int>(m_section, f.key, defaults.*f.whole, static_cast<int>(f.min), static_cast<int>(f.max), f.label,
                                                                       f.help);
                    }

                    h.last = Stored(h);
                    Write(target, f, h.last);
                }

                return true;
            }

            bool fromOutside = false;

            for (size_t i = 0; i < m_fields.size(); i++) {
                const Field<T>& f = m_fields[i];
                Handle& h = m_handles[i];
                const float panel = Read(target, f);
                const float stored = Stored(h);

                if (panel != h.last) {
                    // The panel changed it.
                    if (h.flag) {
                        h.flag->Set(panel != 0.0f);
                    } else if (h.value) {
                        h.value->Set(panel);
                    } else {
                        h.whole->Set(static_cast<int>(panel));
                    }

                    h.last = Stored(h);
                    Write(target, f, h.last);
                } else if (stored != h.last) {
                    // The Settings view changed it.
                    h.last = stored;
                    Write(target, f, stored);
                    fromOutside = true;
                }
            }

            return fromOutside;
        }

    private:
        struct Handle {
            std::unique_ptr<sxc::settings::Bool> flag;
            std::unique_ptr<sxc::settings::Float> value;
            std::unique_ptr<sxc::settings::Int> whole;
            float last = 0.0f;   // the value both sides agreed on
        };

        static float Stored(const Handle& h) {
            return h.flag ? (h.flag->Get() ? 1.0f : 0.0f) : h.value ? h.value->Get() : static_cast<float>(h.whole->Get());
        }

        static float Read(const T& s, const Field<T>& f) {
            return f.flag ? (s.*f.flag ? 1.0f : 0.0f) : f.value ? s.*f.value : static_cast<float>(s.*f.whole);
        }

        static void Write(T& s, const Field<T>& f, float v) {
            if (f.flag) {
                s.*f.flag = v != 0.0f;
            } else if (f.value) {
                s.*f.value = v;
            } else {
                s.*f.whole = static_cast<int>(v);
            }
        }

        const char* m_section;
        std::vector<Field<T>> m_fields;
        std::vector<Handle> m_handles;
    };

    // Every panel's saved settings but the grass's (its own, grass/Grass.cpp): once a frame, from the mod's update.
    void SyncAll();
}
