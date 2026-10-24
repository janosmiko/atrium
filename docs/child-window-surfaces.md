# Child-window surfaces

Atrium hosts eight native surfaces over the browser window: the command bar,
the courtyard, the extension pill, the overview, peek, the permission sheet, the
sidebar reveal panel, and the window's own frame. Several are child NSWindows.
Every one of them meets the same seams. This page names each seam, what it
breaks, and the shape that works, so the next surface starts from here instead
of from a commit message.

## The seams

| Seam | What breaks | The shape that works |
|---|---|---|
| Pointer events | A child window takes every event over its own frame. A hit target the parent draws under it is dead. | Put the target in the parent's own area, just outside the child. The width grip lives in the gap beside the panel, not across its edge. |
| Z-order | A child window draws above the parent's frame view. The traffic lights sit behind it. | Do not move the real buttons onto the child. Draw the surface's own controls in the page and route them to the window commands. |
| Window commands | `performClose`, `performMiniaturize` and `performZoom` ask the window's own buttons first. A window that hides them does nothing. | Route it to the parent window as a surface message - `sidebarWindowClose`, `sidebarWindowMinimize`, `sidebarWindowZoom` - which act on the widget rather than the button. |
| One WebContents, two views | A WebContents shows in one view at a time. | Hand it over on reveal and hand it back on dock. `AtriumSidebarOverlayWidget::Show` takes it, `HideForDock` returns it. |
| Two owners of one gesture | A child with its own tracking areas and a parent with its own both arm the same gesture, and whichever wins decides. | One owner per gesture. The edge zone belongs to `AtriumSidebarEdgeReveal`. The panel never arms it. |
| Behind-window blending | A child window does not sample its parent's content. A material on it paints a flat fill. | Draw the surface's wash in the page. The floating panel's rule in `sidebar.css` is the reference, floor included. |

## The parts that are the same every time

| Part | Home |
|---|---|
| The seam between a WebUI controller and the widget that owns its WebContents | `atrium::SurfaceHost` in `atrium_surface_host.h` - the command bar, courtyard, pill, overview, permission sheet, settings window and the browser window all implement it |
| The message names a surface may send | `kSurfaceMessages` in `atrium_surface_host.h`, checked by `verify-webui-messages.py` |
| The WebContents handover on reveal and dock | `AtriumSidebarOverlayWidget::Show` and `HideForDock` |
| The window commands a child cannot answer itself | `sidebarWindowClose`, `sidebarWindowMinimize`, `sidebarWindowZoom` in `AtriumBrowserWindow::OnSurfaceMessage` |

## Before you add a surface

1. Decide which window owns each gesture the surface reacts to. Write it down in the surface's header.
2. Register every `chrome.send` name in `kSurfaceMessages`. An unregistered name crashes the app at startup.
3. Draw the wash in the page. Do not reach for a material.
4. Put any hit target the parent must own outside the child's frame.
5. Gate the VM scenario on a line the surface logs, not on a settle.
