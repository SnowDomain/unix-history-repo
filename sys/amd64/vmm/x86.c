/*-
 * Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/pcpu.h>
#include <sys/systm.h>
#include <sys/cpuset.h>

#include <machine/clock.h>
#include <machine/cpufunc.h>
#include <machine/md_var.h>
#include <machine/segments.h>
#include <machine/specialreg.h>

#include <machine/vmm.h>

#include "vmm_host.h"
#include "x86.h"

#define	CPUID_VM_HIGH		0x40000000

static const char bhyve_id[12] = "bhyve bhyve ";

static uint64_t bhyve_xcpuids;

int
x86_emulate_cpuid(struct vm *vm, int vcpu_id,
		  uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	const struct xsave_limits *limits;
	uint64_t cr4;
	int error, enable_invpcid;
	unsigned int 	func, regs[4];
	enum x2apic_state x2apic_state;

	/*
	 * Requests for invalid CPUID levels should map to the highest
	 * available level instead.
	 */
	if (cpu_exthigh != 0 && *eax >= 0x80000000) {
		if (*eax > cpu_exthigh)
			*eax = cpu_exthigh;
	} else if (*eax >= 0x40000000) {
		if (*eax > CPUID_VM_HIGH)
			*eax = CPUID_VM_HIGH;
	} else if (*eax > cpu_high) {
		*eax = cpu_high;
	}

	func = *eax;

	/*
	 * In general the approach used for CPU topology is to
	 * advertise a flat topology where all CPUs are packages with
	 * no multi-core or SMT.
	 */
	switch (func) {
		/*
		 * Pass these through to the guest
		 */
		case CPUID_0000_0000:
		case CPUID_0000_0002:
		case CPUID_0000_0003:
		case CPUID_8000_0000:
		case CPUID_8000_0002:
		case CPUID_8000_0003:
		case CPUID_8000_0004:
		case CPUID_8000_0006:
		case CPUID_8000_0008:
			cpuid_count(*eax, *ecx, regs);
			break;

		case CPUID_8000_0001:
			/*
			 * Hide rdtscp/ia32_tsc_aux until we know how
			 * to deal with them.
			 */
			cpuid_count(*eax, *ecx, regs);
			regs[3] &= ~AMDID_RDTSCP;
			break;

		case CPUID_8000_0007:
			cpuid_count(*eax, *ecx, regs);
			/*
			 * If the host TSCs are not synchronized across
			 * physical cpus then we cannot advertise an
			 * invariant tsc to a vcpu.
			 *
			 * XXX This still falls short because the vcpu
			 * can observe the TSC moving backwards as it
			 * migrates across physical cpus. But at least
			 * it should discourage the guest from using the
			 * TSC to keep track of time.
			 */
			if (!smp_tsc)
				regs[3] &= ~AMDPM_TSC_INVARIANT;
			break;

		case CPUID_0000_0001:
			do_cpuid(1, regs);

			error = vm_get_x2apic_state(vm, vcpu_id, &x2apic_state);
			if (error) {
				panic("x86_emulate_cpuid: error %d "
				      "fetching x2apic state", error);
			}

			/*
			 * Override the APIC ID only in ebx
			 */
			regs[1] &= ~(CPUID_LOCAL_APIC_ID);
			regs[1] |= (vcpu_id << CPUID_0000_0001_APICID_SHIFT);

			/*
			 * Don't expose VMX, SpeedStep or TME capability.
			 * Advertise x2APIC capability and Hypervisor guest.
			 */
			regs[2] &= ~(CPUID2_VMX | CPUID2_EST | CPUID2_TM2);

			regs[2] |= CPUID2_HV;

			if (x2apic_state != X2APIC_DISABLED)
				regs[2] |= CPUID2_X2APIC;
			else
				regs[2] &= ~CPUID2_X2APIC;

			/*
			 * Only advertise CPUID2_XSAVE in the guest if
			 * the host is using XSAVE.
			 */
			if (!(regs[2] & CPUID2_OSXSAVE))
				regs[2] &= ~CPUID2_XSAVE;

			/*
			 * If CPUID2_XSAVE is being advertised and the
			 * guest has set CR4_XSAVE, set
			 * CPUID2_OSXSAVE.
			 */
			regs[2] &= ~CPUID2_OSXSAVE;
			if (regs[2] & CPUID2_XSAVE) {
				error = vm_get_register(vm, vcpu_id,
				    VM_REG_GUEST_CR4, &cr4);
				if (error)
					panic("x86_emulate_cpuid: error %d "
					      "fetching %%cr4", error);
				if (cr4 & CR4_XSAVE)
					regs[2] |= CPUID2_OSXSAVE;
			}

			/*
			 * Hide monitor/mwait until we know how to deal with
			 * these instructions.
			 */
			regs[2] &= ~CPUID2_MON;

                        /*
			 * Hide the performance and debug features.
			 */
			regs[2] &= ~CPUID2_PDCM;

			/*
			 * No TSC deadline support in the APIC yet
			 */
			regs[2] &= ~CPUID2_TSCDLT;

			/*
			 * Hide thermal monitoring
			 */
			regs[3] &= ~(CPUID_ACPI | CPUID_TM);
			
			/*
			 * Machine check handling is done in the host.
			 * Hide MTRR capability.
			 */
			regs[3] &= ~(CPUID_MCA | CPUID_MCE | CPUID_MTRR);

                        /*
                        * Hide the debug store capability.
                        */
			regs[3] &= ~CPUID_DS;

			/*
			 * Disable multi-core.
			 */
			regs[1] &= ~CPUID_HTT_CORES;
			regs[3] &= ~CPUID_HTT;
			break;

		case CPUID_0000_0004:
			do_cpuid(4, regs);

			/*
			 * Do not expose topology.
			 *
			 * The maximum number of processor cores in
			 * this physical processor package and the
			 * maximum number of threads sharing this
			 * cache are encoded with "plus 1" encoding.
			 * Adding one to the value in this register
			 * field to obtains the actual value.
			 *
			 * Therefore 0 for both indicates 1 core per
			 * package and no cache sharing.
			 */
			regs[0] &= 0xffff8000;
			break;

		case CPUID_0000_0007:
			regs[0] = 0;
			regs[1] = 0;
			regs[2] = 0;
			regs[3] = 0;

			/* leaf 0 */
			if (*ecx == 0) {
				cpuid_count(*eax, *ecx, regs);

				/* Only leaf 0 is supported */
				regs[0] = 0;

				/*
				 * Expose known-safe features.
				 */
				regs[1] &= (CPUID_STDEXT_FSGSBASE |
				    CPUID_STDEXT_BMI1 | CPUID_STDEXT_HLE |
				    CPUID_STDEXT_AVX2 | CPUID_STDEXT_BMI2 |
				    CPUID_STDEXT_ERMS | CPUID_STDEXT_RTM |
				    CPUID_STDEXT_AVX512F |
				    CPUID_STDEXT_AVX512PF |
				    CPUID_STDEXT_AVX512ER |
				    CPUID_STDEXT_AVX512CD);
				regs[2] = 0;
				regs[3] = 0;

				/* Advertise INVPCID if it is enabled. */
				error = vm_get_capability(vm, vcpu_id,
				    VM_CAP_ENABLE_INVPCID, &enable_invpcid);
				if (error == 0 && enable_invpcid)
					regs[1] |= CPUID_STDEXT_INVPCID;
			}
			break;

		case CPUID_0000_0006:
		case CPUID_0000_000A:
			/*
			 * Handle the access, but report 0 for
			 * all options
			 */
			regs[0] = 0;
			regs[1] = 0;
			regs[2] = 0;
			regs[3] = 0;
			break;

		case CPUID_0000_000B:
			/*
			 * Processor topology enumeration
			 */
			regs[0] = 0;
			regs[1] = 0;
			regs[2] = *ecx & 0xff;
			regs[3] = vcpu_id;
			break;

		case CPUID_0000_000D:
			limits = vmm_get_xsave_limits();
			if (!limits->xsave_enabled) {
				regs[0] = 0;
				regs[1] = 0;
				regs[2] = 0;
				regs[3] = 0;
				break;
			}

			cpuid_count(*eax, *ecx, regs);
			switch (*ecx) {
			case 0:
				/*
				 * Only permit the guest to use bits
				 * that are active in the host in
				 * %xcr0.  Also, claim that the
				 * maximum save area size is
				 * equivalent to the host's current
				 * save area size.  Since this runs
				 * "inside" of vmrun(), it runs with
				 * the guest's xcr0, so the current
				 * save area size is correct as-is.
				 */
				regs[0] &= limits->xcr0_allowed;
				regs[2] = limits->xsave_max_size;
				regs[3] &= (limits->xcr0_allowed >> 32);
				break;
			case 1:
				/* Only permit XSAVEOPT. */
				regs[0] &= CPUID_EXTSTATE_XSAVEOPT;
				regs[1] = 0;
				regs[2] = 0;
				regs[3] = 0;
				break;
			default:
				/*
				 * If the leaf is for a permitted feature,
				 * pass through as-is, otherwise return
				 * all zeroes.
				 */
				if (!(limits->xcr0_allowed & (1ul << *ecx))) {
					regs[0] = 0;
					regs[1] = 0;
					regs[2] = 0;
					regs[3] = 0;
				}
				break;
			}
			break;

		case 0x40000000:
			regs[0] = CPUID_VM_HIGH;
			bcopy(bhyve_id, &regs[1], 4);
			bcopy(bhyve_id + 4, &regs[2], 4);
			bcopy(bhyve_id + 8, &regs[3], 4);
			break;

		default:
			/*
			 * The leaf value has already been clamped so
			 * simply pass this through, keeping count of
			 * how many unhandled leaf values have been seen.
			 */
			atomic_add_long(&bhyve_xcpuids, 1);
			cpuid_count(*eax, *ecx, regs);
			break;
	}

	*eax = regs[0];
	*ebx = regs[1];
	*ecx = regs[2];
	*edx = regs[3];

	return (1);
}
