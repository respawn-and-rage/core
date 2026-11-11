#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/TimerHandle.h"
#include "EROnline.generated.h"

// EOS client credentials. Clients read the packaged EOS.local.ini; a dedicated
// server reads only RR_EOS_* environment variables, never a packaged file.
struct FEROnlineCredentials
{
    FString ProductId, SandboxId, DeploymentId, ClientId, ClientSecret;
    bool IsComplete() const;
};

// EOS is explicitly selected here. Offline/LAN never require a cloud login.
UCLASS()
class FDM_API UEROnline : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    static void SetDiscoveryFilter(FOnlineSessionSearch& Search);
    static FString SanitizeServerName(const FString& Name);
    static FEROnlineCredentials CredentialsFromConfig(const FConfigFile& File);
    static FEROnlineCredentials CredentialsFromEnvironment(TFunctionRef<FString(const TCHAR*)> Read);
    // Listen hosts publish an EOS P2P address; dedicated servers publish IP:port.
    static bool IsJoinEndpointValid(const FString& Address, bool bDedicated);
    // Every dedicated server is a Community server for now: run by anyone with the
    // public Community EOS client, joined at the player's own risk.
    static bool IsCommunityServer(const FOnlineSessionSettings& Settings) { return Settings.bIsDedicated; }
    static FString BrowserLabel(const FOnlineSessionSettings& Settings, int32 OpenSlots);
    // A dedicated server started with -EROnlineServer must host an EOS session
    // and admits nobody until it is advertised.
    static bool RequiresOnline();
    void StartDedicated(int32 Capacity, const FString& MapName);
    void Host();
    void Search();
    void Join(int32 Index);
    void ConnectDirect(const FString& Host, const FString& Port, bool bUseEOS);
    void Cancel();
    void Leave();
    void RegisterPlayer(APlayerController* Player);
    void UnregisterPlayer(AController* Player);
    bool IsBusy() const { return Operation != EOperation::None; }
    bool IsOnline() const { return bOnline; }
    bool HasSession() const { return Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession); }
    FString Status;
    TArray<FOnlineSessionSearchResult> Results;
    virtual void Deinitialize() override;
private:
    enum class EOperation : uint8 { None, Login, Create, Find, Join, Destroy };
    EOperation Operation = EOperation::None;
    bool bCancelled = false;
    bool bOnline = false;
    bool bHostAfterLogin = false;
    FString DirectEndpointAfterLogin;
    FName EOSInstanceIdentifier;
    bool bOwnEOSInstance = false;
    IOnlineIdentityPtr Identity;
    IOnlineSessionPtr Sessions;
    TSharedPtr<FOnlineSessionSearch> Query;
    FDelegateHandle LoginHandle, CreateHandle, FindHandle, JoinHandle, DestroyHandle;
    FTimerHandle DedicatedRetry;
    int32 DedicatedCapacity = 0;
    FString DedicatedMap;
    void DedicatedCreateComplete(FName Name, bool Success);
    void RetryDedicated();
    bool Configure();
    void Begin(bool Hosting, const FString& DirectEndpoint = FString());
    void TravelToServer(const FString& Address);
    void Continue();
    void LoginComplete(int32 User, bool Success, const FUniqueNetId& Id, const FString& Error);
    void CreateComplete(FName Name, bool Success);
    void FindComplete(bool Success);
    void JoinComplete(FName Name, EOnJoinSessionCompleteResult::Type Result);
    void DestroyComplete(FName Name, bool Success);
    void Destroy();
    void Fail(const FString& Message);
};
