/*
 * Tracky is a profiling tool for better plotting for graphics applications.
 * The original Tracky implementation is written in C++ and uses OpenGL.
 * This is a Vulkan implementation of Tracky, which is used in Vultra.
 *
 * Author: Markus Billeter
 * License: No license, use at your own risk.
 * Vulkan Implementation by: Kexuan Zhang
 */

#pragma once

#ifdef TRACKY_VULKAN
#include <vulkan/vulkan.hpp>
#endif

// NOLINTBEGIN
// clang-format off

// Configuration
#if !defined(TRACKY_ENABLE)
#	define TRACKY_ENABLE 1
#endif

#if !defined(TRACKY_DEFAULT_HARD_LEVEL)
#	define TRACKY_DEFAULT_HARD_LEVEL 8llu
#endif
#if !defined(TRACKY_DEFAULT_HARD_MASK)
#	define TRACKY_DEFAULT_HARD_MASK (1llu<<15)
#endif

/* Synopsis:
 *
 * (1) Initialize Tracky with TRACKY_STARTUP().
 *
 * (2) Start a new frame with TRACKY_NEXT_FRAME(). This is likely called
 *     repeatedly.
 *
 * (3a) Record data with the following macros ([brackets] indicate optional
 *     arguments):
 *
 *     - TRACKY_SCOPE( name [, flags] ): measure this C++ scope
 *     - TRACKY_ENTER( name [, flags] ): measure from here until matching LEAVE()
 *     - TRACKY_NEXT( name [, flags] ): conceptually equivalent to ENTER()+LEAVE()
 *     - TRACKY_LEAVE( [flags] ): stop measuring
 *
 *     SCOPE() is equivalent to placing ENTER() at the location of SCOPE() and
 *     LEAVE at the end of the C++ scope.
 *
 *     - name must be a literal string, or a string with a very long lifetime.
 *     - flags may be a combination of the following:
 *        - EFlags::*  : see EFlags for more info
 *        - X_level    : see below (X is a literal integer in [0,2^16-1)
 *        - X_group    : see below (X is a literal integer in [0,16)
 *
 *     Examples:
 *        // CPU only:
 *        TRACKY_SCOPE( "Simple" );
 *
 *		  // Same but with more typing
 *		  TRACKY_SCOPE( "Simple", CPU );
 *
 *        // Measure OpenGL time and assign to level 2.
 *        TRACKY_SCOPE( "OpenGLScope", GPU | 2_level );
 *
 *  (3b) Record additional data with the following macros:
 *
 *     - TRACKY_COUNTER( value, count [, flags] ): change counter by value
 *     - TRACKY_PERSISTENT_COUNTER( value, count [, flags] )
 *
 *     COUNTER() adds value to the named counter, which is reset each frame.
 *     PERSISTENT_COUNTER() functions similarly, except that it does not reset
 *     each frame.
 *
 *     - value is a signed long long integer which is added to the counter
 *       (value may be negative to decrement the counter)
 *     - name must be a literal string, or a string with a very long lifetime.
 *     - flags may be a combination of the following:
 *        - X_level    : see below (X is a literal integer in [0,2^16-1)
 *        - X_group    : see below (X is a literal integer in [0,16)
 *
 *     Note: overhead for counters is not currently tracked. (TODO)
 *
 *  (N) Call TRACKY_TEARDOWN(). This flushes in-flight data and then cleans up
 *      Tracky.
 */

/* Random TODO
 *   - TODO TODO accumulate "other" time for frame.
 *   - TODO TODO 'partner' scopes (E.g., NEXT/LEAVE must share some flags, like GPU)
 *      - TODO: check this.
 */

namespace tracky
{
	/* Tracky uses levels and groups to control subsets of tracked items. Each
	 * tracked item is associated with a level and one or more groups. Tracky
	 * can be told to ignore items with a level greater than a selected value,
	 * or to ignore items associated with certain groups.
	 *
	 * Levels and groups can be hard-masked or soft-masked. Hard-masked items
	 * are disabled at compile time, meaning they will not generate any
	 * additional code. Soft-masked items are filtered at runtime, meaning
	 * associated function calls still take place, but are aborted as early as
	 * possible.
	 *
	 * By default, the hard cap for the level is set to 8, and group 15 is
	 * masked. In this case,
	 *   TRACKY_SCOPE( "enabled" );  // default level 0 and no groups
	 *   TRACKY_SCOPE( "enabled", 1_level ); // level 1
	 * would both be enabled (unless soft-masked at runtime). However,
	 *   TRACKY_SCOPE( "disabled", 200_level ); // level 200
	 *   TRACKY_SCOPE( "disabled", 15_group ); // group 15
	 * would both be disabled at compile time.
	 */
	constexpr unsigned long long kMaxLevel = TRACKY_DEFAULT_HARD_LEVEL; // [0 ... 64k)
	constexpr unsigned long long kGroupMask = TRACKY_DEFAULT_HARD_MASK; // 16bit mask
}

// Macros
#if TRACKY_ENABLE
#	define TRACKY_ENTER( name, ... ) do {                            \
		using enum ::tracky::EFlags;                                 \
		if constexpr( TRACKY_CHECK_(a,__VA_ARGS__) ) {               \
			::tracky::scope_enter( name, a.extra );                  \
		} } while(0) /*ENDM*/
#	define TRACKY_NEXT( name, ... ) do {                             \
		using enum ::tracky::EFlags;                                 \
		if constexpr( TRACKY_CHECK_(a,__VA_ARGS__) ) {               \
			::tracky::scope_next( name, a.extra );                   \
		} } while(0) /*ENDM*/
#	define TRACKY_LEAVE( name, ... ) do {                            \
		using enum ::tracky::EFlags;                                 \
		if constexpr( TRACKY_CHECK_(a,__VA_ARGS__) ) {               \
			::tracky::scope_leave( a.extra );                        \
		} } while(0) /*ENDM*/

#	define TRACKY_SCOPE( name, ... )                                 \
		auto const TRACKY_JOIN_(trackyAutoScope,__LINE__) = [&] {    \
			using enum ::tracky::EFlags;                             \
			if constexpr( TRACKY_CHECK_(a,__VA_ARGS__) )             \
				return ::tracky::detail::AutoScope( name, a.extra ); \
			else                                                     \
				return 0;                                            \
		}() /*ENDM*/

#	define TRACKY_COUNTER( value, name, ... ) do {                   \
		using enum ::tracky::EFlags;                                 \
		if constexpr( TRACKY_CHECK_(a,__VA_ARGS__) ) {               \
			::tracky::counter( name, value, a.extra );               \
		} } while(0) /*ENDM*/

#	define TRACKY_PERSISTENT_COUNTER( value, name, ... ) do {        \
		using enum ::tracky::EFlags;                                 \
		if constexpr( TRACKY_CHECK_(a,__VA_ARGS__) ) {               \
			::tracky::persistent_counter( name, value, a.extra );    \
		} } while(0) /*ENDM*/

//#	define TRACKY_EVENT( name ) //TODO

#	define TRACKY_NEXT_FRAME() ::tracky::next_frame()

#ifdef TRACKY_OPENGL
#	define TRACKY_STARTUP() ::tracky::startup()
#elifdef TRACKY_VULKAN
#	define TRACKY_STARTUP(device, queryCount) ::tracky::startup(device, queryCount)
#   define TRACKY_BIND_CMD_BUFFER(cmdBuf) ::tracky::bind_cmd_buffer(cmdBuf)
#endif
#	define TRACKY_TEARDOWN() ::tracky::teardown()


#	define TRACKY_CHECK_(id,...)                                              \
		constexpr auto id = ::tracky::detail::Args{__VA_ARGS__};              \
		::tracky::detail::level(id.extra) <= ::tracky::kMaxLevel &&           \
		!(::tracky::detail::groups(id.extra) & ::tracky::kGroupMask)          \
	/*ENDM*/
#	define TRACKY_CHECKC_(id,...)                                             \
		constexpr auto id = ::tracky::detail::CArgs{__VA_ARGS__};             \
		::tracky::detail::level(id.extra) <= ::tracky::kMaxLevel &&           \
		!(::tracky::detail::groups(id.extra) & ::tracky::kGroupMask)          \
	/*ENDM*/

#	define TRACKY_JOIN_(a,b) TRACKY_JOIN0_(a,b)
#	define TRACKY_JOIN0_(a,b) a## b
#else // !TRACKY_ENABLE
#	define TRACKY_ENTER( ... ) do {} while(0)
#	define TRACKY_NEXT( ... ) do {} while(0)
#	define TRACKY_LEAVE( ... ) do {} while(0)

#	define TRACKY_SCOPE( ... )  do {} while(0)

#	define TRACKY_COUNTER( value, ... ) do {} while(0)
#	define TRACKY_PERSISTENT_COUNTER( value, ... ) do {} while(0)

//#	define TRACKY_EVENT( name ) //TODO

#	define TRACKY_NEXT_FRAME() do {} while(0)
#ifdef TRACKY_OPENGL
#	define TRACKY_STARTUP() do {} while(0)
#elifdef TRACKY_VULKAN
#	define TRACKY_STARTUP(device, queryCount) do {} while(0)
#   define TRACKY_BIND_CMD_BUFFER(cmdBuf) do {} while(0)
#endif
#	define TRACKY_TEARDOWN() do {} while(0)
#endif // ~ TRACKY_ENABLE

// Main interface
// TODO: should these be noxexcept?
namespace tracky
{
	//using ExtraFlags = unsigned long long;

	enum class EFlags : unsigned
	{
		CPU = 0, // CPU is always selected.
		GPU = (1u<<0),
	};

	struct ExtraFlags
	{
		unsigned long long v;

		constexpr ExtraFlags() noexcept = default;
		constexpr ExtraFlags( unsigned long long aV ) noexcept
			: v(aV)
		{}
		constexpr ExtraFlags( EFlags aF ) noexcept
			: v(unsigned(aF))
		{}

		constexpr explicit operator EFlags() noexcept
		{
			return EFlags(v);
		}
	};

	void scope_enter( char const*, ExtraFlags );
	void scope_next( char const*, ExtraFlags );
	void scope_leave( ExtraFlags );

	void counter( char const*, long long, ExtraFlags );
	void persistent_counter( char const*, long long, ExtraFlags );

	void next_frame();

#ifdef TRACKY_OPENGL
	void startup(); //TODO: options for what output etc., where to store, ...
#elifdef TRACKY_VULKAN
    void startup(vk::Device aDevice, uint32_t aQueryCount);
    void bind_cmd_buffer(vk::CommandBuffer aCmdBuffer);
#endif
	void teardown();

	//TODO: options: frame lag

	inline namespace literals
	{
		constexpr
		ExtraFlags operator ""_level( unsigned long long aVal )
		{
			return aVal << 48;
		}
		constexpr
		ExtraFlags operator ""_group( unsigned long long aVal )
		{
			return ((1llu<<aVal) & 0xffffllu) << 32;
		}
	}

	// Internal
	namespace detail
	{
		struct Args
		{
			// char const* name;
			ExtraFlags extra = 0;
		};
		struct CArgs
		{
			char const* name;
			ExtraFlags extra = 0;
		};

		class AutoScope
		{
			public:
				AutoScope( char const* aName, ExtraFlags aFlags )
					: mFlags( aFlags )
				{
					scope_enter( aName, aFlags );
				}
				~AutoScope()
				{
					scope_leave( mFlags );
				}

			private:
				ExtraFlags mFlags;
		};

		constexpr
		unsigned long long level( ExtraFlags aFlags )
		{
			return aFlags.v >> 48;
		}
		constexpr
		unsigned long long groups( ExtraFlags aFlags )
		{
			return (aFlags.v >> 32) & 0xfffflu;
		}
		constexpr
		EFlags flags( ExtraFlags aFlags )
		{
			return EFlags(aFlags);
		}
	}

	// Operators for ExtraFlags and EFlags
	constexpr
	bool operator! (ExtraFlags aX) noexcept {
		return !aX.v;
	}

	constexpr
	ExtraFlags operator~ (ExtraFlags aX) noexcept {
		return ExtraFlags( ~aX.v );
	}
	constexpr
	ExtraFlags operator| (ExtraFlags aX, ExtraFlags aY) noexcept {
		return ExtraFlags( aX.v|aY.v );
	}
	constexpr
	ExtraFlags operator& (ExtraFlags aX, ExtraFlags aY) noexcept {
		return ExtraFlags( aX.v&aY.v );
	}

	constexpr
	EFlags operator~ (EFlags aX) noexcept {
		return EFlags( ~unsigned(aX) );
	}
	constexpr
	EFlags operator| (EFlags aX, EFlags aY) noexcept {
		return EFlags( unsigned(aX)|unsigned(aY) );
	}
	constexpr
	EFlags operator& (EFlags aX, EFlags aY) noexcept {
		return EFlags( unsigned(aX)&unsigned(aY) );
	}

    // clang-format on
	// NOLINTEND
} // namespace tracky