# wf-dynspaces

Dynamic workspaces plugin for [Wayfire] /* currently the git version */:

- the `add_workspace` keybinding creates a new empty workspace next to the current one
- the `remove_workspace` keybinding removes the current workspace, moving everything from the latter workspaces one workspace back
- the `keep_empty_workspace` option keeps one empty workspace at the end of the list (so workspaces get created by moving stuff to the last one, etc.)
- the `fullscreen_apps_as_workspaces` option tries to make Wayfire treat fullscreen apps as their own workspaces, just like a certain commercial operating system does :)
  - the app will even unfullscreen back to the workspace it was launched from, if that still exists, even if new workspaces were added in between
  - until [wayfire#1384](https://github.com/WayfireWM/wayfire/pull/1384) is merged, the fullscreen apps are movable in the `expo` plugin :/
  - we don't yet do anything against other windows being dropped onto our fullscreen apps' exclusive workspaces…
    - there will be some kind of attempt to push those out, but it shouldn't apply to the app's own modal dialogs :/

[Wayfire]: https://github.com/WayfireWM/wayfire

## Usage

Just install and add to the list, configure using wcm/[gsettings]/whatever.

Do not use together with other plugins that have a `workspace_implementation` (like the tile plugin) or try to do similar things, I guess??

[gsettings]: https://github.com/DankBSD/wf-gsettings

## License

This is free and unencumbered software released into the public domain.  
For more information, please refer to the `UNLICENSE` file or [unlicense.org](https://unlicense.org).
