#pragma once

#include "core/ProjectConfig.h"

namespace lw::web2android {

class ProjectValidator {
public:
    static void Validate(const ProjectConfig& config);
};

}  // namespace lw::web2android
