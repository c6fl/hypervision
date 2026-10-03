# Hypervision

A minimal, **"Type-1 hypervisor"** for Windows x64 built directly on Intel VT-x.
Note that i'd call it a **clone-style** hypervisor cuz the driver puts every logical processor into VMX non-root operation while the already running Windows instance keeps executing as the guest.
So it is not a bootkit powered nor a UEFI based hypervisor, this does load under windows os and virtualizes it.

NO AI WERE USED ! So you better take some time reading this because i made it with love 

## Features

* Intel VT-x / VMX
* Per-CPU VMXON / VMCS
* VM-entry / VM-exit handling
* EPT identity mapping
* 2MB → 4KB EPT splitting
* Runtime EPT mappings
* CR0 / CR4 shadowing
* MSR bitmap
* CPUID virtualization
* VMCALL hypercall
* XSETBV handling
* VMX instruction interception
* EPT violation handling
* Basic EPT page sandboxing
* Usermode test client

`hypervision` is the kernel driver.
`vmx_lab` is a small usermode client used to interact with it and test things like VMX state, EPT sandboxing and VM-exits.

## Requirements

* Windows x64
* Intel CPU with VT-x + EPT
* Visual Studio
* Windows Driver Kit

Build the solution as **x64**.

## Notes

This is a PoC / research project and is intentionally kept small. It's mainly here to experiment with VMX, EPT and virtualizing an already running Windows system.
Expect bugs and crashes while messing with it.

## Credits

vmcall, vmc for helping me writing asm part and some of their structures were used.
