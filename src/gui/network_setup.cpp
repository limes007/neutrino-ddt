/*
	$port: network_setup.cpp,v 1.14 2010/07/01 11:44:19 tuxbox-cvs Exp $

	network setup implementation - Neutrino-GUI

	Copyright (C) 2001 Steffen Hehn 'McClean'
	and some other guys
	Homepage: http://dbox.cyberphoria.org/

	Copyright (C) 2009 T. Graf 'dbt'
	Homepage: http://www.dbox2-tuning.net/

	License: GPL

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <dirent.h>

#include "network_setup.h"
#include <gui/proxyserver_setup.h>
#include <gui/nfs.h>

#include <gui/widget/icons.h>
#include <gui/widget/stringinput.h>
#include <gui/widget/stringinput_ext.h>
#include <gui/widget/keyboard_input.h>
#include <gui/widget/hintbox.h>
#include <gui/widget/msgbox.h>

#include <gui/network_service.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <global.h>
#include <neutrino.h>
#include <mymenu.h>
#include <neutrino_menue.h>

#include <driver/screen_max.h>

#include <system/debug.h>

#include <libnet.h>
#include <libiw/iwscan.h>

extern int pinghost (const std::string &hostname, std::string *ip = NULL);

CNetworkSetup::CNetworkSetup(int wizard_mode)
{
	networkConfig = CNetworkConfig::getInstance();

	is_wizard = wizard_mode;

	width = 40;

	//readNetworkSettings();
}

CNetworkSetup::~CNetworkSetup()
{
	//delete networkConfig;
}

CNetworkSetup* CNetworkSetup::getInstance()
{
	static CNetworkSetup* me = NULL;

	if(!me) {
		me = new CNetworkSetup();
		dprintf(DEBUG_NORMAL, "[CNetworkSetup]\t[%s - %d],  Instance created\n", __func__, __LINE__);
	}
	return me;
}

int CNetworkSetup::exec(CMenuTarget* parent, const std::string &actionKey)
{
	int   res = menu_return::RETURN_REPAINT;

	if (parent)
		parent->hide();

	if(actionKey=="networkapply")
	{
		applyNetworkSettings();
		readNetworkSettings();
		backupNetworkSettings();
		return res;
	}
	else if(actionKey=="networktest")
	{
		testNetworkSettings();
		return res;
	}
	else if(actionKey=="networkshow")
	{
		showCurrentNetworkSettings();
		return res;
	}
	else if(actionKey=="scanssid")
	{
		return showWlanList();
	}
	else if(actionKey=="restore")
	{
		int result =  	ShowMsg(LOCALE_MAINSETTINGS_NETWORK, g_Locale->getText(LOCALE_NETWORKMENU_RESET_SETTINGS_NOW), CMsgBox::mbrNo,
				CMsgBox::mbYes |
				CMsgBox::mbNo ,
				NEUTRINO_ICON_QUESTION,
				width);

		if (result ==  CMsgBox::mbrYes) {
			restoreNetworkSettings();
		}
		return res;
	}

	dprintf(DEBUG_NORMAL, "[CNetworkSetup]\t[%s - %d], init network setup...\n", __func__, __LINE__);
	res = showNetworkSetup();

	return res;
}

void CNetworkSetup::readNetworkSettings()
{
	network_automatic_start = networkConfig->automatic_start;
	network_dhcp 		= networkConfig->inet_static ? NETWORK_DHCP_OFF : NETWORK_DHCP_ON;

	network_address		= networkConfig->address;
	network_netmask		= networkConfig->netmask;
	network_broadcast	= networkConfig->broadcast;
	network_nameserver	= networkConfig->nameserver;
	network_gateway		= networkConfig->gateway;
	network_hostname	= networkConfig->hostname;
	mac_addr		= networkConfig->mac_addr;
	network_ssid		= networkConfig->ssid;
	network_key		= networkConfig->key;
}

void CNetworkSetup::backupNetworkSettings()
{
	old_network_automatic_start 	= networkConfig->automatic_start;
	old_network_dhcp 		= networkConfig->inet_static ? NETWORK_DHCP_OFF : NETWORK_DHCP_ON;

	old_network_address		= networkConfig->address;
	old_network_netmask		= networkConfig->netmask;
	old_network_broadcast		= networkConfig->broadcast;
	old_network_nameserver		= networkConfig->nameserver;
	old_network_gateway		= networkConfig->gateway;
	old_network_hostname		= networkConfig->hostname;
	old_network_ssid		= networkConfig->ssid;
	old_network_key			= networkConfig->key;
	old_ifname 			= g_settings.ifname;
	old_mac_addr			= mac_addr;
}

#define OPTIONS_NTPENABLE_OPTION_COUNT 2
const CMenuOptionChooser::keyval OPTIONS_NTPENABLE_OPTIONS[OPTIONS_NTPENABLE_OPTION_COUNT] =
{
	{ CNetworkSetup::NETWORK_NTP_OFF, LOCALE_OPTIONS_NTP_OFF },
	{ CNetworkSetup::NETWORK_NTP_ON, LOCALE_OPTIONS_NTP_ON }
};

static int my_filter(const struct dirent * dent)
{
	if(dent->d_name[0] == 'l' && dent->d_name[1] == 'o')
		return 0;
	if(dent->d_name[0] == '.')
		return 0;
	return 1;
}

void CNetworkSetup::setBroadcast(void)
{
	in_addr_t na = inet_addr(network_address.c_str());
	in_addr_t nm = inet_addr(network_netmask.c_str());
	struct in_addr in;
	in.s_addr = na | ~nm;
	char tmp[40];
	network_broadcast = (inet_ntop(AF_INET, &in, tmp, sizeof(tmp))) ? std::string(tmp) : "0.0.0.0";
}

int CNetworkSetup::showNetworkSetup()
{
	struct dirent **namelist;

	//if select

	int ifcount = scandir("/sys/class/net", &namelist, my_filter, alphasort);

	CMenuOptionStringChooser * ifSelect = new CMenuOptionStringChooser(LOCALE_NETWORKMENU_SELECT_IF, &g_settings.ifname, ifcount > 1, this, CRCInput::RC_nokey, "", true);
	ifSelect->setHint("", LOCALE_MENU_HINT_NET_IF);

	bool found = false;

	for(int i = 0; i < ifcount; i++) {
		ifSelect->addOption(namelist[i]->d_name);
		if(strcmp(g_settings.ifname.c_str(), namelist[i]->d_name) == 0)
			found = true;
		free(namelist[i]);
	}

	if (ifcount >= 0)
		free(namelist);

	if(!found)
		g_settings.ifname = "eth0";

	networkConfig->readConfig(g_settings.ifname);
	readNetworkSettings();
	backupNetworkSettings();

	//menue init
	CMenuWidget* networkSettings = new CMenuWidget(LOCALE_MAINSETTINGS_HEAD, NEUTRINO_ICON_NETWORK, width, MN_WIDGET_ID_NETWORKSETUP);
	networkSettings->setWizardMode(is_wizard);

	//apply button
	CMenuForwarder *m0 = new CMenuForwarder(LOCALE_NETWORKMENU_SETUPNOW, true, NULL, this, "networkapply", CRCInput::RC_red);
	m0->setHint("", LOCALE_MENU_HINT_NET_SETUPNOW);

	//eth id
	CMenuForwarder *mac = new CMenuForwarder("MAC", false, mac_addr);

	//prepare input entries
	CIPInput networkSettings_NetworkIP(LOCALE_NETWORKMENU_IPADDRESS  , &network_address   , LOCALE_IPSETUP_HINT_1, LOCALE_IPSETUP_HINT_2, this);
	CIPInput networkSettings_NetMask  (LOCALE_NETWORKMENU_NETMASK    , &network_netmask   , LOCALE_IPSETUP_HINT_1, LOCALE_IPSETUP_HINT_2, this);
	CIPInput networkSettings_Gateway  (LOCALE_NETWORKMENU_GATEWAY    , &network_gateway   , LOCALE_IPSETUP_HINT_1, LOCALE_IPSETUP_HINT_2);
	CIPInput networkSettings_NameServer(LOCALE_NETWORKMENU_NAMESERVER, &network_nameserver, LOCALE_IPSETUP_HINT_1, LOCALE_IPSETUP_HINT_2);

	//hostname
	CKeyboardInput networkSettings_Hostname(LOCALE_NETWORKMENU_HOSTNAME, &network_hostname, 0, NULL, NULL, LOCALE_NETWORKMENU_HOSTNAME_HINT1, LOCALE_NETWORKMENU_HOSTNAME_HINT2);

	//auto start
	CMenuOptionChooser* o1 = new CMenuOptionChooser(LOCALE_NETWORKMENU_SETUPONSTARTUP, &network_automatic_start, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true);
	o1->setHint("", LOCALE_MENU_HINT_NET_SETUPONSTARTUP);

	//dhcp
	network_dhcp 	= networkConfig->inet_static ? NETWORK_DHCP_OFF : NETWORK_DHCP_ON;

	CMenuForwarder *m1 = new CMenuForwarder(LOCALE_NETWORKMENU_IPADDRESS , networkConfig->inet_static, network_address   , &networkSettings_NetworkIP );
	CMenuForwarder *m2 = new CMenuForwarder(LOCALE_NETWORKMENU_NETMASK   , networkConfig->inet_static, network_netmask   , &networkSettings_NetMask   );
	setBroadcast();
	CMenuForwarder *m3 = new CMenuForwarder(LOCALE_NETWORKMENU_BROADCAST , false,                      network_broadcast);
	CMenuForwarder *m4 = new CMenuForwarder(LOCALE_NETWORKMENU_GATEWAY   , networkConfig->inet_static, network_gateway   , &networkSettings_Gateway   );
	CMenuForwarder *m5 = new CMenuForwarder(LOCALE_NETWORKMENU_NAMESERVER, networkConfig->inet_static, network_nameserver, &networkSettings_NameServer);
	CMenuForwarder *m8 = new CMenuForwarder(LOCALE_NETWORKMENU_HOSTNAME  , true , network_hostname , &networkSettings_Hostname  );

	m1->setHint("", LOCALE_MENU_HINT_NET_IPADDRESS);
	m2->setHint("", LOCALE_MENU_HINT_NET_NETMASK);
	m3->setHint("", LOCALE_MENU_HINT_NET_BROADCAST);
	m4->setHint("", LOCALE_MENU_HINT_NET_GATEWAY);
	m5->setHint("", LOCALE_MENU_HINT_NET_NAMESERVER);
	m8->setHint("", LOCALE_MENU_HINT_NET_HOSTNAME);

	dhcpDisable.Add(m1);
	dhcpDisable.Add(m2);
	dhcpDisable.Add(m3);
	dhcpDisable.Add(m4);
	dhcpDisable.Add(m5);

	CMenuOptionChooser* o2 = new CMenuOptionChooser(LOCALE_NETWORKMENU_DHCP, &network_dhcp, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true, this);
	o2->setHint("", LOCALE_MENU_HINT_NET_DHCP);

	//paint menu items
	networkSettings->addIntroItems(LOCALE_MAINSETTINGS_NETWORK); //intros
	//-------------------------------------------------
	networkSettings->addItem( m0 ); //apply
	CMenuForwarder * mf = new CMenuForwarder(LOCALE_NETWORKMENU_TEST, true, NULL, this, "networktest", CRCInput::RC_green);
	mf->setHint("", LOCALE_MENU_HINT_NET_TEST);
	networkSettings->addItem(mf); //test

	mf = new CMenuForwarder(LOCALE_NETWORKMENU_SHOW, true, NULL, this, "networkshow", CRCInput::RC_info);
	mf->setHint("", LOCALE_MENU_HINT_NET_SHOW);
	networkSettings->addItem(mf);	//show settings

	networkSettings->addItem(GenericMenuSeparatorLine);
	//------------------------------------------------
	if(ifcount)
		networkSettings->addItem(ifSelect);	//if select
	else
		delete ifSelect;

	networkSettings->addItem(o1);	//set on start
	networkSettings->addItem(GenericMenuSeparatorLine);
	//------------------------------------------------
	if(ifcount > 1) // if there is only one, its probably wired
	{
		//ssid
		CKeyboardInput * networkSettings_ssid = new CKeyboardInput(LOCALE_NETWORKMENU_SSID, &network_ssid);
		//key
		CKeyboardInput * networkSettings_key = new CKeyboardInput(LOCALE_NETWORKMENU_PASSWORD, &network_key);
		CMenuForwarder *m9 = new CMenuDForwarder(LOCALE_NETWORKMENU_SSID      , networkConfig->wireless, network_ssid , networkSettings_ssid );
		CMenuForwarder *m10 = new CMenuDForwarder(LOCALE_NETWORKMENU_PASSWORD , networkConfig->wireless, network_key , networkSettings_key );
		CMenuForwarder *m11 = new CMenuForwarder(LOCALE_NETWORKMENU_SSID_SCAN , networkConfig->wireless, NULL, this, "scanssid");

		m9->setHint("", LOCALE_MENU_HINT_NET_SSID);
		m10->setHint("", LOCALE_MENU_HINT_NET_PASS);
		m11->setHint("", LOCALE_MENU_HINT_NET_SSID_SCAN);

		wlanEnable.Add(m9);
		wlanEnable.Add(m10);
		wlanEnable.Add(m11);

		networkSettings->addItem( m11);	//ssid scan
		networkSettings->addItem( m9);	//ssid
		networkSettings->addItem( m10);	//key
		networkSettings->addItem(GenericMenuSeparatorLine);
	}
	//------------------------------------------------
	networkSettings->addItem(mac);	//eth id
	networkSettings->addItem(GenericMenuSeparatorLine);
	//-------------------------------------------------
	networkSettings->addItem(o2);	//dhcp on/off
	networkSettings->addItem( m8);	//hostname
	networkSettings->addItem(GenericMenuSeparatorLine);
	//-------------------------------------------------
	networkSettings->addItem( m1);	//adress
	networkSettings->addItem( m2);	//mask
	networkSettings->addItem( m3);	//broadcast
	networkSettings->addItem(GenericMenuSeparatorLine);
	//------------------------------------------------
	networkSettings->addItem( m4);	//gateway
	networkSettings->addItem( m5);	//nameserver
	//------------------------------------------------
	sectionsdConfigNotifier = NULL;
	CMenuWidget ntp(LOCALE_MAINSETTINGS_NETWORK, NEUTRINO_ICON_NETWORK, width, MN_WIDGET_ID_NETWORKSETUP_NTP);
#ifdef ENABLE_GUI_MOUNT
	CMenuWidget networkmounts(LOCALE_MAINSETTINGS_NETWORK, NEUTRINO_ICON_NETWORK, width, MN_WIDGET_ID_NETWORKSETUP_MOUNTS);
#endif
	CProxySetup proxy(LOCALE_MAINSETTINGS_NETWORK);
	CNetworkServiceSetup services;

	//ntp submenu
	sectionsdConfigNotifier = new CSectionsdConfigNotifier;
	mf = new CMenuForwarder(LOCALE_NETWORKMENU_NTPTITLE, true, NULL, &ntp, NULL, CRCInput::RC_yellow);
	mf->setHint("", LOCALE_MENU_HINT_NET_NTP);
	networkSettings->addItem(mf);

	showNetworkNTPSetup(&ntp);

#ifdef ENABLE_GUI_MOUNT
	//nfs mount submenu
	mf = new CMenuForwarder(LOCALE_NETWORKMENU_MOUNT, true, NULL, &networkmounts, NULL, CRCInput::RC_blue);
	mf->setHint("", LOCALE_MENU_HINT_NET_MOUNT);
	networkSettings->addItem(mf);
	showNetworkNFSMounts(&networkmounts);
#endif

	//proxyserver submenu
	mf = new CMenuForwarder(LOCALE_FLASHUPDATE_PROXYSERVER_SEP, true, NULL, &proxy, NULL, CRCInput::RC_0);
	mf->setHint("", LOCALE_MENU_HINT_NET_PROXY);
	networkSettings->addItem(mf);
#if 1
	//services
	mf = new CMenuForwarder(LOCALE_NETWORKMENU_SERVICES, true, NULL, &services, NULL, CRCInput::RC_1);
	mf->setHint("", LOCALE_MENU_HINT_NET_SERVICES);
	networkSettings->addItem(mf);
#endif
	int ret = 0;
	while(true) {
		int res = menu_return::RETURN_EXIT;
		ret = networkSettings->exec(NULL, "");

		if (settingsChanged())
			res = saveChangesDialog();
		if(res == menu_return::RETURN_EXIT)
			break;
	}

	dhcpDisable.Clear();
	wlanEnable.Clear();
	delete networkSettings;
	delete sectionsdConfigNotifier;
	return ret;
}

void CNetworkSetup::showNetworkNTPSetup(CMenuWidget *menu_ntp)
{
	//prepare ntp input
	CKeyboardInput * networkSettings_NtpServer = new CKeyboardInput(LOCALE_NETWORKMENU_NTPSERVER, &g_settings.network_ntpserver, 0, sectionsdConfigNotifier, NULL, LOCALE_NETWORKMENU_NTPSERVER_HINT1, LOCALE_NETWORKMENU_NTPSERVER_HINT2);

	CStringInput * networkSettings_NtpRefresh = new CStringInput(LOCALE_NETWORKMENU_NTPREFRESH, &g_settings.network_ntprefresh, 3,LOCALE_NETWORKMENU_NTPREFRESH_HINT1, LOCALE_NETWORKMENU_NTPREFRESH_HINT2 , "0123456789 ", sectionsdConfigNotifier);

	CMenuOptionChooser *ntp1 = new CMenuOptionChooser(LOCALE_NETWORKMENU_NTPENABLE, &g_settings.network_ntpenable, OPTIONS_NTPENABLE_OPTIONS, OPTIONS_NTPENABLE_OPTION_COUNT, true, sectionsdConfigNotifier);
	CMenuForwarder *ntp2 = new CMenuDForwarder( LOCALE_NETWORKMENU_NTPSERVER, true , g_settings.network_ntpserver, networkSettings_NtpServer );
	CMenuForwarder *ntp3 = new CMenuDForwarder( LOCALE_NETWORKMENU_NTPREFRESH, true , g_settings.network_ntprefresh, networkSettings_NtpRefresh );

	ntp1->setHint("", LOCALE_MENU_HINT_NET_NTPENABLE);
	ntp2->setHint("", LOCALE_MENU_HINT_NET_NTPSERVER);
	ntp3->setHint("", LOCALE_MENU_HINT_NET_NTPREFRESH);

	menu_ntp->addIntroItems(LOCALE_NETWORKMENU_NTPTITLE);
	menu_ntp->addItem(ntp1);
	menu_ntp->addItem(ntp2);
	menu_ntp->addItem(ntp3);
}

#ifdef ENABLE_GUI_MOUNT
void CNetworkSetup::showNetworkNFSMounts(CMenuWidget *menu_nfs)
{
	menu_nfs->addIntroItems(LOCALE_NETWORKMENU_MOUNT);
	CMenuForwarder * mf = new CMenuDForwarder(LOCALE_NFS_MOUNT , true, NULL, new CNFSMountGui(), NULL, CRCInput::RC_red);
	mf->setHint("", LOCALE_MENU_HINT_NET_NFS_MOUNT);
	menu_nfs->addItem(mf);
	mf = new CMenuDForwarder(LOCALE_NFS_UMOUNT, true, NULL, new CNFSUmountGui(), NULL, CRCInput::RC_green);
	mf->setHint("", LOCALE_MENU_HINT_NET_NFS_UMOUNT);
	menu_nfs->addItem(mf);
}
#endif

typedef struct n_isettings_t
{
	int old_network_setting;
	int network_setting;
}n_isettings_struct_t;

//checks settings changes for int settings, returns true on changes
bool CNetworkSetup::checkIntSettings()
{
	n_isettings_t n_isettings[]	=
	{
		{old_network_automatic_start, 	network_automatic_start},
		{old_network_dhcp, 		network_dhcp		}
	};
	for (uint i = 0; i < (sizeof(n_isettings) / sizeof(n_isettings[0])); i++)
		if (n_isettings[i].old_network_setting != n_isettings[i].network_setting) {
			dprintf(DEBUG_NORMAL, "[CNetworkSetup]\t[%s - %d],  %d %d -> %d\n", __func__, __LINE__, i, n_isettings[i].old_network_setting, n_isettings[i].network_setting);
			return true;
	}

	return false;
}

typedef struct n_ssettings_t
{
	std::string old_network_setting;
	std::string network_setting;
}n_ssettings_struct_t;

//checks settings changes for int settings, returns true on changes
bool CNetworkSetup::checkStringSettings()
{
	n_ssettings_t n_ssettings[]	=
	{
		{old_network_address,	 	network_address		},
		{old_network_netmask, 		network_netmask		},
		{old_network_broadcast, 	network_broadcast	},
		{old_network_gateway, 		network_gateway		},
		{old_network_nameserver, 	network_nameserver	},
		{old_network_hostname, 		network_hostname	},
		{old_ifname, 			g_settings.ifname	}
	};
	for (uint i = 0; i < (sizeof(n_ssettings) / sizeof(n_ssettings[0])); i++)
		if (n_ssettings[i].old_network_setting != n_ssettings[i].network_setting) {
			dprintf(DEBUG_NORMAL, "[CNetworkSetup]\t[%s - %d],  %d: %s -> %s\n", __func__, __LINE__, i, n_ssettings[i].old_network_setting.c_str(), n_ssettings[i].network_setting.c_str());
			return true;
	}
	if(CNetworkConfig::getInstance()->wireless) {
		if((old_network_ssid != network_ssid) || (old_network_key != network_key))
			return true;
	}

	return false;
}

//returns true, if any settings were changed
bool CNetworkSetup::settingsChanged()
{
	if (networkConfig->modified_from_orig() || checkStringSettings() || checkIntSettings())
		return true;

	return false;
}

//prepares internal settings before commit
void CNetworkSetup::prepareSettings()
{
	networkConfig->automatic_start 	= (network_automatic_start == 1);
	networkConfig->inet_static 	= (network_dhcp ? false : true);
	networkConfig->address 		= network_address;
	networkConfig->netmask 		= network_netmask;
	networkConfig->broadcast 	= network_broadcast;
	networkConfig->gateway 		= network_gateway;
	networkConfig->nameserver 	= network_nameserver;
	networkConfig->hostname 	= network_hostname;
	networkConfig->ssid 		= network_ssid;
	networkConfig->key 		= network_key;

	readNetworkSettings();
	backupNetworkSettings();
}

typedef struct n_settings_t
{
	neutrino_locale_t addr_name;
	std::string network_settings;
}n_settings_struct_t;

//check for addresses, if dhcp disabled, returns false if any address no definied and shows a message
bool CNetworkSetup::checkForIP()
{
	n_settings_t n_settings[]	=
	{
		{LOCALE_NETWORKMENU_IPADDRESS, 	network_address		},
		{LOCALE_NETWORKMENU_NETMASK, 	network_netmask		},
		{LOCALE_NETWORKMENU_BROADCAST, 	network_broadcast	},
		{LOCALE_NETWORKMENU_GATEWAY, 	network_gateway		},
		{LOCALE_NETWORKMENU_NAMESERVER, network_nameserver	}
	};

	if (!network_dhcp)
	{
		for (uint i = 0; i < (sizeof(n_settings) / sizeof(n_settings[0])); i++)
		{
			if (n_settings[i].network_settings.empty()) //no definied setting
			{
				dprintf(DEBUG_NORMAL, "\033[33m\[CNetworkSetup]\t[%s - %d], empty address %s\033[0m\n", __func__, __LINE__, g_Locale->getText(n_settings[i].addr_name));
				char msg[64];
				snprintf(msg, 64, g_Locale->getText(LOCALE_NETWORKMENU_ERROR_NO_ADDRESS), g_Locale->getText(n_settings[i].addr_name));
				ShowMsg(LOCALE_MAINSETTINGS_NETWORK, msg, CMsgBox::mbrOk, CMsgBox::mbOk, NEUTRINO_ICON_ERROR, width);
				return false;
			}
		}
	}

	return true;
}

//saves settings without apply, reboot is required
void CNetworkSetup::saveNetworkSettings()
{
	dprintf(DEBUG_NORMAL, "[CNetworkSetup]\t[%s - %d], saving current network settings...\n", __func__, __LINE__);

	prepareSettings();
	networkConfig->commitConfig();
}

//saves settings and apply
void CNetworkSetup::applyNetworkSettings()
{
	dprintf(DEBUG_NORMAL, "[CNetworkSetup]\t[%s - %d], apply network settings...\n", __func__, __LINE__);

	if (!checkForIP())
		return;

	CHintBox hintBox(LOCALE_MESSAGEBOX_INFO, g_Locale->getText(LOCALE_NETWORKMENU_APPLY_SETTINGS)); // UTF-8
	hintBox.paint();

 	networkConfig->stopNetwork();
	saveNetworkSettings();
 	networkConfig->startNetwork();

	hintBox.hide();
}

//open a message dialog with buttons,
//yes:		applies networksettings and exit network setup
//no:		ask to restore networksettings, and return to menu
int  CNetworkSetup::saveChangesDialog()
{
	// Save the settings after changes, if user wants to!
	int result = 	ShowMsg(LOCALE_MAINSETTINGS_NETWORK, g_Locale->getText(LOCALE_NETWORKMENU_APPLY_SETTINGS_NOW), CMsgBox::mbrYes,
			CMsgBox::mbYes |
			CMsgBox::mbNo ,
			NEUTRINO_ICON_QUESTION,
			width);

	switch(result)
	{
		case CMsgBox::mbrYes:
			if (!checkForIP())
				return menu_return::RETURN_REPAINT;
			return exec(NULL, "networkapply");
			break;

		case CMsgBox::mbrNo: //no
			return exec(NULL, "restore");
			break;
	}
	return menu_return::RETURN_REPAINT;
}

//restores settings
void CNetworkSetup::restoreNetworkSettings()
{
	g_settings.ifname = old_ifname;
	networkConfig->readConfig(g_settings.ifname);//FIXME ?

	mac_addr			= networkConfig->mac_addr;

	network_automatic_start		= old_network_automatic_start;
	network_dhcp			= old_network_dhcp;
	network_address			= old_network_address;
	network_netmask			= old_network_netmask;
	setBroadcast();
	network_nameserver		= old_network_nameserver;
	network_gateway			= old_network_gateway;
	network_hostname		= old_network_hostname;
	network_ssid			= old_network_ssid;
	network_key			= old_network_key;

	networkConfig->automatic_start 	= network_automatic_start;
	networkConfig->inet_static 	= (network_dhcp ? false : true);
	networkConfig->address 		= network_address;
	networkConfig->netmask 		= network_netmask;
	networkConfig->broadcast 	= network_broadcast;
	networkConfig->gateway 		= network_gateway;
	networkConfig->nameserver 	= network_nameserver;
	networkConfig->hostname 	= network_hostname;
	networkConfig->ssid 		= network_ssid;
	networkConfig->key 		= network_key;

	networkConfig->commitConfig();
	changeNotify(LOCALE_NETWORKMENU_SELECT_IF, NULL);
}

bool CNetworkSetup::changeNotify(const neutrino_locale_t locale, void * /*Data*/)
{
	if(locale == LOCALE_NETWORKMENU_IPADDRESS) {
		if (!network_netmask[0]) {
			networkConfig->netmask = (network_address[0] == 10) ? "255.0.0.0" : "255.255.255.0";
			network_netmask = networkConfig->netmask;
		}
		setBroadcast();
	} else if(locale == LOCALE_NETWORKMENU_NETMASK) {
		setBroadcast();
	} else if(locale == LOCALE_NETWORKMENU_SELECT_IF) {
		networkConfig->readConfig(g_settings.ifname);
		readNetworkSettings();
		dprintf(DEBUG_NORMAL, "[CNetworkSetup]\t[%s - %d], using %s, static %d\n", __func__, __LINE__, g_settings.ifname.c_str(), CNetworkConfig::getInstance()->inet_static);

		changeNotify(LOCALE_NETWORKMENU_DHCP, &CNetworkConfig::getInstance()->inet_static);

		wlanEnable.Activate(CNetworkConfig::getInstance()->wireless);
	} else if(locale == LOCALE_NETWORKMENU_DHCP) {
		CNetworkConfig::getInstance()->inet_static = (network_dhcp == NETWORK_DHCP_OFF);
		dhcpDisable.Activate(CNetworkConfig::getInstance()->inet_static);
	}
	return false;
}

void CNetworkSetup::showCurrentNetworkSettings()
{
	dprintf(DEBUG_NORMAL, "[CNetworkSetup]\t[%s - %d], show current network settings...\n", __func__, __LINE__);
	std::string ip, mask, broadcast, router, nameserver, text;
	netGetIP(g_settings.ifname, ip, mask, broadcast);
	if (ip[0] == 0) {
		text = g_Locale->getText(LOCALE_NETWORKMENU_INACTIVE_NETWORK);
	}
	else {
		netGetNameserver(nameserver);
		netGetDefaultRoute(router);
#if 0 // i think we can use current networkConfig instance for that
		CNetworkConfig  _networkConfig;
		std::string dhcp = _networkConfig.inet_static ? g_Locale->getText(LOCALE_OPTIONS_OFF) : g_Locale->getText(LOCALE_OPTIONS_ON);
#endif
		std::string dhcp = networkConfig->inet_static ? g_Locale->getText(LOCALE_OPTIONS_OFF) : g_Locale->getText(LOCALE_OPTIONS_ON);

		text = (std::string)g_Locale->getText(LOCALE_NETWORKMENU_DHCP) + ": " + dhcp + '\n'
			+ g_Locale->getText(LOCALE_NETWORKMENU_IPADDRESS ) + ": " + ip + '\n'
			+ g_Locale->getText(LOCALE_NETWORKMENU_NETMASK   ) + ": " + mask + '\n'
			+ g_Locale->getText(LOCALE_NETWORKMENU_BROADCAST ) + ": " + broadcast + '\n'
			+ g_Locale->getText(LOCALE_NETWORKMENU_NAMESERVER) + ": " + nameserver + '\n'
			+ g_Locale->getText(LOCALE_NETWORKMENU_GATEWAY   ) + ": " + router;
	}
	ShowMsg(LOCALE_NETWORKMENU_SHOW, text, CMsgBox::mbrBack, CMsgBox::mbBack); // UTF-8
}

const char * CNetworkSetup::mypinghost(std::string &host)
{
	int retvalue = pinghost(host);
	switch (retvalue)
	{
		case 1: return (g_Locale->getText(LOCALE_PING_OK));
		case 0: return (g_Locale->getText(LOCALE_PING_UNREACHABLE));
		case -1: return (g_Locale->getText(LOCALE_PING_PROTOCOL));
		case -2: return (g_Locale->getText(LOCALE_PING_SOCKET));
	}
	return "";
}

void CNetworkSetup::testNetworkSettings()
{
	dprintf(DEBUG_NORMAL, "[CNetworkSetup]\t[%s - %d], doing network test...\n", __func__, __LINE__);
	std::string our_ip, our_mask, our_broadcast, our_gateway, our_nameserver;

	std::string text, testsite, offset = "    ";

	//set default testdomain
	std::string defaultsite = "www.google.de";

	//set wiki-URL and wiki-IP
	std::string wiki_URL = "wiki.tuxbox-neutrino.org";
	std::string wiki_IP = "77.11.53.128";

	//get www-domain testsite from /.version
	CConfigFile config('\t');
	config.loadConfig("/.version");
	testsite = config.getString("homepage", defaultsite);

	if (strstr(testsite.c_str(), "http://"))
		testsite.erase(0,7);
	else if (strstr(testsite.c_str(), "https://"))
		testsite.erase(0,8);
	else
		testsite.replace(0, testsite.find("www", 0), "");

	//use default testdomain if testsite missing
	if (testsite.length() == 0)
		testsite = defaultsite;

	if (networkConfig->inet_static)
	{
		our_ip = networkConfig->address;
		our_mask = networkConfig->netmask;
		our_broadcast = networkConfig->broadcast;
		our_gateway = networkConfig->gateway;
		our_nameserver = networkConfig->nameserver;
	}
	else
	{
		// FIXME test with current, not changed ifname ?
		netGetIP(old_ifname, our_ip, our_mask, our_broadcast);
		netGetDefaultRoute(our_gateway);
		netGetNameserver(our_nameserver);
	}

	printf("testNw IP: %s\n", our_ip.c_str());
	printf("testNw MAC-address: %s\n", old_mac_addr.c_str());
	printf("testNw Netmask: %s\n", our_mask.c_str());
	printf("testNw Broadcast: %s\n", our_broadcast.c_str());
	printf("testNw Gateway: %s\n", our_gateway.c_str());
	printf("testNw Nameserver: %s\n", our_nameserver.c_str());
	printf("testNw Testsite: %s\n", testsite.c_str());

	if (our_ip.empty())
	{
		text = g_Locale->getText(LOCALE_NETWORKMENU_INACTIVE_NETWORK);
	}
	else
	{
		//Box
		text = "Box (" + old_mac_addr + "):\n";
		text += offset + our_ip + " " + mypinghost(our_ip) + "\n";
		//Gateway
		text += (std::string)g_Locale->getText(LOCALE_NETWORKMENU_GATEWAY) + " (Router):\n";
		text += offset + our_gateway + " " + mypinghost(our_gateway) + "\n";
		//Nameserver
		text += (std::string)g_Locale->getText(LOCALE_NETWORKMENU_NAMESERVER) + ":\n";
		text += offset + our_nameserver + " " + mypinghost(our_nameserver) + "\n";
#if 0 // deactivated, NTP most no ping possible
		//NTPserver
		if ((pinghost(our_nameserver) == 1) && g_settings.network_ntpenable && (!g_settings.network_ntpserver.empty()))
		{
			text += std::string(g_Locale->getText(LOCALE_NETWORKMENU_NTPSERVER)) + ":\n";
			text += offset + g_settings.network_ntpserver + " " + mypinghost(g_settings.network_ntpserver) + "\n";
		}
#endif
		//Wiki
		text += wiki_URL + ":\n";
		text += offset + "via IP (" + wiki_IP + "): " + mypinghost(wiki_IP) + "\n";
		if (pinghost(our_nameserver) == 1)
		{
			text += offset + "via DNS: " + mypinghost(wiki_URL) + "\n";
			//testsite (or defaultsite)
			text += testsite + ":\n";
			text += offset + "via DNS: " + mypinghost(testsite) + "\n";
		}
	}

	ShowMsg(LOCALE_NETWORKMENU_TEST, text, CMsgBox::mbrBack, CMsgBox::mbBack, NEUTRINO_ICON_NETWORK, MSGBOX_MIN_WIDTH, NO_TIMEOUT, false, CMsgBox::AUTO_WIDTH | CMsgBox::AUTO_HIGH);
}

int CNetworkSetup::showWlanList()
{
	int   res = menu_return::RETURN_REPAINT;

	CHintBox hintBox(LOCALE_MESSAGEBOX_INFO, g_Locale->getText(LOCALE_NETWORKMENU_SSID_SCAN_WAIT));
	hintBox.paint();

	std::vector<wlan_network> networks;
	bool found = get_wlan_list(g_settings.ifname, networks);
	hintBox.hide();
	if (!found) {
		ShowMsg(LOCALE_MESSAGEBOX_ERROR, g_Locale->getText(LOCALE_NETWORKMENU_SSID_SCAN_ERROR), CMsgBox::mbrBack, CMsgBox::mbBack); // UTF-8
		return res;
	}

	CMenuWidget wlist(LOCALE_MAINSETTINGS_NETWORK, NEUTRINO_ICON_NETWORK, width);
	wlist.addIntroItems(LOCALE_NETWORKMENU_SSID_SCAN); //intros

	char cnt[10];
	int select = -1;
	CMenuSelectorTarget * selector = new CMenuSelectorTarget(&select);

	std::string option[networks.size()];
	for (unsigned i = 0; i < networks.size(); ++i) {
		sprintf(cnt, "%d", i);

		option[i] = networks[i].qual;
		option[i] += ", ";
		option[i] += networks[i].channel;

		const char * icon = NULL;
		if (networks[i].encrypted)
			icon = NEUTRINO_ICON_LOCK;
		CMenuForwarder * net = new CMenuForwarder(networks[i].ssid.c_str(), true, option[i], selector, cnt, CRCInput::RC_nokey, NULL, icon);
		net->setItemButton(NEUTRINO_ICON_BUTTON_OKAY, true);
		wlist.addItem(net, networks[i].ssid == network_ssid);
	}
	res = wlist.exec(NULL, "");
	delete selector;

	dprintf(DEBUG_NORMAL, "[CNetworkSetup]\t[%s - %d], selected: %d\n", __func__, __LINE__, select);
	if (select >= 0) {
		network_ssid = networks[select].ssid;
	}
	return res;
}
