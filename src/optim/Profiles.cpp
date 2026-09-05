#include "Profiles.h"

namespace optim
{
    std::vector<Profile> BuildProfiles(const SystemInventory& inventory)
    {
        std::vector<Profile> profiles;

        {
            Profile profile;
            profile.id = "profile.gaming";
            profile.name = "Hranie";
            profile.description =
                "Vypne nahrávanie na pozadí a efekty rozhrania, zapne Herný režim a obmedzí "
                "úlohy aplikácií Store na pozadí.";
            profile.suitedFor =
                "Keď chceš, aby počas hry bežalo na pozadí čo najmenej. Nič z toho nezvýši "
                "takty ani FPS samo o sebe — len ubudne práca, ktorá o zdroje súperí.";
            profile.optimizationIds = {
                "gaming.game_mode",
                "gaming.game_dvr",
                "general.client_animations",
                "general.ui_effects",
                "general.transparency",
                "startup.background_apps",
            };
            profiles.push_back(profile);
        }

        {
            Profile profile;
            profile.id = "profile.responsive";
            profile.name = "Svižné rozhranie";
            profile.description =
                "Odstráni animácie, priehľadnosť a oneskorenie ponúk. Windows reaguje okamžite.";
            profile.suitedFor =
                "Keď ti vadí, že sa okná a ponuky „doťahujú“. Mení dojem z ovládania, "
                "nie priepustnosť systému.";
            profile.optimizationIds = {
                "general.client_animations",
                "general.ui_effects",
                "general.transparency",
                "general.menu_delay",
                "general.taskbar_widgets",
            };
            profiles.push_back(profile);
        }

        {
            Profile profile;
            profile.id = "profile.quiet";
            profile.name = "Menej vyrušovania";
            profile.description =
                "Vypne tipy, návrhy v ponuke Štart, reklamné ID a widgety na paneli úloh.";
            profile.suitedFor =
                "Voľba pohodlia a súkromia. Na výkon vplyv nemá — okrem widgetov, ktoré "
                "si držia vlastné procesy.";
            profile.optimizationIds = {
                "privacy.tips_suggestions",
                "privacy.start_suggestions",
                "privacy.advertising_id",
                "general.taskbar_widgets",
            };
            profiles.push_back(profile);
        }

        // Offered only where there is a battery to save. On a desktop this
        // would be a button with nothing behind it.
        if (inventory.isLaptop)
        {
            Profile profile;
            profile.id = "profile.battery";
            profile.name = "Notebook — výdrž";
            profile.description =
                "Obmedzí prácu na pozadí a vykresľovanie efektov, zapne automatické "
                "uvoľňovanie miesta.";
            profile.suitedFor =
                "Keď si mimo zásuvky. Najväčší vplyv na výdrž má aj tak jas displeja a "
                "plán napájania — tie nastav na stránke Napájanie.";
            profile.optimizationIds = {
                "general.client_animations",
                "general.ui_effects",
                "general.transparency",
                "startup.background_apps",
                "storage.storage_sense",
            };
            profiles.push_back(profile);
        }

        return profiles;
    }
}
