#pragma once

#include <ntddk.h>
#include "../common/common.h"

struct vmx_context;

// 31-8 SDM table
constexpr unsigned long long invept_all_contexts = 2;

class memory_t {
public:
    auto initialize() -> NTSTATUS;
    auto cleanup() -> void;

    auto allocate_spare_pages(vmx_context* ctx) -> NTSTATUS;
    auto build_identity_map(vmx_context* ctx) -> NTSTATUS;
    auto map_2mb(vmx_context* ctx, unsigned long long gpa, bool runtime) -> bool;
    auto sandbox_page(vmx_context* ctx, unsigned long long gpa, bool protect) -> NTSTATUS;
    auto handle_violation(vmx_context* ctx, unsigned long long gpa) -> bool;

    auto invept_all() const -> void;

    auto alloc_aligned(size_t size) -> void*;
    auto free_all() -> void;

private:
    struct pa_va 
    {
        unsigned long long pa;
        void*              va;
    };

    auto register_page(vmx_context* ctx, unsigned long long pa, void* va) -> bool;
    auto pa_to_va(vmx_context* ctx, unsigned long long pa) const -> void*;
    auto alloc_table(vmx_context* ctx, unsigned long long* pa, void** va, bool runtime) -> bool;
    auto split_2mb(vmx_context* ctx, unsigned long long gpa_2mb) -> bool;
    auto walk_leaf_4kb(vmx_context* ctx, unsigned long long gpa, unsigned long long** leaf_out) -> bool;
    auto walk_leaf_4kb_runtime(vmx_context* ctx, unsigned long long gpa, unsigned long long** leaf_out) -> bool;

    PHYSICAL_MEMORY_RANGE phys_ranges_[64];
    unsigned              phys_range_count_;
    unsigned long long    max_physical_address_;

    unsigned              alloc_count_;
    void*    alloc_originals_[1024];
};

extern memory_t* memory_m;