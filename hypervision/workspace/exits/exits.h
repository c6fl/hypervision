#pragma once

#include <ntddk.h>
#include "../vmx/vmx.h"

class exits_t {
public:
    auto on_exit(vmx_context* ctx) -> unsigned long long;

private:
    auto advance_rip(vmx_context* ctx, unsigned long long length) -> void;
    auto inject_exception(vmx_context* ctx, unsigned vector) -> void;

    auto handle_cpuid_exit(vmx_context* ctx) -> void;
    auto handle_cr_access_exit(vmx_context* ctx) -> void;
    auto handle_xsetbv_exit(vmx_context* ctx) -> void;
    auto handle_ept_violation_exit(vmx_context* ctx) -> unsigned long long;
};

extern exits_t* exits_m;