#pragma once

#include <string>

namespace ShellRegistration {
enum Feature : unsigned { Icons = 1, Thumbnails = 2, Preview = 4, All = 7 };
struct Status {
    unsigned enabled = 0;
    unsigned recorded = 0;
    std::wstring details;
};
std::wstring directory();
std::wstring settingsMutexName();
Status status();
// Throws std::exception on failure. Every completed registry write is journaled.
void enable(unsigned features);
void disable(unsigned features);
// Used before upgrade/uninstall. Does not load the DLL or terminate its hosts.
bool filesAvailable();
}
