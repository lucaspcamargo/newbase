#pragma once

#include <entt/core/fwd.hpp>
#include <memory>
#include <string>

namespace nb { struct rgraphplan; }
namespace nb::graphplan { class plan; class editor; }

namespace nb {

class graphplan_editor_widget
{
public:
    graphplan_editor_widget();
    ~graphplan_editor_widget();

    // Returns false if the domain referenced by the resource is not registered.
    bool open(const rgraphplan* res, entt::id_type asset_id);
    void draw();

private:
    std::unique_ptr<graphplan::plan>   _plan;
    std::unique_ptr<graphplan::editor> _editor;
    std::string _path;
};

} // namespace nb
