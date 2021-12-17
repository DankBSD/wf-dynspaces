#define WAYFIRE_PLUGIN
#define WLR_USE_UNSTABLE

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
	wf::point_t ws;
	dynspaces_appspace(wf::point_t _ws) : ws(_ws) {}
};

struct dynspaces_workspace_implementation_t : public wf::workspace_implementation_t {
 public:
	bool view_movable(wayfire_view view) override { return !view->has_data<dynspaces_appspace>(); }

	bool view_resizable(wayfire_view view) override { return !view->has_data<dynspaces_appspace>(); }
};

struct wayfire_dynspaces : public wf::plugin_interface_t {
	wf::geometry_t add_workspace(wf::geometry_t geometry, wf::point_t delta) {
		auto scr_size = output->get_screen_size();
		geometry.x += delta.x * scr_size.width;
		geometry.y += delta.y * scr_size.height;
		return geometry;
	}

	void make_x_room_after(wf::point_t ws) {
		auto grid_size = output->workspace->get_workspace_grid_size();
		auto scr_size = output->get_screen_size();
		for (size_t i = grid_size.width - 1; i > ws.x; i--) {
			wf::point_t iws{static_cast<int>(i), ws.y};
			auto views =
			    output->workspace->get_views_on_workspace(iws, wf::LAYER_MINIMIZED | wf::WM_LAYERS);
			for (auto v : views) {
				if (output->workspace->get_view_main_workspace(v) == iws) {
					auto box = v->get_wm_geometry();
					v->move(box.x + scr_size.width, box.y);
				}
			}
		}
		// NOTE: doing this *after* the view moves to avoid get_view_main_workspace returning the newly
		// created one
		grid_size.width++;
		output->workspace->set_workspace_grid_size(grid_size);
	}

	void shrink_x_room_after(wf::point_t ws) {
		auto grid_size = output->workspace->get_workspace_grid_size();
		auto scr_size = output->get_screen_size();
		for (size_t i = ws.x; i < grid_size.width; i++) {
			wf::point_t iws{static_cast<int>(i), ws.y};
			auto views =
			    output->workspace->get_views_on_workspace(iws, wf::LAYER_MINIMIZED | wf::WM_LAYERS);
			for (auto v : views) {
				// XXX: WTF - all geometry is already shifted 1 workspace to the left here?!?!
				wf::point_t iws2{static_cast<int>(i - 1), ws.y};
				if (output->workspace->get_view_main_workspace(v) == iws2) {
					auto box = v->get_wm_geometry();
					v->move(box.x - scr_size.width, box.y);
				}
			}
		}
		grid_size.width--;
		output->workspace->set_workspace_grid_size(grid_size);
	}

	wf::signal_connection_t on_fullscreen = [=](wf::signal_data_t *ev) {
		auto data = static_cast<wf::view_fullscreen_request_signal *>(ev);
		if (data->state) {
			// NOTE: not data->workspace, we *do* want to go next-of-*current*
			auto ws = output->workspace->get_current_workspace();
			make_x_room_after(ws);
			ws.x++;
			ensure_grid_view(data->view)
			    ->adjust_target_geometry(add_workspace(data->desired_size, {1, 0}), -1);
			output->workspace->request_workspace(ws);
			data->view->store_data(std::make_unique<dynspaces_appspace>(ws));
		} else {
			if (!data->view->has_data<dynspaces_appspace>()) {
				// Possibly when the plugin was added dynamically after this view was created
				LOGE("No app space data for a fullscreen surface!");
				return;
			}
			auto appspace = data->view->get_data<dynspaces_appspace>();
			// TODO: restore to the closest *non-app* space to the left
			ensure_grid_view(data->view)
			    ->adjust_target_geometry(add_workspace(data->desired_size, {-1, 0}), -1);
			shrink_x_room_after(appspace->ws);
			appspace->ws.x--;
			output->workspace->request_workspace(appspace->ws);
			data->view->erase_data<dynspaces_appspace>();
		}
		data->carried_out = true;
		output->focus_view(data->view, true);
		output->refocus(nullptr, wf::MIDDLE_LAYERS);
	};

	wf::signal_connection_t on_minimize = [=](wf::signal_data_t *ev) {
		auto data = static_cast<wf::view_minimize_request_signal *>(ev);
		if (data->view->has_data<dynspaces_appspace>() && data->state) {
			auto appspace = data->view->get_data<dynspaces_appspace>();
			data->view->fullscreen_request(output, false, appspace->ws);
			// This will emit the view-fullscreen-request signal we handle above
		}
	};

	wf::signal_connection_t on_change_workspace = [=](wf::signal_data_t *ev) {
		auto data = static_cast<wf::view_change_workspace_signal *>(ev);
		if (data->view->has_data<dynspaces_appspace>()) {
			// We're trying hard to prevent this but in case it happens let's clean up
			auto appspace = data->view->get_data<dynspaces_appspace>();
			shrink_x_room_after(appspace->ws);
			data->view->erase_data<dynspaces_appspace>();
		}
	};

	void init() override {
		output->workspace->set_workspace_implementation(
		    std::make_unique<dynspaces_workspace_implementation_t>(), true);
		output->connect_signal("view-fullscreen-request", &on_fullscreen);
		output->connect_signal("view-minimize-request", &on_minimize);
		output->connect_signal("view-change-workspace", &on_change_workspace);
	};
	void fini() override { output->workspace->set_workspace_implementation(nullptr, true); }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_dynspaces);
