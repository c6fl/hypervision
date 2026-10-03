#include "memory.h"
#include "../vmx/vmx.h"

namespace { memory_t g_memory_instance; }
memory_t* memory_m = &g_memory_instance;

constexpr unsigned long long page_2mb_mask = ~0x1FFFFFULL;
constexpr unsigned long long page_4kb_mask = ~0xFFFULL;

auto memory_t::alloc_aligned( size_t size ) -> void*
{
    if ( alloc_count_ >= ARRAYSIZE( alloc_originals_ ) ) return nullptr;

    void* original = ExAllocatePoolWithTag( NonPagedPool, size + PAGE_SIZE, 'vPhV' );
    if ( !original ) return nullptr;

    alloc_originals_[ alloc_count_++ ] = original;
    return ( void* )( ( ( uintptr_t )original + PAGE_SIZE - 1 ) & ~( uintptr_t )( PAGE_SIZE - 1 ) );
}

auto memory_t::free_all( ) -> void
{
    for ( unsigned i = 0; i < alloc_count_; i++ ) 
    {
        if ( alloc_originals_[ i ] )
        {
            ExFreePoolWithTag( alloc_originals_[ i ], 'vPhV' );
            alloc_originals_[ i ] = nullptr;
        }
    }

    alloc_count_ = 0;
}

auto memory_t::invept_all( ) const -> void
{
    const unsigned long long descriptor[ 2 ] = { 0, 0 };
    asm_invept( invept_all_contexts, ( void* )descriptor );
}

auto memory_t::register_page( vmx_context* ctx, unsigned long long pa, void* va ) -> bool
{
    const long index = InterlockedIncrement( &ctx->ept_table_count ) - 1;
    if ( index >= ( long )ARRAYSIZE( ctx->ept_tables ) ) return false;

    ctx->ept_tables[ index ].pa = pa;
    ctx->ept_tables[ index ].va = va;

    return true;
}

auto memory_t::pa_to_va( vmx_context* ctx, unsigned long long pa ) const -> void*
{
    pa &= page_4kb_mask;
    const long count = ctx->ept_table_count;

    for ( long i = 0; i < count; i++ )
    {
        if ( ctx->ept_tables[ i ].pa == pa ) 
        { return ctx->ept_tables[ i ].va; }
    }

    return nullptr;
}

auto memory_t::alloc_table( vmx_context* ctx, unsigned long long* pa, void** va, bool runtime ) -> bool
{
    if ( runtime )
    {
        const long index = InterlockedIncrement( &ctx->ept_spare_index ) - 1;
        if ( index >= ( long )ARRAYSIZE( ctx->ept_spare_va ) ) return false;

        void* spare = ctx->ept_spare_va[ index ];
        RtlZeroMemory( spare, PAGE_SIZE );

        *pa = MmGetPhysicalAddress( spare ).QuadPart;
        *va = spare;

        return true;
    }

    void* page = alloc_aligned( PAGE_SIZE );
    if ( !page ) return false;

    RtlZeroMemory( page, PAGE_SIZE );

    const unsigned long long page_pa = MmGetPhysicalAddress( page ).QuadPart;
    if ( !register_page( ctx, page_pa, page ) ) return false;

    *pa = page_pa;
    *va = page;

    return true;
}

auto memory_t::initialize( ) -> NTSTATUS
{
    const PPHYSICAL_MEMORY_RANGE ranges = MmGetPhysicalMemoryRanges( );
    if ( !ranges ) return STATUS_INSUFFICIENT_RESOURCES;

    for ( unsigned i = 0; i < ARRAYSIZE( phys_ranges_ ); i++ )
    {
        if ( ranges[ i ].BaseAddress.QuadPart == 0 && ranges[ i ].NumberOfBytes.QuadPart == 0 ) break;

        phys_ranges_[ i ] = ranges[ i ];
        phys_range_count_++;

        const unsigned long long end = ranges[ i ].BaseAddress.QuadPart + ranges[ i ].NumberOfBytes.QuadPart;
        if ( end > max_physical_address_ ) 
        {
            max_physical_address_ = end;
        }
    }

    ExFreePoolWithTag( ranges, 0 );
    return phys_range_count_ > 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

auto memory_t::cleanup( ) -> void
{
    free_all( );
    phys_range_count_ = 0;
    max_physical_address_ = 0;
}

auto memory_t::allocate_spare_pages( vmx_context* ctx ) -> NTSTATUS
{
    for ( unsigned i = 0; i < ARRAYSIZE( ctx->ept_spare_va ); i++ ) 
    {
        void* spare = alloc_aligned( PAGE_SIZE );
        if ( !spare ) return STATUS_INSUFFICIENT_RESOURCES;

        RtlZeroMemory( spare, PAGE_SIZE );
        ctx->ept_spare_va[ i ] = spare;
        if ( !register_page( ctx, MmGetPhysicalAddress( spare ).QuadPart, spare ) ) { return STATUS_INSUFFICIENT_RESOURCES; }
    }

    return STATUS_SUCCESS;
}

auto memory_t::build_identity_map( vmx_context* ctx ) -> NTSTATUS
{
    void* pml4_va = nullptr;
    unsigned long long pml4_pa = 0;

    if ( !alloc_table( ctx, &pml4_pa, &pml4_va, false ) ) { return STATUS_INSUFFICIENT_RESOURCES; }

    ctx->ept_pml4_pa = pml4_pa;
    ctx->ept_pml4_va = pml4_va;

    for ( unsigned long long gpa = 0; gpa < max_physical_address_; gpa += 0x200000 )
    {
        if ( !map_2mb( ctx, gpa, false ) )
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    return STATUS_SUCCESS;
}

auto memory_t::map_2mb( vmx_context* ctx, unsigned long long gpa, bool runtime ) -> bool
{
    gpa &= page_2mb_mask;

    volatile unsigned long long* pml4 = ( volatile unsigned long long* )ctx->ept_pml4_va;
    const unsigned long long pml4_index = ( gpa >> 39 ) & 0x1FF;
    const unsigned long long pdpt_index = ( gpa >> 30 ) & 0x1FF;
    const unsigned long long pd_index = ( gpa >> 21 ) & 0x1FF;

    auto ensure_table = [ & ]( volatile unsigned long long* entry ) -> volatile unsigned long long* 
    {
        if ( *entry & ept_flag_read ) 
        {
            return ( volatile unsigned long long* )pa_to_va( ctx, *entry );
        }

        unsigned long long pa = 0;
        void* va = nullptr;

        if ( !alloc_table( ctx, &pa, &va, runtime ) ) return nullptr;

        *entry = ( pa & page_4kb_mask ) | ept_table_flags;
        return ( volatile unsigned long long* )va;
    };

    volatile unsigned long long* pdpt = ensure_table( &pml4[ pml4_index ] );
    if ( !pdpt ) return false;

    volatile unsigned long long* pd = ensure_table( &pdpt[ pdpt_index ] );
    if ( !pd ) return false;

    if ( pd[ pd_index ] & ept_flag_read ) return true;
    pd[ pd_index ] = ( gpa & page_2mb_mask ) | ept_leaf_flags_rwx_wb;
    return true;
}

auto memory_t::split_2mb( vmx_context* ctx, unsigned long long gpa_2mb ) -> bool
{
    gpa_2mb &= page_2mb_mask;

    const unsigned long long pml4_index = ( gpa_2mb >> 39 ) & 0x1FF;
    const unsigned long long pdpt_index = ( gpa_2mb >> 30 ) & 0x1FF;
    const unsigned long long pd_index = ( gpa_2mb >> 21 ) & 0x1FF;

    volatile unsigned long long* pml4 = ( volatile unsigned long long* )ctx->ept_pml4_va;

    if ( !( pml4[ pml4_index ] & ept_flag_read ) ) return false;

    volatile unsigned long long* pdpt = ( volatile unsigned long long* )pa_to_va( ctx, pml4[ pml4_index ] );
    if ( !pdpt || !( pdpt[ pdpt_index ] & ept_flag_read ) ) return false;

    volatile unsigned long long* pd = ( volatile unsigned long long* )pa_to_va( ctx, pdpt[ pdpt_index ] );
    if ( !pd ) return false;

    const unsigned long long leaf = pd[ pd_index ];

    if ( !( leaf & ept_flag_read ) ) return false;
    if ( !( leaf & ept_flag_large ) ) return true;

    unsigned long long pt_pa = 0;
    void* pt_va = nullptr;

    if ( !alloc_table( ctx, &pt_pa, &pt_va, false ) ) return false;

    const unsigned long long flags_4kb = ( leaf & ~ept_flag_large ) & 0xFFFULL;
    volatile unsigned long long* pt = ( volatile unsigned long long* )pt_va;

    for ( unsigned i = 0; i < 512; i++ ) 
    {
        pt[ i ] = flags_4kb | ( ( gpa_2mb & page_2mb_mask ) + ( unsigned long long )i * 0x1000 );
    }

    pd[ pd_index ] = ( pt_pa & page_4kb_mask ) | ( leaf & ept_table_flags );
    ctx->ept_page_splits++;

    invept_all( );
    return true;
}

auto memory_t::walk_leaf_4kb( vmx_context* ctx, unsigned long long gpa, unsigned long long** leaf_out ) -> bool
{
    if ( !split_2mb( ctx, gpa & page_2mb_mask ) ) return false;

    const unsigned long long pml4_index = ( gpa >> 39 ) & 0x1FF;
    const unsigned long long pdpt_index = ( gpa >> 30 ) & 0x1FF;
    const unsigned long long pd_index = ( gpa >> 21 ) & 0x1FF;
    const unsigned long long pt_index = ( gpa >> 12 ) & 0x1FF;

    volatile unsigned long long* pml4 = ( volatile unsigned long long* )ctx->ept_pml4_va;
    if ( !( pml4[ pml4_index ] & ept_flag_read ) ) return false;

    volatile unsigned long long* pdpt = ( volatile unsigned long long* )pa_to_va( ctx, pml4[ pml4_index ] );
    if ( !pdpt || !( pdpt[ pdpt_index ] & ept_flag_read ) ) return false;

    volatile unsigned long long* pd = ( volatile unsigned long long* )pa_to_va( ctx, pdpt[ pdpt_index ] );
    if ( !pd || !( pd[ pd_index ] & ept_flag_read ) ) return false;

    volatile unsigned long long* pt = ( volatile unsigned long long* )pa_to_va( ctx, pd[ pd_index ] );
    if ( !pt || ( pd[ pd_index ] & ept_flag_large ) ) return false;

    *leaf_out = ( unsigned long long* ) & pt[ pt_index ];
    return true;
}

auto memory_t::sandbox_page( vmx_context* ctx, unsigned long long gpa, bool protect ) -> NTSTATUS
{
    unsigned long long* leaf = nullptr;
    if ( !walk_leaf_4kb( ctx, gpa, &leaf ) )
    {
        return STATUS_UNSUCCESSFUL;
    }

    if ( protect )
    {
        *leaf &= ~ept_flag_write;
    }

    else 
    {
        *leaf |= ept_flag_write;
    }

    invept_all( );
    return STATUS_SUCCESS;
}

auto memory_t::handle_violation( vmx_context* ctx, unsigned long long gpa ) -> bool
{
    unsigned long long* leaf = nullptr;

    if ( walk_leaf_4kb_runtime( ctx, gpa, &leaf ) )
    {
        if ( !( *leaf & ept_flag_write ) )
        {
            *leaf |= ept_flag_write;
            ctx->sandbox_trapped_writes++;
            invept_all( );
        }

        return true;
    }

    if ( !map_2mb( ctx, gpa, true ) ) 
    {
        return false;
    }

    ctx->ept_runtime_maps++;
    return true;
}

auto memory_t::walk_leaf_4kb_runtime( vmx_context* ctx, unsigned long long gpa, unsigned long long** leaf_out ) -> bool
{
    volatile unsigned long long* pml4 = ( volatile unsigned long long* )ctx->ept_pml4_va;

    const unsigned long long pml4_index = ( gpa >> 39 ) & 0x1FF;
    const unsigned long long pdpt_index = ( gpa >> 30 ) & 0x1FF;
    const unsigned long long pd_index = ( gpa >> 21 ) & 0x1FF;
    const unsigned long long pt_index = ( gpa >> 12 ) & 0x1FF;

    if ( !( pml4[ pml4_index ] & ept_flag_read ) ) return false;

    volatile unsigned long long* pdpt = ( volatile unsigned long long* )pa_to_va( ctx, pml4[ pml4_index ] );
    if ( !pdpt || !( pdpt[ pdpt_index ] & ept_flag_read ) ) return false;

    volatile unsigned long long* pd = ( volatile unsigned long long* )pa_to_va( ctx, pdpt[ pdpt_index ] );
    if ( !pd || !( pd[ pd_index ] & ept_flag_read ) || ( pd[ pd_index ] & ept_flag_large ) )
    {
        return false;
    }

    volatile unsigned long long* pt = ( volatile unsigned long long* )pa_to_va( ctx, pd[ pd_index ] );
    if ( !pt ) return false;

    *leaf_out = ( unsigned long long* ) & pt[ pt_index ];
    return true;
}