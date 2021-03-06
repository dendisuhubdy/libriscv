#pragma once
#include <type_traits>

#ifndef LIKELY
#define LIKELY(x) __builtin_expect((x), 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect((x), 0)
#endif

#ifndef COLD_PATH
#define COLD_PATH() __attribute__((cold))
#endif

#ifndef SYSCALL_EBREAK_NR
#define SYSCALL_EBREAK_NR    0
#endif

#ifndef MEMORY_TRAPS_ENABLED
# ifdef RISCV_DEBUG
#  define MEMORY_TRAPS_ENABLED
# endif
#endif

namespace riscv
{
	static constexpr int SYSCALL_EBREAK = SYSCALL_EBREAK_NR;

	// print information during machine creation
	extern bool verbose_machine;

#ifdef MEMORY_TRAPS_ENABLED
	static constexpr bool memory_traps_enabled = true;
#else
	static constexpr bool memory_traps_enabled = false;
#endif

#ifdef RISCV_DEBUG
	static constexpr bool debugging_enabled = true;
#else
	static constexpr bool debugging_enabled = false;
#endif
	// assert on misaligned reads/writes
	static constexpr bool memory_alignment_check = false;

#ifdef RISCV_EXT_ATOMICS
	static constexpr bool atomics_enabled = true;
#else
	static constexpr bool atomics_enabled = false;
#endif
#ifdef RISCV_EXT_COMPRESSED
	static constexpr bool compressed_enabled = true;
#else
	static constexpr bool compressed_enabled = false;
#endif
#ifdef RISCV_EXT_FLOATS
	static constexpr bool floating_point_enabled = true;
#else
	static constexpr bool floating_point_enabled = false;
#endif

	template <int W>
	struct SerializedMachine;

	template <class...> constexpr std::false_type always_false {};
}
