#include "tree.hpp"

#include <wayfire/core.hpp>
#include <wayfire/geometry.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-operations.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace swayfire
{
/* ------------------------------------------------------------------ *
 *  autocommit transaction helper (same pattern as wf::tile)          *
 * ------------------------------------------------------------------ */
autocommit_transaction_t::autocommit_transaction_t()
{
    tx = wf::txn::transaction_t::create();
}

autocommit_transaction_t::~autocommit_transaction_t()
{
    if (!tx->get_objects().empty())
    {
        wf::get_core().tx_manager->schedule_transaction(std::move(tx));
    }
}

/* ------------------------------------------------------------------ *
 *  wset tree data                                                    *
 * ------------------------------------------------------------------ */
wset_tree_data_t::wset_tree_data_t(std::shared_ptr<wf::workspace_set_t> wset)
{
    this->wset = wset;
    root = std::make_unique<split_node_t>(split_direction_t::HORIZONTAL);

    wset->connect(&on_wset_attached);
    wset->connect(&on_workspace_grid_changed);
    wset->connect(&on_workarea_changed);
    if (wset->get_attached_output())
    {
        wset->get_attached_output()->connect(&on_workarea_changed);
    }
}

wset_tree_data_t& wset_tree_data_t::get(std::shared_ptr<wf::workspace_set_t> wset)
{
    if (!wset->has_data<wset_tree_data_t>())
    {
        wset->store_data(std::make_unique<wset_tree_data_t>(wset));
    }

    return *wset->get_data<wset_tree_data_t>();
}

std::unique_ptr<split_node_t>& wset_tree_data_t::get_root()
{
    return root;
}

void wset_tree_data_t::update_root_size()
{
    auto wo = wset.lock()->get_attached_output();
    wf::geometry_t workarea = wo ? wo->workarea->get_workarea() : default_output_resolution;
    autocommit_transaction_t tx;
    root->set_geometry(workarea, tx.tx);
}

void wset_tree_data_t::flatten()
{
    flatten_tree(root);
}

void wset_tree_data_t::attach_view(wayfire_toplevel_view view)
{
    auto view_node = std::make_unique<view_node_t>(view);
    view->set_allowed_actions(wf::VIEW_ALLOW_WS_CHANGE);
    {
        autocommit_transaction_t tx;
        root->add_child(std::move(view_node), tx.tx);
    }
}

void wset_tree_data_t::detach_view(wayfire_toplevel_view view)
{
    auto node = view_node_t::get_node(view);
    if (!node)
    {
        return;
    }

    {
        autocommit_transaction_t tx;
        view->set_allowed_actions(wf::VIEW_ALLOW_ALL);
        // After this, `node` is freed.
        node->parent->remove_child(node, tx.tx);

        if (view->pending_fullscreen() && view->is_mapped())
        {
            wf::get_core().default_wm->fullscreen_request(view, nullptr, false);
        }
    }

    flatten();
    update_root_size();
}

/* ------------------------------------------------------------------ *
 *  tree_node_t                                                       *
 * ------------------------------------------------------------------ */
void tree_node_t::set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr&)
{
    this->geometry = geometry;
}

nonstd::observer_ptr<split_node_t> tree_node_t::as_split_node()
{
    return nonstd::make_observer(dynamic_cast<split_node_t*>(this));
}

nonstd::observer_ptr<view_node_t> tree_node_t::as_view_node()
{
    return nonstd::make_observer(dynamic_cast<view_node_t*>(this));
}

/* ------------------------------------------------------------------ *
 *  split_node_t                                                      *
 * ------------------------------------------------------------------ */
wf::geometry_t split_node_t::get_child_geometry(int32_t child_pos, int32_t child_size)
{
    wf::geometry_t child_geometry = this->geometry;
    switch (get_split_direction())
      {
        case split_direction_t::HORIZONTAL:
          child_geometry.y += child_pos;
          child_geometry.height = child_size;
          break;

        case split_direction_t::VERTICAL:
          child_geometry.x += child_pos;
          child_geometry.width = child_size;
          break;
      }

    return child_geometry;
}

int32_t split_node_t::calculate_splittable(wf::geometry_t available) const
{
    switch (get_split_direction())
      {
        case split_direction_t::HORIZONTAL:
          return available.height;

        case split_direction_t::VERTICAL:
          return available.width;
      }

    return -1;
}

int32_t split_node_t::calculate_splittable() const
{
    return calculate_splittable(this->geometry);
}

void split_node_t::recalculate_children(wf::geometry_t available, wf::txn::transaction_uptr& tx)
{
    if (children.empty())
    {
        return;
    }

    double old_child_sum = 0.0;
    for (auto& child : children)
    {
        old_child_sum += calculate_splittable(child->geometry);
    }

    int32_t total_splittable = calculate_splittable(available);
    double up_to_now         = 0.0;

    auto progress = [=] (double current)
    {
        return (current / old_child_sum) * total_splittable;
    };

    for (auto& child : children)
    {
        int32_t child_start = static_cast<int32_t>(progress(up_to_now));
        up_to_now += calculate_splittable(child->geometry);
        int32_t child_end = static_cast<int32_t>(progress(up_to_now));
        child->set_geometry(get_child_geometry(child_start, child_end - child_start), tx);
    }
}

void split_node_t::add_child(std::unique_ptr<tree_node_t> child, wf::txn::transaction_uptr& tx, int index)
{
    int num_children = static_cast<int>(children.size());
    int size_new_child;
    if (num_children > 0)
    {
        size_new_child = (calculate_splittable() + num_children - 1) / num_children;
    } else
    {
        size_new_child = calculate_splittable();
    }

    if ((index == -1) || (index > num_children))
    {
        index = num_children;
    }

    child->parent = {this};
    child->geometry = get_child_geometry(0, size_new_child);
    children.emplace(children.begin() + index, std::move(child));

    recalculate_children(geometry, tx);
}

std::unique_ptr<tree_node_t> split_node_t::remove_child(
    nonstd::observer_ptr<tree_node_t> child, wf::txn::transaction_uptr& tx)
{
    std::unique_ptr<tree_node_t> result;
    for (auto it = children.begin(); it != children.end(); ++it)
    {
        if (it->get() == child.get())
        {
            result = std::move(*it);
            children.erase(it);
            break;
        }
    }

    recalculate_children(this->geometry, tx);
    if (result)
    {
        result->parent = nullptr;
    }

    return result;
}

void split_node_t::flip_direction()
{
    split_direction = (split_direction == split_direction_t::HORIZONTAL) ? split_direction_t::VERTICAL :
                                                              split_direction_t::HORIZONTAL;
}

void split_node_t::set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx)
{
    tree_node_t::set_geometry(geometry, tx);
    recalculate_children(geometry, tx);
}

/* ------------------------------------------------------------------ *
 *  view_node_t                                                       *
 * ------------------------------------------------------------------ */
struct view_node_custom_data_t : public wf::custom_data_t
{
    nonstd::observer_ptr<view_node_t> ptr;
    view_node_custom_data_t(view_node_t *node) :
        ptr(nonstd::make_observer(node))
    {}
};

view_node_t::view_node_t(wayfire_toplevel_view view) :
    view(std::move(view))
{
    wf::dassert(!view->has_data<view_node_custom_data_t>(), "View already has a view node!");
    view->store_data(std::make_unique<view_node_custom_data_t>(this));
}

view_node_t::~view_node_t()
{
    view->erase_data<view_node_custom_data_t>();
}

wf::geometry_t view_node_t::calculate_target_geometry()
{
    auto wset = view->get_wset();
    auto local_geometry = get_wset_local_coordinates(wset, geometry);

    auto size = wset->get_last_output_geometry().value_or(default_output_resolution);

    /* Fullscreen takes the full output, regardless of the tile slot */
    if (view->pending_fullscreen())
    {
        local_geometry = adjust_geometry_for_workspace(wset, size, local_geometry);
    }

    if (view->sticky)
    {
        local_geometry.x = std::fmod(std::fmod(local_geometry.x, size.width) + size.width, size.width);
        local_geometry.y = std::fmod(std::fmod(local_geometry.y, size.height) + size.height, size.height);
    }

    return local_geometry;
}

void view_node_t::set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx)
{
    tree_node_t::set_geometry(geometry, tx);

    if (!view->is_mapped())
    {
        return;
    }

    wf::get_core().default_wm->update_last_windowed_geometry(view);
    view->toplevel()->pending().tiled_edges = wf::TILED_EDGES_ALL;

    view->toplevel()->pending().geometry = calculate_target_geometry();
    tx->add_object(view->toplevel());
}

nonstd::observer_ptr<view_node_t> view_node_t::get_node(wayfire_view view)
{
    if (!view || !view->has_data<view_node_custom_data_t>())
    {
        return nullptr;
    }

    return view->get_data<view_node_custom_data_t>()->ptr;
}

/* ------------------------------------------------------------------ *
 *  geometry helpers                                                  *
 * ------------------------------------------------------------------ */
wf::geometry_t get_wset_local_coordinates(std::shared_ptr<wf::workspace_set_t> wset, wf::geometry_t g)
{
    auto vp   = wset->get_current_workspace();
    auto size = wset->get_last_output_geometry().value_or(default_output_resolution);
    return wf::construct_box(
        wf::pointf_t{g.x - vp.x * size.width, g.y - vp.y * size.height},
        wf::fdimensions(g));
}

wf::geometry_t adjust_geometry_for_workspace(
    std::shared_ptr<wf::workspace_set_t> wset,
    const wf::geometry_t& base_geometry,
    const wf::geometry_t& local_geometry)
{
    auto vp   = wset->get_current_workspace();
    auto size = wset->get_last_output_geometry().value_or(default_output_resolution);

    int source_x = std::floor(1.0 * (local_geometry.x + vp.x * size.width) / size.width);
    int source_y = std::floor(1.0 * (local_geometry.y + vp.y * size.height) / size.height);

    int dx = (source_x - vp.x) * size.width;
    int dy = (source_y - vp.y) * size.height;

    wf::geometry_t result = base_geometry;
    result.x += dx;
    result.y += dy;

    return result;
}

/* ------------------------------------------------------------------ *
 *  tree helpers                                                      *
 * ------------------------------------------------------------------ */
bool flatten_tree(std::unique_ptr<tree_node_t>& root)
{
    if (root->as_view_node())
    {
        return true;
    }

    for (auto it = root->children.begin(); it != root->children.end();)
    {
        if (!flatten_tree(*it))
        {
            it = root->children.erase(it);
        } else
        {
            ++it;
        }
    }

    if (root->children.empty())
    {
        return false;
    }

    if (root->children.size() >= 2)
    {
        return true;
    }

    nonstd::observer_ptr<tree_node_t> child_ptr = {root->children.front()};

    if (child_ptr->as_view_node())
    {
        if (!root->parent)
        {
            return true;
        }
    }

    child_ptr->parent = root->parent;
    root = std::move(root->children.front());
    return true;
}

nonstd::observer_ptr<split_node_t> get_root(nonstd::observer_ptr<tree_node_t> node)
{
    if (!node->parent)
    {
        return {dynamic_cast<split_node_t*>(node.get())};
    }

    return get_root(node->parent);
}

bool flatten_tree(std::unique_ptr<split_node_t>& root)
{
    std::unique_ptr<tree_node_t> base = std::move(root);
    bool result = flatten_tree(base);
    root.reset(static_cast<split_node_t*>(base.release()));
    return result;
}

/* ------------------------------------------------------------------ *
 *  view search                                                       *
 * ------------------------------------------------------------------ */
void for_each_view(nonstd::observer_ptr<tree_node_t> root, std::function<void(wayfire_toplevel_view)> func)
{
    if (!root)
    {
        return;
    }

    if (auto view_node = root->as_view_node())
    {
        if (view_node->view)
        {
            func(view_node->view);
        }

        return;
    }

    for (auto& child : root->children)
    {
        for_each_view(child, func);
    }
}

nonstd::observer_ptr<view_node_t> find_view_at(nonstd::observer_ptr<tree_node_t> root, wf::pointf_t point)
{
    if (!root)
    {
        return nullptr;
    }

    if (auto view_node = root->as_view_node())
    {
        auto g = view_node->geometry;
        if ((point.x >= g.x) && (point.x < g.x + g.width) &&
            (point.y >= g.y) && (point.y < g.y + g.height))
        {
            return view_node;
        }

        return nullptr;
    }

    for (auto& child : root->children)
    {
        if (auto result = find_view_at(child, point))
        {
            return result;
        }
    }

    return nullptr;
}

nonstd::observer_ptr<view_node_t> find_first_view_in_direction(
    nonstd::observer_ptr<tree_node_t> from, split_insertion_t direction)
{
    auto window = from->geometry;

    wf::pointf_t point;
    switch (direction)
      {
        case split_insertion_t::ABOVE:
          point = {window.x + window.width / 2.0, window.y - 1.0};
          break;

        case split_insertion_t::BELOW:
          point = {window.x + window.width / 2.0, window.y + window.height};
          break;

        case split_insertion_t::LEFT:
          point = {window.x - 1.0, window.y + window.height / 2.0};
          break;

        case split_insertion_t::RIGHT:
          point = {window.x + window.width, window.y + window.height / 2.0};
          break;

        default:
          return nullptr;
      }

    auto root = from;
    while (root->parent)
    {
        root = root->parent;
    }

    return find_view_at(root, point);
}

} // namespace swayfire
