#include "tree.hpp"

#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-operations.hpp>

#include <memory>

namespace swayfire
{
/** Check whether a view can be managed by the tiling tree. */
static bool can_tile_view(wayfire_toplevel_view view)
{
    if (!view)
    {
        return false;
    }

    // Child views (dialogs) stay with their parent, not tiled separately.
    if (view->parent)
    {
        return false;
    }

    // Fixed-size views (usually dialogs) should not be tiled.
    if ((view->toplevel()->get_min_size() == view->toplevel()->get_max_size()) &&
        (view->toplevel()->get_min_size().width > 0) &&
        (view->toplevel()->get_min_size().height > 0))
    {
        return false;
    }

    return true;
}

/**
 * Per-output tiling engine. Holds the tiling tree for each workspace set of
 * this output (one root per workspace set, keyed by wset pointer).
 *
 * Focus navigation is Sway-style (directional, tree-aware); fullscreen is
 * Hyprland-style atomic: a fullscreen view takes the whole workarea while the
 * rest of the tree keeps its layout.
 */
class swayfire_output_t : public wf::custom_data_t
{
  public:
    wf::output_t *output = nullptr;
    wf::view_matcher_t tile_by_default{"swayfire-next/tile_by_default"};

    swayfire_output_t(wf::output_t *wo)
    {
        this->output = wo;
        output->connect(&on_view_mapped);
        output->connect(&on_view_unmapped);
        output->connect(&on_fullscreen_request);
        output->connect(&on_tile_request);
        output->connect(&on_view_minimized);
        setup_callbacks();
    }

    ~swayfire_output_t()
    {
        output->rem_binding(&on_focus_left);
        output->rem_binding(&on_focus_right);
        output->rem_binding(&on_focus_up);
        output->rem_binding(&on_focus_down);
        output->rem_binding(&on_toggle_fullscreen);
        output->rem_binding(&on_toggle_tiled);
    }

  private:
    /* ---- bindings ---- */
    wf::option_wrapper_t<wf::keybinding_t> key_focus_left{"swayfire-next/key_focus_left"};
    wf::option_wrapper_t<wf::keybinding_t> key_focus_right{"swayfire-next/key_focus_right"};
    wf::option_wrapper_t<wf::keybinding_t> key_focus_up{"swayfire-next/key_focus_up"};
    wf::option_wrapper_t<wf::keybinding_t> key_focus_down{"swayfire-next/key_focus_down"};
    wf::option_wrapper_t<wf::activatorbinding_t> key_toggle_fullscreen{"swayfire-next/key_toggle_fullscreen"};
    wf::option_wrapper_t<wf::keybinding_t> key_toggle_tiled{"swayfire-next/key_toggle_tiled"};

    wf::key_callback on_focus_left = [this] (const wf::keybinding_t&)
    {
        return focus_adjacent(split_insertion_t::LEFT);
    };
    wf::key_callback on_focus_right = [this] (const wf::keybinding_t&)
    {
        return focus_adjacent(split_insertion_t::RIGHT);
    };
    wf::key_callback on_focus_up = [this] (const wf::keybinding_t&)
    {
        return focus_adjacent(split_insertion_t::ABOVE);
    };
    wf::key_callback on_focus_down = [this] (const wf::keybinding_t&)
    {
        return focus_adjacent(split_insertion_t::BELOW);
    };

    wf::activator_callback on_toggle_fullscreen = [this] (const wf::activator_data_t&)
    {
        auto view = wf::toplevel_cast(wf::get_core().seat->get_active_view());
        if (!view || !view_node_t::get_node(view))
        {
            return false;
        }

        auto wset = view->get_wset();
        if (!wset)
        {
            return false;
        }

        bool state = !view->pending_fullscreen();
        view->toplevel()->pending().fullscreen = state;
        wset_tree_data_t::get(wset).update_root_size();
        return true;
    };

    wf::key_callback on_toggle_tiled = [this] (const wf::keybinding_t&)
    {
        auto view = wf::toplevel_cast(wf::get_core().seat->get_active_view());
        if (!view || !can_tile_view(view))
        {
            return false;
        }

        if (view_node_t::get_node(view))
        {
            wset_tree_data_t::get(view->get_wset()).detach_view(view);
        } else if (view->is_mapped() && tile_by_default.matches(view))
        {
            wset_tree_data_t::get(view->get_wset()).attach_view(view);
        }

        return true;
    };

    /* ---- focus navigation ---- */
    bool focus_adjacent(split_insertion_t direction)
    {
        auto current = wf::toplevel_cast(wf::get_core().seat->get_active_view());
        if (!current)
        {
            return false;
        }

        auto node = view_node_t::get_node(current);
        if (!node)
        {
            return false;
        }

        auto adjacent = find_first_view_in_direction(node, direction);
        if (!adjacent)
        {
            return false;
        }

        bool was_fullscreen = current->pending_fullscreen();
        if (was_fullscreen)
        {
            // Hyprland-style: leaving fullscreen restores tiling geometry
            // without touching the tree layout.
            current->toplevel()->pending().fullscreen = false;
        }

        wf::get_core().seat->focus_view(adjacent->view);
        view_bring_to_front(adjacent->view);

        if (was_fullscreen)
        {
            // Keep fullscreen on the new focus (Hyprland `keep_fullscreen`).
            adjacent->view->toplevel()->pending().fullscreen = true;
        }

        return true;
    }

    /* ---- signal handlers ---- */
    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped =
        [this] (wf::view_mapped_signal *ev)
    {
        auto view = wf::toplevel_cast(ev->view);
        if (!view || !should_tile(view))
        {
            return;
        }

        if (view->pending_fullscreen())
        {
            // Fullscreen on map: do not force it into the tree.
            return;
        }

        if (auto wset = view->get_wset())
        {
            wset_tree_data_t::get(wset).attach_view(view);
        }
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped =
        [this] (wf::view_unmapped_signal *ev)
    {
        if (!view_node_t::get_node(ev->view))
        {
            return;
        }

        auto toplevel = wf::toplevel_cast(ev->view);
        if (!toplevel)
        {
            return;
        }

        if (auto wset = toplevel->get_wset())
        {
            wset_tree_data_t::get(wset).detach_view(toplevel);
        }
    };

    wf::signal::connection_t<wf::view_fullscreen_request_signal> on_fullscreen_request =
        [this] (wf::view_fullscreen_request_signal *ev)
    {
        if (ev->carried_out || !view_node_t::get_node(ev->view))
        {
            return;
        }

        // We manage the fullscreen state ourselves (atomic, tree-aware).
        ev->carried_out = true;
        auto toplevel = wf::toplevel_cast(ev->view);
        if (!toplevel)
        {
            return;
        }

        auto wset = toplevel->get_wset();
        if (!wset)
        {
            return;
        }

        toplevel->toplevel()->pending().fullscreen = ev->state;
        wset_tree_data_t::get(wset).update_root_size();
    };

    wf::signal::connection_t<wf::view_tile_request_signal> on_tile_request =
        [this] (wf::view_tile_request_signal *ev)
    {
        if (ev->carried_out || !view_node_t::get_node(ev->view))
        {
            return;
        }

        // We manage the tiled state manually; ignore external tile requests.
        ev->carried_out = true;
    };

    wf::signal::connection_t<wf::view_minimized_signal> on_view_minimized =
        [this] (wf::view_minimized_signal *ev)
    {
        if (!view_node_t::get_node(ev->view))
        {
            return;
        }

        auto toplevel = wf::toplevel_cast(ev->view);
        if (!toplevel || !toplevel->minimized)
        {
            return;
        }

        if (auto wset = toplevel->get_wset())
        {
            wset_tree_data_t::get(wset).detach_view(toplevel);
        }
    };

    /* ---- helpers ---- */
    bool should_tile(wayfire_toplevel_view view)
    {
        return can_tile_view(view) && tile_by_default.matches(view);
    }

    void setup_callbacks()
    {
        output->add_key(key_focus_left, &on_focus_left);
        output->add_key(key_focus_right, &on_focus_right);
        output->add_key(key_focus_up, &on_focus_up);
        output->add_key(key_focus_down, &on_focus_down);
        output->add_activator(key_toggle_fullscreen, &on_toggle_fullscreen);
        output->add_key(key_toggle_tiled, &on_toggle_tiled);
    }
};

/**
 * Top-level plugin: one swayfire_output_t per output, tracked by the
 * per_output_tracker mixin.
 */
class swayfire_next_plugin : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<>
{
  public:
    void init() override
    {
        LOGI("swayfire-next: plugin loaded");
        init_output_tracking();
    }

    void fini() override
    {
        LOGI("swayfire-next: plugin unloaded");
        fini_output_tracking();

        for (auto wset : wf::workspace_set_t::get_all())
        {
            wset->erase_data<wset_tree_data_t>();
        }

        for (auto wo : wf::get_core().output_layout->get_outputs())
        {
            wo->erase_data<swayfire_output_t>();
        }
    }

    void handle_new_output(wf::output_t *output) override
    {
        output->store_data(std::make_unique<swayfire_output_t>(output));
    }

    void handle_output_removed(wf::output_t *output) override
    {
        output->erase_data<swayfire_output_t>();
    }
};

} // namespace swayfire

DECLARE_WAYFIRE_PLUGIN(swayfire::swayfire_next_plugin);
