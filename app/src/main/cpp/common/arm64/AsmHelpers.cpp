// SPDX-FileCopyrightText: 2002-2024 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0

#include "common/arm64/AsmHelpers.h"

#include "common/Assertions.h"
#include "common/BitUtils.h"
#include "common/Console.h"
#include "common/HostSys.h"

const a64::Register& armWRegister(int n)
{
    using namespace vixl::aarch64;
    static constexpr const Register* regs[32] = {&w0, &w1, &w2, &w3, &w4, &w5, &w6, &w7, &w8, &w9, &w10,
                                                 &w11, &w12, &w13, &w14, &w15, &w16, &w17, &w18, &w19, &w20, &w21, &w22, &w23, &w24, &w25, &w26, &w27, &w28,
                                                 &w29, &w30, &w31};
    pxAssert(static_cast<size_t>(n) < std::size(regs));
    return *regs[n];
}

const a64::Register& armXRegister(int n)
{
    using namespace vixl::aarch64;
    static constexpr const Register* regs[32] = {&x0, &x1, &x2, &x3, &x4, &x5, &x6, &x7, &x8, &x9, &x10,
                                                 &x11, &x12, &x13, &x14, &x15, &x16, &x17, &x18, &x19, &x20, &x21, &x22, &x23, &x24, &x25, &x26, &x27, &x28,
                                                 &x29, &x30, &x31};
    pxAssert(static_cast<size_t>(n) < std::size(regs));
    return *regs[n];
}

const a64::VRegister& armSRegister(int n)
{
    using namespace vixl::aarch64;
    static constexpr const VRegister* regs[32] = {&s0, &s1, &s2, &s3, &s4, &s5, &s6, &s7, &vixl::aarch64::s8, &s9, &s10,
                                                  &s11, &s12, &s13, &s14, &s15, &vixl::aarch64::s16, &s17, &s18, &s19, &s20, &s21, &s22, &s23, &s24, &s25, &s26, &s27, &s28,
                                                  &s29, &s30, &s31};
    pxAssert(static_cast<size_t>(n) < std::size(regs));
    return *regs[n];
}

const a64::VRegister& armDRegister(int n)
{
    using namespace vixl::aarch64;
    static constexpr const VRegister* regs[32] = {&d0, &d1, &d2, &d3, &d4, &d5, &d6, &d7, &d8, &d9, &d10,
                                                  &d11, &d12, &d13, &d14, &d15, &d16, &d17, &d18, &d19, &d20, &d21, &d22, &d23, &d24, &d25, &d26, &d27, &d28,
                                                  &d29, &d30, &d31};
    pxAssert(static_cast<size_t>(n) < std::size(regs));
    return *regs[n];
}

const a64::VRegister& armQRegister(int n)
{
    using namespace vixl::aarch64;
    static constexpr const VRegister* regs[32] = {&q0, &q1, &q2, &q3, &q4, &q5, &q6, &q7, &q8, &q9, &q10,
                                                  &q11, &q12, &q13, &q14, &q15, &q16, &q17, &q18, &q19, &q20, &q21, &q22, &q23, &q24, &q25, &q26, &q27, &q28,
                                                  &q29, &q30, &q31};
    pxAssert(static_cast<size_t>(n) < std::size(regs));
    return *regs[n];
}


//#define INCLUDE_DISASSEMBLER

#ifdef INCLUDE_DISASSEMBLER
#include "vixl/aarch64/disasm-aarch64.h"
#endif

thread_local a64::MacroAssembler* armAsm;
thread_local u8* armAsmPtr;
thread_local size_t armAsmCapacity;
thread_local ArmConstantPool* armConstantPool;

#ifdef INCLUDE_DISASSEMBLER
static std::mutex armDisasmMutex;
static std::unique_ptr<a64::PrintDisassembler> armDisasm;
static std::unique_ptr<a64::Decoder> armDisasmDecoder;
#endif

void armSetAsmPtr(void* ptr, size_t capacity, ArmConstantPool* pool)
{
    pxAssert(!armAsm);
    armAsmPtr = static_cast<u8*>(ptr);
    armAsmCapacity = capacity;
    armConstantPool = pool;
}

// Align to 16 bytes, apparently ARM likes that.
void armAlignAsmPtr()
{
    static constexpr uintptr_t ALIGNMENT = 16;
    u8* new_ptr = reinterpret_cast<u8*>((reinterpret_cast<uintptr_t>(armAsmPtr) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1));
    pxAssert(static_cast<size_t>(new_ptr - armAsmPtr) <= armAsmCapacity);
    armAsmCapacity -= (new_ptr - armAsmPtr);
    armAsmPtr = new_ptr;
}

u8* armStartBlock()
{
    armAlignAsmPtr();

    HostSys::BeginCodeWrite();

    pxAssert(!armAsm);
    armAsm = new a64::MacroAssembler(static_cast<vixl::byte*>(armAsmPtr), armAsmCapacity);
    armAsm->GetScratchVRegisterList()->Remove(31);
    armAsm->GetScratchRegisterList()->Remove(RSCRATCHADDR.GetCode());
    return armAsmPtr;
}

u8* armEndBlock()
{
    pxAssert(armAsm);

    armAsm->FinalizeCode();

    const u32 size = static_cast<u32>(armAsm->GetSizeOfCodeGenerated());
    pxAssert(size < armAsmCapacity);

    delete armAsm;
    armAsm = nullptr;

    HostSys::EndCodeWrite();

    HostSys::FlushInstructionCache(armAsmPtr, size);

    armAsmPtr = armAsmPtr + size;
    armAsmCapacity -= size;
    return armAsmPtr;
}

void armDisassembleAndDumpCode(const void* ptr, size_t size)
{
#ifdef INCLUDE_DISASSEMBLER
    std::unique_lock lock(armDisasmMutex);
	if (!armDisasm)
	{
		armDisasm = std::make_unique<a64::PrintDisassembler>(stderr);
		armDisasmDecoder = std::make_unique<a64::Decoder>();
		armDisasmDecoder->AppendVisitor(armDisasm.get());
	}

	armDisasmDecoder->Decode(static_cast<const a64::Instruction*>(ptr), static_cast<const a64::Instruction*>(ptr) + size);
#else
    Console.Error("Not compiled with INCLUDE_DISASSEMBLER");
#endif
}

void armEmitJmp(const void* ptr, bool force_inline)
{
    s64 displacement = GetPCDisplacement(armGetCurrentCodePointer(), ptr);
    bool use_blr = !vixl::IsInt26(displacement);
    if (use_blr && armConstantPool && !force_inline)
    {
        if (u8* trampoline = armConstantPool->GetJumpTrampoline(ptr); trampoline)
        {
            displacement = GetPCDisplacement(armGetCurrentCodePointer(), trampoline);
            use_blr = !vixl::IsInt26(displacement);
        }
    }

    if (use_blr)
    {
        armAsm->Mov(RXVIXLSCRATCH, reinterpret_cast<uintptr_t>(ptr));
        armAsm->Br(RXVIXLSCRATCH);
    }
    else
    {
        a64::SingleEmissionCheckScope guard(armAsm);
        armAsm->b(displacement);
    }
}

//a64::RegList g_cpuList(a64::x3.GetBit() | a64::x5.GetBit() | a64::x12.GetBit() | a64::x13.GetBit() | a64::x14.GetBit() | a64::x15.GetBit());
//a64::CPURegList g_caller_regs = a64::CPURegList(a64::CPURegister::kRegister, a64::kXRegSize, g_cpuList);

void armEmitCall(const void* ptr, bool force_inline)
{
//    armAsm->PushCPURegList(g_caller_regs);

    armAsm->Mov(RSTATE_x19, a64::x3);
    armAsm->Mov(RSTATE_x20, a64::x5);
    armAsm->Mov(RSTATE_x21, a64::x12);
    armAsm->Mov(RSTATE_x22, a64::x13);
    armAsm->Mov(RSTATE_x23, a64::x14);
    armAsm->Mov(RSTATE_x24, a64::x15);

    s64 displacement = GetPCDisplacement(armGetCurrentCodePointer(), ptr);
    bool use_blr = !vixl::IsInt26(displacement);
    if (use_blr && armConstantPool && !force_inline)
    {
        if (u8* trampoline = armConstantPool->GetJumpTrampoline(ptr); trampoline)
        {
            displacement = GetPCDisplacement(armGetCurrentCodePointer(), trampoline);
            use_blr = !vixl::IsInt26(displacement);
        }
    }

    if (use_blr)
    {
        armAsm->Mov(RXVIXLSCRATCH, reinterpret_cast<uintptr_t>(ptr));
        armAsm->Blr(RXVIXLSCRATCH);
    }
    else
    {
        a64::SingleEmissionCheckScope guard(armAsm);
        armAsm->bl(displacement);
    }

//    armAsm->PopCPURegList(g_caller_regs);

    armAsm->Mov(a64::x3,  RSTATE_x19);
    armAsm->Mov(a64::x5,  RSTATE_x20);
    armAsm->Mov(a64::x12, RSTATE_x21);
    armAsm->Mov(a64::x13, RSTATE_x22);
    armAsm->Mov(a64::x14, RSTATE_x23);
    armAsm->Mov(a64::x15, RSTATE_x24);
}

void armEmitCbnz(const a64::Register& reg, const void* ptr)
{
    const s64 jump_distance =
            static_cast<s64>(reinterpret_cast<intptr_t>(ptr) - reinterpret_cast<intptr_t>(armGetCurrentCodePointer()));
    //pxAssert(Common::IsAligned(jump_distance, 4));
    if (a64::Instruction::IsValidImmPCOffset(a64::CompareBranchType, jump_distance >> 2))
    {
        a64::SingleEmissionCheckScope guard(armAsm);
        armAsm->cbnz(reg, jump_distance >> 2);
    }
    else
    {
        a64::MacroEmissionCheckScope guard(armAsm);
        a64::Label branch_not_taken;
        armAsm->cbz(reg, &branch_not_taken);

        const s64 new_jump_distance =
                static_cast<s64>(reinterpret_cast<intptr_t>(ptr) - reinterpret_cast<intptr_t>(armGetCurrentCodePointer()));
        armAsm->b(new_jump_distance >> 2);
        armAsm->bind(&branch_not_taken);
    }
}

void armEmitCondBranch(a64::Condition cond, const void* ptr)
{
    const s64 jump_distance =
            static_cast<s64>(reinterpret_cast<intptr_t>(ptr) - reinterpret_cast<intptr_t>(armGetCurrentCodePointer()));
    //pxAssert(Common::IsAligned(jump_distance, 4));

    if (a64::Instruction::IsValidImmPCOffset(a64::CondBranchType, jump_distance >> 2))
    {
        a64::SingleEmissionCheckScope guard(armAsm);
        armAsm->b(jump_distance >> 2, cond);
    }
    else
    {
        a64::MacroEmissionCheckScope guard(armAsm);
        a64::Label branch_not_taken;
        armAsm->b(&branch_not_taken, a64::InvertCondition(cond));

        const s64 new_jump_distance =
                static_cast<s64>(reinterpret_cast<intptr_t>(ptr) - reinterpret_cast<intptr_t>(armGetCurrentCodePointer()));
        armAsm->b(new_jump_distance >> 2);
        armAsm->bind(&branch_not_taken);
    }
}

void armMoveAddressToReg(const a64::Register& reg, const void* addr)
{
    // psxAsm->Mov(reg, static_cast<u64>(reinterpret_cast<uintptr_t>(addr)));
    pxAssert(reg.IsX());

    const void* current_code_ptr_page = reinterpret_cast<const void*>(
            reinterpret_cast<uintptr_t>(armGetCurrentCodePointer()) & ~static_cast<uintptr_t>(0xFFF));
    const void* ptr_page =
            reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(addr) & ~static_cast<uintptr_t>(0xFFF));
    const s64 page_displacement = GetPCDisplacement(current_code_ptr_page, ptr_page) >> 10;
    const u32 page_offset = static_cast<u32>(reinterpret_cast<uintptr_t>(addr) & 0xFFFu);
    if (vixl::IsInt21(page_displacement) && a64::Assembler::IsImmAddSub(page_offset))
    {
        {
            a64::SingleEmissionCheckScope guard(armAsm);
            armAsm->adrp(reg, page_displacement);
        }
        armAsm->Add(reg, reg, page_offset);
    }
    else if (vixl::IsInt21(page_displacement) && a64::Assembler::IsImmLogical(page_offset, 64))
    {
        {
            a64::SingleEmissionCheckScope guard(armAsm);
            armAsm->adrp(reg, page_displacement);
        }
        armAsm->Orr(reg, reg, page_offset);
    }
    else
    {
        armAsm->Mov(reg, reinterpret_cast<uintptr_t>(addr));
    }
}

void armLoadPtr(const a64::CPURegister& reg, const void* addr)
{
    armMoveAddressToReg(RSCRATCHADDR, addr);
    armAsm->Ldr(reg, a64::MemOperand(RSCRATCHADDR));
}

void armStorePtr(const a64::CPURegister& reg, const void* addr)
{
    armMoveAddressToReg(RSCRATCHADDR, addr);
    armAsm->Str(reg, a64::MemOperand(RSCRATCHADDR));
}

void armBeginStackFrame(bool save_fpr)
{
    // save x19 through x28, x29 could also be used
    armAsm->Sub(a64::sp, a64::sp, save_fpr ? 192 : 144);
    armAsm->Stp(a64::x19, a64::x20, a64::MemOperand(a64::sp, 32));
    armAsm->Stp(a64::x21, a64::x22, a64::MemOperand(a64::sp, 48));
    armAsm->Stp(a64::x23, a64::x24, a64::MemOperand(a64::sp, 64));
    armAsm->Stp(a64::x25, a64::x26, a64::MemOperand(a64::sp, 80));
    armAsm->Stp(a64::x27, a64::x28, a64::MemOperand(a64::sp, 96));
    armAsm->Stp(a64::x29, a64::lr, a64::MemOperand(a64::sp, 112));
    if (save_fpr)
    {
        armAsm->Stp(a64::d8, a64::d9, a64::MemOperand(a64::sp, 128));
        armAsm->Stp(a64::d10, a64::d11, a64::MemOperand(a64::sp, 144));
        armAsm->Stp(a64::d12, a64::d13, a64::MemOperand(a64::sp, 160));
        armAsm->Stp(a64::d14, a64::d15, a64::MemOperand(a64::sp, 176));
    }
}

void armEndStackFrame(bool save_fpr)
{
    if (save_fpr)
    {
        armAsm->Ldp(a64::d14, a64::d15, a64::MemOperand(a64::sp, 176));
        armAsm->Ldp(a64::d12, a64::d13, a64::MemOperand(a64::sp, 160));
        armAsm->Ldp(a64::d10, a64::d11, a64::MemOperand(a64::sp, 144));
        armAsm->Ldp(a64::d8, a64::d9, a64::MemOperand(a64::sp, 128));
    }
    armAsm->Ldp(a64::x29, a64::lr, a64::MemOperand(a64::sp, 112));
    armAsm->Ldp(a64::x27, a64::x28, a64::MemOperand(a64::sp, 96));
    armAsm->Ldp(a64::x25, a64::x26, a64::MemOperand(a64::sp, 80));
    armAsm->Ldp(a64::x23, a64::x24, a64::MemOperand(a64::sp, 64));
    armAsm->Ldp(a64::x21, a64::x22, a64::MemOperand(a64::sp, 48));
    armAsm->Ldp(a64::x19, a64::x20, a64::MemOperand(a64::sp, 32));
    armAsm->Add(a64::sp, a64::sp, save_fpr ? 192 : 144);
}

bool armIsCalleeSavedRegister(int reg)
{
    // same on both linux and windows
    return (reg >= 19);
}

bool armIsCallerSaved(int id)
{
#ifdef _WIN32
    // The x64 ABI considers the registers RAX, RCX, RDX, R8, R9, R10, R11, and XMM0-XMM5 volatile.
		return (id <= 2 || (id >= 8 && id <= 11));
#else
    // rax, rdi, rsi, rdx, rcx, r8, r9, r10, r11 are scratch registers.
    return (id <= 2 || id == 6 || id == 7 || (id >= 8 && id <= 11));
#endif
}

bool armIsCallerSavedXmm(int id)
{
#ifdef _WIN32
    // XMM6 through XMM15 are saved. Upper 128 bits is always volatile.
		return (id < 6);
#else
    // All vector registers are volatile.
    return true;
#endif
}

a64::MemOperand armOffsetMemOperand(const a64::MemOperand& op, s64 offset)
{
    pxAssert(op.GetBaseRegister().IsValid() && op.GetAddrMode() == a64::Offset && op.GetShift() == a64::NO_SHIFT);
    return a64::MemOperand(op.GetBaseRegister(), op.GetOffset() + offset, op.GetAddrMode());
}

void armGetMemOperandInRegister(const a64::Register& addr_reg, const a64::MemOperand& op, s64 extra_offset /*= 0*/)
{
    pxAssert(addr_reg.IsX());
    pxAssert(op.GetBaseRegister().IsValid() && op.GetAddrMode() == a64::Offset && op.GetShift() == a64::NO_SHIFT);
    armAsm->Add(addr_reg, op.GetBaseRegister(), op.GetOffset() + extra_offset);
}

void armLoadConstant128(const a64::VRegister& reg, const void* ptr)
{
    u64 low, high;
    memcpy(&low, ptr, sizeof(low));
    memcpy(&high, static_cast<const u8*>(ptr) + sizeof(low), sizeof(high));
    armAsm->Ldr(reg, high, low);
}

void armEmitVTBL(const a64::VRegister& dst, const a64::VRegister& src1, const a64::VRegister& src2, const a64::VRegister& tbl)
{
    pxAssert(src1.GetCode() != RQSCRATCH.GetCode() && src2.GetCode() != RQSCRATCH2.GetCode());
    pxAssert(tbl.GetCode() != RQSCRATCH.GetCode() && tbl.GetCode() != RQSCRATCH2.GetCode());

    // must be consecutive
    if (src2.GetCode() == (src1.GetCode() + 1))
    {
        armAsm->Tbl(dst.V16B(), src1.V16B(), src2.V16B(), tbl.V16B());
        return;
    }

    armAsm->Mov(RQSCRATCH.Q(), src1.Q());
    armAsm->Mov(RQSCRATCH2.Q(), src2.Q());
    armAsm->Tbl(dst.V16B(), RQSCRATCH.V16B(), RQSCRATCH2.V16B(), tbl.V16B());
}


void ArmConstantPool::Init(void* ptr, u32 capacity)
{
    m_base_ptr = static_cast<u8*>(ptr);
    m_capacity = capacity;
    m_used = 0;
    m_jump_targets.clear();
    m_literals.clear();
}

void ArmConstantPool::Destroy()
{
    m_base_ptr = nullptr;
    m_capacity = 0;
    m_used = 0;
    m_jump_targets.clear();
    m_literals.clear();
}

void ArmConstantPool::Reset()
{
    m_used = 0;
    m_jump_targets.clear();
    m_literals.clear();
}

u8* ArmConstantPool::GetJumpTrampoline(const void* target)
{
    auto it = m_jump_targets.find(target);
    if (it != m_jump_targets.end())
        return m_base_ptr + it->second;

    // align to 16 bytes?
    const u32 offset = Common::AlignUpPow2(m_used, 16);

    // 4 movs plus a jump
    if ((m_capacity - offset) < 20)
    {
        Console.Error("Ran out of space in constant pool");
        return nullptr;
    }

    a64::MacroAssembler masm(static_cast<vixl::byte*>(m_base_ptr + offset), m_capacity - offset);
    masm.Mov(RXVIXLSCRATCH, reinterpret_cast<intptr_t>(target));
    masm.Br(RXVIXLSCRATCH);
    masm.FinalizeCode();

    pxAssert(masm.GetSizeOfCodeGenerated() < 20);
    m_jump_targets.emplace(target, offset);
    m_used = offset + static_cast<u32>(masm.GetSizeOfCodeGenerated());

    HostSys::FlushInstructionCache(reinterpret_cast<void*>(m_base_ptr + offset), m_used - offset);

    return m_base_ptr + offset;
}

u8* ArmConstantPool::GetLiteral(u64 value)
{
    return GetLiteral(u128::From64(value));
}

u8* ArmConstantPool::GetLiteral(const u128& value)
{
    auto it = m_literals.find(value);
    if (it != m_literals.end())
        return m_base_ptr + it->second;

    if (GetRemainingCapacity() < 8)
        return nullptr;

    const u32 offset = Common::AlignUpPow2(m_used, 16);
    std::memcpy(&m_base_ptr[offset], &value, sizeof(value));
    m_used = offset + sizeof(value);
    return m_base_ptr + offset;
}

u8* ArmConstantPool::GetLiteral(const u8* bytes, size_t len)
{
    pxAssertMsg(len <= 16, "literal length is less than 16 bytes");
    u128 table_u128 = {};
    std::memcpy(table_u128._u8, bytes, len);
    return GetLiteral(table_u128);
}

void ArmConstantPool::EmitLoadLiteral(const a64::CPURegister& reg, const u8* literal) const
{
    armMoveAddressToReg(RXVIXLSCRATCH, literal);
    armAsm->Ldr(reg, a64::MemOperand(RXVIXLSCRATCH));
}

//////////////////////////////////////////////////////////////////////////

void armBind(a64::Label* p_label)
{
    if(p_label->IsLinked()) {
        armAsm->Bind(p_label);
    }
}

u32 armEmitJmpPtr(void* code, const void* dst, bool flush_icache)
{
    const s64 displacement = GetPCDisplacement(code, dst);
    bool use_blr = !vixl::IsInt26(displacement);

    if (use_blr)
    {
        armAsm->Mov(RXVIXLSCRATCH, reinterpret_cast<uintptr_t>(dst));
        armAsm->Br(RXVIXLSCRATCH);
    }
    else
    {
        u32 new_code = a64::B | a64::Assembler::ImmUncondBranch(displacement);
        std::memcpy(code, &new_code, sizeof(new_code));
    }

    if (flush_icache) {
        HostSys::FlushInstructionCache(code, a64::kInstructionSize);
    }

    return a64::kInstructionSize;
}

a64::Register armLoadPtr(const void* addr)
{
    armAsm->Ldr(a64::w4, armMemOperandPtr(addr));
    return a64::w4;
}

a64::Register armLoadPtr64(const void* addr)
{
    armAsm->Ldr(a64::x4, armMemOperandPtr(addr));
    return a64::x4;
}

a64::Register armLdrh(const void* addr)
{
    armAsm->Ldrh(a64::w4, armMemOperandPtr(addr));
    return a64::w4;
}

a64::Register armLdrsh(const void* addr)
{
    armAsm->Ldrsh(a64::w4, armMemOperandPtr(addr));
    return a64::w4;
}

void armLoadPtr(const a64::CPURegister& reg, const void* addr, int64_t offset)
{
    armMoveAddressToReg(RSCRATCHADDR, addr);
    armAsm->Ldr(reg, a64::MemOperand(RSCRATCHADDR, offset));
}

void armLoadPtr(const a64::CPURegister& regRt, a64::Register regRs, int64_t offset)
{
    armAsm->Ldr(regRt, a64::MemOperand(regRs, offset));
}

a64::Register armLoadPtr(a64::Register regRs, int64_t offset)
{
    armAsm->Ldr(EEX, a64::MemOperand(regRs, offset));
    return EEX;
}

void armLoadPtr(uint64_t imm, const void* addr, const a64::Register& reg)
{
    armAsm->Mov(reg, imm);
    armLoadPtr(reg, addr);
}

void armLoadPtr(uint64_t imm, a64::Register regRs, int64_t offset, const a64::Register& regRt)
{
    armAsm->Mov(regRt, imm);
    armAsm->Ldr(regRt, a64::MemOperand(regRs, offset));
}

a64::VRegister armLoadPtrV(const void* addr)
{
    armAsm->Ldr(RQSCRATCH, armMemOperandPtr(addr));
    return RQSCRATCH;
}

a64::VRegister armLoadPtrM(a64::Register regRs, int64_t offset)
{
    armAsm->Ldr(RQSCRATCH, a64::MemOperand(regRs, offset));
    return RQSCRATCH;
}

////////////////////////////////////////////////////////////////////////////////

void armStorePtr(const a64::CPURegister& reg, const void* addr, int64_t offset)
{
    armMoveAddressToReg(RSCRATCHADDR, addr);
    armAsm->Str(reg, a64::MemOperand(RSCRATCHADDR, offset));
}

void armStorePtr(const a64::CPURegister& regRt, a64::Register regRs, int64_t offset)
{
    armAsm->Str(regRt, a64::MemOperand(regRs, offset));
}

void armStorePtr(uint64_t imm, const void* addr, const a64::Register& reg)
{
    if(imm == 0) {
        armAsm->Str(a64::xzr, armMemOperandPtr(addr));
    } else {
        armAsm->Mov(reg, imm);
        armAsm->Str(reg, armMemOperandPtr(addr));
    }
}

void armStorePtr(uint64_t imm, a64::Register regRs, int64_t offset, const a64::Register& regRt)
{
    if(imm == 0) {
        armAsm->Str(a64::xzr, a64::MemOperand(regRs, offset));
    } else {
        armAsm->Mov(regRt, imm);
        armAsm->Str(regRt, a64::MemOperand(regRs, offset));
    }
}

a64::MemOperand armMemOperandPtr(const void* addr)
{
    armMoveAddressToReg(RSCRATCHADDR, addr);
    return a64::MemOperand(RSCRATCHADDR);
}

void armLoad(const a64::Register& regRt, a64::MemOperand offset)
{
    armAsm->Ldr(regRt, offset);
}

void armLoadh(const a64::Register& regRt, a64::MemOperand offset)
{
    armAsm->Ldrh(regRt, offset);
}

void armLoadsh(const a64::Register& regRt, a64::MemOperand offset)
{
    armAsm->Ldrsh(regRt, offset);
}

void armLoadsw(const a64::Register& regRt, a64::MemOperand offset)
{
    armAsm->Ldrsw(regRt,  offset);
}

void armLoad(const a64::VRegister& regRt, a64::MemOperand offset)
{
    armAsm->Ldr(regRt,  offset);
}

a64::Register armLoad(a64::MemOperand offset)
{
    armAsm->Ldr(EEX,  offset);
    return EEX;
}

a64::Register armLoad64(a64::MemOperand offset)
{
    armAsm->Ldr(REX,  offset);
    return REX;
}

a64::VRegister armLoadPtrV(a64::MemOperand offset)
{
    armAsm->Ldr(RQSCRATCH,  offset);
    return RQSCRATCH;
}

void armStore(a64::MemOperand offset, const a64::Register& regRt)
{
    armAsm->Str(regRt,  offset);
}

void armStoreh(a64::MemOperand offset, const a64::Register& regRt)
{
    armAsm->Strh(regRt,  offset);
}

void armStore(a64::MemOperand offset, const a64::VRegister& regRt)
{
    armAsm->Str(regRt,  offset);
}

void armStore(a64::MemOperand offset, uint64_t imm)
{
    armAsm->Mov(EEX, imm);
    armAsm->Str(EEX,  offset);
}

void armStore64(a64::MemOperand offset, uint64_t imm)
{
    armAsm->Mov(REX, imm);
    armAsm->Str(REX,  offset);
}

void armAdd(a64::MemOperand p_mop, a64::Operand p_value, bool p_flagUpdate)
{
    armAsm->Ldr(EEX, p_mop);
    if(p_flagUpdate) {
        armAsm->Adds(EEX, EEX, p_value);
    } else {
        armAsm->Add(EEX, EEX, p_value);
    }
    armAsm->Str(EEX, p_mop);
}

void armAdd(const a64::Register& p_reg, a64::MemOperand p_mop, a64::Operand p_value, bool p_flagUpdate)
{
    armAsm->Ldr(p_reg, p_mop);
    if(p_flagUpdate) {
        armAsm->Adds(p_reg, p_reg, p_value);
    } else {
        armAsm->Add(p_reg, p_reg, p_value);
    }
    armAsm->Str(p_reg, p_mop);
}

void armAdd(const void* p_mop, a64::Operand p_value)
{
    armMoveAddressToReg(RSCRATCHADDR, p_mop);
    armAsm->Ldr(EEX, a64::MemOperand(RSCRATCHADDR));
    armAsm->Add(EEX, EEX, p_value);
    armAsm->Str(EEX, a64::MemOperand(RSCRATCHADDR));
}

void armAdd(const a64::Register& p_reg, const void* p_mop, a64::Operand p_value)
{
    armMoveAddressToReg(RSCRATCHADDR, p_mop);
    armAsm->Ldr(p_reg, a64::MemOperand(RSCRATCHADDR));
    armAsm->Add(p_reg, p_reg, p_value);
    armAsm->Str(p_reg, a64::MemOperand(RSCRATCHADDR));
}

void armAddh(const a64::Register& p_reg, const void* p_mop, a64::Operand p_value, bool p_flagUpdate)
{
    armMoveAddressToReg(RSCRATCHADDR, p_mop);
    armAsm->Ldrh(p_reg, a64::MemOperand(RSCRATCHADDR));
    if(p_flagUpdate) {
        armAsm->Adds(p_reg, p_reg, p_value);
    } else {
        armAsm->Add(p_reg, p_reg, p_value);
    }
    armAsm->Strh(p_reg, a64::MemOperand(RSCRATCHADDR));
}

void armAddsh(const a64::Register& p_reg, const void* p_mop, a64::Operand p_value, bool p_flagUpdate)
{
    armMoveAddressToReg(RSCRATCHADDR, p_mop);
    armAsm->Ldrsh(p_reg, a64::MemOperand(RSCRATCHADDR));
    if(p_flagUpdate) {
        armAsm->Adds(p_reg, p_reg, p_value);
    } else {
        armAsm->Add(p_reg, p_reg, p_value);
    }
    armAsm->Strh(p_reg, a64::MemOperand(RSCRATCHADDR));
}

void armSub(a64::MemOperand p_mop, a64::Operand p_value, bool p_flagUpdate)
{
    armLoadsw(EEX, p_mop);
    if(p_flagUpdate) {
        armAsm->Subs(EEX, EEX, p_value);
    } else {
        armAsm->Sub(EEX, EEX, p_value);
    }
    armStore(p_mop, EEX);
}

void armSub(const a64::Register& p_reg, a64::MemOperand p_mop, a64::Operand p_value, bool p_flagUpdate)
{
    armLoadsw(p_reg, p_mop);
    if(p_flagUpdate) {
        armAsm->Subs(p_reg, p_reg, p_value);
    } else {
        armAsm->Sub(p_reg, p_reg, p_value);
    }
    armStore(p_mop, p_reg);
}

void armSub(const void* p_mop, a64::Operand p_value)
{
    armMoveAddressToReg(RSCRATCHADDR, p_mop);
    armAsm->Ldr(EEX, a64::MemOperand(RSCRATCHADDR));
    armAsm->Sub(EEX, EEX, p_value);
    armAsm->Str(EEX, a64::MemOperand(RSCRATCHADDR));
}

void armSub(const a64::Register& p_reg, const void* p_mop, a64::Operand p_value)
{
    armMoveAddressToReg(RSCRATCHADDR, p_mop);
    armAsm->Ldr(p_reg, a64::MemOperand(RSCRATCHADDR));
    armAsm->Sub(p_reg, p_reg, p_value);
    armAsm->Str(p_reg, a64::MemOperand(RSCRATCHADDR));
}

void armAnd(a64::MemOperand p_mop, a64::Operand p_value, bool p_flagUpdate)
{
    armLoad(EEX, p_mop);
    if(p_flagUpdate) {
        armAsm->Ands(EEX, EEX, p_value);
    } else {
        armAsm->And(EEX, EEX, p_value);
    }
    armStore(p_mop, EEX);
}

void armAnd(const a64::Register& p_reg, a64::MemOperand p_mop, a64::Operand p_value, bool p_flagUpdate)
{
    armLoad(p_reg, p_mop);
    if(p_flagUpdate) {
        armAsm->Ands(p_reg, p_reg, p_value);
    } else {
        armAsm->And(p_reg, p_reg, p_value);
    }
    armStore(p_mop, p_reg);
}

void armAnd(const void* p_mop, a64::Operand p_value)
{
    armMoveAddressToReg(RSCRATCHADDR, p_mop);
    armAsm->Ldr(EEX, a64::MemOperand(RSCRATCHADDR));
    armAsm->And(EEX, EEX, p_value);
    armAsm->Str(EEX, a64::MemOperand(RSCRATCHADDR));
}

void armAnd(const a64::Register& p_reg, const void* p_mop, a64::Operand p_value)
{
    armMoveAddressToReg(RSCRATCHADDR, p_mop);
    armAsm->Ldr(p_reg, a64::MemOperand(RSCRATCHADDR));
    armAsm->And(p_reg, p_reg, p_value);
    armAsm->Str(p_reg, a64::MemOperand(RSCRATCHADDR));
}

void armOrr(a64::MemOperand p_mop, a64::Operand p_value)
{
    armLoad(EEX, p_mop);
    armAsm->Orr(EEX, EEX, p_value);
    armStore(p_mop, EEX);
}

void armOrr(const a64::Register& p_reg, a64::MemOperand p_mop, a64::Operand p_value)
{
    armLoad(p_reg, p_mop);
    armAsm->Orr(p_reg, p_reg, p_value);
    armStore(p_mop, p_reg);
}

void armOrr(const void* p_mop, a64::Operand p_value)
{
    armMoveAddressToReg(RSCRATCHADDR, p_mop);
    armAsm->Ldr(EEX, a64::MemOperand(RSCRATCHADDR));
    armAsm->Orr(EEX, EEX, p_value);
    armAsm->Str(EEX, a64::MemOperand(RSCRATCHADDR));
}

void armOrr(const a64::Register& p_reg, const void* p_mop, a64::Operand p_value)
{
    armMoveAddressToReg(RSCRATCHADDR, p_mop);
    armAsm->Ldr(p_reg, a64::MemOperand(RSCRATCHADDR));
    armAsm->Orr(p_reg, p_reg, p_value);
    armAsm->Str(p_reg, a64::MemOperand(RSCRATCHADDR));
}

void armEor(a64::MemOperand p_mop, a64::Operand p_value)
{
    armLoad(EEX, p_mop);
    armAsm->Eor(EEX, EEX, p_value);
    armStore(p_mop, EEX);
}

void armEor(const a64::Register& p_reg, a64::MemOperand p_mop, a64::Operand p_value)
{
    armLoad(p_reg, p_mop);
    armAsm->Eor(p_reg, p_reg, p_value);
    armStore(p_mop, p_reg);
}

void armEor(const void* p_mop, a64::Operand p_value)
{
    armMoveAddressToReg(RSCRATCHADDR, p_mop);
    armAsm->Ldr(EEX, a64::MemOperand(RSCRATCHADDR));
    armAsm->Eor(EEX, EEX, p_value);
    armAsm->Str(EEX, a64::MemOperand(RSCRATCHADDR));
}

void armEor(const a64::Register& p_reg, const void* p_mop, a64::Operand p_value)
{
    armMoveAddressToReg(RSCRATCHADDR, p_mop);
    armAsm->Ldr(p_reg, a64::MemOperand(RSCRATCHADDR));
    armAsm->Eor(p_reg, p_reg, p_value);
    armAsm->Str(p_reg, a64::MemOperand(RSCRATCHADDR));
}

// kArm64I32x4BitMask
void armMOVMSKPS(const a64::Register& reg32, const a64::VRegister& regQ)
{
    armAsm->Movi(RQSCRATCH2.V2D(), 0x0000'0008'0000'0004, 0x0000'0002'0000'0001);
    ////
    armAsm->Sshr(RQSCRATCH.V4S(), regQ.V4S(), 31);
    armAsm->And(RQSCRATCH.V16B(), RQSCRATCH2.V16B(), RQSCRATCH.V16B());

    armAsm->Addv(RQSCRATCH.S(), RQSCRATCH.V4S());
    armAsm->Fmov(reg32, RQSCRATCH.S());
}

void armPBLENDW(const a64::VRegister& regDst, const a64::VRegister& regSrc)
{
    armAsm->Mov(RQSCRATCH, regDst);
    armAsm->Movi(regDst.V4S(), 0xFFFF0000);
    armAsm->Bsl(regDst.V16B(), RQSCRATCH.V16B(), regSrc.V16B());
}

// kArm64I8x16SConvertI16x8
void armPACKSSWB(const a64::VRegister& regDst, const a64::VRegister& regSrc)
{
    a64::VRegister src0 = regDst, src1 = regSrc;
    if (regDst.Is(src1)) {
        armAsm->Mov(RQSCRATCH.V8H(), src1.V8H());
        src1 = RQSCRATCH;
    }
    armAsm->Sqxtn(regDst.V8B(), src0.V8H());
    armAsm->Sqxtn2(regDst.V16B(), src1.V8H());
}

void armPMOVMSKB(const a64::Register& regDst, const a64::VRegister& regSrc)
{
    a64::VRegister tmp = RQSCRATCH;
    a64::VRegister mask = RQSCRATCH2;
    //
    armAsm->Sshr(tmp.V16B(), regSrc.V16B(), 7);
    armAsm->Movi(mask.V2D(), 0x8040'2010'0804'0201);
    armAsm->And(tmp.V16B(), mask.V16B(), tmp.V16B());

    armAsm->Ext(mask.V16B(), tmp.V16B(), tmp.V16B(), 8);
    armAsm->Zip1(tmp.V16B(), tmp.V16B(), mask.V16B());

    armAsm->Addv(tmp.H(), tmp.V8H());
    armAsm->Mov(regDst, tmp.V8H(), 0);
}

void armPshufdPs(const a64::VRegister& dst, const a64::VRegister& src, int a, int b, int c, int d)
{
    armAsm->Mov(RQSCRATCH, src);
    armAsm->Mov(RQSCRATCH2, dst);

    armAsm->Ins(dst.V4S(), 0, RQSCRATCH2.V4S(), a);
    armAsm->Ins(dst.V4S(), 1, RQSCRATCH2.V4S(), b);
    armAsm->Ins(dst.V4S(), 2, RQSCRATCH.V4S(), c);
    armAsm->Ins(dst.V4S(), 3, RQSCRATCH.V4S(), d);
}

void armSHUFPS(const a64::VRegister& dstreg, const a64::VRegister& srcreg, int pIndex) {
    switch (pIndex) {
        case 0: armPshufdPs(dstreg, srcreg, 0, 0, 0, 0); break;
        case 1: armPshufdPs(dstreg, srcreg, 1, 0, 0, 0); break;
        case 2: armPshufdPs(dstreg, srcreg, 2, 0, 0, 0); break;
        case 3: armPshufdPs(dstreg, srcreg, 3, 0, 0, 0); break;
        case 4: armPshufdPs(dstreg, srcreg, 0, 1, 0, 0); break;
        case 5: armPshufdPs(dstreg, srcreg, 1, 1, 0, 0); break;
        case 6: armPshufdPs(dstreg, srcreg, 2, 1, 0, 0); break;
        case 7: armPshufdPs(dstreg, srcreg, 3, 1, 0, 0); break;
        case 8: armPshufdPs(dstreg, srcreg, 0, 2, 0, 0); break;
        case 9: armPshufdPs(dstreg, srcreg, 1, 2, 0, 0); break;
        case 10: armPshufdPs(dstreg, srcreg, 2, 2, 0, 0); break;
        case 11: armPshufdPs(dstreg, srcreg, 3, 2, 0, 0); break;
        case 12: armPshufdPs(dstreg, srcreg, 0, 3, 0, 0); break;
        case 13: armPshufdPs(dstreg, srcreg, 1, 3, 0, 0); break;
        case 14: armPshufdPs(dstreg, srcreg, 2, 3, 0, 0); break;
        case 15: armPshufdPs(dstreg, srcreg, 3, 3, 0, 0); break;
        case 16: armPshufdPs(dstreg, srcreg, 0, 0, 1, 0); break;
        case 17: armPshufdPs(dstreg, srcreg, 1, 0, 1, 0); break;
        case 18: armPshufdPs(dstreg, srcreg, 2, 0, 1, 0); break;
        case 19: armPshufdPs(dstreg, srcreg, 3, 0, 1, 0); break;
        case 20: armPshufdPs(dstreg, srcreg, 0, 1, 1, 0); break;
        case 21: armPshufdPs(dstreg, srcreg, 1, 1, 1, 0); break;
        case 22: armPshufdPs(dstreg, srcreg, 2, 1, 1, 0); break;
        case 23: armPshufdPs(dstreg, srcreg, 3, 1, 1, 0); break;
        case 24: armPshufdPs(dstreg, srcreg, 0, 2, 1, 0); break;
        case 25: armPshufdPs(dstreg, srcreg, 1, 2, 1, 0); break;
        case 26: armPshufdPs(dstreg, srcreg, 2, 2, 1, 0); break;
        case 27: armPshufdPs(dstreg, srcreg, 3, 2, 1, 0); break;
        case 28: armPshufdPs(dstreg, srcreg, 0, 3, 1, 0); break;
        case 29: armPshufdPs(dstreg, srcreg, 1, 3, 1, 0); break;
        case 30: armPshufdPs(dstreg, srcreg, 2, 3, 1, 0); break;
        case 31: armPshufdPs(dstreg, srcreg, 3, 3, 1, 0); break;
        case 32: armPshufdPs(dstreg, srcreg, 0, 0, 2, 0); break;
        case 33: armPshufdPs(dstreg, srcreg, 1, 0, 2, 0); break;
        case 34: armPshufdPs(dstreg, srcreg, 2, 0, 2, 0); break;
        case 35: armPshufdPs(dstreg, srcreg, 3, 0, 2, 0); break;
        case 36: armPshufdPs(dstreg, srcreg, 0, 1, 2, 0); break;
        case 37: armPshufdPs(dstreg, srcreg, 1, 1, 2, 0); break;
        case 38: armPshufdPs(dstreg, srcreg, 2, 1, 2, 0); break;
        case 39: armPshufdPs(dstreg, srcreg, 3, 1, 2, 0); break;
        case 40: armPshufdPs(dstreg, srcreg, 0, 2, 2, 0); break;
        case 41: armPshufdPs(dstreg, srcreg, 1, 2, 2, 0); break;
        case 42: armPshufdPs(dstreg, srcreg, 2, 2, 2, 0); break;
        case 43: armPshufdPs(dstreg, srcreg, 3, 2, 2, 0); break;
        case 44: armPshufdPs(dstreg, srcreg, 0, 3, 2, 0); break;
        case 45: armPshufdPs(dstreg, srcreg, 1, 3, 2, 0); break;
        case 46: armPshufdPs(dstreg, srcreg, 2, 3, 2, 0); break;
        case 47: armPshufdPs(dstreg, srcreg, 3, 3, 2, 0); break;
        case 48: armPshufdPs(dstreg, srcreg, 0, 0, 3, 0); break;
        case 49: armPshufdPs(dstreg, srcreg, 1, 0, 3, 0); break;
        case 50: armPshufdPs(dstreg, srcreg, 2, 0, 3, 0); break;
        case 51: armPshufdPs(dstreg, srcreg, 3, 0, 3, 0); break;
        case 52: armPshufdPs(dstreg, srcreg, 0, 1, 3, 0); break;
        case 53: armPshufdPs(dstreg, srcreg, 1, 1, 3, 0); break;
        case 54: armPshufdPs(dstreg, srcreg, 2, 1, 3, 0); break;
        case 55: armPshufdPs(dstreg, srcreg, 3, 1, 3, 0); break;
        case 56: armPshufdPs(dstreg, srcreg, 0, 2, 3, 0); break;
        case 57: armPshufdPs(dstreg, srcreg, 1, 2, 3, 0); break;
        case 58: armPshufdPs(dstreg, srcreg, 2, 2, 3, 0); break;
        case 59: armPshufdPs(dstreg, srcreg, 3, 2, 3, 0); break;
        case 60: armPshufdPs(dstreg, srcreg, 0, 3, 3, 0); break;
        case 61: armPshufdPs(dstreg, srcreg, 1, 3, 3, 0); break;
        case 62: armPshufdPs(dstreg, srcreg, 2, 3, 3, 0); break;
        case 63: armPshufdPs(dstreg, srcreg, 3, 3, 3, 0); break;
        case 64: armPshufdPs(dstreg, srcreg, 0, 0, 0, 1); break;
        case 65: armPshufdPs(dstreg, srcreg, 1, 0, 0, 1); break;
        case 66: armPshufdPs(dstreg, srcreg, 2, 0, 0, 1); break;
        case 67: armPshufdPs(dstreg, srcreg, 3, 0, 0, 1); break;
        case 68: armPshufdPs(dstreg, srcreg, 0, 1, 0, 1); break;
        case 69: armPshufdPs(dstreg, srcreg, 1, 1, 0, 1); break;
        case 70: armPshufdPs(dstreg, srcreg, 2, 1, 0, 1); break;
        case 71: armPshufdPs(dstreg, srcreg, 3, 1, 0, 1); break;
        case 72: armPshufdPs(dstreg, srcreg, 0, 2, 0, 1); break;
        case 73: armPshufdPs(dstreg, srcreg, 1, 2, 0, 1); break;
        case 74: armPshufdPs(dstreg, srcreg, 2, 2, 0, 1); break;
        case 75: armPshufdPs(dstreg, srcreg, 3, 2, 0, 1); break;
        case 76: armPshufdPs(dstreg, srcreg, 0, 3, 0, 1); break;
        case 77: armPshufdPs(dstreg, srcreg, 1, 3, 0, 1); break;
        case 78: armPshufdPs(dstreg, srcreg, 2, 3, 0, 1); break;
        case 79: armPshufdPs(dstreg, srcreg, 3, 3, 0, 1); break;
        case 80: armPshufdPs(dstreg, srcreg, 0, 0, 1, 1); break;
        case 81: armPshufdPs(dstreg, srcreg, 1, 0, 1, 1); break;
        case 82: armPshufdPs(dstreg, srcreg, 2, 0, 1, 1); break;
        case 83: armPshufdPs(dstreg, srcreg, 3, 0, 1, 1); break;
        case 84: armPshufdPs(dstreg, srcreg, 0, 1, 1, 1); break;
        case 85: armPshufdPs(dstreg, srcreg, 1, 1, 1, 1); break;
        case 86: armPshufdPs(dstreg, srcreg, 2, 1, 1, 1); break;
        case 87: armPshufdPs(dstreg, srcreg, 3, 1, 1, 1); break;
        case 88: armPshufdPs(dstreg, srcreg, 0, 2, 1, 1); break;
        case 89: armPshufdPs(dstreg, srcreg, 1, 2, 1, 1); break;
        case 90: armPshufdPs(dstreg, srcreg, 2, 2, 1, 1); break;
        case 91: armPshufdPs(dstreg, srcreg, 3, 2, 1, 1); break;
        case 92: armPshufdPs(dstreg, srcreg, 0, 3, 1, 1); break;
        case 93: armPshufdPs(dstreg, srcreg, 1, 3, 1, 1); break;
        case 94: armPshufdPs(dstreg, srcreg, 2, 3, 1, 1); break;
        case 95: armPshufdPs(dstreg, srcreg, 3, 3, 1, 1); break;
        case 96: armPshufdPs(dstreg, srcreg, 0, 0, 2, 0); break;
        case 97: armPshufdPs(dstreg, srcreg, 1, 0, 2, 1); break;
        case 98: armPshufdPs(dstreg, srcreg, 2, 0, 2, 1); break;
        case 99: armPshufdPs(dstreg, srcreg, 3, 0, 2, 1); break;
        case 100: armPshufdPs(dstreg, srcreg, 0, 1, 2, 1); break;
        case 101: armPshufdPs(dstreg, srcreg, 1, 1, 2, 1); break;
        case 102: armPshufdPs(dstreg, srcreg, 2, 1, 2, 1); break;
        case 103: armPshufdPs(dstreg, srcreg, 3, 1, 2, 1); break;
        case 104: armPshufdPs(dstreg, srcreg, 0, 2, 2, 1); break;
        case 105: armPshufdPs(dstreg, srcreg, 1, 2, 2, 1); break;
        case 106: armPshufdPs(dstreg, srcreg, 2, 2, 2, 1); break;
        case 107: armPshufdPs(dstreg, srcreg, 3, 2, 2, 1); break;
        case 108: armPshufdPs(dstreg, srcreg, 0, 3, 2, 1); break;
        case 109: armPshufdPs(dstreg, srcreg, 1, 3, 2, 1); break;
        case 110: armPshufdPs(dstreg, srcreg, 2, 3, 2, 1); break;
        case 111: armPshufdPs(dstreg, srcreg, 3, 3, 2, 1); break;
        case 112: armPshufdPs(dstreg, srcreg, 0, 0, 3, 1); break;
        case 113: armPshufdPs(dstreg, srcreg, 1, 0, 3, 1); break;
        case 114: armPshufdPs(dstreg, srcreg, 2, 0, 3, 1); break;
        case 115: armPshufdPs(dstreg, srcreg, 3, 0, 3, 1); break;
        case 116: armPshufdPs(dstreg, srcreg, 0, 1, 3, 1); break;
        case 117: armPshufdPs(dstreg, srcreg, 1, 1, 3, 1); break;
        case 118: armPshufdPs(dstreg, srcreg, 2, 1, 3, 1); break;
        case 119: armPshufdPs(dstreg, srcreg, 3, 1, 3, 1); break;
        case 120: armPshufdPs(dstreg, srcreg, 0, 2, 3, 1); break;
        case 121: armPshufdPs(dstreg, srcreg, 1, 2, 3, 1); break;
        case 122: armPshufdPs(dstreg, srcreg, 2, 2, 3, 1); break;
        case 123: armPshufdPs(dstreg, srcreg, 3, 2, 3, 1); break;
        case 124: armPshufdPs(dstreg, srcreg, 0, 3, 3, 1); break;
        case 125: armPshufdPs(dstreg, srcreg, 1, 3, 3, 1); break;
        case 126: armPshufdPs(dstreg, srcreg, 2, 3, 3, 1); break;
        case 127: armPshufdPs(dstreg, srcreg, 3, 3, 3, 1); break;
        case 128: armPshufdPs(dstreg, srcreg, 0, 0, 0, 2); break;
        case 129: armPshufdPs(dstreg, srcreg, 1, 0, 0, 2); break;
        case 130: armPshufdPs(dstreg, srcreg, 2, 0, 0, 2); break;
        case 131: armPshufdPs(dstreg, srcreg, 3, 0, 0, 2); break;
        case 132: armPshufdPs(dstreg, srcreg, 0, 1, 0, 2); break;
        case 133: armPshufdPs(dstreg, srcreg, 1, 1, 0, 2); break;
        case 134: armPshufdPs(dstreg, srcreg, 2, 1, 0, 2); break;
        case 135: armPshufdPs(dstreg, srcreg, 3, 1, 0, 2); break;
        case 136: armPshufdPs(dstreg, srcreg, 0, 2, 0, 2); break;
        case 137: armPshufdPs(dstreg, srcreg, 1, 2, 0, 2); break;
        case 138: armPshufdPs(dstreg, srcreg, 2, 2, 0, 2); break;
        case 139: armPshufdPs(dstreg, srcreg, 3, 2, 0, 2); break;
        case 140: armPshufdPs(dstreg, srcreg, 0, 3, 0, 2); break;
        case 141: armPshufdPs(dstreg, srcreg, 1, 3, 0, 2); break;
        case 142: armPshufdPs(dstreg, srcreg, 2, 3, 0, 2); break;
        case 143: armPshufdPs(dstreg, srcreg, 3, 3, 0, 2); break;
        case 144: armPshufdPs(dstreg, srcreg, 0, 0, 1, 2); break;
        case 145: armPshufdPs(dstreg, srcreg, 1, 0, 1, 2); break;
        case 146: armPshufdPs(dstreg, srcreg, 2, 0, 1, 2); break;
        case 147: armPshufdPs(dstreg, srcreg, 3, 0, 1, 2); break;
        case 148: armPshufdPs(dstreg, srcreg, 0, 1, 1, 2); break;
        case 149: armPshufdPs(dstreg, srcreg, 1, 1, 1, 2); break;
        case 150: armPshufdPs(dstreg, srcreg, 2, 1, 1, 2); break;
        case 151: armPshufdPs(dstreg, srcreg, 3, 1, 1, 2); break;
        case 152: armPshufdPs(dstreg, srcreg, 0, 2, 1, 2); break;
        case 153: armPshufdPs(dstreg, srcreg, 1, 2, 1, 2); break;
        case 154: armPshufdPs(dstreg, srcreg, 2, 2, 1, 2); break;
        case 155: armPshufdPs(dstreg, srcreg, 3, 2, 1, 2); break;
        case 156: armPshufdPs(dstreg, srcreg, 0, 3, 1, 2); break;
        case 157: armPshufdPs(dstreg, srcreg, 1, 3, 1, 2); break;
        case 158: armPshufdPs(dstreg, srcreg, 2, 3, 1, 2); break;
        case 159: armPshufdPs(dstreg, srcreg, 3, 3, 1, 2); break;
        case 160: armPshufdPs(dstreg, srcreg, 0, 0, 2, 2); break;
        case 161: armPshufdPs(dstreg, srcreg, 1, 0, 2, 2); break;
        case 162: armPshufdPs(dstreg, srcreg, 2, 0, 2, 2); break;
        case 163: armPshufdPs(dstreg, srcreg, 3, 0, 2, 2); break;
        case 164: armPshufdPs(dstreg, srcreg, 0, 1, 2, 2); break;
        case 165: armPshufdPs(dstreg, srcreg, 1, 1, 2, 2); break;
        case 166: armPshufdPs(dstreg, srcreg, 2, 1, 2, 2); break;
        case 167: armPshufdPs(dstreg, srcreg, 3, 1, 2, 2); break;
        case 168: armPshufdPs(dstreg, srcreg, 0, 2, 2, 2); break;
        case 169: armPshufdPs(dstreg, srcreg, 1, 2, 2, 2); break;
        case 170: armPshufdPs(dstreg, srcreg, 2, 2, 2, 2); break;
        case 171: armPshufdPs(dstreg, srcreg, 3, 2, 2, 2); break;
        case 172: armPshufdPs(dstreg, srcreg, 0, 3, 2, 2); break;
        case 173: armPshufdPs(dstreg, srcreg, 1, 3, 2, 2); break;
        case 174: armPshufdPs(dstreg, srcreg, 2, 3, 2, 2); break;
        case 175: armPshufdPs(dstreg, srcreg, 3, 3, 2, 2); break;
        case 176: armPshufdPs(dstreg, srcreg, 0, 0, 3, 2); break;
        case 177: armPshufdPs(dstreg, srcreg, 1, 0, 3, 2); break;
        case 178: armPshufdPs(dstreg, srcreg, 2, 0, 3, 2); break;
        case 179: armPshufdPs(dstreg, srcreg, 3, 0, 3, 2); break;
        case 180: armPshufdPs(dstreg, srcreg, 0, 1, 3, 2); break;
        case 181: armPshufdPs(dstreg, srcreg, 1, 1, 3, 2); break;
        case 182: armPshufdPs(dstreg, srcreg, 2, 1, 3, 2); break;
        case 183: armPshufdPs(dstreg, srcreg, 3, 1, 3, 2); break;
        case 184: armPshufdPs(dstreg, srcreg, 0, 2, 3, 2); break;
        case 185: armPshufdPs(dstreg, srcreg, 1, 2, 3, 2); break;
        case 186: armPshufdPs(dstreg, srcreg, 2, 2, 3, 2); break;
        case 187: armPshufdPs(dstreg, srcreg, 3, 2, 3, 2); break;
        case 188: armPshufdPs(dstreg, srcreg, 0, 3, 3, 2); break;
        case 189: armPshufdPs(dstreg, srcreg, 1, 3, 3, 2); break;
        case 190: armPshufdPs(dstreg, srcreg, 2, 3, 3, 2); break;
        case 191: armPshufdPs(dstreg, srcreg, 3, 3, 3, 2); break;
        case 192: armPshufdPs(dstreg, srcreg, 0, 0, 0, 3); break;
        case 193: armPshufdPs(dstreg, srcreg, 1, 0, 0, 3); break;
        case 194: armPshufdPs(dstreg, srcreg, 2, 0, 0, 3); break;
        case 195: armPshufdPs(dstreg, srcreg, 3, 0, 0, 3); break;
        case 196: armPshufdPs(dstreg, srcreg, 0, 1, 0, 3); break;
        case 197: armPshufdPs(dstreg, srcreg, 1, 1, 0, 3); break;
        case 198: armPshufdPs(dstreg, srcreg, 2, 1, 0, 3); break;
        case 199: armPshufdPs(dstreg, srcreg, 3, 1, 0, 3); break;
        case 200: armPshufdPs(dstreg, srcreg, 0, 2, 0, 3); break;
        case 201: armPshufdPs(dstreg, srcreg, 1, 2, 0, 3); break;
        case 202: armPshufdPs(dstreg, srcreg, 2, 2, 0, 3); break;
        case 203: armPshufdPs(dstreg, srcreg, 3, 2, 0, 3); break;
        case 204: armPshufdPs(dstreg, srcreg, 0, 3, 0, 3); break;
        case 205: armPshufdPs(dstreg, srcreg, 1, 3, 0, 3); break;
        case 206: armPshufdPs(dstreg, srcreg, 2, 3, 0, 3); break;
        case 207: armPshufdPs(dstreg, srcreg, 3, 3, 0, 3); break;
        case 208: armPshufdPs(dstreg, srcreg, 0, 0, 1, 3); break;
        case 209: armPshufdPs(dstreg, srcreg, 1, 0, 1, 3); break;
        case 210: armPshufdPs(dstreg, srcreg, 2, 0, 1, 3); break;
        case 211: armPshufdPs(dstreg, srcreg, 3, 0, 1, 3); break;
        case 212: armPshufdPs(dstreg, srcreg, 0, 1, 1, 3); break;
        case 213: armPshufdPs(dstreg, srcreg, 1, 1, 1, 3); break;
        case 214: armPshufdPs(dstreg, srcreg, 2, 1, 1, 3); break;
        case 215: armPshufdPs(dstreg, srcreg, 3, 1, 1, 3); break;
        case 216: armPshufdPs(dstreg, srcreg, 0, 2, 1, 3); break;
        case 217: armPshufdPs(dstreg, srcreg, 1, 2, 1, 3); break;
        case 218: armPshufdPs(dstreg, srcreg, 2, 2, 1, 3); break;
        case 219: armPshufdPs(dstreg, srcreg, 3, 2, 1, 3); break;
        case 220: armPshufdPs(dstreg, srcreg, 0, 3, 1, 3); break;
        case 221: armPshufdPs(dstreg, srcreg, 1, 3, 1, 3); break;
        case 222: armPshufdPs(dstreg, srcreg, 2, 3, 1, 3); break;
        case 223: armPshufdPs(dstreg, srcreg, 3, 3, 1, 3); break;
        case 224: armPshufdPs(dstreg, srcreg, 0, 0, 2, 3); break;
        case 225: armPshufdPs(dstreg, srcreg, 1, 0, 2, 3); break;
        case 226: armPshufdPs(dstreg, srcreg, 2, 0, 2, 3); break;
        case 227: armPshufdPs(dstreg, srcreg, 3, 0, 2, 3); break;
        case 228: armPshufdPs(dstreg, srcreg, 0, 1, 2, 3); break;
        case 229: armPshufdPs(dstreg, srcreg, 1, 1, 2, 3); break;
        case 230: armPshufdPs(dstreg, srcreg, 2, 1, 2, 3); break;
        case 231: armPshufdPs(dstreg, srcreg, 3, 1, 2, 3); break;
        case 232: armPshufdPs(dstreg, srcreg, 0, 2, 2, 3); break;
        case 233: armPshufdPs(dstreg, srcreg, 1, 2, 2, 3); break;
        case 234: armPshufdPs(dstreg, srcreg, 2, 2, 2, 3); break;
        case 235: armPshufdPs(dstreg, srcreg, 3, 2, 2, 3); break;
        case 236: armPshufdPs(dstreg, srcreg, 0, 3, 2, 3); break;
        case 237: armPshufdPs(dstreg, srcreg, 1, 3, 2, 3); break;
        case 238: armPshufdPs(dstreg, srcreg, 2, 3, 2, 3); break;
        case 239: armPshufdPs(dstreg, srcreg, 3, 3, 2, 3); break;
        case 240: armPshufdPs(dstreg, srcreg, 0, 0, 3, 3); break;
        case 241: armPshufdPs(dstreg, srcreg, 1, 0, 3, 3); break;
        case 242: armPshufdPs(dstreg, srcreg, 2, 0, 3, 3); break;
        case 243: armPshufdPs(dstreg, srcreg, 3, 0, 3, 3); break;
        case 244: armPshufdPs(dstreg, srcreg, 0, 1, 3, 3); break;
        case 245: armPshufdPs(dstreg, srcreg, 1, 1, 3, 3); break;
        case 246: armPshufdPs(dstreg, srcreg, 2, 1, 3, 3); break;
        case 247: armPshufdPs(dstreg, srcreg, 3, 1, 3, 3); break;
        case 248: armPshufdPs(dstreg, srcreg, 0, 2, 3, 3); break;
        case 249: armPshufdPs(dstreg, srcreg, 1, 2, 3, 3); break;
        case 250: armPshufdPs(dstreg, srcreg, 2, 2, 3, 3); break;
        case 251: armPshufdPs(dstreg, srcreg, 3, 2, 3, 3); break;
        case 252: armPshufdPs(dstreg, srcreg, 0, 3, 3, 3); break;
        case 253: armPshufdPs(dstreg, srcreg, 1, 3, 3, 3); break;
        case 254: armPshufdPs(dstreg, srcreg, 2, 3, 3, 3); break;
        case 255: armPshufdPs(dstreg, srcreg, 3, 3, 3, 3); break;
    }
}

void armPshufd(const a64::VRegister& dst, const a64::VRegister& src, int a, int b, int c, int d)
{
    armAsm->Mov(RQSCRATCH, src);

    armAsm->Ins(dst.V4S(), 0, RQSCRATCH.V4S(), a);
    armAsm->Ins(dst.V4S(), 1, RQSCRATCH.V4S(), b);
    armAsm->Ins(dst.V4S(), 2, RQSCRATCH.V4S(), c);
    armAsm->Ins(dst.V4S(), 3, RQSCRATCH.V4S(), d);
}

void armPSHUFD(const a64::VRegister& dstreg, const a64::VRegister& srcreg, int pIndex)
{
    switch (pIndex) {
        case 0: armPshufd(dstreg, srcreg, 0, 0, 0, 0); break;
        case 1: armPshufd(dstreg, srcreg, 1, 0, 0, 0); break;
        case 2: armPshufd(dstreg, srcreg, 2, 0, 0, 0); break;
        case 3: armPshufd(dstreg, srcreg, 3, 0, 0, 0); break;
        case 4: armPshufd(dstreg, srcreg, 0, 1, 0, 0); break;
        case 5: armPshufd(dstreg, srcreg, 1, 1, 0, 0); break;
        case 6: armPshufd(dstreg, srcreg, 2, 1, 0, 0); break;
        case 7: armPshufd(dstreg, srcreg, 3, 1, 0, 0); break;
        case 8: armPshufd(dstreg, srcreg, 0, 2, 0, 0); break;
        case 9: armPshufd(dstreg, srcreg, 1, 2, 0, 0); break;
        case 10: armPshufd(dstreg, srcreg, 2, 2, 0, 0); break;
        case 11: armPshufd(dstreg, srcreg, 3, 2, 0, 0); break;
        case 12: armPshufd(dstreg, srcreg, 0, 3, 0, 0); break;
        case 13: armPshufd(dstreg, srcreg, 1, 3, 0, 0); break;
        case 14: armPshufd(dstreg, srcreg, 2, 3, 0, 0); break;
        case 15: armPshufd(dstreg, srcreg, 3, 3, 0, 0); break;
        case 16: armPshufd(dstreg, srcreg, 0, 0, 1, 0); break;
        case 17: armPshufd(dstreg, srcreg, 1, 0, 1, 0); break;
        case 18: armPshufd(dstreg, srcreg, 2, 0, 1, 0); break;
        case 19: armPshufd(dstreg, srcreg, 3, 0, 1, 0); break;
        case 20: armPshufd(dstreg, srcreg, 0, 1, 1, 0); break;
        case 21: armPshufd(dstreg, srcreg, 1, 1, 1, 0); break;
        case 22: armPshufd(dstreg, srcreg, 2, 1, 1, 0); break;
        case 23: armPshufd(dstreg, srcreg, 3, 1, 1, 0); break;
        case 24: armPshufd(dstreg, srcreg, 0, 2, 1, 0); break;
        case 25: armPshufd(dstreg, srcreg, 1, 2, 1, 0); break;
        case 26: armPshufd(dstreg, srcreg, 2, 2, 1, 0); break;
        case 27: armPshufd(dstreg, srcreg, 3, 2, 1, 0); break;
        case 28: armPshufd(dstreg, srcreg, 0, 3, 1, 0); break;
        case 29: armPshufd(dstreg, srcreg, 1, 3, 1, 0); break;
        case 30: armPshufd(dstreg, srcreg, 2, 3, 1, 0); break;
        case 31: armPshufd(dstreg, srcreg, 3, 3, 1, 0); break;
        case 32: armPshufd(dstreg, srcreg, 0, 0, 2, 0); break;
        case 33: armPshufd(dstreg, srcreg, 1, 0, 2, 0); break;
        case 34: armPshufd(dstreg, srcreg, 2, 0, 2, 0); break;
        case 35: armPshufd(dstreg, srcreg, 3, 0, 2, 0); break;
        case 36: armPshufd(dstreg, srcreg, 0, 1, 2, 0); break;
        case 37: armPshufd(dstreg, srcreg, 1, 1, 2, 0); break;
        case 38: armPshufd(dstreg, srcreg, 2, 1, 2, 0); break;
        case 39: armPshufd(dstreg, srcreg, 3, 1, 2, 0); break;
        case 40: armPshufd(dstreg, srcreg, 0, 2, 2, 0); break;
        case 41: armPshufd(dstreg, srcreg, 1, 2, 2, 0); break;
        case 42: armPshufd(dstreg, srcreg, 2, 2, 2, 0); break;
        case 43: armPshufd(dstreg, srcreg, 3, 2, 2, 0); break;
        case 44: armPshufd(dstreg, srcreg, 0, 3, 2, 0); break;
        case 45: armPshufd(dstreg, srcreg, 1, 3, 2, 0); break;
        case 46: armPshufd(dstreg, srcreg, 2, 3, 2, 0); break;
        case 47: armPshufd(dstreg, srcreg, 3, 3, 2, 0); break;
        case 48: armPshufd(dstreg, srcreg, 0, 0, 3, 0); break;
        case 49: armPshufd(dstreg, srcreg, 1, 0, 3, 0); break;
        case 50: armPshufd(dstreg, srcreg, 2, 0, 3, 0); break;
        case 51: armPshufd(dstreg, srcreg, 3, 0, 3, 0); break;
        case 52: armPshufd(dstreg, srcreg, 0, 1, 3, 0); break;
        case 53: armPshufd(dstreg, srcreg, 1, 1, 3, 0); break;
        case 54: armPshufd(dstreg, srcreg, 2, 1, 3, 0); break;
        case 55: armPshufd(dstreg, srcreg, 3, 1, 3, 0); break;
        case 56: armPshufd(dstreg, srcreg, 0, 2, 3, 0); break;
        case 57: armPshufd(dstreg, srcreg, 1, 2, 3, 0); break;
        case 58: armPshufd(dstreg, srcreg, 2, 2, 3, 0); break;
        case 59: armPshufd(dstreg, srcreg, 3, 2, 3, 0); break;
        case 60: armPshufd(dstreg, srcreg, 0, 3, 3, 0); break;
        case 61: armPshufd(dstreg, srcreg, 1, 3, 3, 0); break;
        case 62: armPshufd(dstreg, srcreg, 2, 3, 3, 0); break;
        case 63: armPshufd(dstreg, srcreg, 3, 3, 3, 0); break;
        case 64: armPshufd(dstreg, srcreg, 0, 0, 0, 1); break;
        case 65: armPshufd(dstreg, srcreg, 1, 0, 0, 1); break;
        case 66: armPshufd(dstreg, srcreg, 2, 0, 0, 1); break;
        case 67: armPshufd(dstreg, srcreg, 3, 0, 0, 1); break;
        case 68: armPshufd(dstreg, srcreg, 0, 1, 0, 1); break;
        case 69: armPshufd(dstreg, srcreg, 1, 1, 0, 1); break;
        case 70: armPshufd(dstreg, srcreg, 2, 1, 0, 1); break;
        case 71: armPshufd(dstreg, srcreg, 3, 1, 0, 1); break;
        case 72: armPshufd(dstreg, srcreg, 0, 2, 0, 1); break;
        case 73: armPshufd(dstreg, srcreg, 1, 2, 0, 1); break;
        case 74: armPshufd(dstreg, srcreg, 2, 2, 0, 1); break;
        case 75: armPshufd(dstreg, srcreg, 3, 2, 0, 1); break;
        case 76: armPshufd(dstreg, srcreg, 0, 3, 0, 1); break;
        case 77: armPshufd(dstreg, srcreg, 1, 3, 0, 1); break;
        case 78: armPshufd(dstreg, srcreg, 2, 3, 0, 1); break;
        case 79: armPshufd(dstreg, srcreg, 3, 3, 0, 1); break;
        case 80: armPshufd(dstreg, srcreg, 0, 0, 1, 1); break;
        case 81: armPshufd(dstreg, srcreg, 1, 0, 1, 1); break;
        case 82: armPshufd(dstreg, srcreg, 2, 0, 1, 1); break;
        case 83: armPshufd(dstreg, srcreg, 3, 0, 1, 1); break;
        case 84: armPshufd(dstreg, srcreg, 0, 1, 1, 1); break;
        case 85: armPshufd(dstreg, srcreg, 1, 1, 1, 1); break;
        case 86: armPshufd(dstreg, srcreg, 2, 1, 1, 1); break;
        case 87: armPshufd(dstreg, srcreg, 3, 1, 1, 1); break;
        case 88: armPshufd(dstreg, srcreg, 0, 2, 1, 1); break;
        case 89: armPshufd(dstreg, srcreg, 1, 2, 1, 1); break;
        case 90: armPshufd(dstreg, srcreg, 2, 2, 1, 1); break;
        case 91: armPshufd(dstreg, srcreg, 3, 2, 1, 1); break;
        case 92: armPshufd(dstreg, srcreg, 0, 3, 1, 1); break;
        case 93: armPshufd(dstreg, srcreg, 1, 3, 1, 1); break;
        case 94: armPshufd(dstreg, srcreg, 2, 3, 1, 1); break;
        case 95: armPshufd(dstreg, srcreg, 3, 3, 1, 1); break;
        case 96: armPshufd(dstreg, srcreg, 0, 0, 2, 0); break;
        case 97: armPshufd(dstreg, srcreg, 1, 0, 2, 1); break;
        case 98: armPshufd(dstreg, srcreg, 2, 0, 2, 1); break;
        case 99: armPshufd(dstreg, srcreg, 3, 0, 2, 1); break;
        case 100: armPshufd(dstreg, srcreg, 0, 1, 2, 1); break;
        case 101: armPshufd(dstreg, srcreg, 1, 1, 2, 1); break;
        case 102: armPshufd(dstreg, srcreg, 2, 1, 2, 1); break;
        case 103: armPshufd(dstreg, srcreg, 3, 1, 2, 1); break;
        case 104: armPshufd(dstreg, srcreg, 0, 2, 2, 1); break;
        case 105: armPshufd(dstreg, srcreg, 1, 2, 2, 1); break;
        case 106: armPshufd(dstreg, srcreg, 2, 2, 2, 1); break;
        case 107: armPshufd(dstreg, srcreg, 3, 2, 2, 1); break;
        case 108: armPshufd(dstreg, srcreg, 0, 3, 2, 1); break;
        case 109: armPshufd(dstreg, srcreg, 1, 3, 2, 1); break;
        case 110: armPshufd(dstreg, srcreg, 2, 3, 2, 1); break;
        case 111: armPshufd(dstreg, srcreg, 3, 3, 2, 1); break;
        case 112: armPshufd(dstreg, srcreg, 0, 0, 3, 1); break;
        case 113: armPshufd(dstreg, srcreg, 1, 0, 3, 1); break;
        case 114: armPshufd(dstreg, srcreg, 2, 0, 3, 1); break;
        case 115: armPshufd(dstreg, srcreg, 3, 0, 3, 1); break;
        case 116: armPshufd(dstreg, srcreg, 0, 1, 3, 1); break;
        case 117: armPshufd(dstreg, srcreg, 1, 1, 3, 1); break;
        case 118: armPshufd(dstreg, srcreg, 2, 1, 3, 1); break;
        case 119: armPshufd(dstreg, srcreg, 3, 1, 3, 1); break;
        case 120: armPshufd(dstreg, srcreg, 0, 2, 3, 1); break;
        case 121: armPshufd(dstreg, srcreg, 1, 2, 3, 1); break;
        case 122: armPshufd(dstreg, srcreg, 2, 2, 3, 1); break;
        case 123: armPshufd(dstreg, srcreg, 3, 2, 3, 1); break;
        case 124: armPshufd(dstreg, srcreg, 0, 3, 3, 1); break;
        case 125: armPshufd(dstreg, srcreg, 1, 3, 3, 1); break;
        case 126: armPshufd(dstreg, srcreg, 2, 3, 3, 1); break;
        case 127: armPshufd(dstreg, srcreg, 3, 3, 3, 1); break;
        case 128: armPshufd(dstreg, srcreg, 0, 0, 0, 2); break;
        case 129: armPshufd(dstreg, srcreg, 1, 0, 0, 2); break;
        case 130: armPshufd(dstreg, srcreg, 2, 0, 0, 2); break;
        case 131: armPshufd(dstreg, srcreg, 3, 0, 0, 2); break;
        case 132: armPshufd(dstreg, srcreg, 0, 1, 0, 2); break;
        case 133: armPshufd(dstreg, srcreg, 1, 1, 0, 2); break;
        case 134: armPshufd(dstreg, srcreg, 2, 1, 0, 2); break;
        case 135: armPshufd(dstreg, srcreg, 3, 1, 0, 2); break;
        case 136: armPshufd(dstreg, srcreg, 0, 2, 0, 2); break;
        case 137: armPshufd(dstreg, srcreg, 1, 2, 0, 2); break;
        case 138: armPshufd(dstreg, srcreg, 2, 2, 0, 2); break;
        case 139: armPshufd(dstreg, srcreg, 3, 2, 0, 2); break;
        case 140: armPshufd(dstreg, srcreg, 0, 3, 0, 2); break;
        case 141: armPshufd(dstreg, srcreg, 1, 3, 0, 2); break;
        case 142: armPshufd(dstreg, srcreg, 2, 3, 0, 2); break;
        case 143: armPshufd(dstreg, srcreg, 3, 3, 0, 2); break;
        case 144: armPshufd(dstreg, srcreg, 0, 0, 1, 2); break;
        case 145: armPshufd(dstreg, srcreg, 1, 0, 1, 2); break;
        case 146: armPshufd(dstreg, srcreg, 2, 0, 1, 2); break;
        case 147: armPshufd(dstreg, srcreg, 3, 0, 1, 2); break;
        case 148: armPshufd(dstreg, srcreg, 0, 1, 1, 2); break;
        case 149: armPshufd(dstreg, srcreg, 1, 1, 1, 2); break;
        case 150: armPshufd(dstreg, srcreg, 2, 1, 1, 2); break;
        case 151: armPshufd(dstreg, srcreg, 3, 1, 1, 2); break;
        case 152: armPshufd(dstreg, srcreg, 0, 2, 1, 2); break;
        case 153: armPshufd(dstreg, srcreg, 1, 2, 1, 2); break;
        case 154: armPshufd(dstreg, srcreg, 2, 2, 1, 2); break;
        case 155: armPshufd(dstreg, srcreg, 3, 2, 1, 2); break;
        case 156: armPshufd(dstreg, srcreg, 0, 3, 1, 2); break;
        case 157: armPshufd(dstreg, srcreg, 1, 3, 1, 2); break;
        case 158: armPshufd(dstreg, srcreg, 2, 3, 1, 2); break;
        case 159: armPshufd(dstreg, srcreg, 3, 3, 1, 2); break;
        case 160: armPshufd(dstreg, srcreg, 0, 0, 2, 2); break;
        case 161: armPshufd(dstreg, srcreg, 1, 0, 2, 2); break;
        case 162: armPshufd(dstreg, srcreg, 2, 0, 2, 2); break;
        case 163: armPshufd(dstreg, srcreg, 3, 0, 2, 2); break;
        case 164: armPshufd(dstreg, srcreg, 0, 1, 2, 2); break;
        case 165: armPshufd(dstreg, srcreg, 1, 1, 2, 2); break;
        case 166: armPshufd(dstreg, srcreg, 2, 1, 2, 2); break;
        case 167: armPshufd(dstreg, srcreg, 3, 1, 2, 2); break;
        case 168: armPshufd(dstreg, srcreg, 0, 2, 2, 2); break;
        case 169: armPshufd(dstreg, srcreg, 1, 2, 2, 2); break;
        case 170: armPshufd(dstreg, srcreg, 2, 2, 2, 2); break;
        case 171: armPshufd(dstreg, srcreg, 3, 2, 2, 2); break;
        case 172: armPshufd(dstreg, srcreg, 0, 3, 2, 2); break;
        case 173: armPshufd(dstreg, srcreg, 1, 3, 2, 2); break;
        case 174: armPshufd(dstreg, srcreg, 2, 3, 2, 2); break;
        case 175: armPshufd(dstreg, srcreg, 3, 3, 2, 2); break;
        case 176: armPshufd(dstreg, srcreg, 0, 0, 3, 2); break;
        case 177: armPshufd(dstreg, srcreg, 1, 0, 3, 2); break;
        case 178: armPshufd(dstreg, srcreg, 2, 0, 3, 2); break;
        case 179: armPshufd(dstreg, srcreg, 3, 0, 3, 2); break;
        case 180: armPshufd(dstreg, srcreg, 0, 1, 3, 2); break;
        case 181: armPshufd(dstreg, srcreg, 1, 1, 3, 2); break;
        case 182: armPshufd(dstreg, srcreg, 2, 1, 3, 2); break;
        case 183: armPshufd(dstreg, srcreg, 3, 1, 3, 2); break;
        case 184: armPshufd(dstreg, srcreg, 0, 2, 3, 2); break;
        case 185: armPshufd(dstreg, srcreg, 1, 2, 3, 2); break;
        case 186: armPshufd(dstreg, srcreg, 2, 2, 3, 2); break;
        case 187: armPshufd(dstreg, srcreg, 3, 2, 3, 2); break;
        case 188: armPshufd(dstreg, srcreg, 0, 3, 3, 2); break;
        case 189: armPshufd(dstreg, srcreg, 1, 3, 3, 2); break;
        case 190: armPshufd(dstreg, srcreg, 2, 3, 3, 2); break;
        case 191: armPshufd(dstreg, srcreg, 3, 3, 3, 2); break;
        case 192: armPshufd(dstreg, srcreg, 0, 0, 0, 3); break;
        case 193: armPshufd(dstreg, srcreg, 1, 0, 0, 3); break;
        case 194: armPshufd(dstreg, srcreg, 2, 0, 0, 3); break;
        case 195: armPshufd(dstreg, srcreg, 3, 0, 0, 3); break;
        case 196: armPshufd(dstreg, srcreg, 0, 1, 0, 3); break;
        case 197: armPshufd(dstreg, srcreg, 1, 1, 0, 3); break;
        case 198: armPshufd(dstreg, srcreg, 2, 1, 0, 3); break;
        case 199: armPshufd(dstreg, srcreg, 3, 1, 0, 3); break;
        case 200: armPshufd(dstreg, srcreg, 0, 2, 0, 3); break;
        case 201: armPshufd(dstreg, srcreg, 1, 2, 0, 3); break;
        case 202: armPshufd(dstreg, srcreg, 2, 2, 0, 3); break;
        case 203: armPshufd(dstreg, srcreg, 3, 2, 0, 3); break;
        case 204: armPshufd(dstreg, srcreg, 0, 3, 0, 3); break;
        case 205: armPshufd(dstreg, srcreg, 1, 3, 0, 3); break;
        case 206: armPshufd(dstreg, srcreg, 2, 3, 0, 3); break;
        case 207: armPshufd(dstreg, srcreg, 3, 3, 0, 3); break;
        case 208: armPshufd(dstreg, srcreg, 0, 0, 1, 3); break;
        case 209: armPshufd(dstreg, srcreg, 1, 0, 1, 3); break;
        case 210: armPshufd(dstreg, srcreg, 2, 0, 1, 3); break;
        case 211: armPshufd(dstreg, srcreg, 3, 0, 1, 3); break;
        case 212: armPshufd(dstreg, srcreg, 0, 1, 1, 3); break;
        case 213: armPshufd(dstreg, srcreg, 1, 1, 1, 3); break;
        case 214: armPshufd(dstreg, srcreg, 2, 1, 1, 3); break;
        case 215: armPshufd(dstreg, srcreg, 3, 1, 1, 3); break;
        case 216: armPshufd(dstreg, srcreg, 0, 2, 1, 3); break;
        case 217: armPshufd(dstreg, srcreg, 1, 2, 1, 3); break;
        case 218: armPshufd(dstreg, srcreg, 2, 2, 1, 3); break;
        case 219: armPshufd(dstreg, srcreg, 3, 2, 1, 3); break;
        case 220: armPshufd(dstreg, srcreg, 0, 3, 1, 3); break;
        case 221: armPshufd(dstreg, srcreg, 1, 3, 1, 3); break;
        case 222: armPshufd(dstreg, srcreg, 2, 3, 1, 3); break;
        case 223: armPshufd(dstreg, srcreg, 3, 3, 1, 3); break;
        case 224: armPshufd(dstreg, srcreg, 0, 0, 2, 3); break;
        case 225: armPshufd(dstreg, srcreg, 1, 0, 2, 3); break;
        case 226: armPshufd(dstreg, srcreg, 2, 0, 2, 3); break;
        case 227: armPshufd(dstreg, srcreg, 3, 0, 2, 3); break;
        case 228: armPshufd(dstreg, srcreg, 0, 1, 2, 3); break;
        case 229: armPshufd(dstreg, srcreg, 1, 1, 2, 3); break;
        case 230: armPshufd(dstreg, srcreg, 2, 1, 2, 3); break;
        case 231: armPshufd(dstreg, srcreg, 3, 1, 2, 3); break;
        case 232: armPshufd(dstreg, srcreg, 0, 2, 2, 3); break;
        case 233: armPshufd(dstreg, srcreg, 1, 2, 2, 3); break;
        case 234: armPshufd(dstreg, srcreg, 2, 2, 2, 3); break;
        case 235: armPshufd(dstreg, srcreg, 3, 2, 2, 3); break;
        case 236: armPshufd(dstreg, srcreg, 0, 3, 2, 3); break;
        case 237: armPshufd(dstreg, srcreg, 1, 3, 2, 3); break;
        case 238: armPshufd(dstreg, srcreg, 2, 3, 2, 3); break;
        case 239: armPshufd(dstreg, srcreg, 3, 3, 2, 3); break;
        case 240: armPshufd(dstreg, srcreg, 0, 0, 3, 3); break;
        case 241: armPshufd(dstreg, srcreg, 1, 0, 3, 3); break;
        case 242: armPshufd(dstreg, srcreg, 2, 0, 3, 3); break;
        case 243: armPshufd(dstreg, srcreg, 3, 0, 3, 3); break;
        case 244: armPshufd(dstreg, srcreg, 0, 1, 3, 3); break;
        case 245: armPshufd(dstreg, srcreg, 1, 1, 3, 3); break;
        case 246: armPshufd(dstreg, srcreg, 2, 1, 3, 3); break;
        case 247: armPshufd(dstreg, srcreg, 3, 1, 3, 3); break;
        case 248: armPshufd(dstreg, srcreg, 0, 2, 3, 3); break;
        case 249: armPshufd(dstreg, srcreg, 1, 2, 3, 3); break;
        case 250: armPshufd(dstreg, srcreg, 2, 2, 3, 3); break;
        case 251: armPshufd(dstreg, srcreg, 3, 2, 3, 3); break;
        case 252: armPshufd(dstreg, srcreg, 0, 3, 3, 3); break;
        case 253: armPshufd(dstreg, srcreg, 1, 3, 3, 3); break;
        case 254: armPshufd(dstreg, srcreg, 2, 3, 3, 3); break;
        case 255: armPshufd(dstreg, srcreg, 3, 3, 3, 3); break;
    }
}
