#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/output.hpp>
#include <memory>
#include <vector>

namespace swayfire {

enum class layout_node_type_t {
    WORKSPACE,
    SPLIT_CONTAINER,
    VIEW
};

struct node_t {
    layout_node_type_t type;
    wf::geometry_t geometry;
    std::vector<std::unique_ptr<node_t>> children;
    node_t* parent = nullptr;
    wf::view_interface_t* view = nullptr;
};

class swayfire_next_plugin : public wf::plugin_interface_t
{
    std::unique_ptr<node_t> root_tree;

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev) {
        if (!ev || !ev->view) return;
        LOGI("Swayfire-Next: View mapped");
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped = [=] (wf::view_unmapped_signal *ev) {
        if (!ev || !ev->view) return;
        LOGI("Swayfire-Next: View unmapped");
    };

public:
    void init() override
    {
        LOGI("Swayfire-Next: Initializing tiling engine...");
        
        root_tree = std::make_unique<node_t>();
        root_tree->type = layout_node_type_t::WORKSPACE;

        wf::get_core().connect(&on_view_mapped);
        wf::get_core().connect(&on_view_unmapped);
    }

    void fini() override
    {
        LOGI("Swayfire-Next: Unloading tiling engine...");
        root_tree.reset();
    }
};

} // namespace swayfire

DECLARE_WAYFIRE_PLUGIN(swayfire::swayfire_next_plugin);
