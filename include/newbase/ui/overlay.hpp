#pragma once

namespace nb {

class ui_overlay
{
public:
	virtual ~ui_overlay() = default;
	virtual void draw() const = 0;
};

}