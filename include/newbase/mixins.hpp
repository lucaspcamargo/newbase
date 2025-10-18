#pragma once

namespace nb {

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