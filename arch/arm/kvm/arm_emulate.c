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

#include <linux/mm.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <trace/events/kvm.h>

#include "trace.h"
#include "debug.h"
#include "trace.h"

#define USR_REG_OFFSET_0_7 \
	/* r0 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[0]), \
	/* r1 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[1]), \
	/* r2 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[2]), \
	/* r3 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[3]), \
	/* r4 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[4]), \
	/* r5 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[5]), \
	/* r6 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[6]), \
	/* r7 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[7])
#define USR_REG_OFFSET_8_12 \
	/* r8 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[8]), \
	/* r9 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[9]), \
	/* r10 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[10]), \
	/* r11 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[11]), \
	/* r12 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[12]) \

static unsigned long vcpu_reg_offsets[MODE_SYS + 1][16] =
{
	/* FIQ Registers */
	{
			USR_REG_OFFSET_0_7,
	/* r8 */	offsetof(struct kvm_vcpu_arch, regs.fiq_regs[0]),
	/* r9 */	offsetof(struct kvm_vcpu_arch, regs.fiq_regs[1]),
	/* r10 */	offsetof(struct kvm_vcpu_arch, regs.fiq_regs[2]),
	/* r11 */	offsetof(struct kvm_vcpu_arch, regs.fiq_regs[3]),
	/* r12 */	offsetof(struct kvm_vcpu_arch, regs.fiq_regs[4]),
	/* r13 */	offsetof(struct kvm_vcpu_arch, regs.fiq_regs[5]),
	/* r14 */	offsetof(struct kvm_vcpu_arch, regs.fiq_regs[6]),
	/* r15 */	offsetof(struct kvm_vcpu_arch, regs.pc),
	},

	/* IRQ Registers */
	{
			USR_REG_OFFSET_0_7,
			USR_REG_OFFSET_8_12,
	/* r13 */	offsetof(struct kvm_vcpu_arch, regs.irq_regs[0]),
	/* r14 */	offsetof(struct kvm_vcpu_arch, regs.irq_regs[1]),
	/* r15 */	offsetof(struct kvm_vcpu_arch, regs.pc),
	},

	/* SVC Registers */
	{
			USR_REG_OFFSET_0_7,
			USR_REG_OFFSET_8_12,
	/* r13 */	offsetof(struct kvm_vcpu_arch, regs.svc_regs[0]),
	/* r14 */	offsetof(struct kvm_vcpu_arch, regs.svc_regs[1]),
	/* r15 */	offsetof(struct kvm_vcpu_arch, regs.pc),
	},

	/* ABT Registers */
	{
			USR_REG_OFFSET_0_7,
			USR_REG_OFFSET_8_12,
	/* r13 */	offsetof(struct kvm_vcpu_arch, regs.abt_regs[0]),
	/* r14 */	offsetof(struct kvm_vcpu_arch, regs.abt_regs[1]),
	/* r15 */	offsetof(struct kvm_vcpu_arch, regs.pc),
	},

	/* UND Registers */
	{
			USR_REG_OFFSET_0_7,
			USR_REG_OFFSET_8_12,
	/* r13 */	offsetof(struct kvm_vcpu_arch, regs.und_regs[0]),
	/* r14 */	offsetof(struct kvm_vcpu_arch, regs.und_regs[1]),
	/* r15 */	offsetof(struct kvm_vcpu_arch, regs.pc),
	},

	/* USR Registers */
	{
			USR_REG_OFFSET_0_7,
			USR_REG_OFFSET_8_12,
	/* r13 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[13]),
	/* r14 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[14]),
	/* r15 */	offsetof(struct kvm_vcpu_arch, regs.pc),
	},

	/* SYS Registers */
	{
			USR_REG_OFFSET_0_7,
			USR_REG_OFFSET_8_12,
	/* r13 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[13]),
	/* r14 */	offsetof(struct kvm_vcpu_arch, regs.usr_regs[14]),
	/* r15 */	offsetof(struct kvm_vcpu_arch, regs.pc),
	},
};

/*
 * Return a pointer to the register number valid in the specified mode of
 * the virtual CPU.
 */
u32* kvm_vcpu_reg(struct kvm_vcpu *vcpu, u8 reg_num, u32 mode)
{
	BUG_ON(reg_num > 15);
	BUG_ON(mode > MODE_SYS);

	return (u32 *)((void *)&vcpu->arch + vcpu_reg_offsets[mode][reg_num]);
}

/******************************************************************************
 * Utility functions common for all emulation code
 *****************************************************************************/

/*
 * This one accepts a matrix where the first element is the
 * bits as they must be, and the second element is the bitmask.
 */
#define INSTR_NONE	-1
static int kvm_instr_index(u32 instr, u32 table[][2], int table_entries)
{
	int i;
	u32 mask;

	for (i = 0; i < table_entries; i++) {
		mask = table[i][1];
		if ((table[i][0] & mask) == (instr & mask))
			return i;
	}
	return INSTR_NONE;
}

/******************************************************************************
 * Co-processor emulation
 *****************************************************************************/

struct coproc_params {
	unsigned long CRm;
	unsigned long CRn;
	unsigned long Op1;
	unsigned long Op2;
	unsigned long Rt1;
	unsigned long Rt2;
	bool is_64bit;
	bool is_write;
};

#define CP15_OP(_vcpu, _params, _cp15_reg) \
do { \
	if (_params->is_write) \
		_vcpu->arch.cp15._cp15_reg = *vcpu_reg(_vcpu, _params->Rt1); \
	else \
		*vcpu_reg(_vcpu, _params->Rt1) = _vcpu->arch.cp15._cp15_reg; \
} while (0);


static inline void print_cp_instr(struct coproc_params *p)
{
	if (p->is_64bit) {
		kvm_msg("    %s\tp15, %u, r%u, r%u, c%u",
				(p->is_write) ? "mcrr" : "mrrc",
				p->Op1, p->Rt1, p->Rt2, p->CRm);
	} else {
		kvm_msg("    %s\tp15, %u, r%u, c%u, c%u, %u",
				(p->is_write) ? "mcr" : "mrc",
				p->Op1, p->Rt1, p->CRn, p->CRm, p->Op2);
	}
}

int kvm_handle_cp10_id(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	KVMARM_NOT_IMPLEMENTED();
	return -EINVAL;
}

int kvm_handle_cp_0_13_access(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	KVMARM_NOT_IMPLEMENTED();
	return -EINVAL;
}

int kvm_handle_cp14_load_store(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	KVMARM_NOT_IMPLEMENTED();
	return -EINVAL;
}

int kvm_handle_cp14_access(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	KVMARM_NOT_IMPLEMENTED();
	return -EINVAL;
}

/**
 * emulate_cp15_c10_access -- emulates cp15 accesses for CRn == 10
 * @vcpu: The VCPU pointer
 * @p:    The coprocessor parameters struct pointer holding trap inst. details
 *
 * This funciton may not need to exist - if we can ignore guest attempts to
 * tamper with TLB lockdowns then it should be enough to store/restore the
 * host/guest PRRR and NMRR memory remap registers and allow guest direct access
 * to these registers.
 */
static int emulate_cp15_c10_access(struct kvm_vcpu *vcpu,
				   struct coproc_params *p)
{
	BUG_ON(p->CRn != 10);
	BUG_ON(p->is_64bit);

	if ((p->CRm == 0 || p->CRm == 1 || p->CRm == 4 || p->CRm == 8) &&
	    (p->Op2 <= 7)) {
		/* TLB Lockdown operations - ignored */
		return 0;
	}

	if (p->CRm == 2 && p->Op2 == 0) {
		CP15_OP(vcpu, p, c10_PRRR);
		return 0;
	}

	if (p->CRm == 2 && p->Op2 == 1) {
		CP15_OP(vcpu, p, c10_NMRR);
		return 0;
	}

	return -EINVAL;
}

/**
 * emulate_cp15_c15_access -- emulates cp15 accesses for CRn == 15
 * @vcpu: The VCPU pointer
 * @p:    The coprocessor parameters struct pointer holding trap inst. details
 *
 * The CP15 c15 register is implementation defined, but some guest kernels
 * attempt to read/write a diagnostics register here. We always return 0 and
 * ignore writes and hope for the best. This may need to be refined.
 */
static int emulate_cp15_c15_access(struct kvm_vcpu *vcpu,
				   struct coproc_params *p)
{
	trace_kvm_emulate_cp15_imp(p->Op1, p->Rt1, p->CRn, p->CRm,
				   p->Op2, p->is_write);

	if (!p->is_write)
		*vcpu_reg(vcpu, p->Rt1) = 0;

	return 0;
}

/**
 * kvm_handle_cp15_access -- handles a trap on a guest CP15 access
 * @vcpu: The VCPU pointer
 * @run:  The kvm_run struct
 *
 * Investigates the CRn/CRm and wether this was mcr/mrc or mcrr/mrrc and either
 * simply errors out if the operation was not supported (should maybe raise
 * undefined to guest instead?) and otherwise emulated access.
 */
int kvm_handle_cp15_access(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	unsigned long hsr_ec, instr_len;
	struct coproc_params params;
	int ret = 0;

	hsr_ec = vcpu->arch.hsr >> HSR_EC_SHIFT;
	params.CRm = (vcpu->arch.hsr >> 1) & 0xf;
	params.Rt1 = (vcpu->arch.hsr >> 5) & 0xf;
	BUG_ON(params.Rt1 >= 15);
	params.is_write = ((vcpu->arch.hsr & 1) == 0);
	params.is_64bit = (hsr_ec == HSR_EC_CP15_64);

	if (params.is_64bit) {
		/* mrrc, mccr operation */
		params.Op1 = (vcpu->arch.hsr >> 16) & 0xf;
		params.Op2 = 0;
		params.Rt2 = (vcpu->arch.hsr >> 10) & 0xf;
		BUG_ON(params.Rt2 >= 15);
		params.CRn = 0;
	} else {
		params.CRn = (vcpu->arch.hsr >> 10) & 0xf;
		params.Op1 = (vcpu->arch.hsr >> 14) & 0x7;
		params.Op2 = (vcpu->arch.hsr >> 17) & 0x7;
		params.Rt2 = 0;
	}

	/* So far no mrrc/mcrr accesses are emulated */
	if (params.is_64bit)
		goto unsupp_err_out;

	switch (params.CRn) {
	case 10:
		ret = emulate_cp15_c10_access(vcpu, &params);
		break;
	case 15:
		ret = emulate_cp15_c15_access(vcpu, &params);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		goto unsupp_err_out;

	/* Skip instruction, since it was emulated */
	instr_len = ((vcpu->arch.hsr >> 25) & 1) ? 4 : 2;
	*vcpu_reg(vcpu, 15) += instr_len;

	return ret;
unsupp_err_out:
	kvm_msg("Unsupported guest CP15 access at: %08x", vcpu->arch.regs.pc);
	print_cp_instr(&params);
	return -EINVAL;
}

int kvm_handle_wfi(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	return 0;
}


/******************************************************************************
 * Load-Store instruction emulation
 *****************************************************************************/

/*
 * Must be ordered with LOADS first and WRITES afterwards
 * for easy distinction when doing MMIO.
 */
#define NUM_LD_INSTR  9
enum INSTR_LS_INDEXES
{
	INSTR_LS_LDRBT, INSTR_LS_LDRT, INSTR_LS_LDR, INSTR_LS_LDRB,
	INSTR_LS_LDRD, INSTR_LS_LDREX, INSTR_LS_LDRH, INSTR_LS_LDRSB,
	INSTR_LS_LDRSH,
	INSTR_LS_STRBT, INSTR_LS_STRT, INSTR_LS_STR, INSTR_LS_STRB,
	INSTR_LS_STRD, INSTR_LS_STREX, INSTR_LS_STRH,
	NUM_LS_INSTR
};

static u32 ls_instr[NUM_LS_INSTR][2] = {
	 {0x04700000, 0x0d700000} /* LDRBT */
	,{0x04300000, 0x0d700000} /* LDRT  */
	,{0x04100000, 0x0c500000} /* LDR   */
	,{0x04500000, 0x0c500000} /* LDRB  */
	,{0x000000d0, 0x0e1000f0} /* LDRD  */
	,{0x01900090, 0x0ff000f0} /* LDREX */
	,{0x001000b0, 0x0e1000f0} /* LDRH  */
	,{0x001000d0, 0x0e1000f0} /* LDRSB */
	,{0x001000f0, 0x0e1000f0} /* LDRSH */
	,{0x04600000, 0x0d700000} /* STRBT */
	,{0x04200000, 0x0d700000} /* STRT  */
	,{0x04000000, 0x0c500000} /* STR   */
	,{0x04400000, 0x0c500000} /* STRB  */
	,{0x000000f0, 0x0e1000f0} /* STRD  */
	,{0x01800090, 0x0ff000f0} /* STREX */
	,{0x000000b0, 0x0e1000f0} /* STRH  */
};

static inline int get_arm_ls_instr_index(u32 instr)
{
	return kvm_instr_index(instr, ls_instr, NUM_LS_INSTR);
}

/*
 * Load-Store instruction decoding
 */
#define INSTR_LS_TYPE_BIT		26
#define INSTR_LS_RD_MASK		0x0000f000
#define INSTR_LS_RD_SHIFT		12
#define INSTR_LS_RN_MASK		0x000f0000
#define INSTR_LS_RN_SHIFT		16
#define INSTR_LS_RM_MASK		0x0000000f
#define INSTR_LS_OFFSET12_MASK		0x00000fff

#define INSTR_LS_BIT_P			24
#define INSTR_LS_BIT_U			23
#define INSTR_LS_BIT_B			22
#define INSTR_LS_BIT_W			21
#define INSTR_LS_BIT_L			20
#define INSTR_LS_BIT_S			 6
#define INSTR_LS_BIT_H			 5

/*
 * ARM addressing mode defines
 */
#define OFFSET_IMM_MASK			0x0e000000
#define OFFSET_IMM_VALUE		0x04000000
#define OFFSET_REG_MASK			0x0e000ff0
#define OFFSET_REG_VALUE		0x06000000
#define OFFSET_SCALE_MASK		0x0e000010
#define OFFSET_SCALE_VALUE		0x06000000

#define SCALE_SHIFT_MASK		0x000000a0
#define SCALE_SHIFT_SHIFT		5
#define SCALE_SHIFT_LSL			0x0
#define SCALE_SHIFT_LSR			0x1
#define SCALE_SHIFT_ASR			0x2
#define SCALE_SHIFT_ROR_RRX		0x3
#define SCALE_SHIFT_IMM_MASK		0x00000f80
#define SCALE_SHIFT_IMM_SHIFT		6

#define PSR_BIT_C			29

static unsigned long ls_word_calc_offset(struct kvm_vcpu *vcpu,
					 unsigned long instr)
{
	int offset = 0;

	if ((instr & OFFSET_IMM_MASK) == OFFSET_IMM_VALUE) {
		/* Immediate offset/index */
		offset = instr & INSTR_LS_OFFSET12_MASK;

		if (!(instr & (1U << INSTR_LS_BIT_U))) {
			offset = -offset;
		}
	}

	if ((instr & OFFSET_REG_MASK) == OFFSET_REG_VALUE) {
		/* Register offset/index */
		u8 rm = instr & INSTR_LS_RM_MASK;
		offset = *vcpu_reg(vcpu, rm);

		if (!(instr & (1U << INSTR_LS_BIT_P)))
			offset = 0;
	}

	if ((instr & OFFSET_SCALE_MASK) == OFFSET_SCALE_VALUE) {
		/* Scaled register offset */
		int asr_test;
		u8 rm = instr & INSTR_LS_RM_MASK;
		u8 shift = (instr & SCALE_SHIFT_MASK) >> SCALE_SHIFT_SHIFT;
		u32 shift_imm = (instr & SCALE_SHIFT_IMM_MASK)
				>> SCALE_SHIFT_IMM_SHIFT;
		offset = *vcpu_reg(vcpu, rm);

		switch (shift) {
		case SCALE_SHIFT_LSL:
			offset = offset << shift_imm;
			break;
		case SCALE_SHIFT_LSR:
			if (shift_imm == 0)
				offset = 0;
			else
				offset = ((u32)offset) >> shift_imm;
			break;
		case SCALE_SHIFT_ASR:
			/* Test that the compiler used arithmetic right shift
			 * for signed values. */
			asr_test = 0xffffffff;
			BUG_ON((asr_test >> 2) >= 0);
			if (shift_imm == 0) {
				if (offset & (1U << 31))
					offset = 0xffffffff;
				else
					offset = 0;
			} else {
				offset = offset >> shift_imm;
			}
			break;
		case SCALE_SHIFT_ROR_RRX:
			/* Test that the compiler used arithmetic right shift
			 * for signed values. */
			asr_test = 0xffffffff;
			BUG_ON((asr_test >> 2) >= 0);
			if (shift_imm == 0) {
				u32 C = (vcpu->arch.regs.cpsr &
						(1U << PSR_BIT_C));
				offset = (C << 31) | offset >> 1;
			} else {
				offset = ror32(offset, shift_imm);
			}
			break;
		}

		if (instr & (1U << INSTR_LS_BIT_U))
			return offset;
		else
			return -offset;
	}

	if (instr & (1U << INSTR_LS_BIT_U))
		return offset;
	else
		return -offset;

	BUG();
}

static int kvm_ls_length(struct kvm_vcpu *vcpu, u32 instr)
{
	int index;

	index = get_arm_ls_instr_index(instr);
	BUG_ON(index == INSTR_NONE);

	if (instr & (1U << INSTR_LS_TYPE_BIT)) {
		/* LS word or unsigned byte */
		if (instr & (1U << INSTR_LS_BIT_B))
			return sizeof(unsigned char);
		else
			return sizeof(u32);
	} else {
		/* LS halfword, doubleword or signed byte */
		u32 H = (instr & (1U << INSTR_LS_BIT_H));
		u32 S = (instr & (1U << INSTR_LS_BIT_S));
		u32 L = (instr & (1U << INSTR_LS_BIT_L));

		if (!L && S) {
			kvm_msg("WARNING: d-word for MMIO");
			return 2 * sizeof(u32);
		} else if (L && S && !H)
			return sizeof(char);
		else
			return sizeof(u16);
	}

	BUG();
}

/**
 *
 */
int kvm_emulate_mmio_ls(struct kvm_vcpu *vcpu, phys_addr_t fault_ipa,
			unsigned long instr)
{
	unsigned long rd, rn, offset, len;
	int index;
	bool is_write;

	index = get_arm_ls_instr_index(instr);
	if (index == INSTR_NONE) {
		kvm_err(-EINVAL, "Unknown load/store instruction");
		return -EINVAL;
	}

	is_write = (index < NUM_LD_INSTR) ? false : true;
	rd = (instr & INSTR_LS_RD_MASK) >> INSTR_LS_RD_SHIFT;
	len = kvm_ls_length(vcpu, instr);

	vcpu->run->exit_reason = KVM_EXIT_MMIO;
	vcpu->run->mmio.is_write = is_write;
	vcpu->run->mmio.phys_addr = fault_ipa;
	vcpu->run->mmio.len = len;
	vcpu->arch.mmio_sign_extend = false;
	vcpu->arch.mmio_rd = rd;

	trace_kvm_mmio_emulate(vcpu->arch.regs.pc);
	trace_kvm_mmio((is_write) ? KVM_TRACE_MMIO_WRITE :
				    KVM_TRACE_MMIO_READ_UNSATISFIED,
			len, fault_ipa, (is_write) ? *vcpu_reg(vcpu, rd) : 0);

	/* Handle base register writeback */
	if (!(instr & (1U << INSTR_LS_BIT_P)) ||
	     (instr & (1U << INSTR_LS_BIT_W))) {
		rn = (instr & INSTR_LS_RN_MASK) >> INSTR_LS_RN_SHIFT;
		offset = ls_word_calc_offset(vcpu, instr);
		*vcpu_reg(vcpu, rn) += offset;
	}

	/*
	 * The MMIO instruction is emulated and should not be re-executed
	 * in the guest. (XXX We don't support Thumb instructions yet).
	 */
	*vcpu_reg(vcpu, 15) += 4;
	return 0;
}
