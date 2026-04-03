#include <newbase/reflection/rtti_dump.hpp>
#include <newbase/reflection/data.hpp>
#include <entt/entt.hpp>
#include <sstream>
#include <iomanip>
#include <string>

namespace nb {

std::string dump_rtti_info()
{
    std::ostringstream oss;
    oss << "Registered entt::meta types:\n";
    oss << "==========================\n\n";

    for (auto [id, type] : entt::resolve()) {
        oss << "Type ID: 0x" << std::hex << id << std::dec << "\n";
        oss << "Name: " << (type.info().name().size() ? std::string{type.info().name()}.c_str() : "<unnamed>");

        if(type.info().name().size() != 0)
            oss << std::hex << " (hashed: 0x" << entt::hashed_string(type.info().name().begin(), type.info().name().length()).operator entt::id_type() << ")\n";
        else 
            oss << "\n";

        const rtti::type_info* info = type.custom().operator const rtti::type_info*();
        if (info) {
            oss << "Identifier: " << info->identifier.c_str() << "\n";
            oss << "Class: ";
            switch (info->type_class) {
                case rtti::TYPE_CLASS_NONE: oss << "NONE"; break;
                case rtti::TYPE_CLASS_COMPONENT: oss << "COMPONENT"; break;
                case rtti::TYPE_CLASS_RESOURCE: oss << "RESOURCE"; break;
                case rtti::TYPE_CLASS_SYSTEM: oss << "SYSTEM"; break;
                case rtti::TYPE_CLASS_SINGLETON: oss << "SINGLETON"; break;
                case rtti::TYPE_CLASS_RES_STORAGE: oss << "RES_STORAGE"; break;
                case rtti::TYPE_CLASS_SERVICE: oss << "SERVICE"; break;
                case rtti::TYPE_CLASS_RESOURCE_PTR: oss << "RESOURCE_PTR"; break;
                default: oss << "UNKNOWN"; break;
            }
            oss << "\n";

            // Add specific data based on type_class
            switch (info->type_class) {
                case rtti::TYPE_CLASS_COMPONENT:
                    oss << "Editor Icon: " << (info->data.component.editor_icon ? info->data.component.editor_icon : "none") << "\n";
                    break;
                case rtti::TYPE_CLASS_RESOURCE:
                    oss << "Editor Icon: " << (info->data.resource.editor_icon ? info->data.resource.editor_icon : "none") << "\n";
                    oss << "Extensions: " << (info->data.resource.extensions ? info->data.resource.extensions : "none") << "\n";
                    break;
                case rtti::TYPE_CLASS_RESOURCE_PTR:
                    oss << "Resource Type ID: 0x" << std::hex << info->data.resource_ptr.resource_type_id << std::dec << "\n";
                    break;
                // Add other cases as needed
                default:
                    break;
            }
        } else {
            oss << "No custom RTTI info\n";
        }

        // List data members
        // TODO

        // List functions
        // TODO

        oss << "\n";
    }

    return oss.str();
}

} // namespace nb