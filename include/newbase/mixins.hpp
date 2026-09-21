#pragma once

namespace nb {

/**
 * Utility class "mixin" that disables copy and assignment
 * Move operations are still allowed
 */
struct nocopy {
protected:
    ~nocopy() = default;
public:
    explicit nocopy() = default;
    nocopy(const nocopy&) = delete;
	nocopy& operator=(const nocopy&) = delete;
	
	// (default) implemented move operations
	nocopy(nocopy&&) = default;
	nocopy& operator=(nocopy&&) = default;
};

};
