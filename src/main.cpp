#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/signal-definitions.hpp>

class swayfire_next_plugin : public wf::plugin_interface_t
{
public:
    void init() override
    {
        LOGI("Swayfire-Next plugin initialized successfully.");
    }

    void fini() override
    {
        LOGI("Swayfire-Next plugin unloaded.");
    }
};

DECLARE_WAYFIRE_PLUGIN(swayfire_next_plugin);
