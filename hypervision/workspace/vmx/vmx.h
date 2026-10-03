#pragma once

#include <ntddk.h>
#include <intrin.h>
#include "../common/common.h"

// thanks to vmcall for structs & defs

constexpr unsigned long msr_ia32_feature_control    = 0x03A;
constexpr unsigned long msr_ia32_sysenter_cs        = 0x174;
constexpr unsigned long msr_ia32_sysenter_esp       = 0x175;
constexpr unsigned long msr_ia32_sysenter_eip       = 0x176;
constexpr unsigned long msr_ia32_debugctl           = 0x1D9;
constexpr unsigned long msr_ia32_pat                = 0x277;
constexpr unsigned long msr_ia32_efer               = 0xC0000080;
constexpr unsigned long msr_ia32_fs_base            = 0xC0000100;
constexpr unsigned long msr_ia32_gs_base            = 0xC0000101;

constexpr unsigned long msr_vmx_basic               = 0x480;
constexpr unsigned long msr_vmx_pinbased_ctls       = 0x481;
constexpr unsigned long msr_vmx_procbased_ctls      = 0x482;
constexpr unsigned long msr_vmx_exit_ctls           = 0x483;
constexpr unsigned long msr_vmx_entry_ctls          = 0x484;
constexpr unsigned long msr_vmx_misc                = 0x485;
constexpr unsigned long msr_vmx_cr0_fixed0          = 0x486;
constexpr unsigned long msr_vmx_cr0_fixed1          = 0x487;
constexpr unsigned long msr_vmx_cr4_fixed0          = 0x488;
constexpr unsigned long msr_vmx_cr4_fixed1          = 0x489;
constexpr unsigned long msr_vmx_procbased_ctls2     = 0x48B;
constexpr unsigned long msr_vmx_ept_vpid_cap        = 0x48C;
constexpr unsigned long msr_vmx_true_pinbased_ctls  = 0x48D;
constexpr unsigned long msr_vmx_true_procbased_ctls = 0x48E;
constexpr unsigned long msr_vmx_true_exit_ctls      = 0x48F;
constexpr unsigned long msr_vmx_true_entry_ctls     = 0x490;

// 16-bit guest-state
constexpr unsigned long long vmx_guest_es_selector   = 0x0800;
constexpr unsigned long long vmx_guest_cs_selector   = 0x0802;
constexpr unsigned long long vmx_guest_ss_selector   = 0x0804;
constexpr unsigned long long vmx_guest_ds_selector   = 0x0806;
constexpr unsigned long long vmx_guest_fs_selector   = 0x0808;
constexpr unsigned long long vmx_guest_gs_selector   = 0x080A;
constexpr unsigned long long vmx_guest_ldtr_selector = 0x080C;
constexpr unsigned long long vmx_guest_tr_selector   = 0x080E;

// 16-bit host-state
constexpr unsigned long long vmx_host_es_selector    = 0x0C00;
constexpr unsigned long long vmx_host_cs_selector    = 0x0C02;
constexpr unsigned long long vmx_host_ss_selector    = 0x0C04;
constexpr unsigned long long vmx_host_ds_selector    = 0x0C06;
constexpr unsigned long long vmx_host_fs_selector    = 0x0C08;
constexpr unsigned long long vmx_host_gs_selector    = 0x0C0A;
constexpr unsigned long long vmx_host_tr_selector    = 0x0C0C;

// 64-bit control fields
constexpr unsigned long long vmx_msr_bitmap_addr     = 0x2004;
constexpr unsigned long long vmx_ept_pointer         = 0x201A;

// 64-bit read-only data
constexpr unsigned long long vmx_guest_physical_addr = 0x2400;

// 64-bit guest-state
constexpr unsigned long long vmx_vmcs_link_ptr       = 0x2800;
constexpr unsigned long long vmx_guest_debugctl      = 0x2802;
constexpr unsigned long long vmx_guest_pat           = 0x2804;
constexpr unsigned long long vmx_guest_efer          = 0x2806;

// 64-bit host-state
constexpr unsigned long long vmx_host_pat            = 0x2C00;
constexpr unsigned long long vmx_host_efer           = 0x2C02;
constexpr unsigned long long vmx_host_fs_base        = 0x2C06;
constexpr unsigned long long vmx_host_gs_base        = 0x2C08;

// 32-bit control fields
constexpr unsigned long long vmx_pin_based_ctls      = 0x4000;
constexpr unsigned long long vmx_proc_based_ctls     = 0x4002;
constexpr unsigned long long vmx_exception_bitmap    = 0x4004;
constexpr unsigned long long vmx_pf_error_mask       = 0x4006;
constexpr unsigned long long vmx_pf_error_match      = 0x4008;
constexpr unsigned long long vmx_cr3_target_count    = 0x400A;
constexpr unsigned long long vmx_exit_ctls           = 0x400C;
constexpr unsigned long long vmx_entry_ctls          = 0x4012;
constexpr unsigned long long vmx_entry_intr_info     = 0x4016;
constexpr unsigned long long vmx_tpr_threshold       = 0x401C;
constexpr unsigned long long vmx_secondary_proc_ctls = 0x401E;

// 32-bit read-only data
constexpr unsigned long long vmx_exit_reason         = 0x4402;
constexpr unsigned long long vmx_exit_instr_len      = 0x440C;

// 32-bit guest-state
constexpr unsigned long long vmx_guest_es_limit      = 0x4800;
constexpr unsigned long long vmx_guest_cs_limit      = 0x4802;
constexpr unsigned long long vmx_guest_ss_limit      = 0x4804;
constexpr unsigned long long vmx_guest_ds_limit      = 0x4806;
constexpr unsigned long long vmx_guest_fs_limit      = 0x4808;
constexpr unsigned long long vmx_guest_gs_limit      = 0x480A;
constexpr unsigned long long vmx_guest_ldtr_limit    = 0x480C;
constexpr unsigned long long vmx_guest_tr_limit      = 0x480E;
constexpr unsigned long long vmx_guest_es_ar         = 0x4814;
constexpr unsigned long long vmx_guest_cs_ar         = 0x4816;
constexpr unsigned long long vmx_guest_ss_ar         = 0x4818;
constexpr unsigned long long vmx_guest_ds_ar         = 0x481A;
constexpr unsigned long long vmx_guest_fs_ar         = 0x481C;
constexpr unsigned long long vmx_guest_gs_ar         = 0x481E;
constexpr unsigned long long vmx_guest_ldtr_ar       = 0x4820;
constexpr unsigned long long vmx_guest_tr_ar         = 0x4822;
constexpr unsigned long long vmx_guest_interruptibility = 0x4824;
constexpr unsigned long long vmx_guest_activity_state   = 0x4826;
constexpr unsigned long long vmx_guest_smbase           = 0x4828;
constexpr unsigned long long vmx_guest_sysenter_cs      = 0x482A;
constexpr unsigned long long vmx_host_sysenter_cs       = 0x4C00;

// natural-width control fields
constexpr unsigned long long vmx_cr0_guest_host_mask = 0x6000;
constexpr unsigned long long vmx_cr4_guest_host_mask = 0x6002;
constexpr unsigned long long vmx_cr0_read_shadow     = 0x6004;
constexpr unsigned long long vmx_cr4_read_shadow     = 0x6006;
constexpr unsigned long long vmx_exit_qualification  = 0x6400;

// natural-width guest-state
constexpr unsigned long long vmx_guest_cr0           = 0x6800;
constexpr unsigned long long vmx_guest_cr3           = 0x6802;
constexpr unsigned long long vmx_guest_cr4           = 0x6804;
constexpr unsigned long long vmx_guest_dr7           = 0x6806;
constexpr unsigned long long vmx_guest_cs_base       = 0x6808;
constexpr unsigned long long vmx_guest_ds_base       = 0x680A;
constexpr unsigned long long vmx_guest_es_base       = 0x680C;
constexpr unsigned long long vmx_guest_ss_base       = 0x680E;
constexpr unsigned long long vmx_guest_fs_base       = 0x6810;
constexpr unsigned long long vmx_guest_gs_base       = 0x6812;
constexpr unsigned long long vmx_guest_rsp           = 0x6814;
constexpr unsigned long long vmx_guest_rip           = 0x6816;
constexpr unsigned long long vmx_guest_ldtr_base     = 0x6818;
constexpr unsigned long long vmx_guest_tr_base       = 0x681A;
constexpr unsigned long long vmx_guest_rflags        = 0x6820;
constexpr unsigned long long vmx_guest_pending_debug = 0x6822;
constexpr unsigned long long vmx_guest_sysenter_esp  = 0x6824;
constexpr unsigned long long vmx_guest_sysenter_eip  = 0x6826;

// natural-width host-state
constexpr unsigned long long vmx_host_cr0            = 0x6C00;
constexpr unsigned long long vmx_host_cr3            = 0x6C02;
constexpr unsigned long long vmx_host_cr4            = 0x6C04;
constexpr unsigned long long vmx_host_gdtr_base      = 0x6C06;
constexpr unsigned long long vmx_host_idtr_base      = 0x6C08;
constexpr unsigned long long vmx_host_tr_base        = 0x6C0A;
constexpr unsigned long long vmx_host_sysenter_esp   = 0x6C10;
constexpr unsigned long long vmx_host_sysenter_eip   = 0x6C12;
constexpr unsigned long long vmx_host_rsp            = 0x6C14;
constexpr unsigned long long vmx_host_rip            = 0x6C16;

constexpr unsigned long long vmx_proc_use_msr_bitmaps    = 1ULL << 28;
constexpr unsigned long long vmx_proc_activate_secondary = 1ULL << 31;

constexpr unsigned long long vmx_proc2_enable_ept        = 1ULL << 1;
constexpr unsigned long long vmx_proc2_enable_rdtscp     = 1ULL << 3;
constexpr unsigned long long vmx_proc2_enable_invpcid    = 1ULL << 12;
constexpr unsigned long long vmx_proc2_enable_xsaves     = 1ULL << 20;

constexpr unsigned long long vmx_exit_host_addr_space    = 1ULL << 9;
constexpr unsigned long long vmx_exit_save_debug_ctls    = 1ULL << 2;

constexpr unsigned long long vmx_entry_ia32e_guest       = 1ULL << 9;
constexpr unsigned long long vmx_entry_load_debug_ctls   = 1ULL << 2;
constexpr unsigned long long vmx_entry_load_ia32_efer    = 1ULL << 15;

constexpr unsigned long long cr0_host_mask = (1ULL << 0) | (1ULL << 31); // PE | PG
constexpr unsigned long long cr4_host_mask = (1ULL << 13);               // VMXE
constexpr unsigned long long cr4_vmxe      = (1ULL << 13);

constexpr unsigned long long vmx_exit_cpuid          = 10;
constexpr unsigned long long vmx_exit_invd           = 13;
constexpr unsigned long long vmx_exit_vmcall         = 18;
constexpr unsigned long long vmx_exit_vmxon          = 27; // 19..27: guest VMX instructions
constexpr unsigned long long vmx_exit_cr_access      = 28;
constexpr unsigned long long vmx_exit_msr_read       = 31;
constexpr unsigned long long vmx_exit_msr_write      = 32;
constexpr unsigned long long vmx_exit_ept_violation  = 49;
constexpr unsigned long long vmx_exit_invept         = 51;
constexpr unsigned long long vmx_exit_invvid         = 52;
constexpr unsigned long long vmx_exit_xsetbv         = 55;

constexpr unsigned long long ept_flag_read      = 0x01;
constexpr unsigned long long ept_flag_write     = 0x02;
constexpr unsigned long long ept_flag_supv_exec = 0x04; // must be 1 unless mode-based execute control
constexpr unsigned long long ept_flag_exec      = 0x08;
constexpr unsigned long long ept_flag_large     = 0x80; // 2 MB leaf
constexpr unsigned long long ept_memtype_wb     = 6;

constexpr unsigned long long ept_leaf_flags_rwx_wb = ept_flag_read | ept_flag_write | ept_flag_supv_exec | ept_flag_exec | ept_flag_large | (ept_memtype_wb << 5);
constexpr unsigned long long ept_table_flags = ept_flag_read | ept_flag_write | ept_flag_supv_exec | ept_flag_exec;

struct vmx_context 
{
    unsigned long long guest_registers[16];   // 0x000: rax rcx rdx rbx rsp rbp rsi rdi r8..r15

    unsigned long long guest_rip;             // 0x080
    unsigned long long guest_rsp;             // 0x088
    unsigned long long guest_rflags;          // 0x090
    unsigned long long guest_gs_base;         // 0x098
    unsigned long long guest_cs_selector;     // 0x0A0
    unsigned long long guest_ss_selector;     // 0x0A8
    unsigned long long fatal_reason;          // 0x0B0
    unsigned long long fatal_qualification;   // 0x0B8

    unsigned long long self_pointer;          // 0x0C0 == (unsigned long long)this
    unsigned long long host_gs_base;          // 0x0C8 == (unsigned long long)&self_pointer

    unsigned        processor_index;
    volatile long   launched;                 // 1 = in VMX
    volatile long   stop_requested;

    unsigned long long vmxon_pa;
    unsigned long long vmcs_pa;
    unsigned long long msr_bitmap_pa;
    void*              host_stack;

    unsigned long long cr0_shadow;
    unsigned long long cr4_shadow;
    unsigned long long cr4_fixed1;

    unsigned long long vmcs_pin_ctls;
    unsigned long long vmcs_proc_ctls;
    unsigned long long vmcs_proc2_ctls;
    unsigned long long vmcs_exit_ctls;
    unsigned long long vmcs_entry_ctls;
    unsigned long long vmcs_eptp;

    unsigned long long ept_pml4_pa;
    void*              ept_pml4_va;

    struct ept_pa_va 
    {
        unsigned long long pa;
        void*              va;
    };

    ept_pa_va          ept_tables[512];       // pa -> va reverse map
    volatile long      ept_table_count;
    void*              ept_spare_va[8];
    volatile long      ept_spare_index;

    unsigned long long exit_counts[hv_exit_reason_count];
    unsigned long long ept_runtime_maps;
    unsigned long long ept_page_splits;
    unsigned long long sandbox_trapped_writes;
};

static_assert(offsetof(vmx_context, guest_registers)      == 0x000);
static_assert(offsetof(vmx_context, guest_rip)            == 0x080);
static_assert(offsetof(vmx_context, guest_rsp)            == 0x088);
static_assert(offsetof(vmx_context, guest_rflags)         == 0x090);
static_assert(offsetof(vmx_context, guest_gs_base)        == 0x098);
static_assert(offsetof(vmx_context, guest_cs_selector)    == 0x0A0);
static_assert(offsetof(vmx_context, guest_ss_selector)    == 0x0A8);
static_assert(offsetof(vmx_context, fatal_reason)         == 0x0B0);
static_assert(offsetof(vmx_context, fatal_qualification)  == 0x0B8);
static_assert(offsetof(vmx_context, self_pointer)         == 0x0C0);
static_assert(offsetof(vmx_context, host_gs_base)         == 0x0C8);

enum gpr_index : unsigned 
{
    gpr_rax = 0, gpr_rcx, gpr_rdx, gpr_rbx,
    gpr_rsp, gpr_rbp, gpr_rsi, gpr_rdi,
    gpr_r8, gpr_r9, gpr_r10, gpr_r11,
    gpr_r12, gpr_r13, gpr_r14, gpr_r15,
};

extern "C"
{
    auto asm_vm_launch(vmx_context* ctx) -> void;        // never returns on success
    auto asm_vm_guest_resume() -> void;                  // guest entry point
    auto asm_vm_exit_handler() -> void;                  // host RIP
    auto asm_xsetbv(unsigned long long xcr, unsigned long long value) -> void;
    auto asm_invept(unsigned long long type, void* descriptor) -> unsigned long long;
    auto asm_capture_segments(void* out6x32) -> void;
    auto asm_get_tr() -> unsigned short;
    auto asm_get_gdtr(void* out10) -> void;
    auto asm_get_idtr(void* out10) -> void;

    auto hv_launch_finalize(vmx_context* ctx) -> NTSTATUS;
    auto hv_handle_exit(vmx_context* ctx) -> unsigned long long;
}

FORCEINLINE auto vmx_read(unsigned long long field) -> unsigned long long
{
    unsigned long long value = 0;
    __vmx_vmread(field, &value);
    return value;
}

FORCEINLINE auto vmx_write(unsigned long long field, unsigned long long value) -> bool
{
    return __vmx_vmwrite(field, value) == 0;
}

class vmx_t
{
public:
    auto initialize() -> NTSTATUS; 
    auto cleanup() -> void;
    auto start_all_cpus() -> bool; 
    auto stop_all_cpus() -> void;

    auto initialize_cpu(vmx_context* ctx) -> NTSTATUS; 
    auto prepare_vmx_off(vmx_context* ctx) -> void;  
    auto launch_finalize(vmx_context* ctx) -> NTSTATUS;  

    auto contexts() -> vmx_context* { return contexts_; }
    auto cpu_count() const -> unsigned { return cpu_count_; }
    auto vmx_supported() const -> bool { return vmx_supported_; }

private:
    auto check_vmx_support() -> bool;
    auto adjust_controls(unsigned long long desired, unsigned long legacy_msr,
                         unsigned long true_msr) -> unsigned long long;
    auto capture_guest_state(vmx_context* ctx, void* gdtr) -> NTSTATUS;
    auto setup_vmcs_controls(vmx_context* ctx) -> NTSTATUS;
    auto setup_vmcs_host_state(vmx_context* ctx, void* gdtr, void* idtr) -> NTSTATUS;

    vmx_context* contexts_;
    unsigned     cpu_count_;
    bool         vmx_supported_;
};

extern vmx_t* vmx_m;
