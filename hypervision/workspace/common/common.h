#pragma once

#define HV_DEVICE_NAME     L"\\Device\\Hypervision"
#define HV_SYMLINK_NAME    L"\\DosDevices\\Hypervision"
#define HV_DOS_DEVICE      L"\\\\.\\Hypervision"

constexpr unsigned long hv_file_device = 0x00008050;

constexpr unsigned long hv_ioctl_get_status = CTL_CODE(hv_file_device, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr unsigned long hv_ioctl_sandbox_page = CTL_CODE(hv_file_device, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS);

constexpr unsigned long long hv_hypercall_magic = 0x4B4F5648ULL; // HVOK
constexpr unsigned hv_exit_reason_count = 64;
constexpr unsigned hv_max_cpus = 128;

#pragma pack(push, 1)

struct hv_cpu_status
{
    unsigned processor_index;
    unsigned vmx_launched;
    unsigned long long exit_counts[hv_exit_reason_count];

    unsigned long long vmcs_pin_ctls;
    unsigned long long vmcs_proc_ctls;
    unsigned long long vmcs_proc2_ctls;
    unsigned long long vmcs_exit_ctls;
    unsigned long long vmcs_entry_ctls;
    unsigned long long vmcs_eptp;

    unsigned long long ept_runtime_maps; // GPAs
    unsigned long long ept_page_splits; // 2 MB leaves split into 4 KB ^^
    unsigned long long sandbox_trapped_writes;
};

struct hv_status 
{
    unsigned num_cpus;
    unsigned vmx_supported;
    hv_cpu_status cpus[hv_max_cpus];
};

struct hv_sandbox_request 
{
    void*    address;
    unsigned protect; // 1 = trap - 0 = release
};

#pragma pack(pop)
