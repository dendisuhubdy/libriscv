#pragma once

template <int W>
template <typename T>
T Memory<W>::read(address_t address)
{
	const auto pageno = page_number(address);
	if (m_current_rd_page != pageno) {
		m_current_rd_page = pageno;
		m_current_rd_ptr = &get_pageno(pageno);
	}
	const auto& page = *m_current_rd_ptr;

	if constexpr (memory_traps_enabled) {
		if (UNLIKELY(page.has_trap())) {
			return page.trap(address & (Page::size()-1), sizeof(T) | TRAP_READ, 0);
		}
	}
	if (LIKELY(page.attr.read)) {
		return page.template aligned_read<T>(address & (Page::size()-1));
	}
	this->protection_fault();
	return T {};
}

template <int W>
template <typename T>
void Memory<W>::write(address_t address, T value)
{
	const auto pageno = page_number(address);
	if (m_current_wr_page != pageno) {
		m_current_wr_page = pageno;
		m_current_wr_ptr = &create_page(pageno);
	}
	auto& page = *m_current_wr_ptr;

	if constexpr (memory_traps_enabled) {
		if (UNLIKELY(page.has_trap())) {
			page.trap(address & (Page::size()-1), sizeof(T) | TRAP_WRITE, value);
			return;
		}
	}
	if (LIKELY(page.attr.write)) {
		page.template aligned_write<T>(address & (Page::size()-1), value);
		return;
	}
	this->protection_fault();
}

template <int W>
inline const Page& Memory<W>::get_page(const address_t address) const noexcept
{
	const auto page = page_number(address);
	return get_pageno(page);
}

template <int W>
inline const Page& Memory<W>::get_pageno(const address_t page) const noexcept
{
	auto it = m_pages.find(page);
	if (it != m_pages.end()) {
		return it->second;
	}
	// uninitialized memory is all zeroes on this system
	return Page::cow_page();
}

template <int W>
inline Page& Memory<W>::create_page(const address_t pageno)
{
	auto it = m_pages.find(pageno);
	if (it != m_pages.end()) {
		return it->second;
	}
	// create page on-demand, or throw exception when out of memory
	if (this->m_page_fault_handler == nullptr) {
		return default_page_fault(*this, pageno);
	}
	return m_page_fault_handler(*this, pageno);
}

template <int W> inline void
Memory<W>::set_page_attr(address_t dst, size_t len, PageAttributes options)
{
	const bool is_default = options.is_default();
	while (len > 0)
	{
		const size_t size = std::min(Page::size(), len);
		const size_t pageno = dst >> Page::SHIFT;
		// unfortunately, have to create pages for non-default attrs
		if (!is_default) {
			this->create_page(pageno).attr = options;
		} else {
			// set attr on non-COW pages only!
			const auto& page = this->get_pageno(pageno);
			if (page.attr.is_cow == false) {
				// this page has been written to, or had attrs set,
				// otherwise it would still be CoW.
				this->create_page(pageno).attr = options;
			}
		}

		dst += size;
		len -= size;
	}
}
template <int W> inline
const PageAttributes& Memory<W>::get_page_attr(address_t src) const noexcept
{
	const size_t pageno = src >> Page::SHIFT;
	const auto& page = this->get_pageno(pageno);
	return page.attr;
}


template <int W> inline void
Memory<W>::invalidate_page(address_t pageno, Page& page)
{
	// it's only possible to a have CoW read-only page
	if (m_current_rd_page == pageno) {
		m_current_rd_ptr = &page;
	}
}

template <int W> inline void
Memory<W>::free_pages(address_t dst, size_t len)
{
	while (len > 0)
	{
		const size_t size = std::min(Page::size(), len);
		const address_t pageno = dst >> Page::SHIFT;
		auto& page = this->get_pageno(pageno);
		if (page.attr.is_cow == false) {
			m_pages.erase(pageno);
		}
		dst += size;
		len -= size;
	}
}

template <int W>
void Memory<W>::memset(address_t dst, uint8_t value, size_t len)
{
	while (len > 0)
	{
		const size_t offset = dst & (Page::size()-1); // offset within page
		const size_t size = std::min(Page::size() - offset, len);
		auto& page = this->create_page(dst >> Page::SHIFT);
		__builtin_memset(page.data() + offset, value, size);

		dst += size;
		len -= size;
	}
}

template <int W>
void Memory<W>::memcpy(address_t dst, const void* vsrc, size_t len)
{
	auto* src = (uint8_t*) vsrc;
	while (len != 0)
	{
		const size_t offset = dst & (Page::size()-1); // offset within page
		const size_t size = std::min(Page::size() - offset, len);
		auto& page = this->create_page(dst >> Page::SHIFT);
		std::copy(src, src + size, page.data() + offset);

		dst += size;
		src += size;
		len -= size;
	}
}

template <int W>
void Memory<W>::memcpy_out(void* vdst, address_t src, size_t len)
{
	auto* dst = (uint8_t*) vdst;
	while (len != 0)
	{
		const size_t offset = src & (Page::size()-1);
		const size_t size = std::min(Page::size() - offset, len);
		const auto& page = this->get_page(src);
		std::copy(page.data() + offset, page.data() + offset + size, dst);

		dst += size;
		src += size;
		len -= size;
	}
}

template <int W>
void Memory<W>::memview(address_t addr, size_t len,
			delegate<void(const uint8_t*, size_t)> callback)
{
	const size_t offset = addr & (Page::size()-1);
	// fast-path
	if (LIKELY(offset + len <= Page::size()))
	{
		const auto& page = this->get_page(addr);
		callback(page.data() + offset, len);
		return;
	}
	// slow path
	uint8_t buffer[len];
	memcpy_out(buffer, addr, len);
	callback(buffer, len);
}
template <int W>
template <typename T>
void Memory<W>::memview(address_t addr, delegate<void(const T&)> callback)
{
	static_assert(std::is_trivial_v<T>, "Type T must be Plain-Old-Data");
	const size_t offset = addr & (Page::size()-1);
	// fast-path
	if (LIKELY(offset + sizeof(T) <= Page::size()))
	{
		const auto& page = this->get_page(addr);
		callback(*(const T*) &page.data()[offset]);
		return;
	}
	// slow path
	T object;
	memcpy_out(&object, addr, sizeof(object));
	callback(object);
}

template <int W>
std::string Memory<W>::memstring(address_t addr, const size_t max_len)
{
	std::string result;
	size_t pageno = page_number(addr);
	// fast-path
	{
		address_t offset = addr & (Page::size()-1);
		const Page& page = this->get_pageno(pageno);
		const char* start = (const char*) &page.data()[offset];
		const char* pgend = (const char*) &page.data()[std::min(Page::size(), offset + max_len)];
		//
		const char* reader = start + strnlen(start, pgend - start);
		// early exit
		if (reader < pgend) {
			return std::string(start, reader);
		}
		// we are crossing a page
		result.append(start, reader);
	}
	// slow-path: cross page-boundary
	while (result.size() < max_len)
	{
		const size_t max_bytes = std::min(Page::size(), max_len - result.size());
		pageno ++;
		const Page& page = this->get_pageno(pageno);
		const char* start = (const char*) page.data();
		const char* endptr = (const char*) &page.data()[max_bytes];
		//
		const char* reader = start + strnlen(start, max_bytes);
		result.append(start, reader);

		if (reader < endptr) {
			if (*reader == 0) return result;
		}
	}
	return result;
}

template <int W>
inline void Memory<W>::protection_fault()
{
	machine().cpu.trigger_exception(PROTECTION_FAULT);
}

template <int W>
void Memory<W>::trap(address_t page_addr, mmio_cb_t callback)
{
	auto& page = create_page(page_number(page_addr));
	page.set_trap(callback);
}

template <int W>
address_type<W> Memory<W>::resolve_address(const char* name)
{
	const auto& it = sym_lookup.find(name);
	if (it != sym_lookup.end()) return it->second;

	auto* sym = resolve_symbol(name);
	address_t addr = (sym) ? sym->st_value : 0x0;
	sym_lookup.emplace(strdup(name), addr);
	return addr;
}

template <int W>
address_type<W> Memory<W>::exit_address() const noexcept
{
	return this->m_exit_address;
}

template <int W>
void Memory<W>::set_exit_address(address_t addr)
{
	this->m_exit_address = addr;
}
