// Tests for the parts of the optimization framework where a mistake is
// expensive: the backup must survive repeated applies, restoring a value
// that did not originally exist must delete it rather than write a default,
// and a verified failure must never be reported as success.
//
// These run against a scratch key under HKCU (deleted on the way out), so
// they exercise the real registry code paths rather than a mock. No admin
// rights and nothing outside the test key is touched.
//
// Built as a separate executable: cmake --build build --target NasakiTests

#include "../src/optim/RegistryValue.h"
#include "../src/optim/RegistryOptimization.h"
#include "../src/optim/BackupStore.h"

#include <windows.h>
#include <cstdio>
#include <string>

namespace
{
    int g_failures = 0;
    int g_checks = 0;

    void Check(bool condition, const char* what)
    {
        g_checks++;
        if (!condition)
        {
            g_failures++;
            std::printf("  FAIL  %s\n", what);
        }
        else
        {
            std::printf("  ok    %s\n", what);
        }
    }

    const wchar_t* kTestKey = L"Software\\NasakiTests\\Scratch";

    optim::RegPath TestPath(const wchar_t* valueName)
    {
        return { HKEY_CURRENT_USER, kTestKey, valueName };
    }

    void DeleteTestKey()
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\NasakiTests");
    }

    optim::Info MakeInfo(const char* id)
    {
        optim::Info info;
        info.id = id;
        info.title = "Test";
        info.description = "Test";
        return info;
    }

    // A value that does not exist beforehand must come back as deleted, not
    // as a zero — writing a "default" is how tweak tools silently change
    // behaviour they were asked to restore.
    void TestRestoreDeletesValueThatNeverExisted(optim::BackupStore& backups)
    {
        std::printf("restore deletes a value that never existed\n");
        DeleteTestKey();

        std::vector<optim::RegistryOptimization::Target> targets = {
            { "flag", TestPath(L"Flag"), 1u },
        };
        // The key must exist for the optimization to consider itself
        // supported, but the value must not.
        optim::reg::WriteDword(TestPath(L"Unrelated"), 7u);

        optim::RegistryOptimization opt(MakeInfo("test.absent"), targets, &backups);

        optim::Status before = opt.Read();
        Check(before.state == optim::State::NotApplied, "starts NotApplied when the value is absent");

        optim::Error applyError = opt.Apply();
        Check(applyError.ok(), "apply succeeds");
        Check(opt.Read().state == optim::State::Applied, "reads back as Applied");

        optim::Error restoreError = opt.Restore();
        Check(restoreError.ok(), "restore succeeds");

        optim::RegSnapshot after = optim::reg::Read(TestPath(L"Flag"));
        Check(!after.existed, "value is deleted again, not written as 0");
    }

    // Applying twice must not overwrite the recorded original with our own
    // value, or rollback becomes a no-op.
    void TestBackupSurvivesRepeatedApply(optim::BackupStore& backups)
    {
        std::printf("backup keeps the first original across repeated applies\n");
        DeleteTestKey();
        optim::reg::WriteDword(TestPath(L"Mode"), 3u); // the user's original value

        std::vector<optim::RegistryOptimization::Target> targets = {
            { "mode", TestPath(L"Mode"), 1u },
        };
        optim::RegistryOptimization opt(MakeInfo("test.repeat"), targets, &backups);

        Check(opt.Apply().ok(), "first apply succeeds");
        Check(opt.Apply().ok(), "second apply succeeds");

        std::optional<optim::RegSnapshot> stored = backups.Find("test.repeat", "mode");
        Check(stored.has_value(), "a backup exists");
        Check(stored.has_value() && stored->existed && stored->dword == 3u,
              "backup still holds the pre-Nasaki value (3), not our applied value");

        Check(opt.Restore().ok(), "restore succeeds");
        optim::RegSnapshot after = optim::reg::Read(TestPath(L"Mode"));
        Check(after.existed && after.dword == 3u, "original value 3 is back");
    }

    // Restoring with nothing recorded must report that clearly instead of
    // guessing at a value.
    void TestRestoreWithoutBackupFails(optim::BackupStore& backups)
    {
        std::printf("restore without a backup reports NoBackup\n");
        DeleteTestKey();
        optim::reg::WriteDword(TestPath(L"Untouched"), 5u);

        std::vector<optim::RegistryOptimization::Target> targets = {
            { "untouched", TestPath(L"Untouched"), 1u },
        };
        optim::RegistryOptimization opt(MakeInfo("test.nobackup"), targets, &backups);

        optim::Error error = opt.Restore();
        Check(!error.ok(), "restore fails");
        Check(error.code == optim::Error::Code::NoBackup, "error code is NoBackup");

        optim::RegSnapshot after = optim::reg::Read(TestPath(L"Untouched"));
        Check(after.existed && after.dword == 5u, "the value was left alone");
    }

    // An optimization whose key does not exist at all must report
    // Unsupported and refuse to write.
    void TestUnsupportedIsNotApplied(optim::BackupStore& backups)
    {
        std::printf("unsupported setting refuses to apply\n");
        DeleteTestKey();

        std::vector<optim::RegistryOptimization::Target> targets = {
            { "ghost", { HKEY_CURRENT_USER, L"Software\\NasakiTests\\DoesNotExist", L"Ghost" }, 1u },
        };
        optim::RegistryOptimization opt(MakeInfo("test.unsupported"), targets, &backups);

        Check(opt.Read().state == optim::State::Unsupported, "reads as Unsupported");

        optim::Error error = opt.Apply();
        Check(!error.ok(), "apply fails");
        Check(error.code == optim::Error::Code::NotSupported, "error code is NotSupported");

        optim::RegSnapshot after = optim::reg::Read(
            { HKEY_CURRENT_USER, L"Software\\NasakiTests\\DoesNotExist", L"Ghost" });
        Check(!after.existed, "nothing was written");
    }

    // A multi-value optimization is only "Applied" when every value matches;
    // one value drifting back must show as NotApplied, not a false success.
    void TestPartialStateIsNotReportedAsApplied(optim::BackupStore& backups)
    {
        std::printf("partial application does not read as Applied\n");
        DeleteTestKey();
        optim::reg::WriteDword(TestPath(L"Seed"), 1u);

        std::vector<optim::RegistryOptimization::Target> targets = {
            { "a", TestPath(L"A"), 1u },
            { "b", TestPath(L"B"), 1u },
        };
        optim::RegistryOptimization opt(MakeInfo("test.partial"), targets, &backups);

        Check(opt.Apply().ok(), "apply succeeds for both values");
        Check(opt.Read().state == optim::State::Applied, "both values applied");

        // Simulate something else changing one of them back.
        optim::reg::WriteDword(TestPath(L"B"), 0u);
        Check(opt.Read().state == optim::State::NotApplied,
              "one value drifting makes the whole optimization NotApplied");

        Check(opt.Restore().ok(), "restore succeeds");
        Check(!optim::reg::Read(TestPath(L"A")).existed, "A deleted (never existed originally)");
        Check(!optim::reg::Read(TestPath(L"B")).existed, "B deleted (never existed originally)");
    }

    // The REG_SZ kind must round-trip text exactly, and restoring must put
    // the original string back rather than an empty value.
    void TestStringOptimizationApplyAndRestore(optim::BackupStore& backups)
    {
        std::printf("REG_SZ optimization applies and rolls back to the original text\n");
        DeleteTestKey();

        // The user's original setting, as Windows would store it.
        optim::reg::WriteString(TestPath(L"MenuShowDelay"), L"400");

        std::vector<optim::RegistryStringOptimization::Target> targets = {
            { "MenuShowDelay", TestPath(L"MenuShowDelay"), L"0" },
        };
        optim::RegistryStringOptimization opt(MakeInfo("test.string"), targets, &backups);

        Check(opt.Read().state == optim::State::NotApplied, "starts NotApplied at \"400\"");
        Check(opt.Apply().ok(), "apply succeeds");
        Check(opt.Read().state == optim::State::Applied, "reads back as Applied");

        optim::RegSnapshot applied = optim::reg::Read(TestPath(L"MenuShowDelay"));
        Check(applied.type == REG_SZ, "still stored as REG_SZ, not converted to a DWORD");
        Check(optim::reg::SnapshotAsString(applied) == L"0", "value is the string \"0\"");

        Check(opt.Restore().ok(), "restore succeeds");
        optim::RegSnapshot after = optim::reg::Read(TestPath(L"MenuShowDelay"));
        Check(after.existed && optim::reg::SnapshotAsString(after) == L"400",
              "original string \"400\" is back");
    }

    // A string value whose text differs must not be reported as applied just
    // because the value exists.
    void TestStringMismatchIsNotApplied(optim::BackupStore& backups)
    {
        std::printf("REG_SZ optimization compares text, not mere existence\n");
        DeleteTestKey();
        optim::reg::WriteString(TestPath(L"Delay"), L"0");

        std::vector<optim::RegistryStringOptimization::Target> targets = {
            { "Delay", TestPath(L"Delay"), L"0" },
        };
        optim::RegistryStringOptimization opt(MakeInfo("test.string.match"), targets, &backups);
        Check(opt.Read().state == optim::State::Applied, "matching text reads as Applied");

        optim::reg::WriteString(TestPath(L"Delay"), L"00");
        Check(opt.Read().state == optim::State::NotApplied,
              "\"00\" is not \"0\" — not reported as Applied");
    }

    // A build gate must show as Unsupported and refuse to write even though
    // the key itself is present.
    void TestSupportCheckBlocksApply(optim::BackupStore& backups)
    {
        std::printf("extra support check blocks apply even when the key exists\n");
        DeleteTestKey();
        optim::reg::WriteDword(TestPath(L"Seed"), 1u);

        std::vector<optim::RegistryOptimization::Target> targets = {
            { "gated", TestPath(L"Gated"), 1u },
        };
        optim::RegistryOptimization opt(MakeInfo("test.gated"), targets, &backups,
                                        [] { return false; });

        Check(opt.Read().state == optim::State::Unsupported,
              "reads as Unsupported despite the key existing");
        optim::Error error = opt.Apply();
        Check(!error.ok() && error.code == optim::Error::Code::NotSupported, "apply refused");
        Check(!optim::reg::Read(TestPath(L"Gated")).existed, "nothing was written");
    }

    // Snapshot round-trip for a non-DWORD type: restoring must put the exact
    // original bytes and type back.
    void TestSnapshotPreservesTypeAndBytes()
    {
        std::printf("snapshot preserves a REG_SZ value exactly\n");
        DeleteTestKey();

        HKEY key = nullptr;
        RegCreateKeyExW(HKEY_CURRENT_USER, kTestKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        const wchar_t* original = L"hello world";
        RegSetValueExW(key, L"Text", 0, REG_SZ, reinterpret_cast<const BYTE*>(original),
            (DWORD)((wcslen(original) + 1) * sizeof(wchar_t)));
        RegCloseKey(key);

        optim::RegSnapshot snapshot = optim::reg::Read(TestPath(L"Text"));
        Check(snapshot.existed && snapshot.type == REG_SZ, "captured as REG_SZ");

        optim::reg::WriteDword(TestPath(L"Text"), 42u); // clobber with a different type
        Check(optim::reg::Read(TestPath(L"Text")).type == REG_DWORD, "clobbered to REG_DWORD");

        Check(optim::reg::WriteSnapshot(TestPath(L"Text"), snapshot), "snapshot written back");
        optim::RegSnapshot restored = optim::reg::Read(TestPath(L"Text"));
        Check(restored == snapshot, "type and bytes match the original exactly");
    }
}

int main()
{
    std::printf("Nasaki optimization framework tests\n");
    std::printf("(uses a scratch key under HKCU\\Software\\NasakiTests)\n\n");

    // Redirected to a temp file so the tests exercise the real save path
    // without touching the user's actual %APPDATA% journal.
    optim::BackupStore backups;
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring journal = std::wstring(tempDir) + L"nasaki-tests-backups.json";
    DeleteFileW(journal.c_str());
    backups.SetStorePathForTesting(journal);

    TestRestoreDeletesValueThatNeverExisted(backups);
    TestBackupSurvivesRepeatedApply(backups);
    TestRestoreWithoutBackupFails(backups);
    TestUnsupportedIsNotApplied(backups);
    TestPartialStateIsNotReportedAsApplied(backups);
    TestStringOptimizationApplyAndRestore(backups);
    TestStringMismatchIsNotApplied(backups);
    TestSupportCheckBlocksApply(backups);
    TestSnapshotPreservesTypeAndBytes();

    DeleteTestKey();
    DeleteFileW(journal.c_str());

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
