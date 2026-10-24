// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_MESSAGE_ROUTER_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_MESSAGE_ROUTER_H_

#include <string_view>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/values.h"

namespace atrium {

// Maps a surface message name to its handler by scanning the owner's own
// constexpr route table, rather than a callback built per row.
// the window used to route 68 names through one if-chain.
class SurfaceMessageRouter {
 public:
  SurfaceMessageRouter();
  SurfaceMessageRouter(const SurfaceMessageRouter&) = delete;
  SurfaceMessageRouter& operator=(const SurfaceMessageRouter&) = delete;
  ~SurfaceMessageRouter();

  // `RouteType` is the owner's own `{name, handler}` row struct. `routes`
  // takes any extent so a fixed-size table converts without a copy.
  template <typename Owner, typename RouteType, size_t Extent>
  void Bind(Owner* owner, base::span<const RouteType, Extent> routes) {
    route_fn_ = base::BindRepeating(&RunRoute<Owner, RouteType>,
                                    base::Unretained(owner),
                                    base::span<const RouteType>(routes));
  }

  // False for a name no bound row names, so the caller can fall through.
  bool Route(std::string_view name, const base::ListValue& args) const {
    return route_fn_ && route_fn_.Run(name, args);
  }

 private:
  // A later row wins over an earlier one for the same name.
  template <typename Owner, typename RouteType>
  static bool RunRoute(Owner* owner,
                       base::span<const RouteType> routes,
                       std::string_view name,
                       const base::ListValue& args) {
    for (size_t i = routes.size(); i-- > 0;) {
      if (routes[i].name == name) {
        (owner->*routes[i].handler)(args);
        return true;
      }
    }
    return false;
  }

  base::RepeatingCallback<bool(std::string_view, const base::ListValue&)>
      route_fn_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_MESSAGE_ROUTER_H_
