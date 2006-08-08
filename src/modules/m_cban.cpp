/*   +------------------------------------+
 *   | Inspire Internet Relay Chat Daemon |
 *   +------------------------------------+
 *
 *  InspIRCd is copyright (C) 2002-2006 ChatSpike-Dev.
 *   E-mail:
 *<brain@chatspike.net>
 *   	  <Craig@chatspike.net>
 *						<omster@gmail.com>
 * 
 * Written by Craig Edwards, Craig McLure, and others.
 * This program is free but copyrighted software; see
 *the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#include <algorithm>
#include <vector>
#include <string>
#include <sstream>
#include "users.h"
#include "channels.h"
#include "modules.h"
#include "helperfuncs.h"
#include "hashcomp.h"

/* $ModDesc: Gives /cban, aka C:lines. Think Q:lines, for channels. */

class CBan : public classbase
{
public:
	irc::string chname;
	std::string set_by;
	time_t set_on;
	long length;
	std::string reason;

	CBan()
	{
	}

	CBan(irc::string cn, std::string sb, time_t so, long ln, std::string rs) : chname(cn), set_by(sb), set_on(so), length(ln), reason(rs)
	{
	}
};

std::string EncodeCBan(const CBan &ban);
CBan DecodeCBan(const std::string &data);
bool CBanComp(const CBan &ban1, const CBan &ban2);
void ExpireBans();

extern time_t TIME;
typedef std::vector<CBan> cbanlist;

/* cbans is declared here, as our type is right above. Don't try move it. */
cbanlist cbans;

class cmd_cban : public command_t
{
 private:
	Server *Srv;

 public:
	cmd_cban(Server* Me) : command_t("CBAN", 'o', 1)
	{
		this->source = "m_cban.so";
		this->Srv = Me;
		syntax = "<channel> [<duration> :<reason>]";
	}

	void Handle(const char** parameters, int pcnt, userrec *user)
	{
		/* syntax: CBAN #channel time :reason goes here */
		/* 'time' is a human-readable timestring, like 2d3h2s. */
		
		ExpireBans();

		if(pcnt == 1)
		{
			/* form: CBAN #channel removes a CBAN */
			for (cbanlist::iterator iter = cbans.begin(); iter != cbans.end(); iter++)
			{
				if (parameters[0] == iter->chname)
				{
					unsigned long remaining = (iter->set_on + iter->length) - TIME;
					user->WriteServ( "386 %s %s :Removed CBAN with %lu seconds left before expiry (%s)", user->nick, iter->chname.c_str(), remaining, iter->reason.c_str());
					cbans.erase(iter);
					break;
				}
			}
		}
		else if (pcnt >= 2)
		{
			/* full form to add a CBAN */
			if (IsValidChannelName(parameters[0]))
			{
				// parameters[0] = #channel
				// parameters[1] = 1h3m2s
				// parameters[2] = Tortoise abuser
				long length = Srv->CalcDuration(parameters[1]);
				std::string reason = (pcnt > 2) ? parameters[2] : "No reason supplied";
				
				cbans.push_back(CBan(parameters[0], user->nick, TIME, length, reason));
					
				std::sort(cbans.begin(), cbans.end(), CBanComp);
				
				if(length > 0)
				{
					user->WriteServ( "385 %s %s :Added %lu second channel ban (%s)", user->nick, parameters[0], length, reason.c_str());
					WriteOpers("*** %s added %lu second channel ban on %s (%s)", user->nick, length, parameters[0], reason.c_str());
				}
				else
				{
					user->WriteServ( "385 %s %s :Added permenant channel ban (%s)", user->nick, parameters[0], reason.c_str());
					WriteOpers("*** %s added permenant channel ban on %s (%s)", user->nick, parameters[0], reason.c_str());
				}
			}
			else
			{
				user->WriteServ( "403 %s %s :Invalid channel name", user->nick, parameters[0]);
			}
		}
	}
};

class ModuleCBan : public Module
{
	cmd_cban* mycommand;
	Server* Srv;

 public:
	ModuleCBan(Server* Me) : Module::Module(Me)
	{
		Srv = Me;
		mycommand = new cmd_cban(Srv);
		Srv->AddCommand(mycommand);
	}

	void Implements(char* List)
	{
		List[I_OnUserPreJoin] = List[I_OnSyncOtherMetaData] = List[I_OnDecodeMetaData] = List[I_OnStats] = 1;
	}
	
	virtual int OnStats(char symbol, userrec* user, string_list &results)
	{
		ExpireBans();
	
		if(symbol == 'C')
		{
			for(cbanlist::iterator iter = cbans.begin(); iter != cbans.end(); iter++)
			{
				unsigned long remaining = (iter->set_on + iter->length) - TIME;
				results.push_back(Srv->GetServerName()+" 210 "+user->nick+" "+iter->chname.c_str()+" "+iter->set_by+" "+ConvToStr(iter->set_on)+" "+ConvToStr(iter->length)+" "+ConvToStr(remaining)+" :"+iter->reason);
			}
		}
		
		return 0;
	}

	virtual int OnUserPreJoin(userrec *user, chanrec *chan, const char *cname)
	{
		ExpireBans();
	
		/* check cbans in here, and apply as necessary. */
		for(cbanlist::iterator iter = cbans.begin(); iter != cbans.end(); iter++)
		{
			if(iter->chname == cname && !user->modes[UM_OPERATOR])
			{
				// Channel is banned.
				user->WriteServ( "384 %s %s :Cannot join channel, CBANed (%s)", user->nick, cname, iter->reason.c_str());
				WriteOpers("*** %s tried to join %s which is CBANed (%s)", user->nick, cname, iter->reason.c_str());
				return 1;
			}
		}
		return 0;
	}
	
	virtual void OnSyncOtherMetaData(Module* proto, void* opaque)
	{
		for(cbanlist::iterator iter = cbans.begin(); iter != cbans.end(); iter++)
		{
			proto->ProtoSendMetaData(opaque, TYPE_OTHER, NULL, "cban", EncodeCBan(*iter));
		}
	}
	
	virtual void OnDecodeMetaData(int target_type, void* target, const std::string &extname, const std::string &extdata)
	{
		if((target_type == TYPE_OTHER) && (extname == "cban"))
		{
			cbans.push_back(DecodeCBan(extdata));
			std::sort(cbans.begin(), cbans.end(), CBanComp);
		}
	}

	virtual ~ModuleCBan()
	{
	}
	
	virtual Version GetVersion()
	{
		return Version(1,0,0,1,VF_VENDOR);
	}
};

std::string EncodeCBan(const CBan &ban)
{
	std::ostringstream stream;	
	stream << ban.chname << " " << ban.set_by << " " << ban.set_on << " " << ban.length << " " << ban.reason;
	return stream.str();	
}

CBan DecodeCBan(const std::string &data)
{
	CBan res;
	std::istringstream stream(data);
	stream >> res.chname;
	stream >> res.set_by;
	stream >> res.set_on;
	stream >> res.length;
	res.reason = stream.str();
	
	return res;
}

bool CBanComp(const CBan &ban1, const CBan &ban2)
{
	return ((ban1.set_on + ban1.length) < (ban2.set_on + ban2.length));
}

void ExpireBans()
{
	bool go_again = true;

	while (go_again)
	{
		go_again = false;

		for (cbanlist::iterator iter = cbans.begin(); iter != cbans.end(); iter++)
		{
			/* 0 == permanent, don't mess with them! -- w00t */
			if (iter->length != 0)
			{
				if (iter->set_on + iter->length <= TIME)
				{
					log(DEBUG, "m_cban.so: Ban on %s expired, removing...", iter->chname.c_str());
					WriteOpers("*** %li second CBAN on %s (%s) set %u seconds ago expired", iter->length, iter->chname.c_str(), iter->reason.c_str(), TIME - iter->set_on);
					cbans.erase(iter);
					go_again = true;
				}
			}

			if (go_again == true)
				break;
		}
	}
}

class ModuleCBanFactory : public ModuleFactory
{
 public:
	ModuleCBanFactory()
	{
	}
	
	~ModuleCBanFactory()
	{
	}
	
	virtual Module * CreateModule(Server* Me)
	{
		return new ModuleCBan(Me);
	}
	
};


extern "C" void * init_module( void )
{
	return new ModuleCBanFactory;
}
