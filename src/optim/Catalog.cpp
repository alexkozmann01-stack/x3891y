#include "Catalog.h"
#include "RegistryOptimization.h"
#include "ManualGuide.h"

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
        case Category::Storage: return "Úložisko";
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

    const char* ClassificationLabel(Classification classification)
    {
        switch (classification)
        {
        case Classification::Recommended:   return "Odporúčané pre tento počítač";
        case Classification::Situational:   return "Podľa situácie";
        case Classification::Advanced:      return "Pokročilé";
        case Classification::Informational: return "Návod";
        }
        return "";
    }

    namespace
    {
        // Small helper so every entry states, in one place, both what it was
        // classified as and the machine fact that decided it.
        void Classify(Info& info, Classification classification, std::string reason)
        {
            info.classification = classification;
            info.classificationReason = std::move(reason);
        }

        // A shared judgement used by several entries: this machine is
        // resource-constrained enough that shedding UI/background work is a
        // real win rather than a matter of taste.
        bool ResourceConstrained(const SystemInventory& inv)
        {
            return inv.LowMemory() || inv.FewCores() || !inv.gpuLikelyDiscrete;
        }
    }

    std::vector<std::unique_ptr<Optimization>> BuildCatalog(BackupStore* backups,
                                                            const SystemInventory& inv)
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

            if (inv.FewCores() || inv.LowMemory())
            {
                Classify(info, Classification::Recommended,
                    "Game Mode pomáha najviac tam, kde sa o zdroje súperí — táto zostava má "
                    + std::to_string(inv.logicalProcessors) + " logických jadier a "
                    + FormatBytes(inv.totalPhysicalBytes) + " RAM.");
            }
            else
            {
                Classify(info, Classification::Situational,
                    "Zostava má dosť jadier aj pamäte, takže rozdiel býva malý. Zapni, ak ti "
                    "hry padajú na výkone pri práci na pozadí.");
            }

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

            // Machine-independent: the background encoder runs whether or not
            // you ever use the clips, so turning it off costs nothing unless
            // you actually record.
            Classify(info, Classification::Recommended,
                "Nahrávanie na pozadí beží stále, aj keď ho nepoužívaš. Nechaj zapnuté iba ak "
                "naozaj používaš „Nahrať posledných X minút“.");

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

            if (ResourceConstrained(inv))
            {
                Classify(info, Classification::Recommended,
                    inv.gpuLikelyDiscrete
                        ? "Zostava je skromnejšia — animácie sú tu najviac cítiť."
                        : "Grafika je integrovaná, takže animácie kreslí to isté GPU, ktoré počíta hru.");
            }
            else
            {
                Classify(info, Classification::Situational,
                    "Tento počítač animácie zvládne bez problémov — je to už len otázka vkusu.");
            }

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

            if (ResourceConstrained(inv))
            {
                Classify(info, Classification::Recommended,
                    "Na tejto zostave sa vypnutie efektov prejaví na odozve rozhrania.");
            }
            else
            {
                Classify(info, Classification::Situational,
                    "Výkonná zostava — zapni len ak chceš maximálne strohé rozhranie.");
            }

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

            if (!inv.gpuLikelyDiscrete)
            {
                Classify(info, Classification::Recommended,
                    "Rozostrenie sa počíta na GPU a toto je integrovaná grafika"
                    + std::string(inv.gpuName.empty() ? "" : " (" + inv.gpuName + ")") + ".");
            }
            else
            {
                Classify(info, Classification::Situational,
                    "Samostatné GPU efekt priehľadnosti ani nezaznamená — čisto vec vzhľadu.");
            }

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

            Classify(info, Classification::Situational,
                "Voľba pohodlia a súkromia, nie výkonu — zaraď sa podľa toho, či ťa tipy rušia.");

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

            Classify(info, Classification::Situational,
                "Voľba súkromia bez vplyvu na výkon.");

            std::vector<RegistryOptimization::Target> targets = {
                { "AdvertisingInfo.Enabled",
                  { HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled" }, 0u },
            };
            catalog.push_back(std::make_unique<RegistryOptimization>(info, targets, backups));
        }

        {
            Info info;
            info.id = "privacy.start_suggestions";
            info.category = Category::Privacy;
            info.title = "Vypnúť návrhy v ponuke Štart";
            info.description =
                "Ponuka Štart prestane navrhovať aplikácie, ktoré nemáš nainštalované.";
            info.rationale =
                "Ten istý ContentDeliveryManager ako pri tipoch, iný odber obsahu. Zodpovedá "
                "Nastavenia → Prispôsobenie → Štart → Zobrazovať odporúčania.";
            info.benefit = Benefit::Convenience;
            info.evidence = Evidence::Documented;
            info.tradeoffs = "Neovplyvní výkon, len obsah ponuky Štart.";
            info.changeSummary = "SubscribedContent-338388Enabled = 0 (ContentDeliveryManager)";
            Classify(info, Classification::Situational,
                "Voľba pohodlia — nič sa nezrýchli, len ubudne navrhovaný obsah.");

            std::vector<RegistryOptimization::Target> targets = {
                { "SubscribedContent-338388Enabled",
                  { HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",
                    L"SubscribedContent-338388Enabled" }, 0u },
            };
            catalog.push_back(std::make_unique<RegistryOptimization>(info, targets, backups));
        }

        // ---- Responsiveness ---------------------------------------------

        {
            Info info;
            info.id = "general.menu_delay";
            info.category = Category::General;
            info.title = "Zrýchliť otváranie ponúk";
            info.description =
                "Podponuky sa rozbalia okamžite namiesto predvoleného oneskorenia 400 ms.";
            info.rationale =
                "MenuShowDelay je dokumentovaná hodnota v Control Panel\\Desktop, ktorú USER32 "
                "číta pri prihlásení. Ide o skutočné oneskorenie zabudované do rozhrania, nie "
                "o „zrýchlenie systému“.";
            info.benefit = Benefit::PerceivedResponsiveness;
            info.evidence = Evidence::Documented;
            info.tradeoffs =
                "Ponuky sa môžu rozbaliť aj keď kurzorom len prechádzaš. Prejaví sa až po "
                "odhlásení a prihlásení.";
            info.requiresRestart = true;
            info.changeSummary = "MenuShowDelay = \"0\" (HKCU\\Control Panel\\Desktop, pôvodne \"400\")";
            Classify(info, Classification::Situational,
                "Mení dojem z ovládania, nie výkon. Vhodné, ak ti prekáža pauza pri rozbaľovaní ponúk.");

            std::vector<RegistryStringOptimization::Target> targets = {
                { "MenuShowDelay",
                  { HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"MenuShowDelay" }, L"0" },
            };
            catalog.push_back(std::make_unique<RegistryStringOptimization>(info, targets, backups));
        }

        {
            Info info;
            info.id = "general.taskbar_widgets";
            info.category = Category::General;
            info.title = "Odstrániť Widgety z panela úloh";
            info.description =
                "Skryje tlačidlo Widgety. Widgety bežia na webovom komponente, ktorý si drží "
                "vlastné procesy a pamäť.";
            info.rationale =
                "TaskbarDa je hodnota, ktorú prepína samotné Nastavenia → Prispôsobenie → Panel "
                "úloh. Widgety hostí WebView2, takže po vypnutí ubudnú procesy na pozadí.";
            info.benefit = Benefit::PerceivedResponsiveness;
            info.evidence = Evidence::Plausible;
            info.tradeoffs =
                "Prídeš o počasie a novinky na paneli úloh. Prejaví sa po reštarte Prieskumníka "
                "alebo prihlásení.";
            info.requiresRestart = true;
            info.changeSummary = "TaskbarDa = 0 (HKCU\\...\\Explorer\\Advanced)";

            if (inv.LowMemory() || inv.FewCores())
            {
                Classify(info, Classification::Recommended,
                    "Zostava má " + FormatBytes(inv.totalPhysicalBytes) +
                    " RAM — procesy Widgetov na pozadí sú tu citeľné.");
            }
            else
            {
                Classify(info, Classification::Situational,
                    "Dosť pamäte na to, aby Widgety neprekážali — vypni ich, ak ich nepoužívaš.");
            }

            std::vector<RegistryOptimization::Target> targets = {
                { "TaskbarDa",
                  { HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                    L"TaskbarDa" }, 0u },
            };
            // The parent key exists on Windows 10 too, where this value does
            // nothing — so gate on the build that introduced Widgets.
            uint32_t build = inv.osBuild;
            catalog.push_back(std::make_unique<RegistryOptimization>(
                info, targets, backups, [build] { return build >= 22000; }));
        }

        // ---- Background work --------------------------------------------

        {
            Info info;
            info.id = "startup.background_apps";
            info.category = Category::Startup;
            info.title = "Obmedziť aplikácie na pozadí";
            info.description =
                "Aplikácie z Microsoft Store nebudú spúšťať úlohy na pozadí, keď ich nemáš otvorené.";
            info.rationale =
                "GlobalUserDisabled je hodnota, ktorou Windows sám vypína úlohy na pozadí pre "
                "balíčkované aplikácie. Týka sa výlučne aplikácií Store — klasické programy, "
                "služby ani ovládače nezastavuje.";
            info.benefit = Benefit::Throughput;
            info.evidence = Evidence::Plausible;
            info.tradeoffs =
                "Aplikácie Store prestanú doručovať oznámenia a aktualizovať dlaždice, kým ich "
                "neotvoríš. Nemá vplyv na Poštu ani Kalendár, ak ich máš otvorené.";
            info.changeSummary =
                "GlobalUserDisabled = 1 (HKCU\\...\\BackgroundAccessApplications)";

            if (inv.LowMemory() || inv.FewCores())
            {
                Classify(info, Classification::Recommended,
                    "Pri " + FormatBytes(inv.totalPhysicalBytes) + " RAM a " +
                    std::to_string(inv.logicalProcessors) +
                    " logických jadrách sa práca na pozadí prejaví najviac.");
            }
            else
            {
                Classify(info, Classification::Situational,
                    "Zostava prácu na pozadí unesie. Zapni, ak chceš mať istotu, že počas hrania "
                    "nič nezačne bežať.");
            }

            std::vector<RegistryOptimization::Target> targets = {
                { "GlobalUserDisabled",
                  { HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications",
                    L"GlobalUserDisabled" }, 1u },
            };
            catalog.push_back(std::make_unique<RegistryOptimization>(info, targets, backups));
        }

        // ---- Storage -----------------------------------------------------

        {
            Info info;
            info.id = "storage.storage_sense";
            info.category = Category::Storage;
            info.title = "Zapnúť Snímač úložiska";
            info.description =
                "Windows bude sám mazať dočasné súbory a dávno vyprázdnený Kôš.";
            info.rationale =
                "Snímač úložiska je zabudovaná funkcia Windows. Zapíname presne tú hodnotu, ktorú "
                "prepína Nastavenia → Systém → Ukladací priestor. Uvoľňovanie robí Windows sám — "
                "Nasaki nič nemaže a nepoužíva žiadny „čistič registry“.";
            info.benefit = Benefit::Storage;
            info.evidence = Evidence::Documented;
            info.tradeoffs =
                "Dočasné súbory aplikácií môžu zmiznúť skôr, než ich aplikácia znovu použije. "
                "Osobných súborov sa to netýka.";
            info.changeSummary = "StoragePolicy\\01 = 1 (HKCU\\...\\StorageSense\\Parameters)";

            if (inv.SystemDriveLowOnSpace())
            {
                Classify(info, Classification::Recommended,
                    "Systémový disk je takmer plný — Windows potrebuje voľné miesto na "
                    "aktualizácie aj stránkovací súbor.");
            }
            else
            {
                Classify(info, Classification::Situational,
                    "Na systémovom disku je zatiaľ dosť miesta; zapni ako prevenciu.");
            }

            std::vector<RegistryOptimization::Target> targets = {
                { "StoragePolicy.01",
                  { HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\StorageSense\\Parameters\\StoragePolicy",
                    L"01" }, 1u },
            };
            catalog.push_back(std::make_unique<RegistryOptimization>(info, targets, backups));
        }

        // ---- Informational: real settings we deliberately do not write ----

        {
            // HAGS lives in HKLM, needs elevation and a reboot, and whether it
            // helps or hurts depends on the GPU driver. Writing it from a
            // user-level tool would be both a permission lie and an
            // unsubstantiated claim, so this entry only reads and links out.
            Info info;
            info.id = "gaming.hags";
            info.category = Category::Gaming;
            info.title = "Hardvérovo akcelerované plánovanie GPU";
            info.description =
                "Presúva plánovanie práce GPU na samotnú grafickú kartu. Môže mierne znížiť "
                "latenciu — ale na niektorých ovládačoch aj uškodí.";
            info.rationale =
                "Nastavenie je systémové (HKLM), vyžaduje správcu aj reštart a jeho účinok závisí "
                "od ovládača GPU. Nasaki ho preto nemení — ukáže aktuálny stav a otvorí správnu "
                "stránku Nastavení.";
            info.benefit = Benefit::Gaming;
            info.evidence = Evidence::Plausible;
            info.tradeoffs =
                "Zmena vyžaduje reštart. Ak po zapnutí klesne plynulosť, vráť ju späť na tom "
                "istom mieste.";
            info.requiresAdmin = true;
            info.requiresRestart = true;
            info.changeSummary = "Nasaki nemení nič — otvorí Nastavenia → Zobrazenie → Grafika.";

            ManualGuideOptimization::Probe probe;
            probe.path = { HKEY_LOCAL_MACHINE,
                           L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode" };
            probe.describeValue = [](const std::optional<uint32_t>& value) -> std::string {
                if (!value.has_value()) return "Aktuálny stav: neznámy (ovládač hodnotu neuvádza).";
                if (*value == 2) return "Aktuálny stav: zapnuté.";
                if (*value == 1) return "Aktuálny stav: vypnuté.";
                return "Aktuálny stav: nerozpoznaná hodnota " + std::to_string(*value) + ".";
            };

            catalog.push_back(std::make_unique<ManualGuideOptimization>(
                info, L"ms-settings:display-advancedgraphics", probe));
        }

        {
            Info info;
            info.id = "gaming.gpu_preference";
            info.category = Category::Gaming;
            info.title = "Preferované GPU pre hru";
            info.description =
                "Windows vie priradiť konkrétnej hre výkonné GPU namiesto úsporného.";
            info.rationale =
                "Priradenie je per-aplikácia a Windows si ho ukladá vo vlastnom formáte, ktorý "
                "nie je dokumentovaný na zápis tretími stranami. Namiesto nespoľahlivého zápisu "
                "otvoríme priamo stránku, kde sa to nastavuje.";
            info.benefit = Benefit::Gaming;
            info.evidence = Evidence::Documented;
            info.tradeoffs = "Nastavuje sa pre každú hru zvlášť.";
            info.changeSummary = "Nasaki nemení nič — otvorí Nastavenia → Zobrazenie → Grafika.";

            if (inv.isLaptop && inv.gpuLikelyDiscrete)
            {
                info.classificationReason =
                    "Notebook so samostatným GPU — práve tu sa stáva, že hra beží na "
                    "integrovanej grafike.";
            }
            else
            {
                info.classificationReason =
                    "Týka sa hlavne zostáv s dvoma GPU; na tomto počítači ide skôr o informáciu.";
            }

            catalog.push_back(std::make_unique<ManualGuideOptimization>(
                info, L"ms-settings:display-advancedgraphics"));
        }

        if (inv.DisplayBelowItsRefresh())
        {
            // Only offered when we measured it: the panel advertises a higher
            // refresh than the one in use. No claim is made otherwise.
            Info info;
            info.id = "display.refresh_rate";
            info.category = Category::General;
            info.title = "Displej beží pod svojou frekvenciou";
            info.description =
                "Monitor podporuje " + std::to_string(inv.displayMaxRefreshHz) +
                " Hz, ale práve beží na " + std::to_string(inv.displayRefreshHz) + " Hz.";
            info.rationale =
                "Zistené priamo z režimov, ktoré displej hlási systému (EnumDisplaySettings) pri "
                "aktuálnom rozlíšení. Zo všetkého v tomto zozname má práve toto najväčší a "
                "najistejší dopad na plynulosť.";
            info.benefit = Benefit::Gaming;
            info.evidence = Evidence::Documented;
            info.tradeoffs =
                "Na notebooku vyššia frekvencia spotrebuje viac batérie. Zmena rozlíšenia a "
                "frekvencie patrí systému, preto ju otvoríme v Nastaveniach.";
            info.changeSummary = "Nasaki nemení nič — otvorí Nastavenia → Zobrazenie → Rozšírené.";
            info.classificationReason =
                "Namerané na tomto displeji: " + std::to_string(inv.displayRefreshHz) + " Hz z " +
                std::to_string(inv.displayMaxRefreshHz) + " Hz.";

            catalog.push_back(std::make_unique<ManualGuideOptimization>(
                info, L"ms-settings:display-advanced"));
        }

        return catalog;
    }
}
