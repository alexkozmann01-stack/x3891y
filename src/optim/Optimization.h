#pragma once

#include <string>
#include <vector>
#include <optional>

// The contract every optimization implements. The point of routing
// everything through one interface is that the UI never needs to know how a
// given setting is stored, and every setting gets the same guarantees:
// its original value is captured before any write, applying is verified by
// reading the value back, and restoring puts back exactly what was there —
// including "this value did not exist".
namespace optim
{
    enum class Category
    {
        General,   // animations, transparency, notifications
        Gaming,    // Game Mode, capture
        Privacy,   // telemetry-adjacent preferences
        Startup,
        Power,
        Network,
        Storage,
    };

    // What a setting actually buys you. Kept separate from Category so the
    // UI can be honest: a cosmetic preference is not a throughput win.
    enum class Benefit
    {
        PerceivedResponsiveness, // feels snappier; does not raise throughput
        Throughput,              // measurably more work per second
        Startup,
        Storage,
        Gaming,
        Privacy,
        Convenience,
    };

    // How well-supported the claim behind a setting is. Shown on the card so
    // a user can tell a documented OS switch from folklore.
    enum class Evidence
    {
        Documented,  // Microsoft documents the setting and what it does
        Plausible,   // mechanism is understood, effect varies by machine
        Anecdotal,   // widely repeated, not substantiated — we don't ship these
    };

    // Whether *this machine* should change the setting. Deliberately not a
    // property of the setting alone: the same registry value can be the right
    // call on a 4-core laptop and pointless on a desktop that is already
    // fast. Computed in Catalog.cpp from SystemInventory plus the current
    // read, never assigned just because an option exists.
    enum class Classification
    {
        Recommended,   // hardware/OS/current state say this machine benefits
        Situational,   // real effect, but only under conditions we spell out
        Advanced,      // narrow or experimental; opt-in, never pre-selected
        Informational, // we cannot change it safely — manual guide only
    };

    enum class State
    {
        Unknown,        // not read yet
        Applied,        // our optimized value is in place
        NotApplied,     // system is at its own/default value
        Unsupported,    // not available on this machine/OS build
        PendingRestart, // written, but won't take effect until restart
        Failed,         // last operation failed; see lastError
        Manual,         // real setting, but we deliberately don't write it —
                        // the card links to where Windows exposes it
    };

    struct Error
    {
        enum class Code
        {
            None,
            AccessDenied,
            NotSupported,
            ReadFailed,
            WriteFailed,
            VerifyMismatch, // wrote successfully but read back something else
            NoBackup,
        };

        Code code = Code::None;
        std::string message;   // human-readable, shown in the UI
        long systemError = 0;  // GetLastError()/LSTATUS where applicable

        bool ok() const { return code == Code::None; }
        static Error Ok() { return {}; }
        static Error Make(Code c, std::string msg, long sys = 0)
        {
            Error e;
            e.code = c;
            e.message = std::move(msg);
            e.systemError = sys;
            return e;
        }
    };

    struct Info
    {
        std::string id;       // stable; used as the backup-journal key
        Category category = Category::General;
        std::string title;
        std::string description; // plain language, one or two sentences
        std::string rationale;   // why this does anything, and the source
        Benefit benefit = Benefit::PerceivedResponsiveness;
        Evidence evidence = Evidence::Documented;
        std::string tradeoffs;   // what the user gives up; empty if none
        bool requiresAdmin = false;
        bool requiresRestart = false;
        // What "applied" concretely changes, shown in the details pane and
        // in the profile preview before anything is written.
        std::string changeSummary;

        // Filled in per machine when the catalog is built. `classification`
        // decides which tab a card appears under; `classificationReason` is
        // shown verbatim so the user can see *why* it landed there
        // ("8 GB RAM detected", "no battery — desktop") rather than trusting
        // a bare label.
        Classification classification = Classification::Situational;
        std::string classificationReason;
    };

    // A read of the live system, plus whatever went wrong reading it.
    struct Status
    {
        State state = State::Unknown;
        Error lastError;
        std::string detail; // e.g. the actual current value, for the details pane
    };

    class Optimization
    {
    public:
        virtual ~Optimization() = default;

        virtual const Info& info() const = 0;

        // Cheap enough to call on a scan; must not block on network or disk
        // beyond a registry/API read.
        virtual Status Read() const = 0;

        // Captures the current value into the backup store (idempotent — an
        // existing backup is never overwritten, so the *original* value
        // survives repeated applies), writes the optimized value, then reads
        // it back and only reports success if it matches.
        virtual Error Apply() = 0;

        // Puts back exactly what the backup recorded, then verifies.
        virtual Error Restore() = 0;

        // True if this machine/OS build supports the setting at all.
        virtual bool Supported() const { return true; }
    };
}
