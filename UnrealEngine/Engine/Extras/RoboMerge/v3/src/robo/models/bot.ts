// Copyright Epic Games, Inc. All Rights Reserved.

var mongoose = require('mongoose');

/**
 * Bot schema
 */
var botSchema = new mongoose.Schema({
  botname: { type: String, index: true },
  details: {  }
});

module.exports = mongoose.model('Bot', botSchema);
