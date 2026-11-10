#include "EROnline.h"
#include "ERLoadout.h"
#include "ERPlayerController.h"
#include "ERMatch.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSubsystemEOSTypesPublic.h"
#include "Online/OnlineSessionNames.h"
#include "EOSSettings.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/CoreMisc.h"
#include "Misc/PackageName.h"
#include "HAL/PlatformMisc.h"
#include "TimerManager.h"

#if !RR_EOS_DEVICE_ID
#error Prepare the project EOS plugin with scripts/prepare-eos-guest.py before building.
#endif

namespace
{
const FName VersionKey(TEXT("RR_VERSION"));
const FName GameKey(TEXT("RR_GAME"));
FOnlineSessionSettings MakeSessionSettings(int32 Capacity, const FString& ServerName, const FString& Host, const FString& Version, const FString& Map)
{
    FOnlineSessionSettings Settings;
    Settings.Set(OSSEOS_BUCKET_ID_ATTRIBUTE_KEY, FString(TEXT("RespawnAndRage")), EOnlineDataAdvertisementType::ViaOnlineService);
    Settings.bIsLANMatch = false;
    Settings.bShouldAdvertise = true;
    Settings.bAllowJoinInProgress = true;
    Settings.bUseLobbiesIfAvailable = false;
    Settings.bUsesPresence = false;
    Settings.NumPublicConnections = Capacity;
    Settings.NumPrivateConnections = 0;
    Settings.Set(FName(TEXT("RR_SERVER_NAME")), UEROnline::SanitizeServerName(ServerName), EOnlineDataAdvertisementType::ViaOnlineService);
    Settings.Set(GameKey, FString(TEXT("RespawnAndRage")), EOnlineDataAdvertisementType::ViaOnlineService);
    Settings.Set(FName(TEXT("RR_HOST")), Host, EOnlineDataAdvertisementType::ViaOnlineService);
    Settings.Set(VersionKey, Version, EOnlineDataAdvertisementType::ViaOnlineService);
    Settings.Set(SETTING_MAPNAME, Map, EOnlineDataAdvertisementType::ViaOnlineService);
    return Settings;
}
bool IsPort(const FString& Text)
{
    int32 Port = 0;
    return !Text.IsEmpty() && Text.Len() <= 5 && Text.IsNumeric() && LexTryParseString(Port, *Text) && Port >= 1 && Port <= 65535;
}
FString PlayerOptions(UERGameInstance* GI)
{
    return GI->LoadoutOptions() + FString::Printf(TEXT("?Name=%s?Team=%d?Primary=%d?AlphaSkin=%d?OmegaSkin=%d?Magazines=%d?HE=%d?Smoke=%d?Stun=%d"),
        *AERGameMode::SanitizeName(GI->LocalName), FMath::Clamp(GI->PlayerTeam,0,1), GI->GetStartingWeapon(),
        GI->AlphaSkin, GI->OmegaSkin, GI->SpareMagazines, GI->HECount, GI->SmokeCount, GI->StunCount);
}
}
void UEROnline::Fail(const FString& Message)
{
    Operation = EOperation::None;
    Status = Message;
    UE_LOG(LogTemp, Warning, TEXT("ER_ONLINE_ERROR %s"), *Message);
}
bool FEROnlineCredentials::IsComplete() const
{
    return !ProductId.IsEmpty() && !SandboxId.IsEmpty() && !DeploymentId.IsEmpty() && !ClientId.IsEmpty() && !ClientSecret.IsEmpty();
}
FEROnlineCredentials UEROnline::CredentialsFromConfig(const FConfigFile& File)
{
    FEROnlineCredentials Result;
    File.GetString(TEXT("EOS"), TEXT("ProductId"), Result.ProductId);
    File.GetString(TEXT("EOS"), TEXT("SandboxId"), Result.SandboxId);
    File.GetString(TEXT("EOS"), TEXT("DeploymentId"), Result.DeploymentId);
    File.GetString(TEXT("EOS"), TEXT("ClientId"), Result.ClientId);
    File.GetString(TEXT("EOS"), TEXT("ClientSecret"), Result.ClientSecret);
    return Result;
}
FEROnlineCredentials UEROnline::CredentialsFromEnvironment(TFunctionRef<FString(const TCHAR*)> Read)
{
    FEROnlineCredentials Result;
    Result.ProductId = Read(TEXT("RR_EOS_PRODUCT_ID")).TrimStartAndEnd();
    Result.SandboxId = Read(TEXT("RR_EOS_SANDBOX_ID")).TrimStartAndEnd();
    Result.DeploymentId = Read(TEXT("RR_EOS_DEPLOYMENT_ID")).TrimStartAndEnd();
    Result.ClientId = Read(TEXT("RR_EOS_CLIENT_ID")).TrimStartAndEnd();
    Result.ClientSecret = Read(TEXT("RR_EOS_CLIENT_SECRET")).TrimStartAndEnd();
    return Result;
}
bool UEROnline::IsJoinEndpointValid(const FString& Address, bool bDedicated)
{
    if (!bDedicated) return Address.StartsWith(TEXT("[EOS:"));
    // A dedicated session advertises the public IP EOS observed plus our port
    // attribute. Accept only a literal address and port: nothing that could
    // carry a map path or URL options into ClientTravel.
    FString Host, Port;
    if (Address.StartsWith(TEXT("[")))
    {
        const int32 Close = Address.Find(TEXT("]:"));
        if (Close <= 1) return false;
        Host = Address.Mid(1, Close - 1);
        Port = Address.Mid(Close + 2);
        for (const TCHAR C : Host) if (!FChar::IsHexDigit(C) && C != TEXT(':') && C != TEXT('.')) return false;
        return Host.Contains(TEXT(":")) && IsPort(Port);
    }
    if (!Address.Split(TEXT(":"), &Host, &Port) || Port.Contains(TEXT(":"))) return false;
    TArray<FString> Parts;
    if (Host.ParseIntoArray(Parts, TEXT("."), false) != 4) return false;
    for (const FString& Part : Parts)
    {
        int32 Value = -1;
        if (Part.IsEmpty() || Part.Len() > 3 || !Part.IsNumeric() || !LexTryParseString(Value, *Part) || Value < 0 || Value > 255) return false;
    }
    return IsPort(Port);
}
FString UEROnline::BrowserLabel(const FOnlineSessionSettings& Settings, int32 OpenSlots)
{
    FString HostName, ServerName;
    Settings.Get(FName(TEXT("RR_HOST")), HostName);
    Settings.Get(FName(TEXT("RR_SERVER_NAME")), ServerName);
    const FString Name = SanitizeServerName(ServerName.IsEmpty() ? HostName : ServerName);
    return FString::Printf(TEXT("Join %s%s · %d / %d players"), IsCommunityServer(Settings) ? TEXT("[Community] ") : TEXT(""), *Name,
        Settings.NumPublicConnections - OpenSlots, Settings.NumPublicConnections);
}
bool UEROnline::RequiresOnline()
{
    return IsRunningDedicatedServer() && FParse::Param(FCommandLine::Get(), TEXT("EROnlineServer"));
}
bool UEROnline::Configure()
{
    if (Sessions.IsValid() && Identity.IsValid()) return true;
    FEROnlineCredentials Credentials;
    if (IsRunningDedicatedServer())
    {
        // The server credential is supplied at runtime only; the Server target
        // never stages EOS.local.ini.
        Credentials = CredentialsFromEnvironment([](const TCHAR* Name) { return FPlatformMisc::GetEnvironmentVariable(Name); });
        if (!Credentials.IsComplete())
        { Fail(TEXT("EOS server credentials missing: set RR_EOS_PRODUCT_ID, RR_EOS_SANDBOX_ID, RR_EOS_DEPLOYMENT_ID, RR_EOS_CLIENT_ID and RR_EOS_CLIENT_SECRET.")); return false; }
    }
    else
    {
        // Runtime-only private configuration: never flush these values to public ini.
        FConfigFile Private;
        Private.Read(FPaths::ProjectConfigDir() / TEXT("EOS.local.ini"));
        Credentials = CredentialsFromConfig(Private);
        if (!Credentials.IsComplete())
        { Fail(TEXT("Internet play is not configured in this build. LAN play is available.")); return false; }
    }
    FArtifactSettings Artifact;
    Artifact.ArtifactName = TEXT("RespawnAndRage");
    Artifact.ProductId = Credentials.ProductId;
    Artifact.SandboxId = Credentials.SandboxId;
    Artifact.DeploymentId = Credentials.DeploymentId;
    Artifact.ClientId = Credentials.ClientId;
    Artifact.ClientSecret = Credentials.ClientSecret;
    auto* Settings = GetMutableDefault<UEOSSettings>();
    Settings->DefaultArtifactName = Artifact.ArtifactName;
    Settings->Artifacts = {Artifact};
    Settings->bPreferPersistentAuth = false;
    Settings->bUseEAS = false;
    Settings->bUseEOSConnect = true;
    Settings->bUseEOSRTC = false;
    Settings->bEnableOverlay = false;
    Settings->bEnableSocialOverlay = false;
    Settings->AuthScopeFlags.Empty();
    EOSInstanceIdentifier = FName(TEXT("EOS"));
#if UE_EDITOR
    if (auto* Utils = Online::GetUtils()) EOSInstanceIdentifier = Utils->GetOnlineIdentifier(GetWorld(), EOSInstanceIdentifier);
#endif
    bOwnEOSInstance = !IOnlineSubsystem::DoesInstanceExist(EOSInstanceIdentifier);
    auto* EOS = Online::GetSubsystem(GetWorld(), FName(TEXT("EOS")));
    if (!EOS) { Fail(TEXT("Epic Online Services could not initialize. Check this build's EOS configuration.")); return false; }
    Identity = EOS->GetIdentityInterface();
    Sessions = EOS->GetSessionInterface();
    if (!Identity.IsValid() || !Sessions.IsValid()) { Fail(TEXT("Epic Online Services is unavailable.")); return false; }
    return true;
}
FString UEROnline::SanitizeServerName(const FString& Name)
{
    FString Clean;
    for (const TCHAR C : Name.TrimStartAndEnd())
        if (!FChar::IsControl(C) && Clean.Len() < 48) Clean.AppendChar(C);
    Clean.TrimStartAndEndInline();
    return Clean.IsEmpty() ? TEXT("Respawn & Rage") : Clean;
}
void UEROnline::SetDiscoveryFilter(FOnlineSessionSearch& Search)
{
    // OSS EOS otherwise uses the engine build ID as its implicit bucket, which
    // can differ across platforms. RR_VERSION remains the compatibility gate.
    Search.QuerySettings.Set(OSSEOS_BUCKET_ID_ATTRIBUTE_KEY, FString(TEXT("RespawnAndRage")), EOnlineComparisonOp::Equals);
}
void UEROnline::Host() { Begin(true); }
void UEROnline::Search() { Begin(false); }
void UEROnline::Begin(bool Hosting)
{
    if (IsBusy() || bOnline) return;
    auto* GI = Cast<UERGameInstance>(GetGameInstance());
    if (!GI || GetWorld()->GetNetMode() != NM_Standalone) return;
    GI->CancelLANSearch();
    GI->SaveProfile();
    Results.Empty(); bCancelled = false; bHostAfterLogin = Hosting;
    if (!Configure()) return;
    if (Sessions->GetNamedSession(NAME_GameSession))
    { Status = TEXT("Leaving the previous session. Please retry shortly."); Destroy(); return; }
    if (Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn) { Continue(); return; }
    Operation = EOperation::Login;
    Status = TEXT("Connecting to online services...");
    LoginHandle = Identity->AddOnLoginCompleteDelegate_Handle(0, FOnLoginCompleteDelegate::CreateUObject(this, &UEROnline::LoginComplete));
    // Device credentials stay in EOS's OS-local secure storage. This path never
    // calls EAS/Account Portal or uses a launcher's Epic credentials.
    const FOnlineAccountCredentials Guest(TEXT("connect:DeviceIdAccessToken"), AERGameMode::SanitizeName(GI->LocalName), FString());
    if (!Identity->Login(0, Guest) && Operation == EOperation::Login)
    { Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginHandle); Fail(TEXT("Online access could not start. Please retry.")); }
}
void UEROnline::LoginComplete(int32 User, bool Success, const FUniqueNetId& Id, const FString& Error)
{
    Identity->ClearOnLoginCompleteDelegate_Handle(User, LoginHandle);
    Operation = EOperation::None;
    if (bCancelled) { Status = TEXT("Cancelled."); return; }
    if (!Success) { Fail(TEXT("Online access failed. Check your connection and retry.")); return; }
    if (auto* Player = GetGameInstance()->GetFirstGamePlayer()) Player->SetCachedUniqueNetId(Identity->GetUniquePlayerId(0));
    UE_LOG(LogTemp, Display, TEXT("ER_ONLINE_LOGIN success mode=device_id"));
    Continue();
}
void UEROnline::Continue()
{
    auto* GI = CastChecked<UERGameInstance>(GetGameInstance());
    if (auto* Player = GI->GetFirstGamePlayer()) Player->SetCachedUniqueNetId(Identity->GetUniquePlayerId(0));
    if (bHostAfterLogin)
    {
        GI->CommitServerRules();
        const FOnlineSessionSettings Settings = MakeSessionSettings(GI->MaxPlayers, GI->InternetServerName,
            AERGameMode::SanitizeName(GI->LocalName), GI->GetNetworkBuildVersion(), TEXT("Lobby"));
        Operation = EOperation::Create;
        Status = TEXT("Creating Internet server...");
        CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &UEROnline::CreateComplete));
        if (!Sessions->CreateSession(0, NAME_GameSession, Settings) && Operation == EOperation::Create)
        { Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle); Fail(TEXT("Could not create the Internet server. Please retry.")); }
    }
    else
    {
        Query = MakeShared<FOnlineSessionSearch>();
        SetDiscoveryFilter(*Query);
        Query->bIsLanQuery = false;
        Query->MaxSearchResults = 100;
        Query->TimeoutInSeconds = 30;
        Query->QuerySettings.Set(GameKey, FString(TEXT("RespawnAndRage")), EOnlineComparisonOp::Equals);
        Query->QuerySettings.Set(VersionKey, GI->GetNetworkBuildVersion(), EOnlineComparisonOp::Equals);
        Operation = EOperation::Find;
        Status = TEXT("Searching Internet servers...");
        FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this, &UEROnline::FindComplete));
        if (!Sessions->FindSessions(0, Query.ToSharedRef()) && Operation == EOperation::Find)
        { Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle); Fail(TEXT("Internet search could not start. Please retry.")); }
    }
}
void UEROnline::CreateComplete(FName Name, bool Success)
{
    Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
    Operation = EOperation::None;
    if (bCancelled) { Destroy(); return; }
    if (!Success) { Fail(TEXT("Internet server creation failed. Check the EOS client policy and server capacity, then retry.")); return; }
    bOnline = true;
    Status = TEXT("Internet server ready.");
    auto* GI = CastChecked<UERGameInstance>(GetGameInstance());
    GI->SessionMode = 1;
    const FString Options = FString::Printf(TEXT("listen?EROnline=1?ERLobby=1?ERMaxPlayers=%d?MaxPlayers=%d?ERAlphaBots=%d?EROmegaBots=%d"),
        GI->MaxPlayers, GI->MaxPlayers, FMath::Clamp(GI->AlphaBots,0,10), FMath::Clamp(GI->OmegaBots,0,10)) + PlayerOptions(GI);
    UE_LOG(LogTemp, Display, TEXT("ER_ONLINE_HOST capacity=%d"), GI->MaxPlayers);
    UGameplayStatics::OpenLevel(GetWorld(), TEXT("/Game/EclipseRecoil/Maps/L_MainMenu"), true, Options);
}
void UEROnline::StartDedicated(int32 Capacity, const FString& MapName)
{
    if (!RequiresOnline()) return;
    DedicatedCapacity = FMath::Max(1, Capacity);
    DedicatedMap = FPackageName::GetShortName(MapName);
    if (IsBusy() || bOnline) return;
    GetGameInstance()->GetTimerManager().ClearTimer(DedicatedRetry);
    if (!Configure()) { GetGameInstance()->GetTimerManager().SetTimer(DedicatedRetry, this, &UEROnline::RetryDedicated, 60.f, false); return; }
    // A stale local session (e.g. after a failed update) is cleared first.
    if (Sessions->GetNamedSession(NAME_GameSession))
    { Destroy(); GetGameInstance()->GetTimerManager().SetTimer(DedicatedRetry, this, &UEROnline::RetryDedicated, 60.f, false); return; }
    const auto* GI = CastChecked<UERGameInstance>(GetGameInstance());
    // No user login: OSS EOS runs as a server (bIsServer) and EOS advertises
    // this host's public IP plus the game port. Players connect over IP.
    FOnlineSessionSettings Settings = MakeSessionSettings(DedicatedCapacity, FPlatformMisc::GetEnvironmentVariable(TEXT("RR_SERVER_NAME")),
        TEXT("Community server"), GI->GetNetworkBuildVersion(), DedicatedMap);
    Settings.bIsDedicated = true;
    Operation = EOperation::Create;
    Status = TEXT("Creating dedicated Internet session...");
    CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &UEROnline::DedicatedCreateComplete));
    if (!Sessions->CreateSession(0, NAME_GameSession, Settings) && Operation == EOperation::Create)
    {
        Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
        Fail(TEXT("Could not create the dedicated Internet session; retrying in 60s."));
        GetGameInstance()->GetTimerManager().SetTimer(DedicatedRetry, this, &UEROnline::RetryDedicated, 60.f, false);
    }
}
void UEROnline::DedicatedCreateComplete(FName Name, bool Success)
{
    Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
    Operation = EOperation::None;
    if (!Success)
    {
        Fail(TEXT("Dedicated Internet session creation failed (check the EOS server client policy); retrying in 60s."));
        GetGameInstance()->GetTimerManager().SetTimer(DedicatedRetry, this, &UEROnline::RetryDedicated, 60.f, false);
        return;
    }
    bOnline = true;
    Status = TEXT("Dedicated Internet session ready.");
    if (auto* State = GetWorld() ? GetWorld()->GetGameState<AERGameState>() : nullptr) State->bOnline = true;
    UE_LOG(LogTemp, Display, TEXT("ER_ONLINE_DEDICATED ready capacity=%d map=%s"), DedicatedCapacity, *DedicatedMap);
}
void UEROnline::RetryDedicated() { StartDedicated(DedicatedCapacity, DedicatedMap); }
void UEROnline::FindComplete(bool Success)
{
    Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
    Operation = EOperation::None;
    if (bCancelled) { Status = TEXT("Cancelled."); return; }
    if (!Success) { Fail(TEXT("Internet search failed. Check your connection and retry.")); return; }
    auto* GI = CastChecked<UERGameInstance>(GetGameInstance());
    Results.Empty();
    for (const auto& Result : Query->SearchResults)
    {
        FString Game, Version;
        Result.Session.SessionSettings.Get(GameKey, Game);
        Result.Session.SessionSettings.Get(VersionKey, Version);
        if (Game == TEXT("RespawnAndRage") && Version == GI->GetNetworkBuildVersion() && Result.Session.NumOpenPublicConnections > 0)
            Results.Add(Result);
    }
    Status = Results.IsEmpty() ? TEXT("No compatible Internet servers found. Check that the host is online and running the same game version, then search again.") : FString::Printf(TEXT("%d Internet server(s) found. Choose a server to join."), Results.Num());
    UE_LOG(LogTemp, Display, TEXT("ER_ONLINE_SEARCH results=%d"), Results.Num());
}
void UEROnline::Join(int32 Index)
{
    if (IsBusy() || bOnline || !Sessions.IsValid() || !Results.IsValidIndex(Index)) return;
    bCancelled = false;
    Operation = EOperation::Join;
    Status = TEXT("Joining Internet server...");
    JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &UEROnline::JoinComplete));
    if (!Sessions->JoinSession(0, NAME_GameSession, Results[Index]) && Operation == EOperation::Join)
    { Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle); Fail(TEXT("Could not join this Internet server. Refresh the list and retry.")); }
}
void UEROnline::JoinComplete(FName Name, EOnJoinSessionCompleteResult::Type Result)
{
    Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
    Operation = EOperation::None;
    if (bCancelled) { Destroy(); return; }
    if (Result != EOnJoinSessionCompleteResult::Success)
    { Status = Result == EOnJoinSessionCompleteResult::SessionIsFull ? TEXT("Server full. Search again.") : TEXT("Unable to join. The server may have closed; search again."); Destroy(); return; }
    FString Address;
    const auto* Joined = Sessions->GetNamedSession(NAME_GameSession);
    const bool bDedicated = Joined && Joined->SessionSettings.bIsDedicated;
    if (!Sessions->GetResolvedConnectString(NAME_GameSession, Address) || !IsJoinEndpointValid(Address, bDedicated))
    { Status = TEXT("This server did not provide a valid connection endpoint."); Destroy(); return; }
    auto* GI = CastChecked<UERGameInstance>(GetGameInstance());
    auto* PC = GI->GetFirstLocalPlayerController();
    if (!PC) { Status = TEXT("Local player unavailable."); Destroy(); return; }
    GI->SaveProfile();
    GI->SessionMode = 2;
    bOnline = true;
    Status = TEXT("Connecting to Internet server...");
    UE_LOG(LogTemp, Display, TEXT("ER_ONLINE_JOIN connecting"));
    PC->ClientTravel(Address + PlayerOptions(GI), TRAVEL_Absolute);
}
void UEROnline::Cancel()
{
    Results.Empty();
    if (IsBusy()) { bCancelled = true; Status = TEXT("Cancelling the current online request..."); }
    else Status.Empty();
}
void UEROnline::Leave()
{
    Cancel(); bOnline = false;
    if (!IsBusy() && Sessions.IsValid()) Destroy();
}
void UEROnline::Destroy()
{
    bOnline = false;
    if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession)) { Operation = EOperation::None; return; }
    Operation = EOperation::Destroy;
    DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this, &UEROnline::DestroyComplete));
    if (!Sessions->DestroySession(NAME_GameSession) && Operation == EOperation::Destroy)
    { Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle); Fail(TEXT("Could not leave the previous online session. Please restart the game.")); }
}
void UEROnline::DestroyComplete(FName Name, bool Success)
{
    Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
    Operation = EOperation::None;
    if (!Success) Fail(TEXT("Could not leave the previous online session. Please restart the game."));
    else if (bCancelled) Status = TEXT("Cancelled.");
}
void UEROnline::RegisterPlayer(APlayerController* Player)
{
    if (!bOnline || !Sessions.IsValid() || !Player || !Player->PlayerState) return;
    const auto Id = Player->PlayerState->GetUniqueId();
    if (Id.IsValid() && Id.GetType() == FName(TEXT("EOS"))) Sessions->RegisterPlayer(NAME_GameSession, *Id.GetUniqueNetId(), false);
}
void UEROnline::UnregisterPlayer(AController* Player)
{
    if (!bOnline || !Sessions.IsValid() || !Player || !Player->PlayerState) return;
    const auto Id = Player->PlayerState->GetUniqueId();
    if (Id.IsValid() && Id.GetType() == FName(TEXT("EOS"))) Sessions->UnregisterPlayer(NAME_GameSession, *Id.GetUniqueNetId());
}
void UEROnline::Deinitialize()
{
    // GameEngine shuts down world net drivers before GameInstance teardown.
    // Release our lazily-created EOS instance now, while EOSShared and its SDK
    // are still loaded, rather than relying on the later module unload order.
    if (Identity.IsValid()) Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginHandle);
    if (auto* GI = GetGameInstance()) GI->GetTimerManager().ClearTimer(DedicatedRetry);
    if (Sessions.IsValid())
    {
        Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
        Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
        Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
        Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
        if (Sessions->GetNamedSession(NAME_GameSession)) Sessions->DestroySession(NAME_GameSession);
    }
    Results.Empty(); Query.Reset(); Sessions.Reset(); Identity.Reset();
    if (bOwnEOSInstance && !EOSInstanceIdentifier.IsNone())
    {
        IOnlineSubsystem::Destroy(EOSInstanceIdentifier);
        UE_LOG(LogTemp, Display, TEXT("ER_ONLINE_SHUTDOWN instance_released"));
        bOwnEOSInstance = false;
    }
    Super::Deinitialize();
}
