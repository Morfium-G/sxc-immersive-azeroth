#include "Profiler.h"

#include "SXC/Api/sxc_profiler.h"
#include "wxl/sdk.hpp"

namespace ia::prof {
    namespace {
        const char* const kNames[kStepCount] = {
            "Immersive Azeroth: world snapshot", "Immersive Azeroth: sky", "Immersive Azeroth: climate", "Immersive Azeroth: ground",
            "Immersive Azeroth: wind", "Immersive Azeroth: grass", "Immersive Azeroth: zones' weather", "Immersive Azeroth: fields",
            "Immersive Azeroth: fog field", "Immersive Azeroth: fog hand-over", "Immersive Azeroth: fog layers built", "Immersive Azeroth: covers", "Immersive Azeroth: lights",
        };

        const sxc_profiler_api* Api() {
            static const sxc_profiler_api* s_api =
                wxl::Host() ? static_cast<const sxc_profiler_api*>(wxl::Host()->get_service(SXC_PROFILER_SERVICE, SXC_PROFILER_SERVICE_VERSION)) : nullptr;
            return s_api;
        }

        // Registered on first use (the update runs before the profiler shows anything).
        int Id(Step step) {
            static int s_ids[kStepCount];
            static bool s_registered;

            if (!s_registered) {
                s_registered = true;

                for (int i = 0; i < kStepCount; i++) {
                    // The update draws nothing: CPU only.
                    s_ids[i] = Api()->register_category(kNames[i], SXC_PROFILER_CPU_ONLY);
                }
            }

            return s_ids[step];
        }
    }

    void Enter(Step step) {
        const sxc_profiler_api* api = Api();

        if (api && api->on()) {
            api->enter(Id(step));
        }
    }

    void Leave(Step step) {
        const sxc_profiler_api* api = Api();

        if (api && api->on()) {
            api->leave(Id(step));
        }
    }
}
