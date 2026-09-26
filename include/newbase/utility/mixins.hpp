#pragma once

namespace nb {

/**
 * Utility class "mixin" that disables copy and assignment
 * Move operations are still allowed
 */
struct nocopy {
protected:
    constexpr nocopy() noexcept = default;
    ~nocopy() = default;

	// custom no-op moves
	nocopy(nocopy&&) noexcept {}
	nocopy& operator=(nocopy&&) noexcept { return *this; }

public:
    nocopy(const nocopy&) = delete;
	nocopy& operator=(const nocopy&) = delete;

	// moves still permitted on this empty struct type
};


// Disable both copy and move (pinned in memory)
struct pinned {
protected:
	constexpr pinned() noexcept = default;
	~pinned() = default;
public:
	pinned(const pinned&) = delete;
	pinned& operator=(const pinned&) = delete;
	pinned(pinned&&) = delete;
	pinned& operator=(pinned&&) = delete;
};


} // ::nb
