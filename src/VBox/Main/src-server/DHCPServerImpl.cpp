/* $Id: DHCPServerImpl.cpp 76592 2019-01-01 20:13:07Z vboxsync $ */
/** @file
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#define LOG_GROUP LOG_GROUP_MAIN_DHCPSERVER
#include "NetworkServiceRunner.h"
#include "DHCPServerImpl.h"
#include "AutoCaller.h"
#include "LoggingNew.h"

#include <iprt/asm.h>
#include <iprt/file.h>
#include <iprt/net.h>
#include <iprt/path.h>
#include <iprt/cpp/utils.h>
#include <iprt/cpp/xml.h>

#include <VBox/com/array.h>
#include <VBox/settings.h>

#include "VirtualBoxImpl.h"

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////
const std::string DHCPServerRunner::kDsrKeyGateway = "--gateway";
const std::string DHCPServerRunner::kDsrKeyLowerIp = "--lower-ip";
const std::string DHCPServerRunner::kDsrKeyUpperIp = "--upper-ip";
const std::string DHCPServerRunner::kDsrKeyConfig  = "--config";
const std::string DHCPServerRunner::kDsrKeyComment = "--comment";


struct DHCPServer::Data
{
    Data()
        : enabled(FALSE)
        , router(false)
    {
        tempConfigFileName[0] = '\0';
    }

    Utf8Str IPAddress;
    Utf8Str lowerIP;
    Utf8Str upperIP;

    BOOL enabled;
    bool router;
    DHCPServerRunner dhcp;

    settings::DhcpOptionMap GlobalDhcpOptions;
    settings::VmSlot2OptionsMap VmSlot2Options;

    char tempConfigFileName[RTPATH_MAX];
    com::Utf8Str networkName;
    com::Utf8Str trunkName;
    com::Utf8Str trunkType;
};


DHCPServer::DHCPServer()
  : m(NULL)
  , mVirtualBox(NULL)
{
    m = new DHCPServer::Data();
}


DHCPServer::~DHCPServer()
{
    if (m)
    {
        delete m;
        m = NULL;
    }
}


HRESULT DHCPServer::FinalConstruct()
{
    return BaseFinalConstruct();
}


void DHCPServer::FinalRelease()
{
    uninit ();

    BaseFinalRelease();
}


void DHCPServer::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    if (m->dhcp.isRunning())
        stop();

    unconst(mVirtualBox) = NULL;
}


HRESULT DHCPServer::init(VirtualBox *aVirtualBox, const Utf8Str &aName)
{
    AssertReturn(!aName.isEmpty(), E_INVALIDARG);

    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* share VirtualBox weakly (parent remains NULL so far) */
    unconst(mVirtualBox) = aVirtualBox;

    unconst(mName) = aName;
    m->IPAddress = "0.0.0.0";
    m->GlobalDhcpOptions[DhcpOpt_SubnetMask] = settings::DhcpOptValue("0.0.0.0");
    m->enabled = FALSE;

    m->lowerIP = "0.0.0.0";
    m->upperIP = "0.0.0.0";

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}


HRESULT DHCPServer::init(VirtualBox *aVirtualBox,
                         const settings::DHCPServer &data)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* share VirtualBox weakly (parent remains NULL so far) */
    unconst(mVirtualBox) = aVirtualBox;

    unconst(mName) = data.strNetworkName;
    m->IPAddress = data.strIPAddress;
    m->enabled = data.fEnabled;
    m->lowerIP = data.strIPLower;
    m->upperIP = data.strIPUpper;

    m->GlobalDhcpOptions.clear();
    m->GlobalDhcpOptions.insert(data.GlobalDhcpOptions.begin(),
                               data.GlobalDhcpOptions.end());

    m->VmSlot2Options.clear();
    m->VmSlot2Options.insert(data.VmSlot2OptionsM.begin(),
                            data.VmSlot2OptionsM.end());

    autoInitSpan.setSucceeded();

    return S_OK;
}


HRESULT DHCPServer::i_saveSettings(settings::DHCPServer &data)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    data.strNetworkName = mName;
    data.strIPAddress = m->IPAddress;

    data.fEnabled = !!m->enabled;
    data.strIPLower = m->lowerIP;
    data.strIPUpper = m->upperIP;

    data.GlobalDhcpOptions.clear();
    data.GlobalDhcpOptions.insert(m->GlobalDhcpOptions.begin(),
                                  m->GlobalDhcpOptions.end());

    data.VmSlot2OptionsM.clear();
    data.VmSlot2OptionsM.insert(m->VmSlot2Options.begin(),
                                m->VmSlot2Options.end());

    return S_OK;
}


HRESULT DHCPServer::getNetworkName(com::Utf8Str &aName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aName = mName;
    return S_OK;
}


HRESULT DHCPServer::getEnabled(BOOL *aEnabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aEnabled = m->enabled;
    return S_OK;
}


HRESULT DHCPServer::setEnabled(BOOL aEnabled)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->enabled = aEnabled;

    // save the global settings; for that we should hold only the VirtualBox lock
    alock.release();
    AutoWriteLock vboxLock(mVirtualBox COMMA_LOCKVAL_SRC_POS);
    HRESULT rc = mVirtualBox->i_saveSettings();

    return rc;
}


HRESULT DHCPServer::getIPAddress(com::Utf8Str &aIPAddress)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aIPAddress = Utf8Str(m->IPAddress);
    return S_OK;
}


HRESULT DHCPServer::getNetworkMask(com::Utf8Str &aNetworkMask)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aNetworkMask = m->GlobalDhcpOptions[DhcpOpt_SubnetMask].text;
    return S_OK;
}


HRESULT DHCPServer::getLowerIP(com::Utf8Str &aIPAddress)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aIPAddress = Utf8Str(m->lowerIP);
    return S_OK;
}


HRESULT DHCPServer::getUpperIP(com::Utf8Str &aIPAddress)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aIPAddress = Utf8Str(m->upperIP);
    return S_OK;
}


HRESULT DHCPServer::setConfiguration(const com::Utf8Str &aIPAddress,
                                     const com::Utf8Str &aNetworkMask,
                                     const com::Utf8Str &aLowerIP,
                                     const com::Utf8Str &aUpperIP)
{
    RTNETADDRIPV4 IPAddress, NetworkMask, LowerIP, UpperIP;

    int vrc = RTNetStrToIPv4Addr(aIPAddress.c_str(), &IPAddress);
    if (RT_FAILURE(vrc))
        return mVirtualBox->setErrorBoth(E_INVALIDARG, vrc, "Invalid server address");

    vrc = RTNetStrToIPv4Addr(aNetworkMask.c_str(), &NetworkMask);
    if (RT_FAILURE(vrc))
        return mVirtualBox->setErrorBoth(E_INVALIDARG, vrc, "Invalid netmask");

    vrc = RTNetStrToIPv4Addr(aLowerIP.c_str(), &LowerIP);
    if (RT_FAILURE(vrc))
        return mVirtualBox->setErrorBoth(E_INVALIDARG, vrc, "Invalid range lower address");

    vrc = RTNetStrToIPv4Addr(aUpperIP.c_str(), &UpperIP);
    if (RT_FAILURE(vrc))
        return mVirtualBox->setErrorBoth(E_INVALIDARG, vrc, "Invalid range upper address");

    /*
     * Insist on continuous mask.  May be also accept prefix length
     * here or address/prefix for aIPAddress?
     */
    vrc = RTNetMaskToPrefixIPv4(&NetworkMask, NULL);
    if (RT_FAILURE(vrc))
        return mVirtualBox->setErrorBoth(E_INVALIDARG, vrc, "Invalid netmask");

    /* It's more convenient to convert to host order once */
    IPAddress.u = RT_N2H_U32(IPAddress.u);
    NetworkMask.u = RT_N2H_U32(NetworkMask.u);
    LowerIP.u = RT_N2H_U32(LowerIP.u);
    UpperIP.u = RT_N2H_U32(UpperIP.u);

    /*
     * Addresses must be unicast and from the same network
     */
    if (   (IPAddress.u & UINT32_C(0xe0000000)) == UINT32_C(0xe0000000)
        || (IPAddress.u & ~NetworkMask.u) == 0
        || ((IPAddress.u & ~NetworkMask.u) | NetworkMask.u) == UINT32_C(0xffffffff))
        return mVirtualBox->setError(E_INVALIDARG, "Invalid server address");

    if (   (LowerIP.u & UINT32_C(0xe0000000)) == UINT32_C(0xe0000000)
        || (LowerIP.u & NetworkMask.u) != (IPAddress.u &NetworkMask.u)
        || (LowerIP.u & ~NetworkMask.u) == 0
        || ((LowerIP.u & ~NetworkMask.u) | NetworkMask.u) == UINT32_C(0xffffffff))
        return mVirtualBox->setError(E_INVALIDARG, "Invalid range lower address");

    if (   (UpperIP.u & UINT32_C(0xe0000000)) == UINT32_C(0xe0000000)
        || (UpperIP.u & NetworkMask.u) != (IPAddress.u &NetworkMask.u)
        || (UpperIP.u & ~NetworkMask.u) == 0
        || ((UpperIP.u & ~NetworkMask.u) | NetworkMask.u) == UINT32_C(0xffffffff))
        return mVirtualBox->setError(E_INVALIDARG, "Invalid range upper address");

    /* The range should be valid ... */
    if (LowerIP.u > UpperIP.u)
        return mVirtualBox->setError(E_INVALIDARG, "Invalid range bounds");

    /* ... and shouldn't contain the server's address */
    if (LowerIP.u <= IPAddress.u && IPAddress.u <= UpperIP.u)
        return mVirtualBox->setError(E_INVALIDARG, "Server address within range bounds");

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->IPAddress = aIPAddress;
    m->GlobalDhcpOptions[DhcpOpt_SubnetMask] = aNetworkMask;

    m->lowerIP = aLowerIP;
    m->upperIP = aUpperIP;

    // save the global settings; for that we should hold only the VirtualBox lock
    alock.release();
    AutoWriteLock vboxLock(mVirtualBox COMMA_LOCKVAL_SRC_POS);
    return mVirtualBox->i_saveSettings();
}


HRESULT DHCPServer::encodeOption(com::Utf8Str &aEncoded,
                                 uint32_t aOptCode,
                                 const settings::DhcpOptValue &aOptValue)
{
    switch (aOptValue.encoding)
    {
        case DhcpOptEncoding_Legacy:
        {
            /*
             * This is original encoding which assumed that for each
             * option we know its format and so we know how option
             * "value" text is to be interpreted.
             *
             *   "2:10800"           # integer 32
             *   "6:1.2.3.4 8.8.8.8" # array of ip-address
             */
            aEncoded = Utf8StrFmt("%d:%s", aOptCode, aOptValue.text.c_str());
            break;
        }

        case DhcpOptEncoding_Hex:
        {
            /*
             * This is a bypass for any option - preformatted value as
             * hex string with no semantic involved in formatting the
             * value for the DHCP reply.
             *
             *   234=68:65:6c:6c:6f:2c:20:77:6f:72:6c:64
             */
            aEncoded = Utf8StrFmt("%d=%s", aOptCode, aOptValue.text.c_str());
            break;
        }

        default:
        {
            /*
             * Try to be forward compatible.
             *
             *   "254@42=i hope you know what this means"
             */
            aEncoded = Utf8StrFmt("%d@%d=%s", aOptCode, (int)aOptValue.encoding,
                                  aOptValue.text.c_str());
            break;
        }
    }

    return S_OK;
}


int DHCPServer::addOption(settings::DhcpOptionMap &aMap,
                          DhcpOpt_T aOption, const com::Utf8Str &aValue)
{
    settings::DhcpOptValue OptValue;

    if (aOption != 0)
    {
        OptValue = settings::DhcpOptValue(aValue, DhcpOptEncoding_Legacy);
    }
    /*
     * This is a kludge to sneak in option encoding information
     * through existing API.  We use option 0 and supply the real
     * option/value in the same format that encodeOption() above
     * produces for getter methods.
     */
    else
    {
        uint8_t u8Code;
        char    *pszNext;
        int vrc = RTStrToUInt8Ex(aValue.c_str(), &pszNext, 10, &u8Code);
        if (!RT_SUCCESS(vrc))
            return VERR_PARSE_ERROR;

        uint32_t u32Enc;
        switch (*pszNext)
        {
            case ':':           /* support legacy format too */
            {
                u32Enc = DhcpOptEncoding_Legacy;
                break;
            }

            case '=':
            {
                u32Enc = DhcpOptEncoding_Hex;
                break;
            }

            case '@':
            {
                vrc = RTStrToUInt32Ex(pszNext + 1, &pszNext, 10, &u32Enc);
                if (!RT_SUCCESS(vrc))
                    return VERR_PARSE_ERROR;
                if (*pszNext != '=')
                    return VERR_PARSE_ERROR;
                break;
            }

            default:
                return VERR_PARSE_ERROR;
        }

        aOption = (DhcpOpt_T)u8Code;
        OptValue = settings::DhcpOptValue(pszNext + 1, (DhcpOptEncoding_T)u32Enc);
    }

    aMap[aOption] = OptValue;
    return VINF_SUCCESS;
}


HRESULT DHCPServer::addGlobalOption(DhcpOpt_T aOption, const com::Utf8Str &aValue)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int rc = addOption(m->GlobalDhcpOptions, aOption, aValue);
    if (!RT_SUCCESS(rc))
        return E_INVALIDARG;

    /* Indirect way to understand that we're on NAT network */
    if (aOption == DhcpOpt_Router)
    {
        m->router = true;
    }

    alock.release();

    AutoWriteLock vboxLock(mVirtualBox COMMA_LOCKVAL_SRC_POS);
    return mVirtualBox->i_saveSettings();
}


HRESULT DHCPServer::removeGlobalOption(DhcpOpt_T aOption)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    settings::DhcpOptionMap::size_type cErased = m->GlobalDhcpOptions.erase(aOption);
    if (!cErased)
        return E_INVALIDARG;

    alock.release();

    AutoWriteLock vboxLock(mVirtualBox COMMA_LOCKVAL_SRC_POS);
    return mVirtualBox->i_saveSettings();
}


HRESULT DHCPServer::removeGlobalOptions()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->GlobalDhcpOptions.clear();

    alock.release();

    AutoWriteLock vboxLock(mVirtualBox COMMA_LOCKVAL_SRC_POS);
    return mVirtualBox->i_saveSettings();
}


HRESULT DHCPServer::getGlobalOptions(std::vector<com::Utf8Str> &aValues)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aValues.resize(m->GlobalDhcpOptions.size());
    settings::DhcpOptionMap::const_iterator it;
    size_t i = 0;
    for (it = m->GlobalDhcpOptions.begin(); it != m->GlobalDhcpOptions.end(); ++it, ++i)
    {
        uint32_t OptCode = (*it).first;
        const settings::DhcpOptValue &OptValue = (*it).second;

        encodeOption(aValues[i], OptCode, OptValue);
    }

    return S_OK;
}

HRESULT DHCPServer::getVmConfigs(std::vector<com::Utf8Str> &aValues)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aValues.resize(m->VmSlot2Options.size());
    settings::VmSlot2OptionsMap::const_iterator it;
    size_t i = 0;
    for (it = m->VmSlot2Options.begin(); it != m->VmSlot2Options.end(); ++it, ++i)
    {
        aValues[i] = Utf8StrFmt("[%s]:%d", it->first.VmName.c_str(), it->first.Slot);
    }

    return S_OK;
}


HRESULT DHCPServer::addVmSlotOption(const com::Utf8Str &aVmName,
                                    LONG aSlot,
                                    DhcpOpt_T aOption,
                                    const com::Utf8Str &aValue)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    settings::DhcpOptionMap &map = m->VmSlot2Options[settings::VmNameSlotKey(aVmName, aSlot)];
    int rc = addOption(map, aOption, aValue);
    if (!RT_SUCCESS(rc))
        return E_INVALIDARG;

    alock.release();

    AutoWriteLock vboxLock(mVirtualBox COMMA_LOCKVAL_SRC_POS);
    return mVirtualBox->i_saveSettings();
}


HRESULT DHCPServer::removeVmSlotOption(const com::Utf8Str &aVmName, LONG aSlot, DhcpOpt_T aOption)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    settings::DhcpOptionMap &map = i_findOptMapByVmNameSlot(aVmName, aSlot);
    settings::DhcpOptionMap::size_type cErased = map.erase(aOption);
    if (!cErased)
        return E_INVALIDARG;

    alock.release();

    AutoWriteLock vboxLock(mVirtualBox COMMA_LOCKVAL_SRC_POS);
    return mVirtualBox->i_saveSettings();
}


HRESULT DHCPServer::removeVmSlotOptions(const com::Utf8Str &aVmName, LONG aSlot)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    settings::DhcpOptionMap &map = i_findOptMapByVmNameSlot(aVmName, aSlot);
    map.clear();

    alock.release();

    AutoWriteLock vboxLock(mVirtualBox COMMA_LOCKVAL_SRC_POS);
    return mVirtualBox->i_saveSettings();
}

/**
 * this is mapping (vm, slot)
 */
HRESULT DHCPServer::getVmSlotOptions(const com::Utf8Str &aVmName,
                                     LONG aSlot,
                                     std::vector<com::Utf8Str> &aValues)
{

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    settings::DhcpOptionMap &map = i_findOptMapByVmNameSlot(aVmName, aSlot);
    aValues.resize(map.size());
    size_t i = 0;
    settings::DhcpOptionMap::const_iterator it;
    for (it = map.begin(); it != map.end(); ++it, ++i)
    {
        uint32_t OptCode = (*it).first;
        const settings::DhcpOptValue &OptValue = (*it).second;

        encodeOption(aValues[i], OptCode, OptValue);
    }

    return S_OK;
}


HRESULT DHCPServer::getMacOptions(const com::Utf8Str &aMAC, std::vector<com::Utf8Str> &aOption)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = S_OK;
    ComPtr<IMachine> machine;
    ComPtr<INetworkAdapter> nic;
    settings::VmSlot2OptionsIterator it;
    for(it = m->VmSlot2Options.begin(); it != m->VmSlot2Options.end(); ++it)
    {
        alock.release();
        hrc = mVirtualBox->FindMachine(Bstr(it->first.VmName).raw(), machine.asOutParam());
        alock.acquire();

        if (FAILED(hrc))
            continue;

        alock.release();
        hrc = machine->GetNetworkAdapter(it->first.Slot, nic.asOutParam());
        alock.acquire();

        if (FAILED(hrc))
            continue;

        com::Bstr mac;

        alock.release();
        hrc = nic->COMGETTER(MACAddress)(mac.asOutParam());
        alock.acquire();

        if (FAILED(hrc)) /* no MAC address ??? */
            break;
        if (!RTStrICmp(com::Utf8Str(mac).c_str(), aMAC.c_str()))
            return getVmSlotOptions(it->first.VmName,
                                    it->first.Slot,
                                    aOption);
    } /* end of for */

    return hrc;
}

HRESULT DHCPServer::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    NOREF(aEventSource);
    ReturnComNotImplemented();
}


DECLINLINE(void) addOptionChild(xml::ElementNode *pParent, uint32_t OptCode, const settings::DhcpOptValue &OptValue)
{
    xml::ElementNode *pOption = pParent->createChild("Option");
    pOption->setAttribute("name", OptCode);
    pOption->setAttribute("encoding", OptValue.encoding);
    pOption->setAttribute("value", OptValue.text.c_str());
}


HRESULT DHCPServer::restart()
{
    if (!m->dhcp.isRunning())
        return E_FAIL;
    /*
        * Disabled servers will be brought down, but won't be restarted.
        * (see DHCPServer::start)
        */
    HRESULT hrc = stop();
    if (SUCCEEDED(hrc))
        hrc = start(m->networkName, m->trunkName, m->trunkType);
    return hrc;
}


HRESULT DHCPServer::start(const com::Utf8Str &aNetworkName,
                          const com::Utf8Str &aTrunkName,
                          const com::Utf8Str &aTrunkType)
{
    /* Silently ignore attempts to run disabled servers. */
    if (!m->enabled)
        return S_OK;

    /*
     * @todo: the existing code cannot handle concurrent attempts to start DHCP server.
     * Note that technically it may receive different parameters from different callers.
     */
    m->networkName = aNetworkName;
    m->trunkName = aTrunkName;
    m->trunkType = aTrunkType;

    m->dhcp.clearOptions();
#ifdef VBOX_WITH_DHCPD
    int rc = RTPathTemp(m->tempConfigFileName, sizeof(m->tempConfigFileName));
    if (RT_FAILURE(rc))
        return E_FAIL;
    rc = RTPathAppend(m->tempConfigFileName, sizeof(m->tempConfigFileName), "dhcp-config-XXXXX.xml");
    if (RT_FAILURE(rc))
    {
        m->tempConfigFileName[0] = '\0';
        return E_FAIL;
    }
    rc = RTFileCreateTemp(m->tempConfigFileName, 0600);
    if (RT_FAILURE(rc))
    {
        m->tempConfigFileName[0] = '\0';
        return E_FAIL;
    }

    xml::Document doc;
    xml::ElementNode *pElmRoot = doc.createRootElement("DHCPServer");
    pElmRoot->setAttribute("networkName", m->networkName.c_str());
    if (!m->trunkName.isEmpty())
        pElmRoot->setAttribute("trunkName", m->trunkName.c_str());
    pElmRoot->setAttribute("trunkType", m->trunkType.c_str());
    pElmRoot->setAttribute("IPAddress",  Utf8Str(m->IPAddress).c_str());
    pElmRoot->setAttribute("networkMask", Utf8Str(m->GlobalDhcpOptions[DhcpOpt_SubnetMask].text).c_str());
    pElmRoot->setAttribute("lowerIP", Utf8Str(m->lowerIP).c_str());
    pElmRoot->setAttribute("upperIP", Utf8Str(m->upperIP).c_str());

    /* Process global options */
    xml::ElementNode *pOptions = pElmRoot->createChild("Options");
    // settings::DhcpOptionMap::const_iterator itGlobal;
    for (settings::DhcpOptionMap::const_iterator it = m->GlobalDhcpOptions.begin();
         it != m->GlobalDhcpOptions.end();
         ++it)
        addOptionChild(pOptions, (*it).first, (*it).second);

    /* Process network-adapter-specific options */
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = S_OK;
    ComPtr<IMachine> machine;
    ComPtr<INetworkAdapter> nic;
    settings::VmSlot2OptionsIterator it;
    for(it = m->VmSlot2Options.begin(); it != m->VmSlot2Options.end(); ++it)
    {
        alock.release();
        hrc = mVirtualBox->FindMachine(Bstr(it->first.VmName).raw(), machine.asOutParam());
        alock.acquire();

        if (FAILED(hrc))
            continue;

        alock.release();
        hrc = machine->GetNetworkAdapter(it->first.Slot, nic.asOutParam());
        alock.acquire();

        if (FAILED(hrc))
            continue;

        com::Bstr mac;

        alock.release();
        hrc = nic->COMGETTER(MACAddress)(mac.asOutParam());
        alock.acquire();

        if (FAILED(hrc)) /* no MAC address ??? */
            continue;

        /* Convert MAC address from XXXXXXXXXXXX to XX:XX:XX:XX:XX:XX */
        Utf8Str strMacWithoutColons(mac);
        const char *pszSrc = strMacWithoutColons.c_str();
        RTMAC binaryMac;
        if (RTStrConvertHexBytes(pszSrc, &binaryMac, sizeof(binaryMac), 0) != VINF_SUCCESS)
            continue;
        char szMac[18]; /* "XX:XX:XX:XX:XX:XX" */
        if (RTStrPrintHexBytes(szMac, sizeof(szMac), &binaryMac, sizeof(binaryMac), RTSTRPRINTHEXBYTES_F_SEP_COLON) != VINF_SUCCESS)
            continue;

        xml::ElementNode *pMacConfig = pElmRoot->createChild("Config");
        pMacConfig->setAttribute("MACAddress", szMac);

        com::Utf8Str encodedOption;
        settings::DhcpOptionMap &map = i_findOptMapByVmNameSlot(it->first.VmName, it->first.Slot);
        settings::DhcpOptionMap::const_iterator itAdapterOption;
        for (itAdapterOption = map.begin(); itAdapterOption != map.end(); ++itAdapterOption)
            addOptionChild(pMacConfig, (*itAdapterOption).first, (*itAdapterOption).second);
    }

    xml::XmlFileWriter writer(doc);
    writer.write(m->tempConfigFileName, true);

    m->dhcp.setOption(DHCPServerRunner::kDsrKeyConfig, m->tempConfigFileName);
    m->dhcp.setOption(DHCPServerRunner::kDsrKeyComment, m->networkName.c_str());
#else /* !VBOX_WITH_DHCPD */
    /* Main is needed for NATNetwork */
    if (m->router)
        m->dhcp.setOption(NetworkServiceRunner::kNsrKeyNeedMain, "on");

    /* Commmon Network Settings */
    m->dhcp.setOption(NetworkServiceRunner::kNsrKeyNetwork, aNetworkName.c_str());

    if (!aTrunkName.isEmpty())
        m->dhcp.setOption(NetworkServiceRunner::kNsrTrunkName, aTrunkName.c_str());

    m->dhcp.setOption(NetworkServiceRunner::kNsrKeyTrunkType, aTrunkType.c_str());

    /* XXX: should this MAC default initialization moved to NetworkServiceRunner? */
    char strMAC[32];
    Guid guid;
    guid.create();
    RTStrPrintf (strMAC, sizeof(strMAC), "08:00:27:%02X:%02X:%02X",
                 guid.raw()->au8[0],
                 guid.raw()->au8[1],
                 guid.raw()->au8[2]);
    m->dhcp.setOption(NetworkServiceRunner::kNsrMacAddress, strMAC);
    m->dhcp.setOption(NetworkServiceRunner::kNsrIpAddress,  Utf8Str(m->IPAddress).c_str());
    m->dhcp.setOption(NetworkServiceRunner::kNsrIpNetmask, Utf8Str(m->GlobalDhcpOptions[DhcpOpt_SubnetMask].text).c_str());
    m->dhcp.setOption(DHCPServerRunner::kDsrKeyLowerIp, Utf8Str(m->lowerIP).c_str());
    m->dhcp.setOption(DHCPServerRunner::kDsrKeyUpperIp, Utf8Str(m->upperIP).c_str());
#endif /* !VBOX_WITH_DHCPD */

    /* XXX: This parameters Dhcp Server will fetch via API */
    return RT_FAILURE(m->dhcp.start(!m->router /* KillProcOnExit */)) ? E_FAIL : S_OK;
    //m->dhcp.detachFromServer(); /* need to do this to avoid server shutdown on runner destruction */
}


HRESULT DHCPServer::stop (void)
{
#ifdef VBOX_WITH_DHCPD
    if (m->tempConfigFileName[0])
    {
        RTFileDelete(m->tempConfigFileName);
        m->tempConfigFileName[0] = 0;
    }
#endif /* VBOX_WITH_DHCPD */
    return RT_FAILURE(m->dhcp.stop()) ? E_FAIL : S_OK;
}


settings::DhcpOptionMap &DHCPServer::i_findOptMapByVmNameSlot(const com::Utf8Str &aVmName,
                                                              LONG aSlot)
{
    return m->VmSlot2Options[settings::VmNameSlotKey(aVmName, aSlot)];
}
