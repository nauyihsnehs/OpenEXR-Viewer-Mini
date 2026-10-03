#pragma once

#include <windows.h>
#include <utility>

template<class T> class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* p) : pointer(p) { if (pointer) pointer->AddRef(); }
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : pointer(other.detach()) {}
    ComPtr& operator=(ComPtr&& other) noexcept
    { if (this != &other) { reset(); pointer = other.detach(); } return *this; }
    T* get() const { return pointer; }
    T* operator->() const { return pointer; }
    explicit operator bool() const { return pointer != nullptr; }
    T** put() { reset(); return &pointer; }
    T* detach() { T* p = pointer; pointer = nullptr; return p; }
    void reset() { if (pointer) pointer->Release(); pointer = nullptr; }
private:
    T* pointer = nullptr;
};

class Handle {
public:
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value; }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
private:
    HANDLE value;
};
