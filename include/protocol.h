/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2008 Robin Burchell <robin+git@viroteck.net>
 *   Copyright (C) 2008 Craig Edwards <craigedwards@brainbox.cc>
 *
 * This file is part of InspIRCd.  InspIRCd is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#pragma once

#include "hashcomp.h"

class User;

typedef std::vector<std::string> parameterlist;

class ProtoServer
{
 public:
	std::string servername;
	std::string parentname;
	std::string gecos;
	unsigned int usercount;
	unsigned int opercount;
	unsigned int latencyms;
};

typedef std::list<ProtoServer> ProtoServerList;

class ProtocolInterface
{
 public:
	ProtocolInterface() { }
	virtual ~ProtocolInterface() { }

	/** Send an ENCAP message to one or more linked servers.
	 * See the protocol documentation for the purpose of ENCAP.
	 * @param encap This is a list of string parameters, the first of which must be a server ID or glob matching servernames.
	 * The second must be a subcommand. All subsequent parameters are dependant on the subcommand.
	 * ENCAP (should) be used instead of creating new protocol messages for easier third party application support.
	 * @return True if the message was sent out (target exists)
	 */
	virtual bool SendEncapsulatedData(const parameterlist &encap) { return false; }

	/** Send metadata for an object to other linked servers.
	 * @param target The object to send metadata for.
	 * @param key The 'key' of the data, e.g. "swhois" for swhois desc on a user
	 * @param data The string representation of the data
	 */
	virtual void SendMetaData(Extensible* target, const std::string &key, const std::string &data) { }

	/** Send a topic change for a channel
	 * @param channel The channel to change the topic for.
	 * @param topic The new topic to use for the channel.
	 */
	virtual void SendTopic(Channel* channel, std::string &topic) { }

	/** Send mode changes for an object.
	 * @param source The source of the mode change
	 * @param usertarget The target user, NULL if the target is a channel
	 * @param chantarget The target channel, NULL if the target is a user
	 * @param modedata The mode changes to send.
	 * @param translate A list of translation types
	 */
	virtual void SendMode(User* source, User* usertarget, Channel* chantarget, const parameterlist& modedata, const std::vector<TranslateType>& translate) { }

	/** Send a notice to users with a given snomask.
	 * @param snomask The snomask required for the message to be sent.
	 * @param text The message to send.
	 */
	virtual void SendSNONotice(const std::string &snomask, const std::string &text) { }

	/** Send raw data to a remote client.
	 * @param target The user to push data to.
	 * @param rawline The raw IRC protocol line to deliver (":me NOTICE you :foo", whatever).
	 */
	virtual void PushToClient(User* target, const std::string &rawline) { }

	/** Send a message to a channel.
	 * @param target The channel to message.
	 * @param status The status character (e.g. %) required to recieve.
	 * @param text The message to send.
	 */
	virtual void SendChannelPrivmsg(Channel* target, char status, const std::string &text) { }

	/** Send a notice to a channel.
	 * @param target The channel to message.
	 * @param status The status character (e.g. %) required to recieve.
	 * @param text The message to send.
	 */
	virtual void SendChannelNotice(Channel* target, char status, const std::string &text) { }

	/** Send a message to a user.
	 * @param target The user to message.
	 * @param text The message to send.
	 */
	virtual void SendUserPrivmsg(User* target, const std::string &text) { }

	/** Send a notice to a user.
	 * @param target The user to message.
	 * @param text The message to send.
	 */
	virtual void SendUserNotice(User* target, const std::string &text) { }

	/** Fill a list of servers and information about them.
	 * @param sl The list of servers to fill.
	 * XXX: document me properly, this is shit.
	 */
	virtual void GetServerList(ProtoServerList &sl) { }
};
