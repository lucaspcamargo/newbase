#include <newbase/graphplan/plan.hpp>
#include <cassert>

using namespace nb;

nb::graphplan::plan::plan(const domain &dom)
    : dom_(dom)
{
}

nb::graphplan::plan::~plan()
{
}

uint64_t nb::graphplan::plan::add_node_from_type(int type_id, float x, float y)
{
    const auto* tdef = dom_.find_type(type_id);
    assert(tdef && "Unknown node type");

    uint64_t node_id = get_next_unique_id();
    nodes.insert({node_id, node_data{node_id, type_id, {}, {}, x, y}});

    for (int pin_type : tdef->input_types)
    {
        uint64_t pin_id = get_next_unique_id();
        pins.insert({pin_id, pin_data{pin_id, node_id, pin_type}});
        nodes[node_id].input_pins.push_back(pin_id);
    }

    for (int pin_type : tdef->output_types)
    {
        uint64_t pin_id = get_next_unique_id();
        pins.insert({pin_id, pin_data{pin_id, node_id, pin_type}});
        nodes[node_id].output_pins.push_back(pin_id);
    }

    for (const auto& pd : tdef->props)
        nodes[node_id].properties[pd.name] = pd.default_value;

    return node_id;
}