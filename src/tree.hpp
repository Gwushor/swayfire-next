#ifndef SWAYFIRE_NEXT_TREE
#define SWAYFIRE_NEXT_TREE

#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/output.hpp>
#include <wayfire/txn/transaction.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/geometry.hpp>
#include <wayfire/object.hpp>
#include <wayfire/signal-provider.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/nonstd/observer_ptr.h>
#include <memory>
#include <vector>
#include <functional>
#include <optional>

namespace swayfire
{
/* Fallback resolution for a wset not yet attached to an output. */
constexpr wf::geometry_t default_output_resolution = {0, 0, 1920, 1080};

struct split_node_t;
struct view_node_t;

/** Direction for focus/move navigation (Sway-style). */
enum class split_insertion_t
{
    NONE,
    ABOVE,
    BELOW,
    LEFT,
    RIGHT,
};

/**
 * A transaction which schedules itself on destruction (same pattern as the
 * wf::tile plugin's autocommit_transaction_t).
 */
struct autocommit_transaction_t
{
    wf::txn::transaction_uptr tx;

    autocommit_transaction_t();
    ~autocommit_transaction_t();

    autocommit_transaction_t(const autocommit_transaction_t&) = delete;
    autocommit_transaction_t& operator =(const autocommit_transaction_t&) = delete;
};

/**
 * A tree node represents a logical container of views in the tiled part of a
 * workspace. Two kinds of nodes:
 *  1. view_node_t  - leaf, holds a single view;
 *  2. split_node_t - internal, holds one or more children arranged along a
 *     split direction (Sway "splith"/"splitv" semantics).
 */
struct tree_node_t
{
    nonstd::observer_ptr<split_node_t> parent;
    std::vector<std::unique_ptr<tree_node_t>> children;
    wf::geometry_t geometry;

    virtual ~tree_node_t() = default;

    /** Set the geometry available for the node and its subtree. */
    virtual void set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx);

    nonstd::observer_ptr<split_node_t> as_split_node();
    nonstd::observer_ptr<view_node_t> as_view_node();
};

/** A node which splits its children horizontally or vertically (Sway). */
enum class split_direction_t
{
    HORIZONTAL,
    VERTICAL
};

struct split_node_t : public tree_node_t
{
    split_node_t(split_direction_t direction = split_direction_t::HORIZONTAL) :
        split_direction(direction)
    {}

    /**
     * Insert a child. index == -1 appends to the end; existing children are
     * resized proportionally (Sway `layout split` + new container).
     */
    void add_child(std::unique_ptr<tree_node_t> child, wf::txn::transaction_uptr& tx, int index = -1);

    /** Remove the given child and return its unique_ptr. */
    std::unique_ptr<tree_node_t> remove_child(
        nonstd::observer_ptr<tree_node_t> child, wf::txn::transaction_uptr& tx);

    /** Flip the split direction (Sway `layout flip`). */
    void flip_direction();
    split_direction_t get_split_direction() const
    {
        return split_direction;
    }

    void set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx) override;

  private:
    split_direction_t split_direction;

    void recalculate_children(wf::geometry_t available_geometry, wf::txn::transaction_uptr& tx);
    wf::geometry_t get_child_geometry(int32_t child_pos, int32_t child_size);
    int32_t calculate_splittable() const;
    int32_t calculate_splittable(wf::geometry_t geometry) const;
};

/** A leaf holding a single view. */
struct view_node_t : public tree_node_t
{
    view_node_t(wayfire_toplevel_view view);
    ~view_node_t();

    wayfire_toplevel_view view;

    /** Set the geometry of the node and the contained view. */
    void set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx) override;

    /** Return the tree node corresponding to the view, or nullptr if none. */
    static nonstd::observer_ptr<view_node_t> get_node(wayfire_view view);

  private:
    wf::geometry_t calculate_target_geometry();
};

/**
 * The tiling tree root for a single workspace set, stored as custom data on
 * the wset itself so that all plugins share it.
 */
class wset_tree_data_t : public wf::custom_data_t
{
  public:
    std::unique_ptr<split_node_t> root;
    std::weak_ptr<wf::workspace_set_t> wset;

    wset_tree_data_t(std::shared_ptr<wf::workspace_set_t> wset);

    static wset_tree_data_t& get(std::shared_ptr<wf::workspace_set_t> wset);
    std::unique_ptr<split_node_t>& get_root();

    /** Re-layout the root against the current workarea. */
    void update_root_size();

    /** Flatten the tree (collapse single-child split nodes). */
    void flatten();

    /** Insert a view into the tree (Sway-style insertion). */
    void attach_view(wayfire_toplevel_view view);

    /** Remove a view from the tree (Hyprland-style unfloat). */
    void detach_view(wayfire_toplevel_view view);

  private:
    wf::signal::connection_t<wf::workspace_set_attached_signal> on_wset_attached = [=] (auto)
    {
        update_root_size();
    };
    wf::signal::connection_t<wf::workspace_grid_changed_signal> on_workspace_grid_changed = [=] (auto)
    {
        update_root_size();
    };
    wf::signal::connection_t<wf::workarea_changed_signal> on_workarea_changed = [=] (auto)
    {
        update_root_size();
    };
};

/* ---------------- tree helpers ---------------- */

/** Get the root of the tree which the node is part of. */
nonstd::observer_ptr<split_node_t> get_root(nonstd::observer_ptr<tree_node_t> node);

/** Convert tree coordinates (workspace-agnostic) to wset-local coordinates. */
wf::geometry_t get_wset_local_coordinates(std::shared_ptr<wf::workspace_set_t> wset, wf::geometry_t g);

/** Adjust a base geometry by the offset of the workspace containing `local`. */
wf::geometry_t adjust_geometry_for_workspace(
    std::shared_ptr<wf::workspace_set_t> wset,
    const wf::geometry_t& base_geometry,
    const wf::geometry_t& local_geometry);

/** Remove single-child split nodes from the tree; true if it has views. */
bool flatten_tree(std::unique_ptr<tree_node_t>& root);
bool flatten_tree(std::unique_ptr<split_node_t>& root);

/** Run func for each view leaf in the tree. */
void for_each_view(nonstd::observer_ptr<tree_node_t> root,
    std::function<void(wayfire_toplevel_view)> func);

/** Find the view node at the given point (tree coordinates). */
nonstd::observer_ptr<view_node_t> find_view_at(
    nonstd::observer_ptr<tree_node_t> root, wf::pointf_t point);

/** Find the first view in the given direction (Sway neighbor lookup). */
nonstd::observer_ptr<view_node_t> find_first_view_in_direction(
    nonstd::observer_ptr<tree_node_t> from, split_insertion_t direction);

} // namespace swayfire

#endif /* SWAYFIRE_NEXT_TREE */
