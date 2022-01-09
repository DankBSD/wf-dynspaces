#define WAYFIRE_PLUGIN
#define WLR_USE_UNSTABLE

#include <vector>

#include <wayfire/core.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/output.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/workspace-manager.hpp>

#include <wayfire/plugins/common/geometry-animation.hpp>
#include "wayfire/plugins/crossfade.hpp"

static nonstd::observer_ptr<wf::grid::grid_animation_t> ensure_grid_view(wayfire_view view) {
	if (!view->has_data<wf::grid::grid_animation_t>()) {
		// XXX: do not reuse grid settings?
		wf::option_wrapper_t<std::string> animation_type{"grid/type"};
		wf::option_wrapper_t<int> duration{"grid/duration"};

		wf::grid::grid_animation_t::type_t type = wf::grid::grid_animation_t::NONE;
		if (animation_type.value() == "crossfade") {
			type = wf::grid::grid_animation_t::CROSSFADE;
		} else if (animation_type.value() == "wobbly") {
			type = wf::grid::grid_animation_t::WOBBLY;
		}

		view->store_data(std::make_unique<wf::grid::grid_animation_t>(view, type, duration));
	}

	return view->get_data<wf::grid::grid_animation_t>();
}

struct dynspaces_appspace : public wf::custom_data_t {
	uint64_t wsid_launched_from, wsid_ours;
	wf::geometry_t relative_windowed_geom;
	dynspaces_appspace(uint64_t _w1, uint64_t _w2, wf::geometry_t _g)
	    : wsid_launched_from(_w1), wsid_ours(_w2), relative_windowed_geom(_g) {}
};

struct dynspaces_workspace_implementation_t : public wf::workspace_implementation_t {
 public:
	bool view_movable(wayfire_view view) override { return !view->has_data<dynspaces_appspace>(); }

	bool view_resizable(wayfire_view view) override { return !view->has_data<dynspaces_appspace>(); }
};

struct wayfire_dynspaces : public wf::plugin_interface_t {
	wf::option_wrapper_t<bool> keep_empty_workspace{"dynspaces/keep_empty_workspace"};
	wf::option_wrapper_t<bool> fullscreen_apps_as_workspaces{
	    "dynspaces/fullscreen_apps_as_workspaces"};

	inline wf::point_t ws_point(int num) {
		// TODO: if (vertical) ..
		return {num, 0};
	}

	inline wf::dimensions_t ws_gridsize(int num) {
		// TODO: if (vertical) ..
		return {num, 1};
	}

	inline int point_ws(wf::point_t wsp) {
		// TODO: if (vertical) ..
		return wsp.x;
	}

	inline int main_coord(wf::geometry_t geo) {
		// TODO: if (vertical) ..
		return geo.x;
	}

	inline int num_workspaces() {
		auto grid_size = output->workspace->get_workspace_grid_size();
		// TODO: if (vertical) ..
		return grid_size.width;
	}

	inline int current_ws() { return point_ws(output->workspace->get_current_workspace()); }

	inline void switch_to(int ws) { output->workspace->request_workspace(ws_point(ws)); }

	inline void increase_grid() {
		output->workspace->set_workspace_grid_size(ws_gridsize(num_workspaces() + 1));
	}

	inline void decrease_grid() {
		output->workspace->set_workspace_grid_size(ws_gridsize(num_workspaces() - 1));
	}

	// Wayfire workspaces are really just position offsets,
	// we need our own identity layer:
	uint64_t next_id = 0;
	std::vector<uint64_t> wsids;

	int ws_by_id(uint64_t wsid) {
		return std::distance(wsids.begin(), std::find(wsids.begin(), wsids.end(), wsid));
	}

	// Takes relative geometry ("just window position without workspaces"),
	// places it on a workspace (absolute number),
	// but the result is relative to the current workspace (ready for set_geometry)
	wf::geometry_t place_at_ws(wf::geometry_t geometry, int ws) {
		auto scr_size = output->get_screen_size();
		auto ws_delta = ws_point(ws) - output->workspace->get_current_workspace();
		geometry.x += ws_delta.x * scr_size.width;
		geometry.y += ws_delta.y * scr_size.height;
		return geometry;
	}

	int add_ws_after(int ws) {
		auto num = num_workspaces();
		auto scr_size = output->get_screen_size();
		wsids.push_back(UINT64_MAX);
		for (int i = num - 1; i > ws; i--) wsids[i + 1] = wsids[i];
		auto views = output->workspace->get_views_in_layer(wf::LAYER_MINIMIZED | wf::WM_LAYERS);
		for (auto v : views) {
			auto rel_geo = place_at_ws(v->get_wm_geometry(), -1 * (ws + 1));
			if (main_coord(rel_geo) >= 0) {
				auto box = v->get_wm_geometry();
				v->move(box.x + scr_size.width, box.y);
			}
		}
		// NOTE: doing this *after* the view moves to avoid get_view_main_workspace returning the newly
		// created one
		increase_grid();
		wsids[ws + 1] = next_id++;
		return ws + 1;
	}

	void destroy_ws(int ws) {
		auto num = num_workspaces();
		auto scr_size = output->get_screen_size();
		for (int i = ws; i < num; i++) wsids[i] = wsids[i + 1];
		auto views = output->workspace->get_views_in_layer(wf::LAYER_MINIMIZED | wf::WM_LAYERS);
		for (auto v : views) {
			auto rel_geo = place_at_ws(v->get_wm_geometry(), -1 * ws);
			if (main_coord(rel_geo) >= 0) {
				auto box = v->get_wm_geometry();
				v->move(box.x - scr_size.width, box.y);
			}
		}
		decrease_grid();
		wsids.pop_back();
	}

	wf::signal_connection_t on_fullscreen = [=](wf::signal_data_t *ev) {
		if (!fullscreen_apps_as_workspaces) return;
		auto data = static_cast<wf::view_fullscreen_request_signal *>(ev);
		if (data->state) {
			auto wsid_launched_from = wsids[current_ws()];
			auto rel_geom = data->view->get_wm_geometry();
			auto ws = add_ws_after(current_ws());
			ensure_grid_view(data->view)->adjust_target_geometry(place_at_ws(data->desired_size, ws), -1);
			switch_to(ws);
			data->view->store_data(
			    std::make_unique<dynspaces_appspace>(wsid_launched_from, wsids[ws], rel_geom));
		} else {
			if (!data->view->has_data<dynspaces_appspace>()) {
				// Possibly when the plugin was added dynamically after this view was created
				LOGE("No app space data for a fullscreen surface!");
				return;
			}
			auto appspace = data->view->get_data<dynspaces_appspace>();
			ensure_grid_view(data->view)
			    ->adjust_target_geometry(
			        place_at_ws(appspace->relative_windowed_geom, ws_by_id(appspace->wsid_launched_from)),
			        -1);
			switch_to(ws_by_id(appspace->wsid_launched_from));
			destroy_ws(ws_by_id(appspace->wsid_ours));
			data->view->erase_data<dynspaces_appspace>();
		}
		data->carried_out = true;
		output->focus_view(data->view, true);
		output->refocus(nullptr, wf::MIDDLE_LAYERS);
	};

	wf::signal_connection_t on_minimize = [=](wf::signal_data_t *ev) {
		if (!fullscreen_apps_as_workspaces) return;
		auto data = static_cast<wf::view_minimize_request_signal *>(ev);
		if (data->view->has_data<dynspaces_appspace>() && data->state) {
			auto appspace = data->view->get_data<dynspaces_appspace>();
			data->view->fullscreen_request(output, false, ws_point(ws_by_id(appspace->wsid_ours)));
			// This will emit the view-fullscreen-request signal we handle above
		}
	};

	void ensure_empty() {
		if (!keep_empty_workspace) return;
		auto num = num_workspaces();
		int i;
		for (i = num - 1; i > 0; i--)
			if (!output->workspace
			         ->get_views_on_workspace(ws_point(i), wf::LAYER_MINIMIZED | wf::WM_LAYERS)
			         .empty())
				break;
		output->workspace->set_workspace_grid_size(ws_gridsize(i + 2));
	}

	wf::signal_connection_t on_change_workspace = [=](wf::signal_data_t *ev) {
		auto data = static_cast<wf::view_change_workspace_signal *>(ev);
		if (data->view->has_data<dynspaces_appspace>()) {
			// We're trying hard to prevent this but in case it happens let's clean up
			auto appspace = data->view->get_data<dynspaces_appspace>();
			destroy_ws(ws_by_id(appspace->wsid_ours));
			data->view->erase_data<dynspaces_appspace>();
		}
		ensure_empty();
	};

	void init() override {
		output->workspace->set_workspace_grid_size({2, 1});
		wsids.reserve(16);
		wsids.push_back(next_id++);
		wsids.push_back(next_id++);
		output->workspace->set_workspace_implementation(
		    std::make_unique<dynspaces_workspace_implementation_t>(), true);
		output->connect_signal("view-fullscreen-request", &on_fullscreen);
		output->connect_signal("view-minimize-request", &on_minimize);
		output->connect_signal("view-change-workspace", &on_change_workspace);
	};
	void fini() override { output->workspace->set_workspace_implementation(nullptr, true); }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_dynspaces);
