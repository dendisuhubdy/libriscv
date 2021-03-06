#include "rv32i.hpp"
#include "instr_helpers.hpp"

namespace riscv
{
	INSTRUCTION(UNIMPLEMENTED,
	[] (auto& cpu, rv32i_instruction /* instr */) {
		// handler
		cpu.trigger_exception(UNIMPLEMENTED_INSTRUCTION);
	},
	[] (char* buffer, size_t len, auto&, rv32i_instruction instr) -> int {
		// printer
		if (instr.length() == 4) {
			return snprintf(buffer, len, "UNIMPLEMENTED: 4-byte 0x%X (0x%X)",
							instr.opcode(), instr.whole);
		} else {
			return snprintf(buffer, len, "UNIMPLEMENTED: 2-byte %#hx F%#hx (%#hx)",
							instr.compressed().opcode(),
							instr.compressed().funct3(),
							instr.half[0]);
		}
	});

	INSTRUCTION(LOAD,
	[] (auto& cpu, rv32i_instruction instr)
	{
		if (instr.Itype.rd != 0) {
			auto& reg = cpu.reg(instr.Itype.rd);
			const auto addr = cpu.reg(instr.Itype.rs1) + instr.Itype.signed_imm();
			const uint32_t type = instr.Itype.funct3;
			switch (type) {
			case 0: // LB
				reg = cpu.machine().memory.template read<uint8_t>(addr);
				// sign-extend 8-bit
				if (reg & 0x80) reg |= 0xFFFFFF00;
				return;
			case 1: // LH
				reg = cpu.machine().memory.template read<uint16_t>(addr);
				// sign-extend 16-bit
				if (reg & 0x8000) reg |= 0xFFFF0000;
				return;
			case 2: // LW
				reg = cpu.machine().memory.template read<uint32_t>(addr);
				return;
			case 4: // LBU
				// load zero-extended 8-bit value
				reg = cpu.machine().memory.template read<uint8_t>(addr);
				return;
			case 5: // LHU
				// load zero-extended 16-bit value
				reg = cpu.machine().memory.template read<uint16_t>(addr);
				return;
			}
		}
		cpu.trigger_exception(ILLEGAL_OPERATION);
	},
	[] (char* buffer, size_t len, auto& cpu, rv32i_instruction instr) -> int {
		// printer
		static std::array<const char*, 8> f3 = {"LOADB", "LOADH", "LOADW", "???", "LBU", "LHU", "???", "???"};
		return snprintf(buffer, len, "%s %s, [%s%+d = 0x%X]",
						f3[instr.Itype.funct3], RISCV::regname(instr.Itype.rd),
						RISCV::regname(instr.Itype.rs1), instr.Itype.signed_imm(),
						cpu.reg(instr.Itype.rs1) + instr.Itype.signed_imm());
	});

	INSTRUCTION(STORE,
	[] (auto& cpu, rv32i_instruction instr)
	{
		const auto value = cpu.reg(instr.Stype.rs2);
		const auto addr  = cpu.reg(instr.Stype.rs1) + instr.Stype.signed_imm();
		const auto type  = instr.Stype.funct3;
		if (type == 0) {
			cpu.machine().memory.template write<uint8_t>(addr, value);
			return;
		} else if (type == 1) {
			cpu.machine().memory.template write<uint16_t>(addr, value);
			return;
		} else if (type == 2) {
			cpu.machine().memory.template write<uint32_t>(addr, value);
			return;
		}
		cpu.trigger_exception(ILLEGAL_OPERATION);
	},
	[] (char* buffer, size_t len, auto& cpu, rv32i_instruction instr) -> int {
		// printer
		static std::array<const char*, 4> f3 = {"STOREB", "STOREH", "STOREW", "STORE?"};
		const auto idx = std::min(instr.Stype.funct3, instr.to_word(f3.size()));
		return snprintf(buffer, len, "%s %s, [%s%+d] (0x%X)",
						f3[idx], RISCV::regname(instr.Stype.rs2),
						RISCV::regname(instr.Stype.rs1), instr.Stype.signed_imm(),
						cpu.reg(instr.Stype.rs1) + instr.Stype.signed_imm());
	});

	INSTRUCTION(BRANCH,
	[] (auto& cpu, rv32i_instruction instr) {
		bool comparison = false;
		const auto& reg1 = cpu.reg(instr.Btype.rs1);
		const auto& reg2 = cpu.reg(instr.Btype.rs2);
		switch (instr.Btype.funct3) {
			case 0x0: // BEQ
				comparison = reg1 == reg2;
				break;
			case 0x1: // BNE
				comparison = reg1 != reg2;
				break;
			case 0x2: // ???
			case 0x3: // ???
				cpu.trigger_exception(ILLEGAL_OPERATION);
				break;
			case 0x4: // BLT
				comparison = instr.to_signed(reg1) < instr.to_signed(reg2);
				break;
			case 0x5: // BGE
				comparison = instr.to_signed(reg1) >= instr.to_signed(reg2);
				break;
			case 0x6: // BLTU
				comparison = reg1 < reg2;
				break;
			case 0x7: // BGEU
				comparison = reg1 >= reg2;
				break;
		}
		if (comparison) {
			cpu.jump(cpu.pc() + instr.Btype.signed_imm() - 4);
			if (UNLIKELY(cpu.machine().verbose_jumps)) {
				printf(">>> BRANCH jump to 0x%X\n", cpu.pc() + 4);
			}
		}
	},
	[] (char* buffer, size_t len, auto& cpu, rv32i_instruction instr) -> int {
		// BRANCH compares two registers, BQE = equal taken, BNE = notequal taken
		static std::array<const char*, 8> f3 = {"BEQ", "BNE", "???", "???", "BLT", "BGE", "BLTU", "BGEU"};
		static std::array<const char*, 8> f1z = {"BEQ", "BNE", "???", "???", "BGTZ", "BLEZ", "BLTU", "BGEU"};
		static std::array<const char*, 8> f2z = {"BEQZ", "BNEZ", "???", "???", "BLTZ", "BGEZ", "BLTU", "BGEU"};
		if (instr.Btype.rs1 != 0 && instr.Btype.rs2) {
			return snprintf(buffer, len, "%s %s, %s => PC%+d (0x%X)",
							f3[instr.Btype.funct3],
							RISCV::regname(instr.Btype.rs1),
							RISCV::regname(instr.Btype.rs2),
							instr.Btype.signed_imm(),
							cpu.pc() + instr.Btype.signed_imm());
		} else {
			auto& array = (instr.Btype.rs1) ? f2z : f1z;
			auto  reg   = (instr.Btype.rs1) ? instr.Btype.rs1 : instr.Btype.rs2;
			return snprintf(buffer, len, "%s %s => PC%+d (0x%X)",
							array[instr.Btype.funct3],
							RISCV::regname(reg),
							instr.Btype.signed_imm(),
							cpu.pc() + instr.Btype.signed_imm());
		}
	});

	INSTRUCTION(JALR,
	[] (auto& cpu, rv32i_instruction instr) {
		// jump to register + immediate
		const auto address = cpu.reg(instr.Itype.rs1) + instr.Itype.signed_imm();
		// Link *next* instruction (rd = PC + 4)
		if (LIKELY(instr.Itype.rd != 0)) {
			cpu.reg(instr.Itype.rd) = cpu.pc() + 4;
		}
		cpu.jump(address - 4);
		if (UNLIKELY(cpu.machine().verbose_jumps)) {
		printf(">>> JMP 0x%X <-- %s = 0x%X%+d\n", address,
				RISCV::regname(instr.Itype.rs1), cpu.reg(instr.Itype.rs1), instr.Itype.signed_imm());
		}
	},
	[] (char* buffer, size_t len, auto& cpu, rv32i_instruction instr) -> int {
		// RISC-V's RET instruction: return to register + immediate
		const char* variant = (instr.Itype.rs1 == RISCV::REG_RA) ? "RET" : "JMP";
		const auto address = cpu.reg(instr.Itype.rs1) + instr.Itype.signed_imm();
		return snprintf(buffer, len, "%s %s%+d (0x%X)", variant,
						RISCV::regname(instr.Itype.rs1), instr.Itype.signed_imm(), address);
	});

	INSTRUCTION(JAL,
	[] (auto& cpu, rv32i_instruction instr) {
		// Link *next* instruction (rd = PC + 4)
		if (LIKELY(instr.Jtype.rd != 0)) {
			cpu.reg(instr.Jtype.rd) = cpu.pc() + 4;
		}
		// And Jump (relative)
		cpu.jump(cpu.pc() + instr.Jtype.jump_offset() - 4);
		if (UNLIKELY(cpu.machine().verbose_jumps)) {
			printf(">>> CALL 0x%X <-- %s = 0x%X\n", cpu.pc(),
					RISCV::regname(instr.Jtype.rd), cpu.reg(instr.Jtype.rd));
		}
	},
	[] (char* buffer, size_t len, auto& cpu, rv32i_instruction instr) -> int {
		if (instr.Jtype.rd != 0) {
		return snprintf(buffer, len, "JAL %s, PC%+d (0x%X)",
						RISCV::regname(instr.Jtype.rd), instr.Jtype.jump_offset(),
						cpu.pc() + instr.Jtype.jump_offset());
		}
		return snprintf(buffer, len, "JMP PC%+d (0x%X)",
						instr.Jtype.jump_offset(), cpu.pc() + instr.Jtype.jump_offset());
	});

	INSTRUCTION(OP_IMM,
	[] (auto& cpu, rv32i_instruction instr)
	{
		if (instr.Itype.rd != 0)
		{
			auto& dst = cpu.reg(instr.Itype.rd);
			const auto& src = cpu.reg(instr.Itype.rs1);
			switch (instr.Itype.funct3) {
			case 0x0:
				// ADDI: Add sign-extended 12-bit immediate
				dst = src + instr.Itype.signed_imm();
				break;
			case 0x1: // SLLI:
				dst = src << instr.Itype.shift_imm();
				break;
			case 0x2: // SLTI:
				dst = (instr.to_signed(src) < instr.Itype.signed_imm()) ? 1 : 0;
				break;
			case 0x3: // SLTU:
				dst = (src < (unsigned) instr.Itype.signed_imm()) ? 1 : 0;
				break;
			case 0x4: // XORI:
				dst = src ^ instr.Itype.signed_imm();
				break;
			case 0x5: // SRLI / SRAI:
				if (LIKELY(!instr.Itype.is_srai()))
					dst = src >> instr.Itype.shift_imm();
				else { // SRAI: preserve the sign bit
					const uint32_t shifts = instr.Itype.shift_imm();
					const bool is_signed = (src & 0x80000000) != 0;
					dst = RV32I::SRA(is_signed, shifts, src);
				}
				break;
			case 0x6: // ORI:
				dst = src | instr.Itype.signed_imm();
				break;
			case 0x7: // ANDI:
				dst = src & instr.Itype.signed_imm();
				break;
			}
		} else {
			cpu.trigger_exception(ILLEGAL_OPERATION);
		}
	},
	[] (char* buffer, size_t len, auto& cpu, rv32i_instruction instr) -> int
	{
		if (instr.Itype.imm == 0)
		{
			// this is the official NOP instruction (ADDI x0, x0, 0)
			if (instr.Itype.rd == 0 && instr.Itype.rs1 == 0) {
				return snprintf(buffer, len, "NOP");
			}
			static std::array<const char*, 8> func3 = {"MV", "SLL", "SLT", "SLT", "XOR", "SRL", "OR", "AND"};
			return snprintf(buffer, len, "%s %s, %s",
							func3[instr.Itype.funct3],
							RISCV::regname(instr.Itype.rd),
							RISCV::regname(instr.Itype.rs1));
		}
		else if (instr.Itype.rs1 != 0 && instr.Itype.funct3 == 1) {
			return snprintf(buffer, len, "SLLI %s, %s << %u (0x%X)",
							RISCV::regname(instr.Itype.rd),
							RISCV::regname(instr.Itype.rs1),
							instr.Itype.shift_imm(),
							cpu.reg(instr.Itype.rs1) << instr.Itype.shift_imm());
		} else if (instr.Itype.rs1 != 0 && instr.Itype.funct3 == 5) {
			return snprintf(buffer, len, "%s %s, %s >> %u (0x%X)",
							(instr.Itype.is_srai() ? "SRAI" : "SRLI"),
							RISCV::regname(instr.Itype.rd),
							RISCV::regname(instr.Itype.rs1),
							instr.Itype.shift_imm(),
							cpu.reg(instr.Itype.rs1) >> instr.Itype.shift_imm());
		} else if (instr.Itype.rs1 != 0) {
			static std::array<const char*, 8> func3 = {"ADDI", "SLLI", "SLTI", "SLTU", "XORI", "SRLI", "ORI", "ANDI"};
			if (!(instr.Itype.funct3 == 4 && instr.Itype.signed_imm() == -1)) {
				return snprintf(buffer, len, "%s %s, %s%+d (0x%X)",
								func3[instr.Itype.funct3],
								RISCV::regname(instr.Itype.rd),
								RISCV::regname(instr.Itype.rs1),
								instr.Itype.signed_imm(),
								cpu.reg(instr.Itype.rs1) + instr.Itype.signed_imm());
			} else {
				return snprintf(buffer, len, "NOT %s, %s",
								RISCV::regname(instr.Itype.rd),
								RISCV::regname(instr.Itype.rs1));
			}
		}
		static std::array<const char*, 8> func3 = {"LINT", "SLLI", "SLTI", "SLTU", "XORI", "SRLI", "ORI", "ANDI"};
		return snprintf(buffer, len, "%s %s, %d",
						func3[instr.Itype.funct3],
						RISCV::regname(instr.Itype.rd),
						instr.Itype.signed_imm());
	});

	INSTRUCTION(OP,
	[] (auto& cpu, rv32i_instruction instr)
	{
		if (instr.Rtype.rd != 0)
		{
			auto& dst = cpu.reg(instr.Rtype.rd);
			const auto& src1 = cpu.reg(instr.Rtype.rs1);
			const auto& src2 = cpu.reg(instr.Rtype.rs2);

			switch (instr.Rtype.jumptable_friendly_op()) {
				case 0x0: // ADD / SUB
					dst = src1 + (!instr.Rtype.is_f7() ? src2 : -src2);
					break;
				case 0x1: // SLL
					dst = src1 << (src2 & 0x1F);
					break;
				case 0x2: // SLT
					dst = (instr.to_signed(src1) < instr.to_signed(src2)) ? 1 : 0;
					break;
				case 0x3: // SLTU
					dst = (src1 < src2) ? 1 : 0;
					break;
				case 0x4: // XOR
					dst = src1 ^ src2;
					break;
				case 0x5: // SRL / SRA
					if (!instr.Rtype.is_f7()) { // SRL
						dst = src1 >> (src2 & 0x1F);
					} else { // SRA
						const bool is_signed = (src1 & 0x80000000) != 0;
						const uint32_t shifts = src2 & 0x1F; // max 31 shifts!
						dst = RV32I::SRA(is_signed, shifts, src1);
					}
					break;
				case 0x6: // OR
					dst = src1 | src2;
					break;
				case 0x7: // AND
					dst = src1 & src2;
					break;
				// extension RV32M
				case 0x10: // MUL
					dst = instr.to_signed(src1) * instr.to_signed(src2);
					break;
				case 0x11: // MULH
					dst = ((int64_t) src1 * (int64_t) src2) >> 32u;
					break;
				case 0x12: // MULHSU
					dst = ((int64_t) src1 * (uint64_t) src2) >> 32u;
					break;
				case 0x13: // MULHU
					dst = ((uint64_t) src1 * (uint64_t) src2) >> 32u;
					break;
				case 0x14: // DIV
					// division by zero is not an exception
					if (LIKELY(instr.to_signed(src2) != 0)) {
						// rv32i_instr.cpp:301:2: runtime error:
						// division of -2147483648 by -1 cannot be represented in type 'int'
						if (LIKELY(!(src1 == 2147483648 && src2 == 4294967295)))
							dst = instr.to_signed(src1) / instr.to_signed(src2);
					}
					break;
				case 0x15: // DIVU
					if (LIKELY(src2 != 0)) dst = src1 / src2;
					break;
				case 0x16: // REM
					if (LIKELY(src2 != 0)) {
						if (LIKELY(!(src1 == 2147483648 && src2 == 4294967295)))
							dst = instr.to_signed(src1) % instr.to_signed(src2);
					}
					break;
				case 0x17: // REMU
					if (LIKELY(src2 != 0)) {
						dst = src1 % src2;
					}
					break;
			}
		} else {
			cpu.trigger_exception(ILLEGAL_OPERATION);
		}
	},
	[] (char* buffer, size_t len, auto&, rv32i_instruction instr) -> int
	{
		if (!instr.Rtype.is_32M())
		{
			static std::array<const char*, 8*2> func3 = {
				"ADD", "SLL", "SLT", "SLTU", "XOR", "SRL", "OR", "AND",
				"SUB", "SLL", "SLT", "SLTU", "XOR", "SRA", "OR", "AND"};
			const int EX = instr.Rtype.is_f7() ? 8 : 0;
			return snprintf(buffer, len, "%s %s %s, %s",
							RISCV::regname(instr.Rtype.rs1),
							func3[instr.Rtype.funct3 + EX],
							RISCV::regname(instr.Rtype.rs2),
							RISCV::regname(instr.Rtype.rd));
		}
		else {
			static std::array<const char*, 8> func3 = {
				"MUL", "MULH", "MULHSU", "MULHU", "DIV", "DIVU", "REM", "REMU"};
			return snprintf(buffer, len, "%s %s %s, %s",
							RISCV::regname(instr.Rtype.rs1),
							func3[instr.Rtype.funct3],
							RISCV::regname(instr.Rtype.rs2),
							RISCV::regname(instr.Rtype.rd));
		}
	});

	INSTRUCTION(SYSTEM,
	[] (auto& cpu, rv32i_instruction instr) {
		// system functions
		switch (instr.Itype.funct3)
		{
		case 0x0: // SYSTEM functions
			switch (instr.Itype.imm)
			{
			case 0: // ECALL
				cpu.machine().system_call(cpu.reg(RISCV::REG_ECALL));
				return;
			case 1: // EBREAK
				cpu.machine().system_call(riscv::SYSCALL_EBREAK);
				return;
			}
			break;
		case 0x1: // CSRRW
		case 0x2: // CSRRS
			// if destination is x0, then we do not write to rd
			bool rd = instr.Itype.rd != 0;
			bool wr = instr.Itype.rs1 != 0;
			switch (instr.Itype.imm)
			{
			case 0x001: // fflags (accrued exceptions)
				if (rd) cpu.reg(instr.Itype.rd) = cpu.registers().fcsr().fflags;
				if (wr) cpu.registers().fcsr().fflags = cpu.reg(instr.Itype.rs1);
				return;
			case 0x002: // frm (rounding-mode)
				if (rd) cpu.reg(instr.Itype.rd) = cpu.registers().fcsr().frm;
				if (wr) cpu.registers().fcsr().frm = cpu.reg(instr.Itype.rs1);
				return;
			case 0x003: // fcsr (control and status register)
				if (rd) cpu.reg(instr.Itype.rd) = cpu.registers().fcsr().whole;
				if (wr) cpu.registers().fcsr().whole = cpu.reg(instr.Itype.rs1);
				return;
			case 0xC00: // CSR RDCYCLE (lower)
			case 0xC02: // RDINSTRET (lower)
				if (rd) cpu.reg(instr.Itype.rd) = cpu.registers().counter;
				return;
			case 0xC80: // CSR RDCYCLE (upper)
			case 0xC82: // RDINSTRET (upper)
				if (rd) cpu.reg(instr.Itype.rd) = cpu.registers().counter >> 32u;
				return;
			case 0xC01: // CSR RDTIME (lower)
				if (rd) cpu.reg(instr.Itype.rd) = u64_monotonic_time();
				return;
			case 0xC81: // CSR RDTIME (upper)
				if (rd) cpu.reg(instr.Itype.rd) = u64_monotonic_time() >> 32u;
				return;
			}
			break;
		}
		// if we got here, its an illegal operation!
		cpu.trigger_exception(ILLEGAL_OPERATION);
	},
	[] (char* buffer, size_t len, auto&, rv32i_instruction instr) -> int {
		// system functions
		static std::array<const char*, 2> etype = {"ECALL", "EBREAK"};
		if (instr.Itype.imm < 2 && instr.Itype.funct3 == 0) {
			return snprintf(buffer, len, "SYS %s", etype.at(instr.Itype.imm));
		} else if (instr.Itype.funct3 == 0x2) {
			// CSRRS
			switch (instr.Itype.imm) {
				case 0x001:
					return snprintf(buffer, len, "RDCSR FFLAGS %s", RISCV::regname(instr.Itype.rd));
				case 0x002:
					return snprintf(buffer, len, "RDCSR FRM %s", RISCV::regname(instr.Itype.rd));
				case 0x003:
					return snprintf(buffer, len, "RDCSR FCSR %s", RISCV::regname(instr.Itype.rd));
				case 0xC00:
					return snprintf(buffer, len, "RDCYCLE.L %s", RISCV::regname(instr.Itype.rd));
				case 0xC01:
					return snprintf(buffer, len, "RDINSTRET.L %s", RISCV::regname(instr.Itype.rd));
				case 0xC80:
					return snprintf(buffer, len, "RDCYCLE.U %s", RISCV::regname(instr.Itype.rd));
				case 0xC81:
					return snprintf(buffer, len, "RDINSTRET.U %s", RISCV::regname(instr.Itype.rd));
			}
			return snprintf(buffer, len, "CSRRS (unknown), %s", RISCV::regname(instr.Itype.rd));
		} else {
			return snprintf(buffer, len, "SYS ???");
		}
	});

	INSTRUCTION(LUI,
	[] (auto& cpu, rv32i_instruction instr) {
		// handler
		if (instr.Utype.rd != 0) {
			cpu.reg(instr.Utype.rd) = instr.Utype.upper_imm();
			return;
		}
		cpu.trigger_exception(ILLEGAL_OPERATION);
	},
	[] (char* buffer, size_t len, auto&, rv32i_instruction instr) -> int {
		// printer
		return snprintf(buffer, len, "LUI %s, 0x%X",
						RISCV::regname(instr.Utype.rd), instr.Utype.upper_imm());
	});

	INSTRUCTION(AUIPC,
	[] (auto& cpu, rv32i_instruction instr) {
		// handler
		if (instr.Utype.rd != 0) {
			cpu.reg(instr.Utype.rd) = cpu.pc() + instr.Utype.upper_imm();
			return;
		}
		cpu.trigger_exception(ILLEGAL_OPERATION);
	},
	[] (char* buffer, size_t len, auto& cpu, rv32i_instruction instr) -> int {
		// printer
		return snprintf(buffer, len, "AUIPC %s, PC+0x%X (0x%X)",
						RISCV::regname(instr.Utype.rd), instr.Utype.upper_imm(),
						cpu.pc() + instr.Utype.upper_imm());
	});

	INSTRUCTION(OP_IMM32,
	[] (auto& cpu, rv32i_instruction /* instr */) {
		// handler
		cpu.trigger_exception(UNIMPLEMENTED_INSTRUCTION);
	},
	[] (char* buffer, size_t len, auto&, rv32i_instruction) -> int {
		// printer
		return snprintf(buffer, len, "OP_IMM32");
	});

	INSTRUCTION(OP32,
	[] (auto& cpu, rv32i_instruction /* instr */) {
		// handler
		cpu.trigger_exception(UNIMPLEMENTED_INSTRUCTION);
	},
	[] (char* buffer, size_t len, auto&, rv32i_instruction) -> int {
		// printer
		return snprintf(buffer, len, "OP_32");
	});

	INSTRUCTION(FENCE,
	[] (auto& /* cpu */, rv32i_instruction /* instr */) {
		// literally do nothing
	},
	[] (char* buffer, size_t len, auto&, rv32i_instruction) -> int {
		// printer
		return snprintf(buffer, len, "FENCE");
	});
}
