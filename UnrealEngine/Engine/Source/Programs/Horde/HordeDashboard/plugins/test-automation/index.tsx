// Copyright Epic Games, Inc. All Rights Reserved.

import { registerHordePlugin, MountType } from "..";
import { TestAutomationView } from "./testAutomationView";

registerHordePlugin({
   id: "build",
   routes: [{ path: "test-automation", element: <TestAutomationView /> }],
   mount: {
      type: MountType.TopNav,
      context: "Tools",
      text: "Test Automation Hub(v2)",
      route: `/test-automation`
   }
})