// Copyright Epic Games, Inc. All Rights Reserved.

import React from 'react';
import { Stack } from '@fluentui/react';
import { PluginMount } from  '../legacyPlugins/pluginTypes';
import hordePlugins from  '../legacyPlugins';
import { ComponentMount } from './TestReportView';

type ComponentItem = {
    hasComponent: boolean;
    linkComponent?: React.FC<any>;
}

export const BuildHealthTestReportPanel: React.FC<{ streamId: string }> = ({ streamId }) => {
    const items: ComponentItem[] = hordePlugins.getComponents(PluginMount.BuildHealthSummary).map((item) => {
        return {hasComponent: true, linkComponent: item.component};
    });

    return (
        <Stack styles={{ root: { paddingLeft: 4, paddingRight: 0, paddingTop: 8, paddingBottom: 4 } }} horizontal wrap tokens={{ childrenGap: 30 }}>
            {items.map((item, index) =>
                    <Stack key={`BuildHealthPlugin${index}`}><ComponentMount component={item.linkComponent} streamId={streamId}/></Stack>
                )
            }
        </Stack>
    );
};

