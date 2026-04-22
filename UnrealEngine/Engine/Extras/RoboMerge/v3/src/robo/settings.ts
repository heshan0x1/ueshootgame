// Copyright Epic Games, Inc. All Rights Reserved.

import * as fs from 'fs';
import { Args } from '../common/args';
import { ContextualLogger } from '../common/logger';
import { VersionReader } from '../common/version';
import { PerforceContext, Workspace } from '../common/perforce';
import { RoboArgs } from './roboargs';
import semver = require('semver');
import { migrateMessagesToDB } from './persistedmessages';

const BotModel = require('./models/bot');

/**
 * Descriptions of major versions in settings layout
 * Original / Unversioned / 0.0.0 - The original Robomerge settings, completely unversioned. Pre-Nov 2018
 * 1.0.0 - Slack message storage overhaul Nov 2018
 * 2.2.2 - No change to settings structure -- Going to match Robomerge versions from now on -- May 2019
 * 3.0.0 - Branchbot.ts is split into NodeBot and EdgeBot classes, adding edges settings.
 *       - Reworked pause object into seperate 'manual pause' and 'blockage' objects, need to migrate old data - Sept. '19
 * 3.0.2 - Fix for pause state serialisation.
 * 3.1.0 - Remove backwards compatibility before 3.0.2
 */
const VERSION = VersionReader.getPackageVersion()

let args: Args
export function settingsInit(inArgs: Args) {
	args = inArgs
}
	

const jsonlint: any = require('jsonlint-mod')

function readFileToString(filename: string) {
	try {
		return fs.readFileSync(filename, 'utf8');
	}
	catch (e) {
		return null;
	}
}

export class Context {
	readonly path: string[];
	protected object: { [key: string]: any };

	constructor(settings: Settings, topLevelObjectName: string);
	constructor(settings: Settings, pathToElement: string[]);
	constructor(private settings: Settings, pathToElement: string | string[]) {
		if (typeof(pathToElement) === "string") {
			this.path = [ pathToElement ]
		} else {
			this.path = pathToElement
		}
		
		const settingsObject: any = settings.object;

		// Start on highest level and proceed down the tree
		this.object = settingsObject
		for (let index = 0; index < this.path.length; index++) {
			const elementName = this.path[index]
			if (this.object[elementName] === undefined) {
				this.object[elementName] = {};
			} else if (typeof(this.object[elementName]) !== "object") {
				throw new Error(`Error finding object element for ${this.path.slice(0, index).join('->')}`);
			}
			
			this.object = this.object[elementName]
		}
	}

	getInt(name: string, dflt?: number) {
		let val = parseInt(this.get(name));
		return isNaN(val) ? (dflt === undefined ? 0 : dflt) : val;
	}
	get(name: string) {
		return this.object[name];
	}
	getSubContext(name: string | string[]): Context {
		const path = typeof(name) === "string" ? [...this.path, name] : [...this.path, ...name]
		return new Context(this.settings, path)
	}
	set(name: string, value: any) {
		this.object[name] = value;
		this.settings._saveObject();
	}
}

type FilePurpose = 
	'PERSISTENCE' |
	'BACKUP' |
	'TEMP'

const FilePurposeExtensionMap = {
	'PERSISTENCE' : '.json',
	'BACKUP' : '.bak',
	'TEMP' : '.save'
}

function getPersistenceFilepath(botname: string) {
	return args.persistenceDir + `/${botname}.settings`
}

export class Settings {
	botname: string
	enableSave: boolean
	object: { [key: string]: any }
	private savingState: 'Idle' | 'Saving' | 'NeedsSave' = 'Idle'
	private	lastPersistBackupTime: Date
	private readonly settingsLogger: ContextualLogger
	private readonly p4backupWorkspace?: Workspace

	private constructor(botname: string, parentLogger: ContextualLogger, private p4: PerforceContext) {
		this.settingsLogger = parentLogger.createChild('Settings')

		this.lastPersistBackupTime = new Date(Date.now())
		this.botname = botname.toLowerCase() 
		if (args.persistenceBackupFrequency > 0) {
			this.p4backupWorkspace = {directory: fs.realpathSync(args.persistenceDir), name: args.persistenceBackupWorkspace}
		}

		// see if we should enable saves
		this.enableSave = !process.env["NOSAVE"];
		if (!this.enableSave) {
			this.settingsLogger.info("Saving config has been disabled by NOSAVE environment variable");
		}
	}

	static async CreateAsync(botname: string, parentLogger: ContextualLogger, p4: PerforceContext) {

		const settings = new Settings(botname, parentLogger, p4)

		if (RoboArgs.useMongo()) {
			// first try to get the bot data from the DB
			const botRecord = await BotModel.find({botname: botname})
			if (botRecord.length > 1) {
				throw new Error(`Found multiple records for ${botname}`) 
			}

			if (botRecord.length == 1) {
				settings.object = botRecord[0].details
			}
		}

		let objVersion : semver.SemVer
		let loadedFromFile = false
		if (!settings.object)
		{
			// load the object from disk
			let filebits = readFileToString(settings.getFilename('PERSISTENCE'));
			if (filebits) {
				settings.object = jsonlint.parse(filebits);
				loadedFromFile = true
			}
			else {
				// Create "empty" settings object, but include latest version so we don't needless enter migration code
				settings.object = {
					version: VERSION.raw
				};
				objVersion = VERSION
			}
		}

		// Originally we did not version configuration.
		// If we have no version, assume it needs all migrations
		if (!settings.object.version) {
			settings.settingsLogger.warn("No version found in settings data.")
			objVersion = new semver.SemVer('0.0.0')
		} 
		// Ensure we have a semantic version 
		else {
			const fileSemVer = semver.coerce(String(settings.object.version))
			if (fileSemVer) {
				objVersion = fileSemVer
			} else {
				throw new Error(`Found version field in settings file, but it does not appear to be a semantic version: "${settings.object.version}"`)
			}
		}

		if (semver.lt(objVersion, '3.0.2')) {
			throw new Error(`Settings files prior to 3.0.2 are unsupported`)
		}

		/**
		 * MIGRATION CODE SECTION START
		 */
		// For comparison later
		const previousObjVersion = new semver.SemVer(objVersion)

		// Transition slack messages out of the settings object and into the database
		if (loadedFromFile && RoboArgs.useMongo()) {
			await migrateMessagesToDB(settings)
		}

		// We're up to date!
		if (previousObjVersion.compare(VERSION) !== 0) {
			settings.settingsLogger.info(`Changing settings version from '${previousObjVersion.raw}' to '${VERSION.raw}'`)
		}

		/**
		 * MIGRATION CODE SECTION FINISH
		 */
		
		settings.object.version = VERSION.raw
		settings._saveObject(true);

		return settings
	}

	getContext(name: string) {
		return new Context(this, name);
	}

	private getFilename(purpose: FilePurpose) {
		return getPersistenceFilepath(this.botname) + FilePurposeExtensionMap[purpose]
	}

	_saveObject(ensureInP4?: boolean) {
		if (this.enableSave) {
			if (RoboArgs.useMongo()) {
				const saveToDB = () => {
					this.savingState = 'Saving'
					BotModel.collection.updateOne({botname:this.botname},{$set:{botname:this.botname,details:this.object}},{upsert:true})
					.then(() => {
						if (this.savingState == 'NeedsSave') {
							saveToDB()
						}
						else {
							this.savingState = 'Idle'
						}
					})
				}
				if (this.savingState == 'Idle') {
					saveToDB()
				}
				else {
					this.savingState = 'NeedsSave'
				}
			}
			else {
				const persistentFile = this.getFilename('PERSISTENCE')
				const tempFile = this.getFilename('TEMP')

				if (fs.existsSync(persistentFile)) {
					fs.copyFileSync(persistentFile,this.getFilename('BACKUP'))
				}

				let filebits = JSON.stringify(this.object, null, '  ')
				fs.writeFileSync(tempFile, filebits, "utf8")

				fs.copyFileSync(tempFile, persistentFile)

				if (this.p4backupWorkspace) {
					if (ensureInP4) {
						this.p4.fstat(this.p4backupWorkspace, persistentFile)
						.then((fstat) => {
							if (fstat.length == 0 || fstat[0].headAction == 'delete') {
								this.p4.new_cl(this.p4backupWorkspace!, "Adding persistent backup", [])
								.then((cl: number) => {
									this.p4.add(this.p4backupWorkspace!, cl, persistentFile, "text+wS64")
									.then(() => this.p4.submit(this.p4backupWorkspace!, cl))
								})
							}
						})
					}
					else if ((Date.now() - this.lastPersistBackupTime.getTime()) / 60000 >= args.persistenceBackupFrequency) {
						this.lastPersistBackupTime = new Date(Date.now())

						this.p4.revertFile(this.p4backupWorkspace, persistentFile, ['-k'])
						.then(() => this.p4.sync(this.p4backupWorkspace, persistentFile, {opts: ['-k']}))
						.then(() => this.p4.new_cl(this.p4backupWorkspace!, "Updating persistent backup"))
						.then((cl: number) => {
							this.p4.edit(this.p4backupWorkspace!, cl, persistentFile, ['-k'])
							.then(() => this.p4.submit(this.p4backupWorkspace!, cl))
						})
						
					}
				}
			}
		}
	}
}

