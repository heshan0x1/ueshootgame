// Copyright Epic Games, Inc. All Rights Reserved.

import { registerHordePlugin, MountType } from "..";
import { TelemetryView } from "./telemetryView";

registerHordePlugin({
   id: "analytics",
   routes: [{ path: "analytics", element: <TelemetryView /> }],
   mount: {
      type: MountType.TopNav,
      context: "Tools",
      text: "Analytics",
      route: `/analytics`
   }
})