#include <ntddk.h>
#include "workspace/vmx/vmx.h"
#include "workspace/memory/memory.h"
#include "workspace/exits/exits.h"

extern "C"
{
    VOID KeGenericCallDpc( VOID( * )( PKDPC, PVOID, PVOID, PVOID ), PVOID );
    VOID KeSignalCallDpcDone( PKDPC dpc );
}

VOID hv_start_dpc_routine( PKDPC dpc, PVOID deferred_context, PVOID arg1, PVOID arg2 )
{
    UNREFERENCED_PARAMETER( deferred_context );
    UNREFERENCED_PARAMETER( arg1 );
    UNREFERENCED_PARAMETER( arg2 );

    const unsigned index = KeGetCurrentProcessorNumberEx( nullptr );
    if ( index < vmx_m->cpu_count( ) ) vmx_m->initialize_cpu( vmx_m->contexts( ) + index );

    KeSignalCallDpcDone( dpc );
}

VOID hv_stop_dpc_routine( PKDPC dpc, PVOID deferred_context, PVOID arg1, PVOID arg2 )
{
    UNREFERENCED_PARAMETER( deferred_context );
    UNREFERENCED_PARAMETER( arg1 );
    UNREFERENCED_PARAMETER( arg2 );

    const unsigned index = KeGetCurrentProcessorNumberEx( nullptr );
    if ( index < vmx_m->cpu_count( ) )
    {
        vmx_context* ctx = vmx_m->contexts( ) + index;

        if ( InterlockedCompareExchange( &ctx->launched, 0, 1 ) == 1 )
        {
            InterlockedExchange( &ctx->stop_requested, 1 );
            int regs[ 4 ];
            __cpuid( regs, 0 );
        }

        else if ( ctx->vmxon_pa && ( __readcr4( ) & cr4_vmxe ) )
        {
            __vmx_off( );
            __writecr4( __readcr4( ) & ~cr4_vmxe );
        }
    }

    KeSignalCallDpcDone( dpc );
}

namespace
{
    PDEVICE_OBJECT g_device_object = nullptr;

    auto query_status( PVOID output_buffer, ULONG output_length, ULONG_PTR* bytes_written ) -> NTSTATUS
    {
        *bytes_written = 0;
        if ( !output_buffer || output_length < sizeof( hv_status ) ) return STATUS_BUFFER_TOO_SMALL;

        auto* status = ( hv_status* )output_buffer;
        RtlZeroMemory( status, sizeof( hv_status ) );

        status->num_cpus = vmx_m->cpu_count( );
        status->vmx_supported = vmx_m->vmx_supported( ) ? 1u : 0u;
        const vmx_context* contexts = vmx_m->contexts( );

        for ( unsigned i = 0; i < status->num_cpus; i++ )
        {
            auto& out = status->cpus[ i ];
            const auto& ctx = contexts[ i ];

            out.processor_index = ctx.processor_index;
            out.vmx_launched = ctx.launched ? 1u : 0u;
            out.vmcs_pin_ctls = ctx.vmcs_pin_ctls;
            out.vmcs_proc_ctls = ctx.vmcs_proc_ctls;
            out.vmcs_proc2_ctls = ctx.vmcs_proc2_ctls;
            out.vmcs_exit_ctls = ctx.vmcs_exit_ctls;
            out.vmcs_entry_ctls = ctx.vmcs_entry_ctls;
            out.vmcs_eptp = ctx.vmcs_eptp;
            out.ept_runtime_maps = ctx.ept_runtime_maps;
            out.ept_page_splits = ctx.ept_page_splits;
            out.sandbox_trapped_writes = ctx.sandbox_trapped_writes;

            RtlCopyMemory( out.exit_counts, ctx.exit_counts, sizeof( out.exit_counts ) );
        }

        *bytes_written = sizeof( hv_status );
        return STATUS_SUCCESS;
    }

    auto sandbox_page( PVOID input_buffer, ULONG input_length ) -> NTSTATUS
    {
        if ( !input_buffer || input_length < sizeof( hv_sandbox_request ) ) return STATUS_BUFFER_TOO_SMALL;

        const auto* request = ( const hv_sandbox_request* )input_buffer;
        const unsigned long long gpa = MmGetPhysicalAddress( request->address ).QuadPart;

        if ( !gpa ) return STATUS_INVALID_PARAMETER;

        const bool protect = request->protect != 0;
        for ( unsigned i = 0; i < vmx_m->cpu_count( ); i++ )
        {
            const NTSTATUS status = memory_m->sandbox_page( vmx_m->contexts( ) + i, gpa, protect );
            if ( !NT_SUCCESS( status ) ) return status;
        }

        return STATUS_SUCCESS;
    }

    NTSTATUS dispatch_device_control( PDEVICE_OBJECT device_object, PIRP irp )
    {
        UNREFERENCED_PARAMETER( device_object );

        PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation( irp );
        NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
        ULONG_PTR bytes_written = 0;

        switch ( stack->Parameters.DeviceIoControl.IoControlCode )
        {
            case hv_ioctl_get_status:
            status = query_status( irp->AssociatedIrp.SystemBuffer, stack->Parameters.DeviceIoControl.OutputBufferLength, &bytes_written );
            break;

            case hv_ioctl_sandbox_page:
            status = sandbox_page( irp->AssociatedIrp.SystemBuffer, stack->Parameters.DeviceIoControl.InputBufferLength );
            break;
        }

        irp->IoStatus.Status = status;
        irp->IoStatus.Information = bytes_written;

        IoCompleteRequest( irp, IO_NO_INCREMENT );
        return status;
    }

    NTSTATUS dispatch_create_close( PDEVICE_OBJECT device_object, PIRP irp )
    {
        UNREFERENCED_PARAMETER( device_object );

        irp->IoStatus.Status = STATUS_SUCCESS;
        irp->IoStatus.Information = 0;

        IoCompleteRequest( irp, IO_NO_INCREMENT );
        return STATUS_SUCCESS;
    }

    VOID hv_unload( PDRIVER_OBJECT driver_object )
    {
        UNREFERENCED_PARAMETER( driver_object );

        vmx_m->stop_all_cpus( );
        bool all_stopped = true;

        for ( unsigned i = 0; i < vmx_m->cpu_count( ); i++ )
        {
            if ( vmx_m->contexts( )[ i ].launched )
            {
                all_stopped = false;
                break;
            }
        }

        if ( !all_stopped )
        {
            DbgPrintEx( DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[hypervision_hv] -> CPUs still running VMX\n" );
        }

        UNICODE_STRING symlink_name = RTL_CONSTANT_STRING( HV_SYMLINK_NAME );
        IoDeleteSymbolicLink( &symlink_name );

        if ( g_device_object )
        {
            IoDeleteDevice( g_device_object );
            g_device_object = nullptr;
        }

        vmx_m->cleanup( );
        DbgPrintEx( DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[hypervision_hv] -> unloaded\n" );
    }
}

extern "C" NTSTATUS DriverEntry( PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path )
{
    UNREFERENCED_PARAMETER( registry_path );
    NTSTATUS status = vmx_m->initialize( );

    DbgPrintEx( DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[hypervision_hv] -> super ud init\n" );

    if ( !NT_SUCCESS( status ) )
    {
        DbgPrintEx( DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[hypervision_hv] -> VMX init failed (0x%08x)\n", status );
        return status;
    }

    driver_object->DriverUnload = hv_unload;
    driver_object->MajorFunction[ IRP_MJ_CREATE ] = dispatch_create_close;
    driver_object->MajorFunction[ IRP_MJ_CLOSE ] = dispatch_create_close;
    driver_object->MajorFunction[ IRP_MJ_DEVICE_CONTROL ] = dispatch_device_control;

    UNICODE_STRING device_name = RTL_CONSTANT_STRING( HV_DEVICE_NAME );
    status = IoCreateDevice( driver_object, 0, &device_name, hv_file_device, FILE_DEVICE_SECURE_OPEN, FALSE, &g_device_object );

    if ( !NT_SUCCESS( status ) )
    {
        vmx_m->cleanup( );
        return status;
    }

    UNICODE_STRING symlink_name = RTL_CONSTANT_STRING( HV_SYMLINK_NAME );
    status = IoCreateSymbolicLink( &symlink_name, &device_name );

    if ( !NT_SUCCESS( status ) )
    {
        IoDeleteDevice( g_device_object );
        g_device_object = nullptr;
        vmx_m->cleanup( );
        return status;
    }

    if ( !vmx_m->start_all_cpus( ) )
    {
        DbgPrintEx( DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[hypervision_hv] -> could not launch vmx\n" );
        vmx_m->stop_all_cpus( );

        IoDeleteSymbolicLink( &symlink_name );
        IoDeleteDevice( g_device_object );

        g_device_object = nullptr;
        vmx_m->cleanup( );

        return STATUS_UNSUCCESSFUL;
    }

    DbgPrintEx( DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[hypervision_hv] -> we active on %u CPUs\n", vmx_m->cpu_count( ) );
    return STATUS_SUCCESS;
}