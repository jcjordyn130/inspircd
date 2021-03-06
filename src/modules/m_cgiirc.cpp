/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2009-2010 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2007-2008 John Brooks <john.brooks@dereferenced.net>
 *   Copyright (C) 2008 Pippijn van Steenhoven <pip88nl@gmail.com>
 *   Copyright (C) 2006-2008 Craig Edwards <craigedwards@brainbox.cc>
 *   Copyright (C) 2007 Robin Burchell <robin+git@viroteck.net>
 *   Copyright (C) 2007 Dennis Friis <peavey@inspircd.org>
 *   Copyright (C) 2006 Oliver Lupton <oliverlupton@gmail.com>
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


#include "inspircd.h"
#include "modules/dns.h"
#include "modules/ssl.h"

enum
{
	RPL_WHOISGATEWAY = 350
};

// We need this method up here so that it can be accessed from anywhere
static void ChangeIP(User* user, const std::string& newip)
{
	ServerInstance->Users->RemoveCloneCounts(user);
	user->SetClientIP(newip);
	ServerInstance->Users->AddClone(user);
}

// Encapsulates information about a WebIRC host.
class WebIRCHost
{
 private:
	std::string hostmask;
	std::string fingerprint;
	std::string password;
	std::string passhash;

 public:
	WebIRCHost(const std::string& mask, const std::string& fp, const std::string& pass, const std::string& hash)
		: hostmask(mask)
		, fingerprint(fp)
		, password(pass)
		, passhash(hash)
	{
	}

	bool Matches(LocalUser* user, const std::string& pass) const
	{
		// Did the user send a valid password?
		if (!password.empty() && !ServerInstance->PassCompare(user, password, pass, passhash))
			return false;

		// Does the user have a valid fingerprint?
		const std::string fp = SSLClientCert::GetFingerprint(&user->eh);
		if (!fingerprint.empty() && fp != fingerprint)
			return false;

		// Does the user's hostname match our hostmask?
		if (InspIRCd::Match(user->GetRealHost(), hostmask, ascii_case_insensitive_map))
			return true;

		// Does the user's IP address match our hostmask?
		return InspIRCd::MatchCIDR(user->GetIPString(), hostmask, ascii_case_insensitive_map);
	}
};

/*
 * WEBIRC
 *  This is used for the webirc method of CGIIRC auth, and is (really) the best way to do these things.
 *  Syntax: WEBIRC password gateway hostname ip
 *  Where password is a shared key, gateway is the name of the WebIRC gateway and version (e.g. cgiirc), hostname
 *  is the resolved host of the client issuing the command and IP is the real IP of the client.
 *
 * How it works:
 *  To tie in with the rest of cgiirc module, and to avoid race conditions, /webirc is only processed locally
 *  and simply sets metadata on the user, which is later decoded on full connect to give something meaningful.
 */
class CommandWebIRC : public SplitCommand
{
 public:
	std::vector<WebIRCHost> hosts;
	bool notify;
	StringExtItem gateway;
	StringExtItem realhost;
	StringExtItem realip;

	CommandWebIRC(Module* Creator)
		: SplitCommand(Creator, "WEBIRC", 4)
		, gateway("cgiirc_gateway", ExtensionItem::EXT_USER, Creator)
		, realhost("cgiirc_realhost", ExtensionItem::EXT_USER, Creator)
		, realip("cgiirc_realip", ExtensionItem::EXT_USER, Creator)
	{
		allow_empty_last_param = false;
		works_before_reg = true;
		this->syntax = "password gateway hostname ip";
	}

	CmdResult HandleLocal(const std::vector<std::string>& parameters, LocalUser* user) CXX11_OVERRIDE
	{
		if (user->registered == REG_ALL)
			return CMD_FAILURE;

		irc::sockets::sockaddrs ipaddr;
		if (!irc::sockets::aptosa(parameters[3], 0, ipaddr))
		{
			user->CommandFloodPenalty += 5000;
			ServerInstance->SNO->WriteGlobalSno('a', "Connecting user %s tried to use WEBIRC but gave an invalid IP address.", user->GetFullRealHost().c_str());
			return CMD_FAILURE;
		}

		for (std::vector<WebIRCHost>::const_iterator iter = hosts.begin(); iter != hosts.end(); ++iter)
		{
			// If we don't match the host then skip to the next host.
			if (!iter->Matches(user, parameters[0]))
				continue;

			// The user matched a WebIRC block!
			gateway.set(user, parameters[1]);
			realhost.set(user, user->GetRealHost());
			realip.set(user, user->GetIPString());

			if (notify)
				ServerInstance->SNO->WriteGlobalSno('w', "Connecting user %s is using a WebIRC gateway; changing their IP from %s to %s.",
					user->nick.c_str(), user->GetIPString().c_str(), parameters[3].c_str());

			// Set the IP address sent via WEBIRC. We ignore the hostname and lookup
			// instead do our own DNS lookups because of unreliable gateways.
			ChangeIP(user, parameters[3]);
			return CMD_SUCCESS;
		}

		user->CommandFloodPenalty += 5000;
		ServerInstance->SNO->WriteGlobalSno('w', "Connecting user %s tried to use WEBIRC but didn't match any configured WebIRC hosts.", user->GetFullRealHost().c_str());
		return CMD_FAILURE;
	}
};

class ModuleCgiIRC : public Module, public Whois::EventListener
{
	CommandWebIRC cmd;
	std::vector<std::string> hosts;

	static void RecheckClass(LocalUser* user)
	{
		user->MyClass = NULL;
		user->SetClass();
		user->CheckClass();
	}

	void HandleIdent(LocalUser* user, const std::string& newip)
	{
		cmd.realhost.set(user, user->GetRealHost());
		cmd.realip.set(user, user->GetIPString());

		if (cmd.notify)
			ServerInstance->SNO->WriteGlobalSno('w', "Connecting user %s is using an ident gateway; changing their IP from %s to %s.",
				user->nick.c_str(), user->GetIPString().c_str(), newip.c_str());

		ChangeIP(user, newip);
		RecheckClass(user);
	}

public:
	ModuleCgiIRC()
		: Whois::EventListener(this)
		, cmd(this)
	{
	}

	void init() CXX11_OVERRIDE
	{
		ServerInstance->SNO->EnableSnomask('w', "CGIIRC");
	}

	void ReadConfig(ConfigStatus& status) CXX11_OVERRIDE
	{
		std::vector<std::string> identhosts;
		std::vector<WebIRCHost> webirchosts;

		ConfigTagList tags = ServerInstance->Config->ConfTags("cgihost");
		for (ConfigIter i = tags.first; i != tags.second; ++i)
		{
			ConfigTag* tag = i->second;

			// Ensure that we have the <cgihost:mask> parameter.
			const std::string mask = tag->getString("mask");
			if (mask.empty())
				throw ModuleException("<cgihost:mask> is a mandatory field, at " + tag->getTagLocation());

			// Determine what lookup type this host uses.
			const std::string type = tag->getString("type");
			if (stdalgo::string::equalsci(type, "ident"))
			{
				// The IP address should be looked up from the hex IP address.
				identhosts.push_back(mask);
			}
			else if (stdalgo::string::equalsci(type, "webirc"))
			{
				// The IP address will be received via the WEBIRC command.
				const std::string fingerprint = tag->getString("fingerprint");
				const std::string password = tag->getString("password");

				// WebIRC blocks require a password.
				if (fingerprint.empty() && password.empty())
					throw ModuleException("When using <cgihost type=\"webirc\"> either the fingerprint or password field is required, at " + tag->getTagLocation());

				webirchosts.push_back(WebIRCHost(mask, fingerprint, password, tag->getString("hash")));
			}
			else
			{
				throw ModuleException(type + " is an invalid <cgihost:mask> type, at " + tag->getTagLocation()); 
			}
		}

		// The host configuration was valid so we can apply it.
		hosts.swap(identhosts);
		cmd.hosts.swap(webirchosts);

		// Do we send an oper notice when a m_cgiirc client has their IP changed?
		cmd.notify = ServerInstance->Config->ConfValue("cgiirc")->getBool("opernotice", true);
	}

	ModResult OnCheckReady(LocalUser *user) CXX11_OVERRIDE
	{
		if (!cmd.realip.get(user))
			return MOD_RES_PASSTHRU;

		RecheckClass(user);
		if (user->quitting)
			return MOD_RES_DENY;

		user->CheckLines(true);
		if (user->quitting)
			return MOD_RES_DENY;

		return MOD_RES_PASSTHRU;
	}

	ModResult OnSetConnectClass(LocalUser* user, ConnectClass* myclass) CXX11_OVERRIDE
	{
		// If <connect:webirc> is not set then we have nothing to do.
		const std::string webirc = myclass->config->getString("webirc");
		if (webirc.empty())
			return MOD_RES_PASSTHRU;

		// If the user is not connecting via a WebIRC gateway then they
		// cannot match this connect class.
		const std::string* gateway = cmd.gateway.get(user);
		if (!gateway)
			return MOD_RES_DENY;

		// If the gateway matches the <connect:webirc> constraint then
		// allow the check to continue. Otherwise, reject it.
		return InspIRCd::Match(*gateway, webirc) ? MOD_RES_PASSTHRU : MOD_RES_DENY;
	}

	ModResult OnUserRegister(LocalUser* user) CXX11_OVERRIDE
	{
		for (std::vector<std::string>::const_iterator iter = hosts.begin(); iter != hosts.end(); ++iter)
		{
			if (!InspIRCd::Match(user->GetRealHost(), *iter, ascii_case_insensitive_map) && !InspIRCd::MatchCIDR(user->GetIPString(), *iter, ascii_case_insensitive_map))
				continue;

			CheckIdent(user); // Nothing on failure.
			user->CheckLines(true);
			break;
		}
		return MOD_RES_PASSTHRU;
	}

	void OnWhois(Whois::Context& whois) CXX11_OVERRIDE
	{
		if (!whois.IsSelfWhois() && !whois.GetSource()->HasPrivPermission("users/auspex"))
			return;

		// If these fields are not set then the client is not using a gateway.
		const std::string* realhost = cmd.realhost.get(whois.GetTarget());
		const std::string* realip = cmd.realip.get(whois.GetTarget());
		if (!realhost || !realip)
			return;

		const std::string* gateway = cmd.gateway.get(whois.GetTarget());
		if (gateway)
			whois.SendLine(RPL_WHOISGATEWAY, *realhost, *realip, "is connected via the " + *gateway + " WebIRC gateway");
		else
			whois.SendLine(RPL_WHOISGATEWAY, *realhost, *realip, "is connected via an ident gateway");
	}

	bool CheckIdent(LocalUser* user)
	{
		const char* ident;
		in_addr newip;

		if (user->ident.length() == 8)
			ident = user->ident.c_str();
		else if (user->ident.length() == 9 && user->ident[0] == '~')
			ident = user->ident.c_str() + 1;
		else
			return false;

		errno = 0;
		unsigned long ipaddr = strtoul(ident, NULL, 16);
		if (errno)
			return false;
		newip.s_addr = htonl(ipaddr);
		std::string newipstr(inet_ntoa(newip));

		user->ident = "~cgiirc";
		HandleIdent(user, newipstr);

		return true;
	}

	Version GetVersion() CXX11_OVERRIDE
	{
		return Version("Enables forwarding the real IP address of a user from a gateway to the IRC server", VF_VENDOR);
	}
};

MODULE_INIT(ModuleCgiIRC)
