#include "vmx.h"
#include "../memory/memory.h"

constexpr unsigned long hv_pool_tag = 'vPhV';

namespace 
{
    vmx_t g_vmx_instance;
    unsigned g_alloc_count = 0;
    void* g_alloc_originals[ 1024 ];
}

vmx_t* vmx_m = &g_vmx_instance;

auto vmx_free_all_allocations( ) -> void
{
    for ( unsigned i = 0; i < g_alloc_count; i++ )
    {
        if ( g_alloc_originals[ i ] ) 
        {
            ExFreePoolWithTag( g_alloc_originals[ i ], hv_pool_tag );
            g_alloc_originals[ i ] = nullptr;
        }
    }

    g_alloc_count = 0;
}

auto vmx_alloc_aligned( size_t size ) -> void*
{
    if ( g_alloc_count >= ARRAYSIZE( g_alloc_originals ) ) return nullptr;

    void* original = ExAllocatePoolWithTag( NonPagedPool, size + PAGE_SIZE, hv_pool_tag );
    if ( !original ) return nullptr;

    g_alloc_originals[ g_alloc_count++ ] = original;
    return ( void* )( ( ( uintptr_t )original + PAGE_SIZE - 1 ) & ~( uintptr_t )( PAGE_SIZE - 1 ) );
}

struct gdtr_t
{
    unsigned short limit;
    unsigned long long base;
};

struct segment_access
{
    unsigned long long base;
    unsigned limit;
    unsigned attr;
};

static auto get_segment_descriptor( const gdtr_t* gdtr, unsigned short selector, segment_access* out ) -> void
{
    RtlZeroMemory( out, sizeof( *out ) );

    if ( selector == 0 ) 
    {
        out->attr = 0x10000;
        return;
    }

    const unsigned char* d = ( const unsigned char* )( gdtr->base + ( selector >> 3 ) * 8ULL );

    out->base = d[ 2 ] | ( d[ 3 ] << 8 ) | ( d[ 4 ] << 16 ) | ( ( unsigned long long )d[ 7 ] << 24 );
    out->limit = d[ 0 ] | ( d[ 1 ] << 8 ) | ( ( unsigned )( d[ 6 ] & 0x0F ) << 16 );
    out->attr = d[ 5 ] | ( ( unsigned )( d[ 6 ] & 0xF0 ) << 4 );

    if ( d[ 5 ] & 0x80 ) 
    {
        if ( d[ 6 ] & 0x80 ) 
        {
            out->limit = ( out->limit << 12 ) | 0xFFF;
        }

        if ( ( d[ 5 ] & 0x10 ) == 0 )
        {
            const unsigned char type = d[ 5 ] & 0x0F;
            if ( type == 2 || type == 9 || type == 11 ) 
            {
                out->base |= ( ( unsigned long long )d[ 8 ] << 32 ) | ( ( unsigned long long )d[ 9 ] << 40 ) | ( ( unsigned long long )d[ 10 ] << 48 );
            }
        }
    }

    else 
    {
        out->attr |= 0x10000;
    }
}

auto vmx_t::capture_guest_state( vmx_context* ctx, void* gdtr_void ) -> NTSTATUS
{
    const auto* gdtr = ( const gdtr_t* )gdtr_void;

    unsigned segs[ 6 ] = {};
    asm_capture_segments( segs );

    segment_access cs, ds, es, ss, fs, gs, tr;
    get_segment_descriptor( gdtr, ( unsigned short )segs[ 0 ], &cs );
    get_segment_descriptor( gdtr, ( unsigned short )segs[ 1 ], &ds );
    get_segment_descriptor( gdtr, ( unsigned short )segs[ 2 ], &es );
    get_segment_descriptor( gdtr, ( unsigned short )segs[ 3 ], &ss );
    get_segment_descriptor( gdtr, ( unsigned short )segs[ 4 ], &fs );
    get_segment_descriptor( gdtr, ( unsigned short )segs[ 5 ], &gs );
    get_segment_descriptor( gdtr, asm_get_tr( ), &tr );

    auto w = [ ]( unsigned long long field, unsigned long long value ) -> NTSTATUS 
    {
        return vmx_write( field, value ) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    };

    NTSTATUS status;

    if ( !NT_SUCCESS( status = w( vmx_guest_cs_selector, segs[ 0 ] ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_ds_selector, segs[ 1 ] ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_es_selector, segs[ 2 ] ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_ss_selector, segs[ 3 ] ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_fs_selector, segs[ 4 ] ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_gs_selector, segs[ 5 ] ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_ldtr_selector, 0 ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_tr_selector, asm_get_tr( ) ) ) ) return status;

    struct 
    {
        unsigned long long limit;
        unsigned long long ar;
        segment_access* s;
    } 
    
    segs_out[ ] = 
    {
        { vmx_guest_cs_limit, vmx_guest_cs_ar, &cs },
        { vmx_guest_ds_limit, vmx_guest_ds_ar, &ds },
        { vmx_guest_es_limit, vmx_guest_es_ar, &es },
        { vmx_guest_ss_limit, vmx_guest_ss_ar, &ss },
        { vmx_guest_fs_limit, vmx_guest_fs_ar, &fs },
        { vmx_guest_gs_limit, vmx_guest_gs_ar, &gs },
        { vmx_guest_tr_limit, vmx_guest_tr_ar, &tr },
    };

    for ( const auto& s : segs_out )
    {
        if ( !NT_SUCCESS( status = w( s.limit, s.s->limit ) ) ) return status;
        if ( !NT_SUCCESS( status = w( s.ar, s.s->attr ) ) ) return status;
    }

    if ( !NT_SUCCESS( status = w( vmx_guest_ldtr_limit, 0 ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_ldtr_ar, 0x10000 ) ) ) return status;

    if ( !NT_SUCCESS( status = w( vmx_guest_cs_base, cs.base ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_ds_base, ds.base ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_es_base, es.base ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_ss_base, ss.base ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_fs_base, __readmsr( msr_ia32_fs_base ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_gs_base, __readmsr( msr_ia32_gs_base ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_tr_base, tr.base ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_ldtr_base, 0 ) ) ) return status;

    if ( !NT_SUCCESS( status = w( vmx_guest_cr0, __readcr0( ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_cr3, __readcr3( ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_cr4, ctx->cr4_shadow ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_dr7, 0x400 ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_rflags, 0x2 ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_debugctl, __readmsr( msr_ia32_debugctl ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_pat, __readmsr( msr_ia32_pat ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_efer, __readmsr( msr_ia32_efer ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_sysenter_cs, __readmsr( msr_ia32_sysenter_cs ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_sysenter_esp, __readmsr( msr_ia32_sysenter_esp ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_sysenter_eip, __readmsr( msr_ia32_sysenter_eip ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_activity_state, 0 ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_interruptibility, 0 ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_smbase, 0 ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_guest_pending_debug, 0 ) ) ) return status;

    return STATUS_SUCCESS;
}

auto vmx_t::adjust_controls( unsigned long long desired, unsigned long legacy_msr, unsigned long true_msr ) -> unsigned long long
{
    const unsigned long msr = ( __readmsr( msr_vmx_misc ) & ( 1ULL << 5 ) ) ? true_msr : legacy_msr;

    const unsigned long long value = __readmsr( msr );
    const unsigned long long allowed0 = value & 0xFFFFFFFF;
    const unsigned long long allowed1 = value >> 32;

    return ( allowed0 | desired ) & allowed1;
}

auto vmx_t::setup_vmcs_controls( vmx_context* ctx ) -> NTSTATUS
{
    auto w = [ ]( unsigned long long field, unsigned long long value ) -> NTSTATUS
    {
        return vmx_write( field, value ) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    };

    NTSTATUS status;

    ctx->vmcs_pin_ctls = adjust_controls( 0, msr_vmx_pinbased_ctls, msr_vmx_true_pinbased_ctls );

    if ( !NT_SUCCESS( status = w( vmx_pin_based_ctls, ctx->vmcs_pin_ctls ) ) ) return status;

    ctx->vmcs_proc_ctls = vmx_proc_use_msr_bitmaps | vmx_proc_activate_secondary;
    ctx->vmcs_proc_ctls = adjust_controls( ctx->vmcs_proc_ctls, msr_vmx_procbased_ctls, msr_vmx_true_procbased_ctls );

    if ( !NT_SUCCESS( status = w( vmx_proc_based_ctls, ctx->vmcs_proc_ctls ) ) ) return status;

    const unsigned long long proc2_allowed1 = __readmsr( msr_vmx_procbased_ctls2 ) >> 32;
    unsigned long long proc2 = vmx_proc2_enable_ept | vmx_proc2_enable_rdtscp;

    if ( proc2_allowed1 & vmx_proc2_enable_invpcid ) proc2 |= vmx_proc2_enable_invpcid;
    if ( proc2_allowed1 & vmx_proc2_enable_xsaves ) proc2 |= vmx_proc2_enable_xsaves;

    ctx->vmcs_proc2_ctls = adjust_controls( proc2, msr_vmx_procbased_ctls2, msr_vmx_procbased_ctls2 );
    if ( !NT_SUCCESS( status = w( vmx_secondary_proc_ctls, ctx->vmcs_proc2_ctls ) ) ) return status;

    ctx->vmcs_exit_ctls = adjust_controls( vmx_exit_host_addr_space | vmx_exit_save_debug_ctls, msr_vmx_exit_ctls, msr_vmx_true_exit_ctls );
    if ( !NT_SUCCESS( status = w( vmx_exit_ctls, ctx->vmcs_exit_ctls ) ) ) return status;

    ctx->vmcs_entry_ctls = adjust_controls( vmx_entry_ia32e_guest | vmx_entry_load_debug_ctls | vmx_entry_load_ia32_efer, msr_vmx_entry_ctls, msr_vmx_true_entry_ctls );
    if ( !NT_SUCCESS( status = w( vmx_entry_ctls, ctx->vmcs_entry_ctls ) ) ) return status;

    if ( !NT_SUCCESS( status = w( vmx_msr_bitmap_addr, ctx->msr_bitmap_pa ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_exception_bitmap, 0 ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_pf_error_mask, 0 ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_pf_error_match, 0 ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_cr3_target_count, 0 ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_tpr_threshold, 0 ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_vmcs_link_ptr, ~0ULL ) ) ) return status;

    ctx->vmcs_eptp = ( ctx->ept_pml4_pa & ~0xFFFULL ) | ( 3ULL << 3 ) | ept_memtype_wb;

    if ( !NT_SUCCESS( status = w( vmx_ept_pointer, ctx->vmcs_eptp ) ) ) return status;

    if ( !NT_SUCCESS( status = w( vmx_cr0_guest_host_mask, cr0_host_mask ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_cr4_guest_host_mask, cr4_host_mask ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_cr0_read_shadow, __readcr0( ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_cr4_read_shadow, ctx->cr4_shadow ) ) ) return status;

    return STATUS_SUCCESS;
}

auto vmx_t::setup_vmcs_host_state( vmx_context* ctx, void* gdtr_void, void* idtr_void ) -> NTSTATUS
{
    const auto* gdtr = ( const gdtr_t* )gdtr_void;
    const auto* idtr = ( const gdtr_t* )idtr_void;

    auto w = [ ]( unsigned long long field, unsigned long long value ) -> NTSTATUS
    {
        return vmx_write( field, value ) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    };

    NTSTATUS status;

    if ( !NT_SUCCESS( status = w( vmx_host_cr0, __readcr0( ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_cr3, __readcr3( ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_cr4, __readcr4( ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_rsp, ( unsigned long long )ctx->host_stack ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_rip, ( unsigned long long ) & asm_vm_exit_handler ) ) ) return status;

    unsigned segs[ 6 ] = {};
    asm_capture_segments( segs );

    if ( !NT_SUCCESS( status = w( vmx_host_cs_selector, segs[ 0 ] ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_ds_selector, segs[ 1 ] ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_es_selector, segs[ 2 ] ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_ss_selector, segs[ 3 ] ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_fs_selector, segs[ 4 ] ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_gs_selector, segs[ 5 ] ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_tr_selector, asm_get_tr( ) ) ) ) return status;

    if ( !NT_SUCCESS( status = w( vmx_host_fs_base, __readmsr( msr_ia32_fs_base ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_gs_base, ctx->host_gs_base ) ) ) return status;

    segment_access tr{};
    gdtr_t gdtr_copy = *gdtr;

    get_segment_descriptor( &gdtr_copy, asm_get_tr( ), &tr );

    if ( !NT_SUCCESS( status = w( vmx_host_tr_base, tr.base ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_gdtr_base, gdtr->base ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_idtr_base, idtr->base ) ) ) return status;

    if ( !NT_SUCCESS( status = w( vmx_host_sysenter_cs, __readmsr( msr_ia32_sysenter_cs ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_sysenter_esp, __readmsr( msr_ia32_sysenter_esp ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_sysenter_eip, __readmsr( msr_ia32_sysenter_eip ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_pat, __readmsr( msr_ia32_pat ) ) ) ) return status;
    if ( !NT_SUCCESS( status = w( vmx_host_efer, __readmsr( msr_ia32_efer ) ) ) ) return status;

    return STATUS_SUCCESS;
}

auto vmx_t::launch_finalize( vmx_context* ctx ) -> NTSTATUS
{
    vmx_write( vmx_guest_rsp, ctx->guest_rsp );
    vmx_write( vmx_guest_rip, ( unsigned long long ) & asm_vm_guest_resume );
    vmx_write( vmx_guest_rflags, ctx->guest_rflags );

    return STATUS_SUCCESS;
}

auto vmx_t::initialize_cpu( vmx_context* ctx ) -> NTSTATUS
{
    unsigned long long feature_control = __readmsr( msr_ia32_feature_control );

    if ( feature_control & 1 ) 
    {
        if ( !( feature_control & 4 ) )
        {
            DbgPrintEx( DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[hypervision_hv] -> feature control blocks vmx\n" );
            return STATUS_HV_FEATURE_UNAVAILABLE;
        }
    }

    else 
    {
        __writemsr( msr_ia32_feature_control, feature_control | 1 | 4 );
    }

    ctx->cr4_shadow = __readcr4( ) & ~cr4_vmxe;
    __writecr4( __readcr4( ) | cr4_vmxe );

    unsigned long long vmxon_pa = ctx->vmxon_pa;
    if ( __vmx_on( &vmxon_pa ) != 0 )
    {
        DbgPrintEx( DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[hypervision_hv] -> vmxon failed on cpu %u\n", ctx->processor_index );
        __writecr4( __readcr4( ) & ~cr4_vmxe );

        return STATUS_UNSUCCESSFUL;
    }

    const unsigned long long ept_caps = __readmsr( msr_vmx_ept_vpid_cap );
    const unsigned long long need = ( 1ULL << 6 ) | ( 1ULL << 14 ) | ( 1ULL << 16 );

    if ( ( ept_caps & need ) != need ) 
    {
        DbgPrintEx( DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[hypervision_hv] -> IMAGINE BEING ON AMD (%llx)\n", ept_caps );
        __vmx_off( );
        __writecr4( __readcr4( ) & ~cr4_vmxe );

        return STATUS_HV_FEATURE_UNAVAILABLE;
    }

    NTSTATUS status = memory_m->build_identity_map( ctx );
    if ( !NT_SUCCESS( status ) )
    {
        __vmx_off( );
        __writecr4( __readcr4( ) & ~cr4_vmxe );

        return status;
    }

    unsigned long long vmcs_pa = ctx->vmcs_pa;

    if ( __vmx_vmclear( &vmcs_pa ) != 0 || __vmx_vmptrld( &vmcs_pa ) != 0 )
    {
        __vmx_off( );
        __writecr4( __readcr4( ) & ~cr4_vmxe );

        return STATUS_UNSUCCESSFUL;
    }

    gdtr_t gdtr = {}, idtr = {};
    asm_get_gdtr( &gdtr );
    asm_get_idtr( &idtr );

    status = setup_vmcs_controls( ctx );
    if ( NT_SUCCESS( status ) ) status = capture_guest_state( ctx, &gdtr );
    if ( NT_SUCCESS( status ) ) status = setup_vmcs_host_state( ctx, &gdtr, &idtr );

    if ( !NT_SUCCESS( status ) )
    {
        __vmx_off( );
        __writecr4( __readcr4( ) & ~cr4_vmxe );

        return status;
    }

    InterlockedExchange( &ctx->launched, 1 );

    NTSTATUS launch_status = STATUS_UNSUCCESSFUL;

    __try 
    {
        asm_vm_launch( ctx );
    }

    __except ( EXCEPTION_EXECUTE_HANDLER )
    {
        launch_status = GetExceptionCode( );
    }

    if ( !NT_SUCCESS( launch_status ) ) 
    {
        InterlockedExchange( &ctx->launched, 0 );

        __vmx_off( );
        __writecr4( __readcr4( ) & ~cr4_vmxe );

        DbgPrintEx( DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[hypervision_hv] -> vmlaunch failed on cpu %u (0x%x)\n", ctx->processor_index, launch_status );

        return launch_status;
    }

    return STATUS_SUCCESS;
}

auto vmx_t::prepare_vmx_off( vmx_context* ctx ) -> void
{
    ctx->guest_rip = vmx_read( vmx_guest_rip );
    ctx->guest_rsp = vmx_read( vmx_guest_rsp );
    ctx->guest_rflags = vmx_read( vmx_guest_rflags );
    ctx->guest_cs_selector = vmx_read( vmx_guest_cs_selector );
    ctx->guest_ss_selector = vmx_read( vmx_guest_ss_selector );
    ctx->guest_gs_base = vmx_read( vmx_guest_gs_base );

    __vmx_off( );
    __writecr4( __readcr4( ) & ~cr4_vmxe );

    InterlockedExchange( &ctx->launched, 0 );
}

extern "C" VOID KeGenericCallDpc( VOID( * )( PKDPC, PVOID, PVOID, PVOID ), PVOID );

VOID hv_start_dpc_routine( PKDPC dpc, PVOID deferred_context, PVOID arg1, PVOID arg2 );
VOID hv_stop_dpc_routine( PKDPC dpc, PVOID deferred_context, PVOID arg1, PVOID arg2 );

auto vmx_t::start_all_cpus( ) -> bool
{
    KeGenericCallDpc( hv_start_dpc_routine, nullptr );
    const vmx_context* contexts = contexts_;

    for ( unsigned i = 0; i < cpu_count_; i++ )
    {
        if ( contexts[ i ].launched == 0 ) return false;
    }

    return true;
}

auto vmx_t::stop_all_cpus( ) -> void
{
    KeGenericCallDpc( hv_stop_dpc_routine, nullptr );
}

auto vmx_t::check_vmx_support( ) -> bool
{
    int info[ 4 ];
    __cpuid( info, 1 );

    if ( !( info[ 2 ] & ( 1 << 5 ) ) ) return false;

    const unsigned long long feature_control = __readmsr( msr_ia32_feature_control );
    if ( ( feature_control & 1 ) && !( feature_control & 4 ) ) return false;

    const unsigned long long cr0_fixed0 = __readmsr( msr_vmx_cr0_fixed0 );
    const unsigned long long cr0_fixed1 = __readmsr( msr_vmx_cr0_fixed1 );
    const unsigned long long cr4_fixed0 = __readmsr( msr_vmx_cr4_fixed0 );
    const unsigned long long cr4_fixed1 = __readmsr( msr_vmx_cr4_fixed1 );
    const unsigned long long cr0 = __readcr0( );
    const unsigned long long cr4 = __readcr4( ) & ~cr4_vmxe;

    return ( cr0 & cr0_fixed0 ) == cr0_fixed0 && ( ~cr0 & ~cr0_fixed1 ) == 0 && ( cr4 & cr4_fixed0 ) == cr4_fixed0 && ( ~cr4 & ~cr4_fixed1 ) == 0;
}

auto vmx_t::initialize( ) -> NTSTATUS
{
    if ( !check_vmx_support( ) ) 
    {
        return STATUS_HV_FEATURE_UNAVAILABLE;
    }

    vmx_supported_ = true;
    cpu_count_ = KeQueryActiveProcessorCountEx( ALL_PROCESSOR_GROUPS );

    if ( cpu_count_ == 0 || cpu_count_ > hv_max_cpus ) 
    {
        return STATUS_NOT_SUPPORTED;
    }

    NTSTATUS status = memory_m->initialize( );
    if ( !NT_SUCCESS( status ) ) return status;

    contexts_ = ( vmx_context* )ExAllocatePoolWithTag( NonPagedPool, sizeof( vmx_context ) * cpu_count_, hv_pool_tag );
    if ( !contexts_ ) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory( contexts_, sizeof( vmx_context ) * cpu_count_ );

    for ( unsigned i = 0; i < cpu_count_; i++ )
    {
        vmx_context* ctx = &contexts_[ i ];

        ctx->processor_index = i;
        ctx->self_pointer = ( unsigned long long )ctx;
        ctx->host_gs_base = ( unsigned long long ) & ctx->self_pointer;

        void* va = nullptr;

        va = vmx_alloc_aligned( PAGE_SIZE );
        if ( !va ) return STATUS_INSUFFICIENT_RESOURCES;

        *( unsigned* )va = ( unsigned )__readmsr( msr_vmx_basic );
        ctx->vmxon_pa = MmGetPhysicalAddress( va ).QuadPart;

        va = vmx_alloc_aligned( PAGE_SIZE );
        if ( !va ) return STATUS_INSUFFICIENT_RESOURCES;

        *( unsigned* )va = ( unsigned )__readmsr( msr_vmx_basic );
        ctx->vmcs_pa = MmGetPhysicalAddress( va ).QuadPart;

        va = vmx_alloc_aligned( PAGE_SIZE );
        if ( !va ) return STATUS_INSUFFICIENT_RESOURCES;

        RtlZeroMemory( va, PAGE_SIZE );
        ctx->msr_bitmap_pa = MmGetPhysicalAddress( va ).QuadPart;

        ctx->host_stack = vmx_alloc_aligned( 0x8000 );
        if ( !ctx->host_stack ) return STATUS_INSUFFICIENT_RESOURCES;

        status = memory_m->allocate_spare_pages( ctx );
        if ( !NT_SUCCESS( status ) ) return status;

        ctx->cr4_fixed1 = __readmsr( msr_vmx_cr4_fixed1 );
    }

    return STATUS_SUCCESS;
}

auto vmx_t::cleanup( ) -> void
{
    if ( contexts_ ) 
    { 
        ExFreePoolWithTag( contexts_, hv_pool_tag );
        contexts_ = nullptr;
    }

    cpu_count_ = 0;
    vmx_supported_ = false;

    memory_m->cleanup( );
    vmx_free_all_allocations( );
}

extern "C" auto hv_launch_finalize( vmx_context* ctx ) -> NTSTATUS
{
    return vmx_m->launch_finalize( ctx );
}