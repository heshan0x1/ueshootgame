// Copyright Epic Games, Inc. All Rights Reserved.

import { getJobStepTestDataV2, GetPhaseResponse, getStreamTestsV2, getTestDataV2, GetTestMetadataResponse, GetTestNameResponse, GetTestPhaseSessionResponse, getTestPhaseSessionsV2, getTestPhasesV2, GetTestSessionResponse, getTestSessionsV2, GetTestTagResponse, JobStepTestDataItem, PhaseSessionDetailsResponse, TestData, TestOutcome, TestSessionDetailsResponse } from "./api";
import { TestStreamRef, TestStatus, TestSessionResult, TestNameRef, MetadataRef, TestTagRef, PhaseSessionResult, TestPhaseRef, TestMetaStatus, TestSessionDetails, TestPhaseStatus, DeviceRef, TestVersionFetcher } from "./testData"
import { EventEntry } from "./testEventsModel";

const minuteInWeek = 10080;

class TestPhaseStatusV2 extends TestPhaseStatus {

    constructor(phase: PhaseSessionDetailsResponse, devices: DeviceRef[], parent: TestSessionDetails) {
        super(phase.key, phase.name ?? "<undefinied>", new Date(phase.dateTime), phase.timeElapseSec, devices, phase.outcome, parent)
        this._eventStreamPath = phase.eventStreamPath;
        this.hasWarning = phase.hasWarning;
    }

    async fetchEventStream(): Promise<EventEntry[] | undefined> {
        if (!this._eventStreamPath) return;
        return await this.artifacts?.fetch(this._eventStreamPath);
    }

    private _eventStreamPath?: string;

}

export class TestDataV2 implements TestVersionFetcher {
    get version() { return 2 }

    async fetchStreamsInfo(testStreams: Map<string, TestStreamRef>) {

        const results = await getStreamTestsV2(testStreams.keys().toArray());
        results.forEach((streamInfo) => {
            const testStream = testStreams.get(streamInfo.streamId)!;

            /// Tests
            streamInfo.tests.forEach((test) => {
                testStream.addTest(this.createTestNameRef(test));
            });

            /// Metadata
            streamInfo.testMetadata.forEach((entry) => {
                testStream.addMetadata(this.createMetadataRef(entry));
            });

            /// Tags
            streamInfo.testTags.forEach((tag) => {
                testStream.addTag(this.createTagRef(tag));
            });
            
        });
   
    }

    async fetchTestSessions(streamRef: TestStreamRef, status: TestStatus, weeks: number, onAdd?: (session: TestSessionResult) => void) {

        const streamId = streamRef.id;
        const tests: Map<string, TestNameRef> = new Map(streamRef.tests.filter((t) => t.version >= this.version).map((t) => [t.id, t]));
        const meta: Map<string, MetadataRef> = new Map(streamRef.meta.filter((m) => m.version >= this.version).map((m) => [m.id, m]));

        const minQueryDate = new Date(new Date().valueOf() - (minuteInWeek * weeks * 60000));
        const maxQueryDate = new Date();

        if (tests.size) {
            const responses = await getTestSessionsV2([streamId], Array.from(tests.keys()),  Array.from(meta.keys()), minQueryDate.toISOString(), maxQueryDate.toISOString());
            responses.forEach((session) => {
                if (session.streamId !== streamId) {
                    return;
                }
                const testSession = this.createTestSession(session);
                const testNameRef = tests.get(testSession.nameRef);
                const metadataRef = meta.get(testSession.metadataId);
                if (!!testNameRef && !!metadataRef) {
                    status.addSession(testNameRef, metadataRef, testSession);
                    if (onAdd) onAdd(testSession);
                }
            });
        }

    }

    async fetchTestData(testDataId: string): Promise<TestSessionDetails | undefined> {

        const response = await getTestDataV2(testDataId);
        if (!!response) {
            const sessionDetails = this.createSessionDetails(response);
            return sessionDetails;
        }

        return undefined;

    }

    async fetchJobStepTestData(jobId: string, stepId: string): Promise<JobStepTestDataItem[] | undefined> {
        return await getJobStepTestDataV2(jobId, stepId)
    }

    async fetchPhaseSessions(streamRef: TestStreamRef, status: TestMetaStatus, phaseKey: string, weeks: number): Promise<boolean> {

        const streamId = streamRef.id;
        const meta: Map<string, MetadataRef> = new Map(streamRef.meta.filter((m) => m.version >= this.version).map((m) => [m.id, m]));

        if (!status.test.phases) {
            const responses = await getTestPhasesV2([status.test.id]);
            status.test.phases = new Map(responses.map(i => i.phases.map(p => this.createPhaseRef(p))).flat().map(p => [p.key, p]));
        }
        
        const phaseId = status.test.phases.get(phaseKey)?.id;
        if (phaseId) {
            const minQueryDate = new Date(new Date().valueOf() - (minuteInWeek * weeks * 60000));
            const maxQueryDate = new Date();
    
            const responses = await getTestPhaseSessionsV2([streamId], [phaseId], minQueryDate.toISOString(), maxQueryDate.toISOString());
            responses.forEach((session) => {
                if (session.streamId !== streamId) {
                    return;
                }
                const phaseSession = this.createPhaseSession(session);
                const metadataRef = meta.get(phaseSession.metadataId);
                if (!!metadataRef) {
                    status.addPhaseSession(metadataRef, phaseKey, phaseSession);
                }
            });

            return true;
        }

        return false;

    }

    private testNameRefMap: Map<string, TestNameRef> = new Map();
    private metadataRefMap: Map<string, MetadataRef> = new Map();
    private testTageRefMap: Map<string, TestTagRef> = new Map();

    private createTestNameRef(test: GetTestNameResponse): TestNameRef {
        const globalKey = test.id;
        let item = this.testNameRefMap.get(globalKey);
        if (!item) {
            item = new TestNameRef(test.id, test.key, test.name, this.version);
            this.testNameRefMap.set(globalKey, item);
        }

        return item;
    }

    private createMetadataRef(item: GetTestMetadataResponse): MetadataRef {
        const globalKey = item.id;
        let meta = this.metadataRefMap.get(globalKey);
        if (!meta) {
            meta = new MetadataRef(item.id, item.entries, this.version);
            this.metadataRefMap.set(globalKey, meta);
        }

        return meta;
    }

    private createTagRef(item: GetTestTagResponse): TestTagRef {
        const globalKey = item.id;
        let tag = this.testTageRefMap.get(globalKey);
        if (!tag) {
            tag = new TestTagRef(item.id, item.name, this.version);
            this.testTageRefMap.set(globalKey, tag);
        }

        return tag;
    }

    private createPhaseRef(item: GetPhaseResponse): TestPhaseRef {
        return new TestPhaseRef(item.id, item.key, item. name, this.version);
    }

    private createTestSession(test: GetTestSessionResponse): TestSessionResult {
        const start = new Date(test.startDateTime);
        const result = new TestSessionResult(test.id, start, test.duration, test.commitId, test.commitOrder, test.nameRef, test.metadataId, test.streamId, test.outcome, test.testDataId);
        result.phasesSucceededCount = test.phasesSucceededCount;
        result.phasesFailedCount = test.phasesFailedCount;
        result.phasesUnspecifiedCount = test.phasesUndefinedCount;
        result.jobId = test.jobId;
        result.stepId = test.stepId;

        return result;
    }

    private createPhaseSession(phase: GetTestPhaseSessionResponse): PhaseSessionResult {
        const start = new Date(phase.startDateTime);
        const result = new PhaseSessionResult(phase.id, start, phase.duration, phase.commitId, phase.commitOrder, phase.sessionId, phase.phaseRef, phase.metadataId, phase.streamId, phase.outcome);
        result.hasWarning = phase.hasWarning;
        result.errorFingerprint = phase.errorFingerprint;
        result.jobId = phase.jobId;
        result.stepId = phase.stepId;

        return result;
    }

    private createSessionDetails(testData: TestData): TestSessionDetails {
        const testDetails = testData.data as TestSessionDetailsResponse;
        const start = new Date(testDetails.summary.dateTime);
        const result = new TestSessionDetails(testData.key, testDetails.summary.testName, start, testDetails.summary.timeElapseSec, testData.commitId.name, testData.commitId.order, testData.streamId, testData.jobId, testData.stepId);
        Object.entries(testDetails.devices).forEach(([key, d]) => {
            result.devices.set(key, new DeviceRef(d.name, d.appInstanceLogPath, d.metadata, this.version));
        });
        result.phases.push(...testDetails.phases.map(p => this.createPhaseStatus(p, result.devices, result)));
        result.phasesSucceededCount = testDetails.summary.phasesSucceededCount;
        result.phasesFailedCount = testDetails.summary.phasesFailedCount;
        result.phasesUnspecifiedCount = testDetails.summary.phasesUndefinedCount;
        result.outcome = testDetails.summary.phasesFailedCount > 0 ?
                            TestOutcome.Failure
                            : testDetails.summary.phasesUndefinedCount > 0 ?
                                TestOutcome.Unspecified
                                : testDetails.summary.phasesTotalCount > 0 ?
                                    TestOutcome.Success : TestOutcome.Skipped;
        result.preflightCommitId = testData.preflightCommitId;

        // search for known metadata
        const metaFilter = new Map(Object.entries(testDetails.metadata).map(([key, value]) => [key, [value]]));
        const metaRef = this.metadataRefMap.values().find((meta) => Object.keys(meta.entries).length === metaFilter.size &&  meta.isMatch(metaFilter)) ?? this.createMetadataRef({id: testData.id, entries: testDetails.metadata});
        result.meta = metaRef;

        // search for known test name
        const testNameRef = this.testNameRefMap.values().find((nameRef) => nameRef.key === testData.key) ?? this.createTestNameRef({id: testData.id, key: testData.key, name: result.name});
        result.test = testNameRef;

        return result;
    }

    private createPhaseStatus(phase: PhaseSessionDetailsResponse, deviceRefs: Map<string, DeviceRef>, parent: TestSessionDetails) {
        const devices = phase.deviceKeys.map(key => deviceRefs.get(key) ?? new DeviceRef(key));
        const result = new TestPhaseStatusV2(phase, devices, parent);

        return result;
    }
}

