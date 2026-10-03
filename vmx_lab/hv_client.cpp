#include "hv_client.h"

auto hv_client_t::connect() -> bool
{
    device_ = CreateFileW(HV_DOS_DEVICE, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    return device_ != INVALID_HANDLE_VALUE;
}

auto hv_client_t::disconnect() -> void
{
    if (device_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(device_);
        device_ = INVALID_HANDLE_VALUE;
    }
}

auto hv_client_t::query_status(hv_status* out) -> bool
{
    DWORD bytes = 0;
    RtlZeroMemory(out, sizeof(*out));
    return DeviceIoControl(device_, hv_ioctl_get_status, nullptr, 0, out, sizeof(*out), &bytes, nullptr) != FALSE;
}

auto hv_client_t::sandbox_page(void* address, bool protect) -> bool
{
    hv_sandbox_request request{};

    request.address = address;
    request.protect = protect ? 1u : 0u;

    DWORD bytes = 0;
    return DeviceIoControl(device_, hv_ioctl_sandbox_page, &request, sizeof(request), nullptr, 0, &bytes, nullptr) != FALSE;
}