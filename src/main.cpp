#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/output.hpp>
#include <wayfire/per-output-plugin.hpp>

class swayfire_next_plugin : public wf::per_output_plugin_instance_t
{
public:
    void init() override
    {
        LOGI("Swayfire-Next: Per-output tiling engine initialized.");
    }

    void fini() override
    {
        LOGI("Swayfire-Next: Per-output tiling engine unloaded.");
    }
};

DECLARE_WAYFIRE_PER_OUTPUT_PLUGIN(swayfire_next_plugin);
