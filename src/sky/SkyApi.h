// The client's sxc.sky service (null when the client doesn't offer it).
#pragma once

#include "SXC/Api/sxc_sky.h"
#include "wxl/sdk.hpp"

namespace ia::sky {
    inline const sxc_sky_api* Api() {
        static const sxc_sky_api* s_api =
            wxl::Host() ? static_cast<const sxc_sky_api*>(wxl::Host()->get_service(SXC_SKY_SERVICE, SXC_SKY_SERVICE_VERSION)) : nullptr;
        return s_api;
    }

    // A stock body this frame (zeroed when the service is missing).
    inline sxc_sky_stock Stock(sxc_sky_role role) {
        sxc_sky_stock s = {};
        s.struct_size = sizeof(s);

        if (Api()) {
            Api()->stock(role, &s);
        }

        return s;
    }

    inline void Camera(float out[3]) {
        const wxl_vec3 c = Api() ? Api()->camera() : wxl_vec3{};
        out[0] = c.x;
        out[1] = c.y;
        out[2] = c.z;
    }
}
