#pragma once

enum variant_kind
{
	vk_str,
	vk_char,
	vk_int,
	vk_uint,
	vk_float,
	vk_double,
	vk_bool,
	vk_reference,
	vk_vector,
	vk_hash_map,
	vk_buffer,
	vk_null,  /* null value */
};

namespace runtime {
	using variant_kind = ::variant_kind;
	struct variant;
}
