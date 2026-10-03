#pragma once

#include <windows.h>
#include <winioctl.h>
#include "..\hypervision\workspace\common\common.h"

class hv_client_t 
{
public:
    auto connect() -> bool;
    auto disconnect() -> void;

    auto query_status(hv_status* out) -> bool;
    auto sandbox_page(void* address, bool protect) -> bool;

private:
    HANDLE device_ = INVALID_HANDLE_VALUE;
};