#include "Modules/ModuleManager.h"
#include "Misc/NetworkVersion.h"
#include "Misc/CoreDelegates.h"
#include "ERMatchStats.h"
#include "ERNetworkQuality.h"
class FERGameModule final : public FDefaultGameModuleImpl
{
    virtual void StartupModule() override
    {
        FNetworkVersion::SetGameNetworkProtocolVersion(ERNetworkProtocol::Version);
        FNetworkVersion::SetGameCompatibleNetworkProtocolVersion(ERNetworkProtocol::Version);
        ExitHandle = FCoreDelegates::OnEnginePreExit.AddStatic(&FERMatchStats::WaitForWrites);
    }

    virtual void ShutdownModule() override
    {
        FCoreDelegates::OnEnginePreExit.Remove(ExitHandle);
        FERMatchStats::WaitForWrites();
    }

    FDelegateHandle ExitHandle;
};
IMPLEMENT_PRIMARY_GAME_MODULE(FERGameModule, fdm, "RespawnAndRage");
