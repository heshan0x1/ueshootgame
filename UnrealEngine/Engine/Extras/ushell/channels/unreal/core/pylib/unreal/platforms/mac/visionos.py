# Copyright Epic Games, Inc. All Rights Reserved.

import os
import unreal

#-------------------------------------------------------------------------------
class Platform(unreal.Platform):
    name = "VisionOS"

    def _read_env(self):
        yield from ()

    def _get_version_ue4(self):
        return

    def _get_version_ue5(self):
        return

    def _get_cook_form(self, target):
        if target == "game": return "VisionOS"
        if target == "client": return "VisionOSClient"
