#include "body/LinkBootstrap.h"

namespace body {

LinkBootstrap::LinkBootstrap(uint32_t local_session)
    : port_(), endpoint_(port_, r2link::kRoleBody, local_session) {}

void LinkBootstrap::tick(uint32_t now_ms) { endpoint_.tick(now_ms); }

}  // namespace body
