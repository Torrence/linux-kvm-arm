/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef __ARM_KVM_HOST_H__
#define __ARM_KVM_HOST_H__

#define KVM_MAX_VCPUS 1
#define KVM_MEMORY_SLOTS 32
#define KVM_PRIVATE_MEM_SLOTS 4
#define KVM_COALESCED_MMIO_PAGE_OFFSET 1

/* We don't currently support large pages. */
#define KVM_HPAGE_GFN_SHIFT(x)	0
#define KVM_NR_PAGE_SIZES	1
#define KVM_PAGES_PER_HPAGE(x)	(1UL<<31)

struct kvm_vcpu;
u32* kvm_vcpu_reg(struct kvm_vcpu *vcpu, u8 reg_num, u32 mode);

struct kvm_arch {
	pgd_t *pgd;     /* 1-level 2nd stage table */
};

#define EXCEPTION_NONE      0
#define EXCEPTION_RESET     0x80
#define EXCEPTION_UNDEFINED 0x40
#define EXCEPTION_SOFTWARE  0x20
#define EXCEPTION_PREFETCH  0x10
#define EXCEPTION_DATA      0x08
#define EXCEPTION_IMPRECISE 0x04
#define EXCEPTION_IRQ       0x02
#define EXCEPTION_FIQ       0x01

struct kvm_vcpu_regs {
	u32 usr_regs[15];	/* R0_usr - R14_usr */
	u32 svc_regs[3];	/* SP_svc, LR_svc, SPSR_svc */
	u32 abt_regs[3];	/* SP_abt, LR_abt, SPSR_abt */
	u32 und_regs[3];	/* SP_und, LR_und, SPSR_und */
	u32 irq_regs[3];	/* SP_irq, LR_irq, SPSR_irq */
	u32 fiq_regs[8];	/* R8_fiq - R14_fiq, SPSR_fiq */
	u32 pc;			/* The program counter (r15) */
	u32 cpsr;		/* Guest emulated CPSR */
} __packed;

struct kvm_vcpu_arch {
	/* Pointer to regs struct on shared page */
	struct kvm_vcpu_regs regs;

	/* Pointer to cached mode on shared page */
	unsigned long *mode;

	/* System control coprocessor (cp15) */
	struct {
		u32 c1_SCTLR;		/* System Control Register */
		u32 c1_ACTLR;		/* Auxilliary Control Register */
		u32 c1_CPACR;		/* Coprocessor Access Control Register */
		u64 c2_TTBR0;		/* Translation Table Base Register 0 */
		u64 c2_TTBR1;		/* Translation Table Base Register 1 */
		u32 c2_TTBCR;		/* Translation Table Base Control Register */
		u32 c3_DACR;		/* Domain Access Control Register */
		u32 c5_DFSR;		/* Fault Status Register */
	} cp15;

	u32 guest_exception;  		/* Hardware exception that exited the guest */
	u32 exception_pending;  	/* Exception to raise after emulation */

	/* Host status */
	u32 hsr;		/* Hyp Syndrom Register */
	u32 host_far;		/* Fault access register */
	u32 host_fsr;		/* Fault status register */
	u32 host_ifsr;		/* Fault status register */

	/* IO related fields */
	u32 mmio_rd;

	/* Misc. fields */
	u32 wait_for_interrupts;

	struct kvm_run *kvm_run;
};

struct kvm_vm_stat {
	u32 remote_tlb_flush;
};

struct kvm_vcpu_stat {
	u32 sum_exits;
	u32 mmio_exits;
	u32 dcr_exits;
	u32 signal_exits;
	u32 light_exits;
	/* Account for special types of light exits: */
	u32 itlb_real_miss_exits;
	u32 itlb_virt_miss_exits;
	u32 dtlb_real_miss_exits;
	u32 dtlb_virt_miss_exits;
	u32 syscall_exits;
	u32 isi_exits;
	u32 dsi_exits;
	u32 emulated_inst_exits;
	u32 dec_exits;
	u32 ext_intr_exits;
	u32 halt_wakeup;
};

#endif /* __ARM_KVM_HOST_H__ */
