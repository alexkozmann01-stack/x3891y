#pragma once

#include "Optimization.h"
#include "SystemInventory.h"

#include <string>
#include <vector>

// Named bundles of settings that already exist in the catalog.
//
// A profile is nothing more than a list of optimization ids. It does not
// have powers of its own, it cannot do anything an individual card can't,
// and applying one runs each setting through the same capture / write /
// verify path — so a profile where one setting fails reports exactly that
// rather than a green tick for the whole bundle.
//
// Profiles that don't suit the detected machine are not offered: a battery
// profile on a desktop would be a control with nothing behind it.
namespace optim
{
    struct Profile
    {
        std::string id;
        std::string name;
        std::string description;
        std::string suitedFor;   // who this is for, in plain language
        std::vector<std::string> optimizationIds;
    };

    std::vector<Profile> BuildProfiles(const SystemInventory& inventory);
}
