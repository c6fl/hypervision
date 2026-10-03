#include <stdio.h>
#include <string.h>
#include <intrin.h>
#include "hv_client.h"

namespace
{
    constexpr int exit_invd = 13;
    constexpr int exit_vmxon = 27;

    template <size_t n>
    auto make_stub( const BYTE( &bytes )[ n ] ) -> void*
    {
        BYTE* code = ( BYTE* )VirtualAlloc( nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
        if ( !code ) return nullptr;

        memcpy( code, bytes, n );
        code[ n ] = 0xC3;
        return code;
    }

    auto demo_capabilities( const hv_client_t& hv, const hv_status& status ) -> bool
    {
        printf( "== capabilities ==\n" );

        int regs[ 4 ] = {};
        __cpuid( regs, 1 );

        //const bool vmx = ( regs[ 2 ] & ( 1 << 4 ) ) != 0;
        const bool vmx = ( regs[ 2 ] & ( 1 << 5 ) ) != 0;
        const bool hypervisor = ( regs[ 2 ] & 0x80000000 ) != 0;

        printf( "[usermode] vmx: %s\n", vmx ? "yes" : "no" );
        printf( "[usermode] hypervisor: %s\n", hypervisor ? "yes" : "no" );

        __cpuid( regs, 0x40000000 );

        char vendor[ 13 ] = {};
        memcpy( vendor, &regs[ 1 ], 4 );
        memcpy( vendor + 4, &regs[ 2 ], 4 );
        memcpy( vendor + 8, &regs[ 3 ], 4 );

        printf( "[usermode] vendor: %s\n", vendor );
        printf( "[usermode] cpus: %u\n", status.num_cpus );
        printf( "[usermode] vmx_supported: %u\n", status.vmx_supported );

        unsigned launched = 0;
        for ( unsigned i = 0; i < status.num_cpus; i++ ) 
        launched += status.cpus[ i ].vmx_launched;

        printf( "[usermode] vmx_launched: %u/%u\n", launched, status.num_cpus );

        const hv_cpu_status& cpu = status.cpus[ 0 ];

        printf( "[usermode] proc_ctls: 0x%016llx\n", cpu.vmcs_proc_ctls );
        printf( "[usermode] proc2_ctls: 0x%016llx\n", cpu.vmcs_proc2_ctls );
        printf( "[usermode] eptp: 0x%016llx\n", cpu.vmcs_eptp );

        return hypervisor && strcmp( vendor, "HypervisionHV" ) == 0 && launched == status.num_cpus;
    }

    auto demo_vmxon( hv_client_t& hv, hv_status& status ) -> bool
    {
        printf( "\n== vmxon ==\n" );

        unsigned long long result = 0;
        const BYTE code[ ] = { 0xF3, 0x0F, 0xC7, 0x30, 0x00, 0x00, 0x00, 0x00 };

        void* stub = make_stub( code );
        bool executed = false;

        if ( stub )
        {
            __try
            {
                result = ( ( unsigned long long ( * )( ) )stub )( );
                executed = true;
            }

            __except ( EXCEPTION_EXECUTE_HANDLER ) { }
            VirtualFree( stub, 0, MEM_RELEASE );
        }

        hv_status after{};
        hv.query_status( &after );

        const unsigned long long exits = after.cpus[ 0 ].exit_counts[ exit_vmxon ] - status.cpus[ 0 ].exit_counts[ exit_vmxon ];

        printf( "[usermode] executed: %s\n", executed ? "yes" : "no" );
        printf( "[usermode] exits: +%llu\n", exits );

        memcpy( &status, &after, sizeof( status ) );
        return !executed && exits > 0;
    }

    auto demo_ept( const hv_client_t& hv, const hv_status& status ) -> bool
    {
        printf( "\n== ept ==\n" );

        printf( "[usermode] runtime_maps: %llu\n", status.cpus[ 0 ].ept_runtime_maps );
        printf( "[usermode] page_splits: %llu\n", status.cpus[ 0 ].ept_page_splits );
        printf( "[usermode] trapped_writes: %llu\n", status.cpus[ 0 ].sandbox_trapped_writes );

        return true;
    }

    auto demo_sandbox( hv_client_t& hv, hv_status& status ) -> bool
    {
        printf( "\n== sandbox ==\n" );

        BYTE* page = ( BYTE* )VirtualAlloc( nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
        if ( !page ) return false;

        const unsigned long long before = status.cpus[ 0 ].sandbox_trapped_writes;
        if ( !hv.sandbox_page( page, true ) )
        {
            VirtualFree( page, 0, MEM_RELEASE );
            return false;
        }

        page[ 0 ] = 0x42;
        MemoryBarrier( );

        hv_status after{};
        hv.query_status( &after );

        const unsigned long long trapped = after.cpus[ 0 ].sandbox_trapped_writes - before;

        printf( "[usermode] value: 0x%02x\n", page[ 0 ] );
        printf( "[usermode] violations: +%llu\n", trapped );

        hv.sandbox_page( page, false );
        VirtualFree( page, 0, MEM_RELEASE );

        memcpy( &status, &after, sizeof( status ) );

        return trapped > 0;
    }

    auto demo_privileged( hv_client_t& hv, hv_status& status ) -> bool
    {
        printf( "\n== privileged ==\n" );

        unsigned long long result = 0;
        bool ok = true;

        int regs[ 4 ] = {};
        __cpuid( regs, 0x40000001 );

        printf( "[usermode] version: %u\n", regs[ 0 ] );
        printf( "[usermode] signature: 0x%08x\n", regs[ 1 ] );

        const BYTE vmcall_code[ ] = { 0x0F, 0x01, 0xC1 };
        void* stub = make_stub( vmcall_code );
        bool vmcall_executed = false;

        if ( stub )
        {
            __try
            {
                result = ( ( unsigned long long ( * )( ) )stub )( );
                vmcall_executed = true;
            }

            __except ( EXCEPTION_EXECUTE_HANDLER ) { }
            VirtualFree( stub, 0, MEM_RELEASE );
        }

        printf( "[usermode] vmcall: %s\n", vmcall_executed ? "ok" : "faulteeed" );
        printf( "[usermode] rax: 0x%016llx\n", result );

        ok &= vmcall_executed && result == hv_hypercall_magic;

        const BYTE invd_code[ ] = { 0x0F, 0x08 };
        stub = make_stub( invd_code );

        bool invd_executed = false;

        if ( stub )
        {
            __try
            {
                result = ( ( unsigned long long ( * )( ) )stub )( );
                invd_executed = true;
            }

            __except ( EXCEPTION_EXECUTE_HANDLER ) { }
            VirtualFree( stub, 0, MEM_RELEASE );
        }

        hv_status after{};
        hv.query_status( &after );

        const unsigned long long invd_exits = after.cpus[ 0 ].exit_counts[ exit_invd ] - status.cpus[ 0 ].exit_counts[ exit_invd ];

        printf( "[usermode] invd: %s\n", invd_executed ? "ok" : "nope" );
        printf( "[usermode] invd_exits: +%llu\n", invd_exits );

        memcpy( &status, &after, sizeof( status ) );

        const BYTE wrmsr_code[ ] = { 0x0F, 0x30 };
        stub = make_stub( wrmsr_code );
        bool wrmsr_executed = false;

        if ( stub )
        {
            __try
            {
                result = ( ( unsigned long long ( * )( ) )stub )( );
                wrmsr_executed = true;
            }

            __except ( EXCEPTION_EXECUTE_HANDLER ) { }
            VirtualFree( stub, 0, MEM_RELEASE );
        }

        printf( "[usermode] wrmsr: %s\n", wrmsr_executed ? "ok" : "fault" );
        return ok;
    }
}

int main( )
{
    printf( "super ud usermode example\n" );
    printf( "========\n\n" );

    hv_client_t hv;
    hv_status status{};

    if ( !hv.connect( ) )
    {
        printf( "[usermode] could not connect? %lu\n", GetLastError( ) );
        return 1;
    }

    if ( !hv.query_status( &status ) )
    {
        printf( "[usermode] cant query %lu\n", GetLastError( ) );
        hv.disconnect( );
        return 1;
    }

    bool pass = true;

    pass &= demo_capabilities( hv, status );
    pass &= demo_vmxon( hv, status );
    pass &= demo_ept( hv, status );
    pass &= demo_sandbox( hv, status );
    pass &= demo_privileged( hv, status );

    printf( "\n[usermode] result: %s\n", pass ? "pass" : "fail" );

    hv.disconnect( );
    return pass ? 0 : 1;
}