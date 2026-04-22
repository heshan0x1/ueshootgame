// Copyright Epic Games, Inc. All Rights Reserved.

import { Stack, FontIcon, IComboBoxOption, IContextualMenuItem, IContextualMenuProps, Text, Spinner, SpinnerSize, Label, DefaultButton, ITag, TagPicker, IPickerItemProps, PrimaryButton, Modal, IconButton } from "@fluentui/react";
import { Breadcrumbs } from "horde/components/Breadcrumbs";
import { TopNav } from "horde/components/TopNav";
import { getHordeStyling } from "horde/styles/Styles";
import { observer } from "mobx-react-lite";
import { useNavigate, useSearchParams } from "react-router-dom";
import React, { useEffect, useState, useCallback, useMemo } from "react";
import { projectStore } from 'horde/backend/ProjectStore';
import { TestDataHandler, TestDataVersionRegistrar, TestSessionDetails } from "./testData";
import { TestSummary } from "./testAutomationSummary";
import { getStreamOptions, getTestSessionStatusColor, MultiOptionChooser, SessionStatusBar } from "./testAutomationCommon";
import { TestPhasesView } from "./testAutomationPhaseView";
import { JobStepTestDataItem } from "./api";

import { TestDataV2 } from "./testDataV2Fetcher";
import dashboard from "horde/backend/Dashboard";

const handler = new TestDataHandler();

// register TestDataV2 fetcher
TestDataVersionRegistrar.register(new TestDataV2);

const StreamChooser: React.FC = observer(() => {

    handler.subscribeToFilter();
    handler.subscribeToSearch();

    const stream = handler.filterState.stream;

    const menuProps: IContextualMenuProps = useMemo(() => getStreamOptions(handler.availableStreams, (stream) => handler.selectStream(stream), stream), [stream]);
    
    return <Stack style={{ paddingTop: 12, paddingBottom: 4 }} horizontal>
            <Label style={{ paddingTop: 5, paddingRight: 5 }}>Stream</Label>
            <DefaultButton style={{ width: 270, textAlign: "left", height: 30 }} menuProps={menuProps} text={stream ? projectStore.streamById(stream)?.fullname ?? stream : "Select"} />
        </Stack>   
});

const onRenderSuggestionsItem = (item: ITag) => {
    return <Stack style={{height: 24, padding: 4}}>
                <Text title={item.name} style={{textOverflow: 'ellipsis', overflow: 'hidden', whiteSpace: 'nowrap', maxWidth: 300, fontSize: 12}}>{item.name}</Text>
            </Stack>
}

const onRenderItem = (props: IPickerItemProps<ITag>) => {
    const item = props.item;
    return <Stack style={{ marginTop: 2, marginLeft: 3 }} key={`picker_item_${item.name}`}>
        <PrimaryButton
            iconProps={{ iconName: "Cancel", styles: { root: { fontSize: 12, margin: 0, padding: 0 } } }}
            styles={{label: { textOverflow: 'ellipsis', overflow: 'hidden', whiteSpace: 'nowrap', minWidth: 0, maxWidth: 150, margin: 0, paddingLeft: 3 }}}
            style={{ padding: 2, paddingLeft: 5, paddingRight: 5, fontSize: 12, height: "unset"}}
            text={item.name}
            title={item.name}
            onClick={props.onRemoveItem} />
    </Stack>;
}

const getTextFromTag = (item: ITag) => item.name;

const TestChooser: React.FC = observer(() => {
    
    handler.subscribeToFilter();
    handler.subscribeToSearch();

    const ctests: Set<string> = new Set<string>(handler.filterState.tests ?? []);
    const streamTests = handler.allStreamTestNames;

    const [testsSelection, setTestsSelection] = useState<Set<string>>(ctests);

    useEffect(() => {
        // keep input selection in sync
        if (testsSelection.size !== ctests.size || testsSelection.keys().some(s => !ctests.has(s))) {
            setTestsSelection(ctests);
        }
    }, [handler.searchUpdated]);

    const testTags: ITag[] = streamTests.map(t => ({key: `@${t.toLowerCase()}`, name: `@${t}`}));
    const filterSelectedTests = useCallback((filterText: string, _: ITag[]): ITag[] => {
        if (filterText.length < 2) return [];
        const lowerText = filterText.toLowerCase();
        const closestTags = testTags.values().filter((tag) => (tag.key as string).indexOf(lowerText) >= 0).take(10).toArray();
        const isTagExist = closestTags.find((tag) => tag.key === lowerText);
        if (!isTagExist) closestTags.splice(0, 0, {key: lowerText, name: filterText});
        return closestTags;
    }, [handler.queryLoading, handler.searchUpdated]);

    if (!streamTests.length) {
        return null;
    }

    const selectedItems: ITag[] = testsSelection.keys().toArray().map((key) => ({key: key.toLowerCase(), name: key} as ITag));

    return <Stack style={{ paddingTop: 12, paddingBottom: 4 }} horizontal>
            <Label style={{ paddingTop: 5, paddingRight: 5 }}>Tests</Label>
            <Stack tokens={{ childrenGap: 6 }} style={{ paddingLeft: 12 }} grow>
                <TagPicker
                    styles={{ input: { height: 28, width: 40 }, itemsWrapper: { marginRight: 3, marginBottom: 2 } }}
                    onRenderItem={onRenderItem}
                    onRenderSuggestionsItem={onRenderSuggestionsItem}
                    removeButtonAriaLabel="Remove"
                    selectionAriaLabel="Selected tests"
                    selectedItems={selectedItems}
                    onResolveSuggestions={filterSelectedTests}
                    getTextFromItem={getTextFromTag}
                    onChange={(tags) => {
                        if (!!tags) {
                            const keys = tags!.map(getTextFromTag);
                            keys.forEach((key) => handler.addTest(key));
                            testsSelection.difference(new Set(keys)).values().forEach(key => handler.removeTest(key));
                        } else {
                            testsSelection.values().forEach(key => handler.removeTest(key));
                        }
                        setTestsSelection(new Set(handler.filterState.tests ?? []));
                    }}
                />                
            </Stack>
        </Stack>
});

const MetaKeyChooser: React.FC<{ onUpdateKey: (key: string) => void }> = observer(({ onUpdateKey }) => {

    handler.subscribeToFilter();
    handler.subscribeToSearch();

   const cmetakeys: Set<string> = new Set(handler.filterState.metadata?.keys() ?? []);
   const streamMetadataKeys = handler.allStreamMetadataKeys;

   if (!streamMetadataKeys.length) {
        return null;
   }

   const options: IContextualMenuItem[] = [];
   streamMetadataKeys.forEach(a => {
        if (cmetakeys.has(a)) {
            return;
        }
        options.push({
            key: `metakey_${a}`, text: a, onClick: () => {
                onUpdateKey(a);
            }
        });
   });

   const menuProps: IContextualMenuProps = {
        shouldFocusOnMount: true,
        subMenuHoverDelay: 0,
        items: options,
   };

   return <Stack style={{ paddingTop: 12, paddingBottom: 4 }} horizontal>
            <Label style={{ paddingTop: 5, paddingRight: 5, whiteSpace: 'nowrap' }}>Filter by</Label>
            <DefaultButton style={{width: 270, textAlign: "left", height: 30 }} menuProps={menuProps} text="Metadata" />
        </Stack>   
});

const MetaValueChooser: React.FC<{ metakey: string, onRemove: (key: string) => void }> = observer(({ metakey, onRemove }) => {

    handler.subscribeToFilter();

   const cmetavalues: Set<string> = new Set(handler.filterState.metadata?.get(metakey) ?? []);
   const metavalues = handler.getAllStreamMetadataValues(metakey);

   if (!metavalues.length) {
        return null;
   }

   const options: IComboBoxOption[] = metavalues.map(t => { return { key: t, text: t } });

   return <Stack style={{ paddingTop: 12, paddingBottom: 4, }} horizontal>
            <Label style={{ paddingTop: 5, paddingRight: 5 }}>{metakey}</Label>
            <Stack style={{ padding: 1, width: 270 }}>
                <MultiOptionChooser style={{height: 28}} options={options} initialSelection={cmetavalues.keys().toArray()}
                updateKeys={(keys) => {
                    metavalues.forEach(t => {
                    const selected = keys.includes(t);
                    if (!selected && cmetavalues.has(t)) {
                        handler.removeMetadata(metakey, t);
                    }
                    else if (selected && !cmetavalues.has(t)) {
                        handler.addMetadata(metakey, t);
                    }
                    });
                }} />
            </Stack>
            <DefaultButton style={{ minWidth: 0, fontSize: 11, padding: 5, height: 30 }} title="remove filter"
                    onClick={() => {
                        if (handler.filterState?.metadata?.keys().some((key) => key === metakey)) {
                            handler.removeMetadata(metakey);
                        }
                        onRemove(metakey);
                    }}>
                <FontIcon iconName="CalculatorMultiply" />
            </DefaultButton>
        </Stack>
});


const TestAutomationSidebarLeft: React.FC = observer(() => {

    handler.subscribeToFilter();
    const filterMetaKeys = handler.filterState.metadata?.keys().toArray().sort((a, b) => a.localeCompare(b)) ?? [];

    const [metaKeys, setMetaKeys] = useState<string[]>(filterMetaKeys);
    const { hordeClasses } = getHordeStyling();

    const updateMetaKey = useCallback((key: string) => {
        const idx = metaKeys.indexOf(key);
        if (idx < 0) {
            metaKeys.push(key);
        } else {
            metaKeys.splice(idx, 1);
        }
        setMetaKeys([...metaKeys]);
    }, [metaKeys]);

    useEffect(() => {
        // keep search filter in sync
        if (metaKeys.toString() !== filterMetaKeys.toString()) {
            setMetaKeys(filterMetaKeys);
        }
    }, [handler.searchUpdated]);

   return <Stack style={{ width: 300, paddingRight: 18 }}>
            <Stack className={hordeClasses.modal}>
                <Stack key="chooser_stream">
                    <StreamChooser />
                </Stack>
                <Stack key="chooser_test">
                    <TestChooser />
                </Stack>
                {!!metaKeys.length && metaKeys.map((key) => <Stack key={`chooser_meta_${key}`}><MetaValueChooser metakey={key} onRemove={updateMetaKey} /></Stack>)}
                <Stack>
                    <MetaKeyChooser key="chooser_meta" onUpdateKey={updateMetaKey} />
                </Stack>
            </Stack>
        </Stack>
});

const TestAutomationSummary: React.FC = () => {

    return <Stack tokens={{ childrenGap: 24 }}>
                <TestSummary handler={handler} />
                {/** Additional or alternate view could be implemented here */}
            </Stack>
}

const TestAutomationPanel: React.FC = observer(() => {

    if (handler.queryLoading) {
        return <Stack horizontalAlign='center' style={{ paddingTop: 24, width: "100%" }} tokens={{childrenGap: 8}}>
                    <Text style={{ fontSize: 24 }}>Loading Data</Text>
                    <Spinner size={SpinnerSize.large} />
                </Stack>
    }

    handler.subscribeToSearch();

    if (!handler.selectedStatusStream) {
        return null;
    }

    const status = handler.filteredTests;

    return <Stack grow>
                { !status.length &&
                    <Stack grow horizontalAlign='center' style={{ paddingTop: 24 }}>
                        <Text style={{ fontSize: 24 }}>No Results</Text>
                    </Stack>
                }
                { !!status.length &&
                    <TestAutomationSummary />
                }
            </Stack>

});

const JobStepTestsSelectionModal: React.FC<{ jobId: string, stepId: string }> = ({ jobId, stepId }) => {
    const navigate = useNavigate();
    const [tests, setTests] = useState<(JobStepTestDataItem & {details?: TestSessionDetails})[]>();

    const loadTestData = async (tests: JobStepTestDataItem[]) => {
        const testWithDetails: (JobStepTestDataItem & {details?: TestSessionDetails})[] = [];
        for (const test of tests) {
            const sessionDetails = await handler.queryTestData(test.testDataId);
            testWithDetails.push({...test, details: sessionDetails});
        }

        return testWithDetails;
    }

    useEffect(() => {
        handler.queryJobStepTestData(jobId, stepId).then(tests => {
            if (!!tests && tests.length === 1) {
                // if only one item, redirect automatically
                navigate(`?session=${tests[0].testDataId}`, {replace: true});
            } else {
                const selection = tests ?? [];
                if (selection.length > 0) {
                    loadTestData(selection).then(detailedList => setTests(detailedList));
                } else {
                    setTests(selection);
                }
            }
        })
    }, []);

    const color = dashboard.darktheme ? "rgba(255, 255, 255, 0.1)" : "rgba(0, 0, 0, 0.1)";

    return <Modal isOpen={true} isBlocking={true} topOffsetFixed={true} styles={{ main: { padding: 8, width: 1134, hasBeenOpened: false, top: "200px", position: "absolute" } }} onDismiss={() => navigate(-1)} >
            <Stack grow>
                <Stack horizontal verticalAlign="center">
                    <Stack><Text style={{ paddingLeft: 8, fontSize: 16, fontWeight: 600 }}>Test Reports</Text></Stack>
                    <Stack grow />
                    <Stack style={{ paddingBottom: 4 }}>
                        <IconButton
                            iconProps={{ iconName: 'Cancel' }}
                            ariaLabel="Close popup modal"
                            onClick={() => navigate(-1)}
                        />
                    </Stack>
                </Stack>
                <Stack tokens={{ childrenGap: 9 }} style={{ padding: 24, overflow: "auto", maxHeight: 'calc(100vh - 300px)'}}>
                    {!tests &&
                        <Stack horizontalAlign='center' style={{ paddingTop: 24, width: "100%" }}>
                            <Spinner size={SpinnerSize.large} />
                        </Stack>
                    }
                    {!!tests && tests.length &&
                        tests.map((test, i) =>  <Stack key={`${test.testKey}-${i}`} tokens={{childrenGap: 15}} horizontal verticalAlign="end"
                                                styles={{root: {borderBottom: `1px solid ${color}`, padding: '8px 2px', cursor: 'pointer', selectors: {':hover': {backgroundColor: color}}}}}
                                                onClick={() => navigate(`?session=${test.testDataId}`)}>
                                                <Stack horizontal verticalAlign="center">
                                                    {!!test.details &&
                                                        <Stack style={{ paddingLeft: 0, paddingTop: 1, paddingRight: 4 }}>
                                                            <FontIcon style={{ fontSize: 11, color: getTestSessionStatusColor(test.details) }} iconName="Square" />
                                                        </Stack>
                                                    }
                                                    <Stack>
                                                        <Text style={{ whiteSpace: 'nowrap' }} >{test.testName}{!!test.details?.meta && <span> - {test.details.meta.getValues().join(' / ')}</span>}</Text>                                                        
                                                    </Stack>
                                                </Stack>
                                                <Stack>{!!test.details && SessionStatusBar(test.details, 200, 10)}</Stack>
                                            </Stack>)
                    }
                    {!!tests && tests.length === 0 &&
                        <Text>No report found</Text>
                    }
                </Stack>
            </Stack>
        </Modal>
}

export const TestAutomationView: React.FC = () => {

    const [searchParams, setSearchParams] = useSearchParams();
    const [initialized, setInitialized] = useState(false);

    const testDataId = searchParams.get('session');
    const jobId = searchParams.get('job');
    const stepId = searchParams.get('step');

    useEffect(() => {
        handler.initialize(searchParams.toString(), (search: string, replace?: boolean) => {setSearchParams(search, {replace: replace})}).then(() => {setInitialized(true)});
        return () => {
            handler.clear();
            setInitialized(false); // for dev env
        };
    }, []);

    useEffect(() => {
        initialized && handler.syncSearchParam(searchParams.toString());
    }, [searchParams]);

    const { hordeClasses, modeColors } = getHordeStyling();

    return <Stack className={hordeClasses.horde} key="key_test_automation_hub">
                <TopNav />
                <Breadcrumbs items={[{ text: 'Test Automation Hub' }]} />
                <Stack grow style={{ padding: 12, backgroundColor: modeColors.background, height: 'calc(100vh - 148px)', overflow: 'auto' } } horizontalAlign='center'>
                    {initialized && !!jobId && !!stepId &&
                        <JobStepTestsSelectionModal jobId={jobId} stepId={stepId} />
                    }
                    {initialized && !!testDataId &&
                        <TestPhasesView testDataId={testDataId!} handler={handler}
                            onDismiss={() => {
                                handler.removeSearchParam('session');
                                handler.removeSearchParam('phase');
                            }} />
                    }
                    {initialized && !testDataId && !!handler.availableStreams.length &&
                        <Stack horizontal grow>
                            <TestAutomationSidebarLeft/>
                            <Stack style={{ minWidth: 1024 }}>
                                <TestAutomationPanel/>
                            </Stack>
                        </Stack>
                    }
                </Stack>
            </Stack>
}