#pragma once

#include "Optimization.h"
#include "BackupStore.h"

#include <memory>
#include <vector>

namespace optim
{
    // Builds the shipped set of optimizations.
    //
    // Everything here is a documented, per-user (HKCU / SystemParametersInfo)
    // setting that needs no elevation and can be put back exactly. Things
    // deliberately NOT in this catalog, and why:
    //   * timeBeginPeriod — since Windows 10 2004 it only affects the calling
    //     process, so it cannot speed up a game in another process, and the
    //     docs warn it can reduce overall performance and block CPU power
    //     saving.
    //   * Working-set trimming ("free RAM") — evicts pages the app will
    //     immediately fault back in; costs more than it saves.
    //   * Service disabling, page-file changes, registry cleaning, network
    //     stack tweaks — either unsupported, system-wide, or not
    //     substantiated.
    std::vector<std::unique_ptr<Optimization>> BuildCatalog(BackupStore* backups);

    const char* CategoryLabel(Category category);
    const char* BenefitLabel(Benefit benefit);
    const char* EvidenceLabel(Evidence evidence);
}
