#include "Modules/ModuleManager.h"
#include "Misc/NetworkVersion.h"
#include "ERNetworkQuality.h"
class FERGameModule final : public FDefaultGameModuleImpl
{
    virtual void StartupModule() override
    {
        FNetworkVersion::SetGameNetworkProtocolVersion(ERNetworkProtocol::Version);
        FNetworkVersion::SetGameCompatibleNetworkProtocolVersion(ERNetworkProtocol::Version);
    }
};
IMPLEMENT_PRIMARY_GAME_MODULE(FERGameModule, fdm, "RespawnAndRage");
