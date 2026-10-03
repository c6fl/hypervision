#include "exits.h"
#include "../memory/memory.h"

namespace { exits_t g_exits_instance; }
exits_t* exits_m = &g_exits_instance;

auto exits_t::advance_rip( vmx_context* ctx, unsigned long long length ) -> void
{
    if ( length == 0 ) length = 2;
    vmx_write( vmx_guest_rip, vmx_read( vmx_guest_rip ) + length );
}

auto exits_t::inject_exception( vmx_context* ctx, unsigned vector ) -> void
{
    const unsigned long long interruptibility = vmx_read( vmx_guest_interruptibility );
    if ( interruptibility & 0x2 ) return;
    vmx_write( vmx_entry_intr_info, ( unsigned long long )vector | ( 2ULL << 8 ) | ( 1ULL << 31 ) );
}

auto exits_t::handle_cpuid_exit( vmx_context* ctx ) -> void
{
    int regs[ 4 ];
    const int leaf = ( int )ctx->guest_registers[ gpr_rax ];
    const int subleaf = ( int )ctx->guest_registers[ gpr_rcx ];

    __cpuidex( regs, leaf, subleaf );

    if ( leaf == 1 ) { regs[ 2 ] |= 0x80000000; }
    else if ( leaf == 0x40000000 ) 
    {
        regs[ 0 ] = 0x40000001;
        regs[ 1 ] = 'ypeH';
        regs[ 2 ] = 'isiv';
        regs[ 3 ] = 'VHno';
    }

    else if ( leaf == 0x40000001 ) 
    {
        regs[ 0 ] = 0x00000001;
        regs[ 1 ] = ( int )hv_hypercall_magic;
        regs[ 2 ] = 0;
        regs[ 3 ] = 0;
    }

    ctx->guest_registers[ gpr_rax ] = ( unsigned long long )regs[ 0 ];
    ctx->guest_registers[ gpr_rbx ] = ( unsigned long long )regs[ 1 ];
    ctx->guest_registers[ gpr_rcx ] = ( unsigned long long )regs[ 2 ];
    ctx->guest_registers[ gpr_rdx ] = ( unsigned long long )regs[ 3 ];

    advance_rip( ctx, vmx_read( vmx_exit_instr_len ) );
}

auto exits_t::handle_cr_access_exit( vmx_context* ctx ) -> void
{
    const unsigned long long qualification = vmx_read( vmx_exit_qualification );
    const unsigned cr = ( unsigned )( qualification & 0xF );
    const unsigned type = ( unsigned )( ( qualification >> 4 ) & 0x3 );
    const unsigned gpr = ( unsigned )( ( qualification >> 8 ) & 0xF ) & 15;
    const unsigned long long reg_value = ctx->guest_registers[ gpr ];

    switch ( type )
    {
        case 0:
        {
            if ( cr == 0 )
            {
                ctx->cr0_shadow = reg_value;
                const unsigned long long actual = ( reg_value & ~cr0_host_mask ) | ( __readcr0( ) & cr0_host_mask );
                __writecr0( actual );
            }

            else if ( cr == 4 )
            {
                ctx->cr4_shadow = reg_value & ~cr4_vmxe;
                unsigned long long actual = ( reg_value & ~cr4_host_mask ) | ( __readcr4( ) & cr4_host_mask );
                actual |= ( ctx->cr4_fixed1 & ~cr4_host_mask );
                __writecr4( actual );
            }

            break;
        }

    case 1:
        if ( cr == 0 ) ctx->guest_registers[ gpr ] = ctx->cr0_shadow;
        else if ( cr == 4 ) ctx->guest_registers[ gpr ] = ctx->cr4_shadow;
        else if ( cr == 3 ) ctx->guest_registers[ gpr ] = __readcr3( );
        else if ( cr == 8 ) ctx->guest_registers[ gpr ] = __readcr8( );
        break;

    case 2:
            ctx->cr0_shadow &= ~0x8ULL;
            __writecr0( __readcr0( ) & ~0x8ULL );
        break;

    case 3: 
    {
        const unsigned long long source = qualification & 0xF;
        ctx->cr0_shadow = ( ctx->cr0_shadow & ~0xFULL ) | source | ( ctx->cr0_shadow & 0x1 );
        __writecr0( ( __readcr0( ) & ~0xFULL ) | source | ( __readcr0( ) & 0x1 ) );
        break;
    }

    default:
        break;
    }
}

auto exits_t::handle_xsetbv_exit( vmx_context* ctx ) -> void
{
    const unsigned long long value = ( ctx->guest_registers[ gpr_rdx ] << 32 ) | ctx->guest_registers[ gpr_rax ];
    asm_xsetbv( ctx->guest_registers[ gpr_rcx ], value );
    advance_rip( ctx, 3 );
}

auto exits_t::handle_ept_violation_exit( vmx_context* ctx ) -> unsigned long long
{
    const unsigned long long gpa = vmx_read( vmx_guest_physical_addr );

    if ( !memory_m->handle_violation( ctx, gpa ) ) 
    {
        ctx->fatal_reason = vmx_exit_ept_violation;
        ctx->fatal_qualification = gpa;
        return 2;
    }

    return 0;
}

auto exits_t::on_exit( vmx_context* ctx ) -> unsigned long long
{
    const unsigned long long reason = vmx_read( vmx_exit_reason );

    if ( reason < hv_exit_reason_count )
    {
        ctx->exit_counts[ reason ]++;
    }

    if ( ctx->stop_requested ) 
    {
        vmx_m->prepare_vmx_off( ctx );
        return 1;
    }

    switch ( reason )
    {
    case vmx_exit_cpuid:
        handle_cpuid_exit( ctx );
        return 0;

    case vmx_exit_vmcall:
        ctx->guest_registers[ gpr_rax ] = hv_hypercall_magic;
        advance_rip( ctx, 3 );
        return 0;

    case vmx_exit_cr_access:
        handle_cr_access_exit( ctx );
        return 0;

    case vmx_exit_xsetbv:
        handle_xsetbv_exit( ctx );
        return 0;

    case vmx_exit_ept_violation:
        return handle_ept_violation_exit( ctx );

    case vmx_exit_invd:
        advance_rip( ctx, 2 );
        return 0;

    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case vmx_exit_vmxon:
    case vmx_exit_invept:
    case vmx_exit_invvid:
        inject_exception( ctx, 6 );
        return 0;

    case vmx_exit_msr_read:
    case vmx_exit_msr_write:
        advance_rip( ctx, 2 );
        return 0;

    default:
        ctx->fatal_reason = reason;
        ctx->fatal_qualification = vmx_read( vmx_exit_qualification );
        return 2;
    }
}

extern "C" auto hv_handle_exit( vmx_context* ctx ) -> unsigned long long
{
    return exits_m->on_exit( ctx );
}