#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/output.hpp>
#include <wayfire/per-output-plugin.hpp>

class swayfire_next_plugin : public wf::plugin_interface_t
{
public:
    void init() override
    {
        LOGI("Swayfire-Next: Tiling engine initialized.");
    }

    void fini() override
    {
        LOGI("Swayfire-Next: Tiling engine unloaded.");
    }
};

DECLARE_WAYFIRE_PLUGIN(swayfire_next_plugin);
