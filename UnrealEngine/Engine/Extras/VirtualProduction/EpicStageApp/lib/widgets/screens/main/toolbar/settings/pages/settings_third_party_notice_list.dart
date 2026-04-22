// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';

import 'settings_third_party_notice_view.dart';

class SettingsThirdPartyNoticeList extends StatelessWidget {
  const SettingsThirdPartyNoticeList({super.key});

  static const String route = '/third_party';

  @override
  Widget build(BuildContext context) {
    return ThirdPartyNoticeListSettingsPage(
      viewRoute: SettingsThirdPartyNoticeView.route,
    );
  }
}
