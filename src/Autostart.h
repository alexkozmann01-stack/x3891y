#pragma once

// "Spustiť s Windows" — a value under
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run, which is the
// per-user, no-admin-required way to do this. Failures are silent: this is
// a convenience toggle, not something worth failing the app over.
namespace Autostart
{
    bool IsEnabled();
    void SetEnabled(bool enabled);
}
