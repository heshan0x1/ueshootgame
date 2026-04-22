// Copyright Epic Games, Inc. All Rights Reserved.

import { BranchArg, resolveBranchArg } from './branch-interfaces';
import { ContextualLogger } from '../common/logger';
import { RoboArgs } from './roboargs';
import { Context, Settings } from './settings';
import { SlackMessage } from './slack';

const SlackMessagesModel = require('./models/slackmessages');

const NOTIFICATIONS_PERSISTENCE_KEY = 'notifications'
const SLACK_MESSAGES_PERSISTENCE_KEY = 'slackMessages'
const SUNSET_PERSISTED_NOTIFICATIONS_DAYS = 30

type PersistedMessagePayload = {
	timestamp: string
	permalink: string
	messageOpts: SlackMessage
}

type PersistedMessagePersistence = {[key: string]: PersistedMessagePayload}

interface PersistedMessageKeyComponents {
	cl: number
	branchName: string
	channel: string
}

class PersistedMessageKey {

	private _key: string | PersistedMessageKeyComponents

	constructor(key: string)
	constructor(cl: number, branchName: string, channel: string)
	constructor(arg1: string|number, branchName?: string, channel?: string) {
		if (typeof arg1 === "string") {
			this._key = arg1
		}
		else {
			this._key = {cl: arg1, branchName: branchName!, channel: channel!}
		}
	}

	toString() {
		if (typeof this._key === "string") {
			return this._key
		}
		return PersistedSlackMessages.generateKey(this._key.cl, this._key.branchName, this._key.channel)
	}
	
	toComponents() {
		if (typeof this._key === "string") {
			const components = this._key.split(':')
			if (components.length != 3) {
				throw new Error(`Unexpected number of components in ${this._key}`)
			}
			return {
				cl: parseInt(components[0]),
				branchName: components[1],
				channel: components[2]
			}
		}
		return this._key
	}	
}

export class PersistedSlackMessage implements PersistedMessagePayload {
	timestamp: string
	permalink: string
	messageOpts: SlackMessage

	constructor(payload: PersistedMessagePayload, private _key: PersistedMessageKey, private persistedMessages: PersistedSlackMessages) {
		this.timestamp = payload.timestamp
		this.permalink = payload.permalink
		this.messageOpts = payload.messageOpts
	}

	get key() {
		return this._key
	}

	update() {
		this.persistedMessages.update(this)
	}
}

export abstract class PersistedSlackMessages {

	static get(botSettings: Settings, logger: ContextualLogger): PersistedSlackMessages {
		if (RoboArgs.useMongo()) {
			return new DBPersistedSlackMessages(botSettings.botname)
		}
		return new FilePersistedSlackMessages(botSettings, logger)
	}

    static generateKey(sourceCl: number, targetBranchArg: BranchArg, channel: string) {
        const branchName = resolveBranchArg(targetBranchArg, true)
        return `${sourceCl}:${branchName}:${channel}`
    }

    abstract add(messageOpts: SlackMessage, timestamp: string, permalink: string): void
	abstract find(cl: number, branchArg: BranchArg, channel: string): Promise<PersistedSlackMessage | undefined>
	abstract findAll(cl: number, branchArg: BranchArg): Promise<PersistedSlackMessage[]>
    abstract update(message: PersistedSlackMessage): void
}

class DBPersistedSlackMessages extends PersistedSlackMessages {

	private messageCache: {[key: string]: PersistedSlackMessage} = {}

	constructor(private botname: string) {
		super()		
	}

	private write(message: PersistedSlackMessage) {
		const index = {
			botname: this.botname,
			...message.key.toComponents()
		}
		return SlackMessagesModel.collection.updateOne(
			index,
			{$set:{
				...index, 
				timestamp: message.timestamp, 
				permalink: message.permalink, 
				message: message.messageOpts, 
				timestamp_forSunset: parseFloat(message.timestamp)
			}},
			{upsert:true}
		)
	}

    add(message: SlackMessage, timestamp: string, permalink: string) {
		const key: PersistedMessageKey = new PersistedMessageKey(message.cl!, message.target!, message.channel)
		const persistedMessage = new PersistedSlackMessage({timestamp, permalink, messageOpts: message}, key, this)
		this.messageCache[key.toString()] = persistedMessage
		this.write(persistedMessage)
    }

	async find(cl: number, branchArg: BranchArg, channel: string) {
		const key = new PersistedMessageKey(cl, resolveBranchArg(branchArg, true), channel)
		const cacheKey = key.toString()
		if (this.messageCache[cacheKey]) {
			return this.messageCache[cacheKey]
		}
		const index = {
			botname: this.botname,
			...key.toComponents()
		}
		const messageRecord = await SlackMessagesModel.find(index)
		if (messageRecord.length == 1) {
			const persistedMessage = new PersistedSlackMessage({
						messageOpts: messageRecord.message, 
						permalink: messageRecord.permalink,
						timestamp: messageRecord.timestamp
					}, key, this)
			this.messageCache[cacheKey] = persistedMessage
			return persistedMessage
		}
		else if (messageRecord.length > 1) {
			throw new Error(`Found multiple records for ${this.botname} ${PersistedSlackMessages.generateKey(index.cl, index.branchName, index.channel)}`)
		}

		return undefined
	}

	async findAll(cl: number, branchArg: BranchArg): Promise<PersistedSlackMessage[]> {
		const index = {
			botname: this.botname, 
			branchName: resolveBranchArg(branchArg, true),
			cl
		}
		const messageRecords = await SlackMessagesModel.find(index)
		const messages: PersistedSlackMessage[] = []

		for (const messageRecord of messageRecords) {
			const key = new PersistedMessageKey(cl, index.branchName, messageRecord.channel)
			const cacheKey = key.toString()
			if (!this.messageCache[cacheKey]) {
				const persistedMessage = new PersistedSlackMessage({
						messageOpts: messageRecord.message, 
						permalink: messageRecord.permalink,
						timestamp: messageRecord.timestamp
					}, key, this)
				this.messageCache[cacheKey] = persistedMessage
			}
			messages.push(this.messageCache[cacheKey])
		}

		return messages
	}

	update(message: PersistedSlackMessage) {
		this.write(message)
	}
}

export async function doDBPersistedMessagesHousekeeping() {
	const expiredTime = (Date.now() / 1000) - (SUNSET_PERSISTED_NOTIFICATIONS_DAYS * 60 * 60 * 24)
	return SlackMessagesModel.collection.deleteMany({timestamp_forSunset: { $lt: expiredTime }})
}

export async function migrateMessagesToDB(botSettings: Settings) {
	const filePersistedMessages = botSettings.getContext(NOTIFICATIONS_PERSISTENCE_KEY)?.get(SLACK_MESSAGES_PERSISTENCE_KEY)
	if (filePersistedMessages) {
		const dbPersistedMessages = new DBPersistedSlackMessages(botSettings.botname)
		await Promise.all(Object.keys(filePersistedMessages).map(key => {
			const message = filePersistedMessages[key]
			// Some persisted messages don't have the cl and channel populated, so do so now from the key
			const index = new PersistedMessageKey(key).toComponents()
			message.messageOpts.cl = index.cl
			message.messageOpts.target = index.branchName
			message.messageOpts.channel = index.channel
			return dbPersistedMessages.add(message.messageOpts, message.timestamp, message.permalink)
		}))
	}
	delete botSettings.object[NOTIFICATIONS_PERSISTENCE_KEY]
}

class FilePersistedSlackMessages extends PersistedSlackMessages {

	private persistence: Context

	constructor(botSettings: Settings, logger: ContextualLogger) {
		super()

		this.persistence = botSettings.getContext(NOTIFICATIONS_PERSISTENCE_KEY)

		if (this.persistence.get(SLACK_MESSAGES_PERSISTENCE_KEY)) {
			this.doHouseKeeping(logger)
		}
		else {
			this.persistence.set(SLACK_MESSAGES_PERSISTENCE_KEY, {})
		}
	}

	private write(key: string, messageOpts: SlackMessage, timestamp: string, permalink: string) {
		const persistedMessages: PersistedMessagePersistence = this.persistence.get(SLACK_MESSAGES_PERSISTENCE_KEY)
        persistedMessages[key] = {timestamp, permalink, messageOpts}
        this.persistence.set(SLACK_MESSAGES_PERSISTENCE_KEY, persistedMessages)
	}

    add(messageOpts: SlackMessage, timestamp: string, permalink: string) {
		const conflictKey = PersistedSlackMessages.generateKey(messageOpts.cl!, messageOpts.target!, messageOpts.channel)
		this.write(conflictKey, messageOpts, timestamp, permalink)
    }

	async find(cl: number, branchArg: BranchArg, channel: string) {
		// Get key to search our list of all messages
		const pms: PersistedMessagePersistence = this.persistence.get(SLACK_MESSAGES_PERSISTENCE_KEY)
		const conflictKey = PersistedSlackMessages.generateKey(cl, branchArg, channel)
		const persistedMessage = pms[conflictKey]
		if (persistedMessage) {
			return new PersistedSlackMessage(persistedMessage, new PersistedMessageKey(conflictKey), this)
		}
		return undefined
	}

	// 
	async findAll(cl: number, branchArg: BranchArg) {
		// Get key to search our list of all messages
		const persistedMessages: PersistedMessagePersistence = this.persistence.get(SLACK_MESSAGES_PERSISTENCE_KEY)
		const conflictKey = PersistedSlackMessages.generateKey(cl, branchArg, "") // pass empty string as channel name

		// Filter messages
		const messages: PersistedSlackMessage[] = []

		for (const key in persistedMessages) {
			if (key.startsWith(conflictKey)) {
				messages.push(new PersistedSlackMessage(persistedMessages[key], new PersistedMessageKey(key), this))
			}
		}

		return messages
	}

	update(message: PersistedSlackMessage) {
		this.write(message.key.toString(), message.messageOpts, message.timestamp, message.permalink)
	}	

	private doHouseKeeping(logger: ContextualLogger) {
		const expiredTime = (Date.now() / 1000) - (SUNSET_PERSISTED_NOTIFICATIONS_DAYS * 60 * 60 * 24)

		const persistedMessages: PersistedMessagePersistence = this.persistence.get(SLACK_MESSAGES_PERSISTENCE_KEY)

		let persist = false
		const keys = Object.keys(persistedMessages)
		for (const key of keys) {
			const message = persistedMessages[key]
			const messageTime = parseFloat(message.timestamp)

			// note: serialisation was briefly wrong (11/11/2019), leaving some message records without a message
			if (!message.messageOpts || messageTime < expiredTime) {
				logger.info(`Sunsetting Slack Message "${key}"`)
				delete persistedMessages[key]
				persist = true
			}
		}

		// if we removed any messages, persist
		if (persist) {
			this.persistence.set(SLACK_MESSAGES_PERSISTENCE_KEY, persistedMessages)
		}
	}
}