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

#if !RR_EOS_DEVICE_ID
#error Prepare the project EOS plugin with scripts/prepare-eos-guest.py before building.
#endif

namespace
{
const FName VersionKey(TEXT("RR_VERSION"));
const FName GameKey(TEXT("RR_GAME"));
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
bool UEROnline::Configure()
{
    if (Sessions.IsValid() && Identity.IsValid()) return true;
    // Runtime-only private configuration: never flush these values to public ini.
    FConfigFile Private;
    Private.Read(FPaths::ProjectConfigDir() / TEXT("EOS.local.ini"));
    FArtifactSettings Artifact;
    Artifact.ArtifactName = TEXT("RespawnAndRage");
    Private.GetString(TEXT("EOS"), TEXT("ProductId"), Artifact.ProductId);
    Private.GetString(TEXT("EOS"), TEXT("SandboxId"), Artifact.SandboxId);
    Private.GetString(TEXT("EOS"), TEXT("DeploymentId"), Artifact.DeploymentId);
    Private.GetString(TEXT("EOS"), TEXT("ClientId"), Artifact.ClientId);
    Private.GetString(TEXT("EOS"), TEXT("ClientSecret"), Artifact.ClientSecret);
    if (Artifact.ProductId.IsEmpty() || Artifact.SandboxId.IsEmpty() || Artifact.DeploymentId.IsEmpty() || Artifact.ClientId.IsEmpty() || Artifact.ClientSecret.IsEmpty())
    { Fail(TEXT("Internet play is not configured in this build. LAN play is available.")); return false; }
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
        FOnlineSessionSettings Settings;
        Settings.Set(OSSEOS_BUCKET_ID_ATTRIBUTE_KEY, FString(TEXT("RespawnAndRage")), EOnlineDataAdvertisementType::ViaOnlineService);
        Settings.bIsLANMatch = false;
        Settings.bShouldAdvertise = true;
        Settings.bAllowJoinInProgress = true;
        Settings.bUseLobbiesIfAvailable = false;
        Settings.bUsesPresence = false;
        Settings.NumPublicConnections = GI->MaxPlayers;
        Settings.NumPrivateConnections = 0;
        Settings.Set(FName(TEXT("RR_SERVER_NAME")), SanitizeServerName(GI->InternetServerName), EOnlineDataAdvertisementType::ViaOnlineService);
        Settings.Set(GameKey, FString(TEXT("RespawnAndRage")), EOnlineDataAdvertisementType::ViaOnlineService);
        Settings.Set(FName(TEXT("RR_HOST")), AERGameMode::SanitizeName(GI->LocalName), EOnlineDataAdvertisementType::ViaOnlineService);
        Settings.Set(VersionKey, GI->GetNetworkBuildVersion(), EOnlineDataAdvertisementType::ViaOnlineService);
        Settings.Set(SETTING_MAPNAME, FString(TEXT("Lobby")), EOnlineDataAdvertisementType::ViaOnlineService);
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
    if (!Sessions->GetResolvedConnectString(NAME_GameSession, Address) || !Address.StartsWith(TEXT("[EOS:")))
    { Status = TEXT("This server did not provide a valid EOS P2P endpoint."); Destroy(); return; }
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
