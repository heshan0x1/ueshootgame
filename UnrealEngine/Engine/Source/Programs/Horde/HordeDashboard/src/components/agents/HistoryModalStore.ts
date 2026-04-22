// Copyright Epic Games, Inc. All Rights Reserved.

import { action, makeObservable, observable } from "mobx";
import { AgentData, GetAgentLeaseResponse, GetAgentSessionResponse, GetAgentTelemetrySampleResponse, GetJobResponse, LeaseData, SessionData } from "../../backend/Api" 
import backend from "../../backend";

type LeaseTooltip = {
   lease?: GetAgentLeaseResponse;
   time?: Date;
   id?: string;
   x?: number;
   y?: number;
   frozen?: boolean;
   sample?: GetAgentTelemetrySampleResponse;
}

type InfoPanelItem = {
   key: string;
   name: string;
   data?: string;
   selected: boolean;
};

type InfoPanelSubItem = {
   name: string;
   value: string;
}

class HistoryModalStore {
   constructor() {
      makeObservable(this);
   }

   @observable
   selectedAgentUpdated = 0;

   @observable
   currentLeaseTooltip?: LeaseTooltip;

   get selectedAgent(): AgentData | undefined {
      // subscribe in any observers
      if (this.selectedAgentUpdated) { }
      return this._selectedAgent;
   }

   private _selectedAgent: AgentData | undefined = undefined;
   @observable.shallow currentData: any = [];
   @observable.shallow infoItems: InfoPanelItem[] = [];
   @observable.shallow infoSubItems: InfoPanelSubItem[] = [];
   agentItemCount: number = 0;
   devicesItemCount: number = 0;
   workspaceItemCount: number = 0;
   sortedLeaseColumn = "startTime";
   sortedLeaseColumnDescending = true;
   sortedSessionColumn = "startTime";
   sortedSessionColumnDescending = true;

   @observable mode: string | undefined = undefined;
   modeCurrentIndex: number = 0;
   bUpdatedQueued: boolean = false;

   // sets the pool editor dialog open
   @action
   setSelectedAgent(selectedAgent?: AgentData | undefined) {
      // if we're closing don't reset
      this._selectedAgent = selectedAgent;
      if (this.selectedAgent) {
         // set date to now
         this._initBuilderItems();
         if (!this.mode) {
            this.setMode("info");
         }
         else {
            this.setMode(this.mode, true);
         }
         this._doUpdate();
      }

      this.selectedAgentUpdated++;
   }

   @action
   setMode(newMode?: string, force?: boolean) {
      if (newMode) {
         newMode = newMode.toLowerCase();
         if (this.mode !== newMode || force) {
            this.mode = newMode;
            this.currentData = [];
            this.modeCurrentIndex = 0;
            this._doUpdate();
         }
      }
   }

   @action
   setLeaseTooltip(tooltip?: LeaseTooltip) {
      this.currentLeaseTooltip = tooltip;
   }

   setInfoItemSelected(item: InfoPanelItem) {
      this.infoItems.forEach(infoItem => {
         if (infoItem.key === item.key) {
            infoItem.selected = true;
         }
         else {
            infoItem.selected = false;
         }
      });
      this.setInfoItems(this.infoItems);
   }

   private _initBuilderItems() {
      this.agentItemCount = 0;
      this.devicesItemCount = 0;
      this.workspaceItemCount = 0;
      const items: InfoPanelItem[] = [
         {
            key: "overview",
            name: "Overview",
            selected: false
         }
      ];
      this.agentItemCount = 1;
      if (this.selectedAgent) {
         if (this.selectedAgent.capabilities?.devices) {
            for (const deviceIdx in this.selectedAgent.capabilities?.devices) {
               const device = this.selectedAgent.capabilities?.devices[deviceIdx];
               if (!device["name"]) {
                  device["name"] = "Primary";
               }
               if (device.properties) {
                  items.push({ key: `device${deviceIdx}`, name: device["name"], selected: device["name"] === "Primary" ? true : false });
                  this.devicesItemCount++;
               }
            }
         }
         if (this.selectedAgent.workspaces) {
            const streamCount = new Map<string, number>();
            for (const workspaceIdx in this.selectedAgent.workspaces) {
               const workspace = this.selectedAgent.workspaces[workspaceIdx];
               let count = streamCount.get(workspace.stream) ?? 0;
               if (!count) {
                  count++;
                  streamCount.set(workspace.stream, 1);
               } else {
                  count++;
                  streamCount.set(workspace.stream, count);
               }

               const name: string = count > 1 ? `${workspace.stream} (${count})` : workspace.stream;

               (workspace as any)._hackName = name;
               ;
               items.push({ key: `workspace${workspaceIdx}`, name: name, selected: false, data: name });
               this.workspaceItemCount++;
            }
         }
      }
      this.setInfoItems(items);
   }

   @action
   setInfoItems(items: InfoPanelItem[]) {
      const subItems: InfoPanelSubItem[] = [];
      if (this.selectedAgent) {
         const selectedItem = items.find(item => item.selected);
         if (selectedItem) {
            if (selectedItem.key === "overview") {
               subItems.push({ name: 'Enabled', value: this.selectedAgent.enabled.toString() });
               subItems.push({ name: 'Comment', value: this.selectedAgent.comment ?? "None" });
               subItems.push({ name: 'Ephemeral', value: this.selectedAgent.ephemeral.toString() });
               subItems.push({ name: 'ForceVersion', value: this.selectedAgent.forceVersion ?? "None" });
               subItems.push({ name: 'Id', value: this.selectedAgent.id });
               subItems.push({ name: 'Online', value: this.selectedAgent.online.toString() });
               subItems.push({ name: 'Last Update', value: this.selectedAgent.updateTime.toString() });
               subItems.push({ name: 'Version', value: this.selectedAgent.version ?? "None" });
            }
            else if (selectedItem.key.indexOf("workspace") !== -1) {
               if (this.selectedAgent.workspaces) {
                  for (const workspaceIdx in this.selectedAgent.workspaces) {
                     const workspace = this.selectedAgent.workspaces[workspaceIdx];

                     if ((workspace as any)._hackName === selectedItem.data) {
                        subItems.push({ name: 'Identifier', value: workspace.identifier });
                        subItems.push({ name: 'Stream', value: workspace.stream });
                        subItems.push({ name: 'Incremental', value: workspace.bIncremental.toString() });
                        workspace.serverAndPort && subItems.push({ name: 'Server and Port', value: workspace.serverAndPort });
                        workspace.userName && subItems.push({ name: 'Username', value: workspace.userName });
                        workspace.password && subItems.push({ name: 'Password', value: workspace.password });
                        workspace.view && subItems.push({ name: 'View', value: workspace.view?.join("\n") });
                     }
                  }
               }
            }
            else if (selectedItem.key.indexOf("device") !== -1) {
               if (this.selectedAgent.capabilities?.devices) {
                  for (const deviceIdx in this.selectedAgent.capabilities?.devices) {
                     const device = this.selectedAgent.capabilities?.devices[deviceIdx];
                     if (device.name === selectedItem.name) {
                        if (device.properties) {
                           for (const propIdx in device.properties) {
                              const prop = device.properties[propIdx];
                              const subItemData = prop.split('=');
                              if (subItemData[0].indexOf("RAM") !== -1) {
                                 subItemData[1] += " GB";
                              }
                              else if (subItemData[0].indexOf("Disk") !== -1) {
                                 subItemData[1] = (Number(subItemData[1]) / 1073741824).toLocaleString(undefined, { maximumFractionDigits: 0 }) + " GiB";
                              }
                              subItems.push({ name: subItemData[0], value: subItemData[1] });
                           }
                        }
                     }
                  }
               }
            }
         }
      }
      this.infoItems = [...items];
      this.infoSubItems = subItems;
   }

   private _doUpdate() {
      if (this.mode === "leases") {
         this.UpdateLeases();
      }
      else if (this.mode === "sessions") {
         this.UpdateSessions();
      }
   }

   nullItemTrigger() {
      if (!this.bUpdatedQueued) {
         this.bUpdatedQueued = true;
         this._doUpdate();
      }
   }

   UpdateLeases() {
      const data: LeaseData[] = [];
      backend.getLeases(this.selectedAgent!.id, this.modeCurrentIndex, 30, true).then(responseData => {
         responseData.forEach((dataItem: GetAgentLeaseResponse) => {
            data.push(dataItem);
         });
         this.appendData(data);
      });
   }

   UpdateSessions() {
      const data: SessionData[] = [];
      backend.getSessions(this.selectedAgent!.id, this.modeCurrentIndex, 30).then(responseData => {
         responseData.forEach((dataItem: GetAgentSessionResponse) => {
            data.push(dataItem);
         });
         this.appendData(data);
      });
   }

   jobData = new Map<string, GetJobResponse | boolean>();

   UpdateTelemetry(minutes: number) {

      const requests = [backend.getAgentTelemetry(this.selectedAgent!.id, new Date(Date.now() - 1000 * 60 * minutes), new Date()), backend.getAgentLeases(this.selectedAgent!.id, new Date(Date.now() - 1000 * 60 * minutes), new Date())]
      Promise.all(requests).then((data) => {
         this.setTelemetryData((data[0] as GetAgentTelemetrySampleResponse[]).reverse(), data[1] as GetAgentLeaseResponse[]);
      })
   }

   @action
   setTelemetryData(telemetryData: GetAgentTelemetrySampleResponse[], sessionData: GetAgentLeaseResponse[]) {

      this.currentData = [telemetryData, sessionData];

   }

   @action
   appendData(newData: any[]) {

      if (this.mode === "telemetry") {
         return;
      }

      // if there's any data, there might be more data next time, so add another callback.
      if (newData.length > 0) {
         newData.push(null);
      }

      let combinedData = [...this.currentData];

      // remove previous null if it exists
      if (combinedData[combinedData.length - 1] === null) {
         combinedData.splice(-1, 1);
      }
      // add all the new data
      Array.prototype.push.apply(combinedData, newData);

      const dedupe = new Set<string>();

      this.currentData = combinedData.filter(d => {
         if (dedupe.has(d?.id)) {
            return false;
         }
         dedupe.add(d?.id);
         return true;
      });

      this.modeCurrentIndex += newData.length;
      this.bUpdatedQueued = false;
   }

}

export const historyModalStore = new HistoryModalStore();