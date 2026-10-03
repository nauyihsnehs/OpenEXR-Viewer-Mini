#include "Registration.h"
#include "ShellIds.h"
#include "ComSupport.h"
#include <sddl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

namespace {
const std::wstring Classes = L"Software\\Classes\\";
const std::wstring Association = Classes + L"SystemFileAssociations\\.exr\\shellex\\";
constexpr DWORD MaxStateSize = 2 * 1024 * 1024;

void check(LSTATUS code, const char* operation)
{
    if (code != ERROR_SUCCESS)
        throw std::runtime_error(std::string(operation) + " (Windows error " + std::to_string(code) + ").");
}
class Key {
public:
    ~Key() { if (value) RegCloseKey(value); }
    HKEY value = nullptr;
};
struct Value {
    bool exists = false;
    DWORD type = REG_NONE;
    std::vector<BYTE> data;
    bool operator==(const Value& other) const
    { return exists == other.exists && (!exists || (type == other.type && data == other.data)); }
};
struct Entry {
    unsigned feature;
    std::wstring key, name;
    Value before, written;
};
struct State {
    std::wstring owner;
    std::vector<Entry> entries;
    std::vector<std::wstring> created;
};
struct Spec { unsigned feature; std::wstring key, name; Value value; };

Value read(HKEY root, const std::wstring& path, const std::wstring& name)
{
    Key key;
    LSTATUS code = RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key.value);
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return {};
    check(code, "Cannot read registration key");
    Value result;
    DWORD size = 0;
    code = RegQueryValueExW(key.value, name.c_str(), nullptr, &result.type, nullptr, &size);
    if (code == ERROR_FILE_NOT_FOUND) return {};
    check(code, "Cannot read registration value");
    if (size > 65536) throw std::runtime_error("Registration value is too large to back up.");
    result.data.resize(size);
    check(RegQueryValueExW(key.value, name.c_str(), nullptr, &result.type, result.data.data(), &size), "Cannot read registration value");
    result.data.resize(size); result.exists = true;
    return result;
}
Value stringValue(const std::wstring& text)
{
    Value value; value.exists = true; value.type = REG_SZ;
    value.data.resize((text.size() + 1) * sizeof(wchar_t));
    std::memcpy(value.data.data(), text.c_str(), value.data.size());
    return value;
}
std::wstring asString(const Value& value)
{
    if (!value.exists || (value.type != REG_SZ && value.type != REG_EXPAND_SZ)
        || value.data.size() % 2 || value.data.size() < 2) return {};
    std::wstring result(value.data.size() / 2, L'\0');
    std::memcpy(&result[0], value.data.data(), value.data.size());
    if (result.back() != L'\0') return {};
    result.pop_back();
    return result;
}
bool equalText(const std::wstring& a, const std::wstring& b)
{ return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL; }

std::vector<Spec> specs(const std::wstring& folder)
{
    using namespace ShellRegistration;
    std::vector<Spec> result;
    const auto add = [&](unsigned feature, const std::wstring& key, const std::wstring& name, const std::wstring& text) {
        result.push_back({feature, key, name, stringValue(text)});
    };
    const std::wstring prog = Classes + ShellIds::ProgId;
    add(Icons, prog, L"", L"OpenEXR image");
    add(Icons, prog + L"\\DefaultIcon", L"", L"\"" + folder + L"\\openexr-viewer.exe\",0");
    add(Icons, prog + L"\\shell\\open\\command", L"", L"\"" + folder + L"\\openexr-viewer.exe\" \"%1\"");
    Value none; none.exists = true;
    result.push_back({Icons, Classes + L".exr\\OpenWithProgids", ShellIds::ProgId, none});
    const std::wstring capabilities = L"Software\\OpenEXRViewerMini\\Capabilities";
    add(Icons, capabilities, L"ApplicationName", L"OpenEXR Viewer Mini");
    add(Icons, capabilities, L"ApplicationDescription", L"View OpenEXR images and metadata");
    add(Icons, capabilities + L"\\FileAssociations", L".exr", ShellIds::ProgId);
    add(Icons, L"Software\\RegisteredApplications", L"OpenEXR Viewer Mini", capabilities);
    for (unsigned feature : {unsigned(Thumbnails), unsigned(Preview)}) {
        const wchar_t* clsid = feature == Thumbnails ? ShellIds::ThumbnailClsid : ShellIds::PreviewClsid;
        const wchar_t* slot = feature == Thumbnails ? ShellIds::ThumbnailSlot : ShellIds::PreviewSlot;
        const std::wstring name = feature == Thumbnails ? L"OpenEXR Viewer Mini Thumbnail" : L"OpenEXR Viewer Mini Preview";
        const std::wstring key = Classes + L"CLSID\\" + clsid;
        add(feature, key, L"", name);
        add(feature, key + L"\\InprocServer32", L"", folder + L"\\" + ShellIds::DllName);
        add(feature, key + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
        if (feature == Preview) {
            add(feature, key, L"AppID", L"{6D2B5079-2F0B-48DD-AB7F-97CEC514D30B}");
            add(feature, L"Software\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers", clsid, name);
        }
        // Install the association last, after the COM server is usable.
        add(feature, Association + slot, L"", clsid);
    }
    return result;
}

std::wstring sid()
{
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) check(GetLastError(), "Cannot identify current user");
    Handle token(raw);
    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    if (!size) check(GetLastError(), "Cannot inspect current user");
    std::vector<BYTE> bytes(size);
    if (!GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size)) check(GetLastError(), "Cannot inspect current user");
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &text)) check(GetLastError(), "Cannot identify current user");
    const std::wstring result(text); LocalFree(text); return result;
}
std::wstring statePath() { return ShellRegistration::directory() + L"\\.openexr-shell-" + sid() + L".state"; }

class Lock {
public:
    Lock() : handle(CreateMutexW(nullptr, FALSE, (L"Local\\OpenEXRViewerMini.Shell." + sid()).c_str()))
    {
        if (!handle) check(GetLastError(), "Cannot lock shell configuration");
        const DWORD result = WaitForSingleObject(handle.get(), 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
            throw std::runtime_error("Another shell configuration operation is running.");
    }
    ~Lock() { ReleaseMutex(handle.get()); }
private:
    Handle handle;
};

// Length-delimited local journal, capped and validated against our exact registry
// schema on read. It is never a general-purpose .reg file or a deletion script.
void append(std::vector<BYTE>& bytes, const void* data, size_t size)
{
    if (size > MaxStateSize || bytes.size() > MaxStateSize - size) throw std::runtime_error("Shell rollback journal is too large.");
    if (!size) return;
    const auto* begin = static_cast<const BYTE*>(data); bytes.insert(bytes.end(), begin, begin + size);
}
void number(std::vector<BYTE>& bytes, uint32_t n) { append(bytes, &n, sizeof(n)); }
void string(std::vector<BYTE>& bytes, const std::wstring& value)
{ number(bytes, uint32_t(value.size())); append(bytes, value.data(), value.size() * sizeof(wchar_t)); }
void value(std::vector<BYTE>& bytes, const Value& v)
{ number(bytes, v.exists); number(bytes, v.type); number(bytes, uint32_t(v.data.size())); append(bytes, v.data.data(), v.data.size()); }

class Reader {
public:
    explicit Reader(const std::vector<BYTE>& data) : data(data) {}
    uint32_t number() { uint32_t n; take(&n, 4); return n; }
    std::wstring string()
    {
        const auto count = number();
        if (count > 32768) throw std::runtime_error("Invalid rollback journal string.");
        std::wstring s(count, L'\0'); if (count) take(&s[0], count * 2);
        if (s.find(L'\0') != std::wstring::npos) throw std::runtime_error("Invalid rollback journal path.");
        return s;
    }
    Value value()
    {
        Value v; const auto exists = number();
        if (exists > 1) throw std::runtime_error("Invalid rollback journal value.");
        v.exists = exists != 0; v.type = number(); const auto size = number();
        if (size > 65536) throw std::runtime_error("Invalid rollback journal value size.");
        v.data.resize(size); take(v.data.data(), size); return v;
    }
    bool done() const { return position == data.size(); }
private:
    void take(void* out, size_t size)
    {
        if (size > data.size() - position) throw std::runtime_error("Truncated shell rollback journal.");
        if (size) std::memcpy(out, data.data() + position, size);
        position += size;
    }
    const std::vector<BYTE>& data;
    size_t position = 0;
};

bool parentOf(const std::wstring& parent, const std::wstring& child)
{ return child == parent || (child.size() > parent.size() && child.compare(0, parent.size(), parent) == 0 && child[parent.size()] == L'\\'); }

State load()
{
    State state; state.owner = ShellRegistration::directory();
    const auto path = statePath();
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return state;
        check(GetLastError(), "Cannot read shell rollback journal");
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file.get(), &size)) check(GetLastError(), "Cannot read rollback journal size");
    if (size.QuadPart < 0 || size.QuadPart > MaxStateSize) throw std::runtime_error("Invalid shell rollback journal size.");
    std::vector<BYTE> bytes(size_t(size.QuadPart)); DWORD received;
    if (!ReadFile(file.get(), bytes.data(), DWORD(bytes.size()), &received, nullptr) || received != bytes.size())
        throw std::runtime_error("Cannot read the entire shell rollback journal.");
    Reader r(bytes);
    if (r.number() != 0x31525845) throw std::runtime_error("Unrecognized shell rollback journal.");
    state.owner = r.string();
    if (state.owner.empty() || state.owner.find(L'"') != std::wstring::npos)
        throw std::runtime_error("Invalid shell rollback journal owner.");
    const auto allowed = specs(state.owner);
    const auto count = r.number();
    if (count > allowed.size()) throw std::runtime_error("Invalid rollback journal entries.");
    for (uint32_t i = 0; i < count; ++i) {
        Entry e; e.feature = r.number(); e.key = r.string(); e.name = r.string(); e.before = r.value(); e.written = r.value();
        const auto match = std::find_if(allowed.begin(), allowed.end(), [&](const Spec& spec) {
            return spec.feature == e.feature && spec.key == e.key && spec.name == e.name && spec.value == e.written;
        });
        if (match == allowed.end() || std::any_of(state.entries.begin(), state.entries.end(), [&](const Entry& other) {
            return other.key == e.key && other.name == e.name;
        })) throw std::runtime_error("Rollback journal contains an unexpected registry target.");
        state.entries.push_back(std::move(e));
    }
    const auto created = r.number();
    if (created > 128) throw std::runtime_error("Invalid rollback journal key count.");
    for (uint32_t i = 0; i < created; ++i) {
        auto parentPath = r.string();
        if (parentPath.size() <= 9 || std::none_of(allowed.begin(), allowed.end(), [&](const Spec& s) { return parentOf(parentPath, s.key); }))
            throw std::runtime_error("Rollback journal contains an unexpected parent key.");
        state.created.push_back(std::move(parentPath));
    }
    if (!r.done()) throw std::runtime_error("Unexpected data in shell rollback journal.");
    return state;
}

void save(const State& state)
{
    const auto path = statePath();
    if (state.entries.empty() && state.created.empty()) {
        if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) check(GetLastError(), "Cannot remove shell rollback journal");
        return;
    }
    std::vector<BYTE> bytes;
    number(bytes, 0x31525845); string(bytes, state.owner); number(bytes, uint32_t(state.entries.size()));
    for (const auto& e : state.entries) { number(bytes, e.feature); string(bytes, e.key); string(bytes, e.name); value(bytes, e.before); value(bytes, e.written); }
    number(bytes, uint32_t(state.created.size())); for (const auto& key : state.created) string(bytes, key);
    const auto temporary = path + L".tmp";
    try {
        {
            Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_WRITE_THROUGH, nullptr));
            if (!file) check(GetLastError(), "Program directory must be writable to save rollback information");
            DWORD written = 0;
            if (!WriteFile(file.get(), bytes.data(), DWORD(bytes.size()), &written, nullptr) || written != bytes.size())
                throw std::runtime_error("Cannot save the entire shell rollback journal.");
            if (!FlushFileBuffers(file.get())) check(GetLastError(), "Cannot flush shell rollback journal");
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            check(GetLastError(), "Cannot commit shell rollback journal");
    } catch (...) { DeleteFileW(temporary.c_str()); throw; }
}

void write(const std::wstring& path, const std::wstring& name, const Value& v)
{
    Key key;
    if (v.exists) {
        check(RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key.value, nullptr), "Cannot create registration key");
        check(RegSetValueExW(key.value, name.c_str(), 0, v.type, v.data.data(), DWORD(v.data.size())), "Cannot write registration value");
    } else {
        const auto code = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key.value);
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return;
        check(code, "Cannot open registration key for rollback");
        const auto removed = RegDeleteValueW(key.value, name.c_str());
        if (removed != ERROR_FILE_NOT_FOUND) check(removed, "Cannot remove registration value");
    }
}

void rememberMissingKeys(State& state, const std::wstring& path)
{
    for (size_t end = path.find(L'\\'); ; end = path.find(L'\\', end + 1)) {
        const auto parent = path.substr(0, end);
        Key key;
        const auto code = RegOpenKeyExW(HKEY_CURRENT_USER, parent.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key.value);
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            if (parent.size() > 9 && std::find(state.created.begin(), state.created.end(), parent) == state.created.end())
                state.created.push_back(parent);
        } else check(code, "Cannot inspect registration parent");
        if (end == std::wstring::npos) break;
    }
}

void prune(State& state)
{
    std::sort(state.created.begin(), state.created.end(), [](const std::wstring& a, const std::wstring& b) { return a.size() > b.size(); });
    for (size_t i = 0; i < state.created.size();) {
        const auto path = state.created[i];
        if (std::any_of(state.entries.begin(), state.entries.end(), [&](const Entry& e) { return parentOf(path, e.key); })) { ++i; continue; }
        {
            Key key;
            auto code = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_WOW64_64KEY, &key.value);
            if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
                check(code, "Cannot inspect an empty registration key");
                DWORD subkeys = 0, values = 0;
                check(RegQueryInfoKeyW(key.value, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr), "Cannot inspect registration key contents");
                if (!subkeys && !values) {
                    code = RegDeleteKeyExW(HKEY_CURRENT_USER, path.c_str(), KEY_WOW64_64KEY, 0);
                    if (code != ERROR_FILE_NOT_FOUND) check(code, "Cannot remove empty registration key");
                }
                // A nonempty key now belongs to another registration: leave it.
            }
        }
        state.created.erase(state.created.begin() + i);
        save(state);
    }
}

void undo(State& state, size_t first, unsigned features)
{
    // Reverse order disconnects Shell associations before removing COM entries.
    for (size_t i = state.entries.size(); i > first;) {
        --i;
        const auto& e = state.entries[i];
        if (!(e.feature & features)) continue;
        if (read(HKEY_CURRENT_USER, e.key, e.name) == e.written) write(e.key, e.name, e.before);
        state.entries.erase(state.entries.begin() + i);
        save(state);
    }
    prune(state); save(state);
}

std::wstring effectiveHandler(unsigned feature)
{
    wchar_t text[128] = {}; DWORD size = 128;
    const auto hr = AssocQueryStringW(ASSOCF_NOTRUNCATE, ASSOCSTR_SHELLEXTENSION, L".exr",
        feature == ShellRegistration::Thumbnails ? ShellIds::ThumbnailSlot : ShellIds::PreviewSlot, text, &size);
    if (SUCCEEDED(hr)) return text;
    if (hr == E_POINTER || hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
        throw std::runtime_error("An existing EXR handler could not be identified safely.");
    return {};
}

void conflicts(unsigned features, const std::wstring& folder)
{
    for (unsigned feature : {unsigned(ShellRegistration::Thumbnails), unsigned(ShellRegistration::Preview)}) {
        if (!(features & feature)) continue;
        const auto handler = effectiveHandler(feature);
        const wchar_t* clsid = feature == ShellRegistration::Thumbnails ? ShellIds::ThumbnailClsid : ShellIds::PreviewClsid;
        if (!handler.empty() && !equalText(handler, clsid))
            throw std::runtime_error("Another EXR thumbnail/preview handler is active. Disable it in its application first.");
        const auto server = asString(read(HKEY_CLASSES_ROOT, std::wstring(L"CLSID\\") + clsid + L"\\InprocServer32", L""));
        if (!server.empty() && !equalText(server, folder + L"\\" + ShellIds::DllName))
            throw std::runtime_error("Shell integration belongs to another copy of OpenEXR Viewer. Undo it from that copy first.");
        const auto slot = read(HKEY_CLASSES_ROOT, std::wstring(L"SystemFileAssociations\\.exr\\shellex\\")
            + (feature == ShellRegistration::Thumbnails ? ShellIds::ThumbnailSlot : ShellIds::PreviewSlot), L"");
        if (slot.exists && !equalText(asString(slot), clsid))
            throw std::runtime_error("Another EXR handler owns the shared association. It will not be overwritten.");
    }
    if (features & ShellRegistration::Icons) {
        const auto command = asString(read(HKEY_CLASSES_ROOT, std::wstring(ShellIds::ProgId) + L"\\shell\\open\\command", L""));
        if (!command.empty() && !equalText(command, L"\"" + folder + L"\\openexr-viewer.exe\" \"%1\""))
            throw std::runtime_error("File association belongs to another viewer location. Undo it there first.");
    }
}
void notify() { SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); }
bool dwordSet(HKEY root, const std::wstring& key, const wchar_t* name)
{
    const auto v = read(root, key, name); DWORD n = 0;
    if (v.type == REG_DWORD && v.data.size() == sizeof(n)) std::memcpy(&n, v.data.data(), sizeof(n));
    return n != 0;
}
}

std::wstring ShellRegistration::directory()
{
    std::vector<wchar_t> path(32768);
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!count || count >= path.size()) throw std::runtime_error("Cannot locate shell configuration program.");
    const std::wstring full(path.data(), count);
    const auto end = full.find_last_of(L"\\/");
    if (end == std::wstring::npos) throw std::runtime_error("Cannot locate program directory.");
    return full.substr(0, end);
}

std::wstring ShellRegistration::settingsMutexName()
{ return L"Local\\OpenEXRViewerMini.ShellSettings." + sid(); }

ShellRegistration::Status ShellRegistration::status()
{
    Lock lock;
    const auto state = load();
    Status result;
    for (const auto& e : state.entries) result.recorded |= e.feature;
    const auto expected = specs(directory());
    for (unsigned feature : {unsigned(Icons), unsigned(Thumbnails), unsigned(Preview)}) {
        bool complete = true;
        for (const auto& spec : expected) if (spec.feature == feature && !(read(HKEY_CURRENT_USER, spec.key, spec.name) == spec.value)) complete = false;
        if (complete && (result.recorded & feature)) result.enabled |= feature;
        const auto name = feature == Icons ? L"File association" : feature == Thumbnails ? L"Thumbnails" : L"Preview pane";
        result.details += std::wstring(name) + L": " + (complete ? L"registered" : (result.recorded & feature) ? L"changed or incomplete; undo is available" : L"off") + L"\r\n";
        if (feature != Icons) {
            const auto effective = effectiveHandler(feature);
            const auto ours = feature == Thumbnails ? ShellIds::ThumbnailClsid : ShellIds::PreviewClsid;
            if (!effective.empty() && !equalText(effective, ours)) result.details += L"  A different EXR handler takes priority.\r\n";
            for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
                if (read(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked", ours).exists)
                    result.details += L"  This handler is blocked by Windows settings or policy.\r\n";
        }
    }
    if (!equalText(state.owner, directory())) result.details += L"The program folder moved. Undo the old registration before enabling again.\r\n";
    const std::wstring advanced = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
    if (dwordSet(HKEY_CURRENT_USER, advanced, L"IconsOnly")) result.details += L"Explorer is configured to show icons instead of thumbnails.\r\n";
    const auto previewSetting = read(HKEY_CURRENT_USER, advanced, L"ShowPreviewHandlers");
    if (previewSetting.exists && !dwordSet(HKEY_CURRENT_USER, advanced, L"ShowPreviewHandlers")) result.details += L"Explorer preview handlers are disabled.\r\n";
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        if (dwordSet(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", L"DisableThumbnails"))
            result.details += L"Thumbnail generation is disabled by policy.\r\n";
        if (dwordSet(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", L"DisablePreviewHandlers"))
            result.details += L"Preview handlers are disabled by policy.\r\n";
    }
    result.details += L"\r\nDefault app selection belongs to Windows Settings. Alt+P opens the preview pane.\r\nInternet-marked files may be blocked by Windows.\r\nUndo all integration before moving or deleting this folder.";
    return result;
}

void ShellRegistration::enable(unsigned features)
{
    if (!features || (features & ~All)) throw std::runtime_error("Invalid shell feature selection.");
    Lock lock;
    auto state = load();
    const auto folder = directory();
    if (!equalText(state.owner, folder)) throw std::runtime_error("Program folder moved. Undo previous integration before enabling it here.");
    conflicts(features, folder);
    for (const auto* file : {L"openexr-viewer.exe", ShellIds::DllName}) {
        const auto attributes = GetFileAttributesW((folder + L"\\" + file).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
            throw std::runtime_error("The viewer and shell DLL must be next to this configuration program.");
    }
    const size_t first = state.entries.size();
    try {
        for (const auto& spec : specs(folder)) {
            if (!(spec.feature & features)) continue;
            const auto current = read(HKEY_CURRENT_USER, spec.key, spec.name);
            const auto old = std::find_if(state.entries.begin(), state.entries.end(), [&](const Entry& e) { return e.key == spec.key && e.name == spec.name; });
            if (old != state.entries.end()) {
                if (!(current == old->written)) throw std::runtime_error("Registration changed since it was enabled. Undo it before enabling again.");
                continue;
            }
            if (current == spec.value) throw std::runtime_error("Existing registration has no rollback record. Use the original installation to undo it.");
            rememberMissingKeys(state, spec.key);
            state.entries.push_back({spec.feature, spec.key, spec.name, current, spec.value});
            save(state); // Must succeed before changing this registry value.
            write(spec.key, spec.name, spec.value);
        }
    } catch (...) {
        const auto failure = std::current_exception();
        // Reload the committed journal: a failed save must never authorize undoing
        // a registry value that was not actually written by this transaction.
        try { auto committed = load(); undo(committed, first, features); }
        catch (...) { notify(); throw std::runtime_error("Registration failed and rollback is incomplete. Keep this folder and use Undo all to retry."); }
        notify(); std::rethrow_exception(failure);
    }
    notify();
}

void ShellRegistration::disable(unsigned features)
{
    if (!features || (features & ~All)) throw std::runtime_error("Invalid shell feature selection.");
    Lock lock;
    auto state = load();
    unsigned recorded = 0;
    for (const auto& entry : state.entries) recorded |= entry.feature;
    for (const auto& spec : specs(directory())) {
        if (!(spec.feature & features) || (spec.feature & recorded)) continue;
        const bool anchor = spec.key.find(L"\\InprocServer32") != std::wstring::npos
            || spec.key.find(L"\\shell\\open\\command") != std::wstring::npos;
        if (anchor && spec.name.empty() && read(HKEY_CURRENT_USER, spec.key, spec.name) == spec.value)
            throw std::runtime_error("An active registration has no rollback journal. Keep the original program folder and recover its .state file before uninstalling.");
    }
    try { undo(state, 0, features); }
    catch (...) { notify(); throw; }
    notify();
}

bool ShellRegistration::filesAvailable()
{
    Handle settings(OpenMutexW(SYNCHRONIZE, FALSE, settingsMutexName().c_str()));
    if (settings) return false;
    const auto folder = directory();
    // Open DELETE access without deleting anything. Image mappings deny this
    // while a Shell host still has the DLL loaded.
    for (const auto* name : {ShellIds::DllName, L"openexr-viewer.exe"}) {
        const auto path = folder + L"\\" + name;
        Handle file(CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file && GetLastError() != ERROR_FILE_NOT_FOUND) return false;
    }
    return true;
}
