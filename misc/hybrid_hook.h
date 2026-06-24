#pragma once

#include "MinHook.h"

class c_hook
{
public:
	virtual uintptr_t apply( uintptr_t dest ) { return 0; }
	virtual uintptr_t apply( const uint32_t index, uintptr_t func ) { return 0; }
	virtual bool is_detour() { return false; }
	virtual ~c_hook() {}
};

class c_detour : public c_hook
{
	void* original = nullptr;
	void* src = nullptr;

public:
	c_detour() = default;
	__forceinline c_detour( uintptr_t ent ) : src( reinterpret_cast< void* >( ent ) )
	{}

	~c_detour() override
	{
		if ( src )
		{
			MH_DisableHook( src );
			MH_RemoveHook( src );
		}
	}

	__forceinline bool is_detour() override { return true; }

	__forceinline uintptr_t apply( uintptr_t dest ) override
	{
		MH_Initialize();
		MH_CreateHook( src, reinterpret_cast< void* >( dest ), &original );
		MH_EnableHook( src );
		return reinterpret_cast< uintptr_t >( original );
	}
};


class c_vtable_hook : public c_hook
{
public:
	explicit c_vtable_hook( uintptr_t ent )
	{
		base = reinterpret_cast< uintptr_t* >( ent );
		original = *base;

		const auto l = length() + 1;
		current = std::make_unique<uint32_t[]>( l );
		std::memcpy( current.get(), reinterpret_cast< void* >( original - sizeof( uint32_t ) ), l * sizeof( uint32_t ) );

		patch_pointer( base );
	}

	~c_vtable_hook() override
	{
#ifndef RELEASE
		DWORD old;
		memory::protect_mem( base, sizeof( uintptr_t ), PAGE_READWRITE, old );
		*base = original;
		memory::protect_mem( base, sizeof( uintptr_t ), old, old );
#endif
	}

	__forceinline uintptr_t apply( const uint32_t index, uintptr_t func ) override
	{
		auto old = reinterpret_cast< uintptr_t* >( original )[ index ];
		current.get()[ index + 1 ] = func;
		return old;
	}

	void patch_pointer( uintptr_t* location ) const
	{
		if ( !location )
			return;

		DWORD old;
		memory::protect_mem( location, sizeof( uintptr_t ), PAGE_READWRITE, old );
		*location = reinterpret_cast< uint32_t >( current.get() ) + sizeof( uint32_t );
		memory::protect_mem( location, sizeof( uintptr_t ), old, old );
	}

private:
	uint32_t length() const
	{
		uint32_t index;
		const auto vmt = reinterpret_cast< uint32_t* >( original );

		for ( index = 0; vmt[ index ]; index++ )
			if ( IS_INTRESOURCE( vmt[ index ] ) )
				break;

		return index;
	}

	std::uintptr_t* base;
	std::uintptr_t original;
	std::unique_ptr<uint32_t[]> current;
};