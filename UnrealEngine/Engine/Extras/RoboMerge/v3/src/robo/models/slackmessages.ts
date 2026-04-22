// Copyright Epic Games, Inc. All Rights Reserved.

var mongoose = require('mongoose');

/**
 * Bot schema
 */
var slackMessagesSchema = new mongoose.Schema({
  botname: { type: String, index: true },
  cl: { type: Number, index: true },
  branchName: { type: String, index: true},
  channel: { type: String, index: true },
  timestamp: { type: String },
  timestamp_forSunset: { type: Number },
  permalink: { type: String },
  message: {  }
});

module.exports = mongoose.model('SlackMessages', slackMessagesSchema);
