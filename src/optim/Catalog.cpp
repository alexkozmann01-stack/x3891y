#include "Catalog.h"
#include "RegistryOptimization.h"

#include <windows.h>

namespace optim
{
    const char* CategoryLabel(Category category)
    {
        switch (category)
        {
        case Category::General: return "Všeobecné";
        case Category::Gaming:  return "Hranie";
        case Category::Privacy: return "Súkromie";
        case Category::Startup: return "Štart";
        case Category::Power:   return "Napájanie";
        case Category::Network: return "Sieť";
        }
        return "";
    }

    const char* BenefitLabel(Benefit benefit)
    {
        switch (benefit)
        {
        case Benefit::PerceivedResponsiveness: return "Vnímaná odozva";
        case Benefit::Throughput:              return "Výkon";
        case Benefit::Startup:                 return "Štart systému";
        case Benefit::Storage:                 return "Miesto na disku";
        case Benefit::Gaming:                  return "Hranie";
        case Benefit::Privacy:                 return "Súkromie";
        case Benefit::Convenience:             return "Pohodlie";
        }
        return "";
    }

    const char* EvidenceLabel(Evidence evidence)
    {
        switch (evidence)
        {
        case Evidence::Documented: return "Dokumentované";
        case Evidence::Plausible:  return "Závisí od zostavy";
        case Evidence::Anecdotal:  return "Neoverené";
        }
        return "";
    }

    std::vector<std::unique_ptr<Optimization>> BuildCatalog(BackupStore* backups)
    {
        std::vector<std::unique_ptr<Optimization>> catalog;

        // ---- Gaming ----------------------------------------------------

        {
            Info info;
            info.id = "gaming.game_mode";
            info.category = Category::Gaming;
            info.title = "Windows Game Mode";
            info.description =
                "Windows počas hry obmedzí údržbu na pozadí a uprednostní hru pri prideľovaní zdrojov.";
            info.rationale =
                "Vlastná funkcia Windows, ovládaná rovnakým prepínačom ako v Nastavenia → Hranie → "
                "Herný režim. Meníme presne tú hodnotu, ktorú zapisuje systémové nastavenie.";
            info.benefit = Benefit::Gaming;
            info.evidence = Evidence::Plausible;
            info.tradeoffs =
                "Skutočný dopad sa líši podľa zostavy a hry; na silných PC býva rozdiel malý.";
            info.changeSummary = "AutoGameModeEnabled = 1 (HKCU\\Software\\Microsoft\\GameBar)";

            std::vector<RegistryOptimization::Target> targets = {
                { "AutoGameModeEnabled",
                  { HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled" }, 1u },
            };
            catalog.push_back(std::make_unique<RegistryOptimization>(info, targets, backups));
        }

        {
            Info info;
            info.id = "gaming.game_dvr";
            info.category = Category::Gaming;
            info.title = "Vypnúť nahrávanie na pozadí";
            info.description =
                "Vypne priebežné nahrávanie Xbox Game Baru, ktoré beží aj keď ho nepoužívaš.";
            info.rationale =
                "Nahrávanie na pozadí drží kódovanie videa aktívne počas hrania. Toto je tá istá "
                "voľba ako Nastavenia → Hranie → Zachytávanie → Nahrávať, čo sa stalo.";
            info.benefit = Benefit::Gaming;
            info.evidence = Evidence::Documented;
            info.tradeoffs = "Prídeš o funkciu „Nahrať posledných X minút“.";
            info.changeSummary =
                "GameDVR_Enabled = 0 (HKCU\\System\\GameConfigStore), AppCaptureEnabled = 0";

            std::vector<RegistryOptimization::Target> targets = {
                { "GameDVR_Enabled",
                  { HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_Enabled" }, 0u },
                { "AppCaptureEnabled",
                  { HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                    L"AppCaptureEnabled" }, 0u },
            };
            catalog.push_back(std::make_unique<RegistryOptimization>(info, targets, backups));
        }

        // ---- General (perceived responsiveness) -------------------------

        {
            Info info;
            info.id = "general.client_animations";
            info.category = Category::General;
            info.title = "Vypnúť animácie okien";
            info.description =
                "Okná, menu a zoznamy sa prekreslia okamžite namiesto animovaného prechodu.";
            info.rationale =
                "Nastavuje sa dokumentovaným SystemParametersInfo (SPI_SETCLIENTAREAANIMATION) — "
                "tým istým prepínačom, ktorý ovláda Uľahčenie prístupu → Vizuálne efekty → Animácie.";
            info.benefit = Benefit::PerceivedResponsiveness;
            info.evidence = Evidence::Documented;
            info.tradeoffs =
                "Systém pôsobí svižnejšie, ale nespracuje viac práce za sekundu — nie je to nárast výkonu.";
            info.changeSummary = "SPI_SETCLIENTAREAANIMATION = FALSE";

            catalog.push_back(std::make_unique<SpiBoolOptimization>(
                info, SPI_GETCLIENTAREAANIMATION, SPI_SETCLIENTAREAANIMATION, false, backups));
        }

        {
            Info info;
            info.id = "general.ui_effects";
            info.category = Category::General;
            info.title = "Vypnúť vizuálne efekty";
            info.description =
                "Hlavný vypínač efektov rozhrania — tiene, plynulé rolovanie, prechody.";
            info.rationale =
                "Dokumentované SystemParametersInfo (SPI_SETUIEFFECTS). Zodpovedá voľbe "
                "„Upraviť na najlepší výkon“ v nastaveniach výkonu Windows.";
            info.benefit = Benefit::PerceivedResponsiveness;
            info.evidence = Evidence::Documented;
            info.tradeoffs = "Rozhranie vyzerá výrazne strohejšie. Opäť ide o dojem, nie o priepustnosť.";
            info.changeSummary = "SPI_SETUIEFFECTS = FALSE";

            catalog.push_back(std::make_unique<SpiBoolOptimization>(
                info, SPI_GETUIEFFECTS, SPI_SETUIEFFECTS, false, backups));
        }

        {
            Info info;
            info.id = "general.transparency";
            info.category = Category::General;
            info.title = "Vypnúť priehľadnosť";
            info.description =
                "Vypne efekt priehľadnosti v ponuke Štart, na paneli úloh a v centre oznámení.";
            info.rationale =
                "Tá istá hodnota, ktorú prepína Nastavenia → Prispôsobenie → Farby → Efekty priehľadnosti. "
                "Rozostrenie sa počíta na GPU, takže na slabších integrovaných GPU je efekt citeľnejší.";
            info.benefit = Benefit::PerceivedResponsiveness;
            info.evidence = Evidence::Plausible;
            info.tradeoffs = "Vzhľad systému bude plochší.";
            info.changeSummary =
                "EnableTransparency = 0 "
                "(HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize)";

            std::vector<RegistryOptimization::Target> targets = {
                { "EnableTransparency",
                  { HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    L"EnableTransparency" }, 0u },
            };
            catalog.push_back(std::make_unique<RegistryOptimization>(info, targets, backups));
        }

        // ---- Privacy / convenience --------------------------------------

        {
            Info info;
            info.id = "privacy.tips_suggestions";
            info.category = Category::Privacy;
            info.title = "Vypnúť tipy a návrhy";
            info.description =
                "Windows prestane zobrazovať tipy, triky a odporúčania obsahu.";
            info.rationale =
                "Vypína odber obsahu v ContentDeliveryManager — to isté, čo Nastavenia → Systém → "
                "Oznámenia → Získať tipy a návrhy.";
            info.benefit = Benefit::Convenience;
            info.evidence = Evidence::Documented;
            info.tradeoffs =
                "Toto je voľba pohodlia a súkromia, nie výkonu — nečakaj od nej rýchlejší systém.";
            info.changeSummary =
                "SubscribedContent-338389Enabled = 0 (ContentDeliveryManager)";

            std::vector<RegistryOptimization::Target> targets = {
                { "SubscribedContent-338389Enabled",
                  { HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",
                    L"SubscribedContent-338389Enabled" }, 0u },
            };
            catalog.push_back(std::make_unique<RegistryOptimization>(info, targets, backups));
        }

        {
            Info info;
            info.id = "privacy.advertising_id";
            info.category = Category::Privacy;
            info.title = "Vypnúť reklamné ID";
            info.description =
                "Aplikácie nebudú môcť použiť tvoje reklamné ID na personalizáciu reklám.";
            info.rationale =
                "Zodpovedá Nastavenia → Súkromie a zabezpečenie → Všeobecné → Nechať aplikácie "
                "používať moje reklamné ID.";
            info.benefit = Benefit::Privacy;
            info.evidence = Evidence::Documented;
            info.tradeoffs = "Reklám neubudne, len prestanú byť personalizované. Na výkon vplyv nemá.";
            info.changeSummary = "Enabled = 0 (HKCU\\...\\AdvertisingInfo)";

            std::vector<RegistryOptimization::Target> targets = {
                { "AdvertisingInfo.Enabled",
                  { HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled" }, 0u },
            };
            catalog.push_back(std::make_unique<RegistryOptimization>(info, targets, backups));
        }

        return catalog;
    }
}
