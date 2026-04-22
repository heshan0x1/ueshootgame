// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';

class SettingsThirdPartyNoticeView extends StatelessWidget {
  const SettingsThirdPartyNoticeView({super.key});

  static const String route = '/third_party/view';

  @override
  Widget build(BuildContext context) {
    return ThirdPartyNoticeViewSettingsPage();
  }
}
