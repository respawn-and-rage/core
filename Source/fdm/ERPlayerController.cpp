#include "ERPlayerController.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "ERLoadout.h"
#include "ERClaymore.h"
#include "ERAudioProbe.h"
#include "ERAimPunchProbe.h"
#include "EROnline.h"
#include "ERRecon.h"
#include "ERInterfaceStyle.h"
#include "ERHUDLayout.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "ERMenuPreview.h"
#include "Containers/Ticker.h"
#include "ERMapCatalog.h"
#include "Internationalization/Internationalization.h"
#include "ERMouseResponse.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#include "ERCharacter.h"
#include "ERMovement.h"
#include "ERMatch.h"
#include "ERWeapon.h"
#include "ERBlockRoom.h"
#include "ERBlockNetworkProbe.h"
#include "ERBlockTarget.h"
#include "ERProceduralMap.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameUserSettings.h"
#include "Scalability.h"
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "UnrealClient.h"
#include "DynamicResolutionState.h"
#include "ERResolutionPolicy.h"
#include "ERPerformanceSettings.h"
#include "ERLadder.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/NetworkVersion.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "GlobalRenderResources.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"

#if PLATFORM_MAC
void ERSetMacGameIcon(const FString& IconPath);
#endif

void UERGameInstance::Init()
{
    Super::Init();
    // Apply the game's rendering profile in every executable. Previously these
    // groups only lived in play.command, leaving packaged games at Epic quality.
    auto Quality = Scalability::GetQualityLevels();
    Quality.AntiAliasingQuality = 2;
    Quality.GlobalIlluminationQuality = 0;
    Quality.ReflectionQuality = 0;
    Quality.ShadowQuality = 1;
    Scalability::SetQualityLevels(Quality);
    UE_LOG(LogTemp, Display, TEXT("ER_RENDER_PROFILE aa=2 gi=0 reflections=0 shadows=1"));
    if (!FFileHelper::LoadFileToString(BuildVersion, *(FPaths::ProjectDir() / TEXT("Build/GameVersion.txt"))))
        BuildVersion = TEXT("unversioned");
    BuildVersion.TrimStartAndEndInline();
    FNetworkVersion::SetProjectVersion(*GetNetworkBuildVersion());
    UE_LOG(LogTemp, Display, TEXT("ER_BUILD_VERSION %s"), *BuildVersion);
    FInternationalization::Get().SetCurrentLanguage(TEXT("en"));
#if PLATFORM_MAC
    // Local -game runs use the Editor executable, so also set this process's Dock icon.
    if (!GIsEditor && !IsRunningDedicatedServer())
        ERSetMacGameIcon(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Branding/icon.png")));
#endif
    GConfig->GetInt(TEXT("EclipseRecoil.Loadout"), TEXT("PrimaryWeapon"), PrimaryWeapon, GGameUserSettingsIni);
    PrimaryWeapon = FMath::Clamp(PrimaryWeapon, 0, 6);
    GConfig->GetInt(TEXT("EclipseRecoil.Loadout"), TEXT("ReferenceStartingWeapon"), ReferenceStartingWeapon, GGameUserSettingsIni);
    FString SavedPreset,SavedVariant;
    int32 Version=0;GConfig->GetInt(TEXT("EclipseRecoil.Loadout"),TEXT("Version"),Version,GGameUserSettingsIni);
    GConfig->GetString(TEXT("EclipseRecoil.Loadout"),TEXT("Preset"),SavedPreset,GGameUserSettingsIni);
    GConfig->GetString(TEXT("EclipseRecoil.Loadout"),TEXT("Variant"),SavedVariant,GGameUserSettingsIni);
    const auto Kit=ERLoadouts::Resolve(Version>=1?FName(*SavedPreset):ERLoadouts::Migrate(ReferenceStartingWeapon),Version>=1?FName(*SavedVariant):FName("offense"));
    LoadoutPreset=Kit.Preset;LoadoutVariant=Kit.Variant;
    GConfig->GetInt(TEXT("EclipseRecoil.Appearance"), TEXT("AlphaSkin"), AlphaSkin, GGameUserSettingsIni);
    GConfig->GetInt(TEXT("EclipseRecoil.Appearance"), TEXT("OmegaSkin"), OmegaSkin, GGameUserSettingsIni);
    AlphaSkin = ERSkins::ValidIndex(AlphaSkin); OmegaSkin = ERSkins::ValidIndex(OmegaSkin);
    GConfig->GetFloat(TEXT("EclipseRecoil.Input"), TEXT("MouseSensitivity"), MouseSensitivity, GGameUserSettingsIni);
    MouseSensitivity = FMath::IsFinite(MouseSensitivity) ? FMath::Clamp(MouseSensitivity, 0.f, 1.f) : .5f;
    GConfig->GetFloat(TEXT("EclipseRecoil.Input"), TEXT("MouseAcceleration"), MouseAcceleration, GGameUserSettingsIni);
    MouseAcceleration = FMath::IsFinite(MouseAcceleration) ? FMath::Clamp(MouseAcceleration, 0.f, 1.f) : 0.f;
    GConfig->GetFloat(TEXT("EclipseRecoil.Audio"), TEXT("SoundVolume"), SoundVolume, GGameUserSettingsIni);
    SetSoundVolume(SoundVolume);
    GConfig->GetFloat(TEXT("EclipseRecoil.Audio"), TEXT("MusicVolume"), MusicVolume, GGameUserSettingsIni);
    MusicVolume = FMath::IsFinite(MusicVolume) ? FMath::Clamp(MusicVolume, 0.f, 1.f) : 1.f;
    GConfig->GetString(TEXT("EclipseRecoil.Profile"), TEXT("Name"), LocalName, GGameUserSettingsIni);
    GConfig->GetInt(TEXT("EclipseRecoil.Profile"), TEXT("Team"), PlayerTeam, GGameUserSettingsIni);
    PlayerTeam = FMath::Clamp(PlayerTeam, 0, 1);
    for (auto Pair : {TPair<const TCHAR*, int32*>(TEXT("Magazines"), &SpareMagazines),
        {TEXT("HE"), &HECount}, {TEXT("Smoke"), &SmokeCount}, {TEXT("Stun"), &StunCount}})
    {
        GConfig->GetInt(TEXT("EclipseRecoil.Loadout"), Pair.Key, *Pair.Value, GGameUserSettingsIni);
        *Pair.Value = FMath::Clamp(*Pair.Value, 0, 20);
    }
    GConfig->GetString(TEXT("EclipseRecoil.Server"), TEXT("InternetServerName"), InternetServerName, GGameUserSettingsIni);
    GConfig->GetInt(TEXT("EclipseRecoil.Server"), TEXT("MaxPlayers"), MaxPlayers, GGameUserSettingsIni);
    MaxPlayers = ERServer::ValidMaxPlayers(MaxPlayers);
    ERServer::Load(SessionRules,GGameIni,TEXT("RespawnAndRage.ServerRules"));
    ERServer::Load(ServerRules,GGameUserSettingsIni,TEXT("RespawnAndRage.ServerRules"));
    // Game.ini remains the source for direct/dedicated launches.
    for(const auto& V:SessionRules.Values)if(!ServerRules.Has(V.Key))ServerRules.Set(V.Key,V.Value);
    DiscoveryTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UERGameInstance::TickDiscovery), .1f);
    NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UERGameInstance::NetworkFailure);
    TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UERGameInstance::TravelFailure);
}
FString UERGameInstance::LoadoutOptions() const
{ return FString::Printf(TEXT("?Preset=%s?Variant=%s"),*LoadoutPreset.ToString(),*LoadoutVariant.ToString()); }
void UERGameInstance::SaveLoadout()
{
    GConfig->SetInt(TEXT("EclipseRecoil.Loadout"),TEXT("Version"),1,GGameUserSettingsIni);
    GConfig->SetString(TEXT("EclipseRecoil.Loadout"),TEXT("Preset"),*LoadoutPreset.ToString(),GGameUserSettingsIni);
    GConfig->SetString(TEXT("EclipseRecoil.Loadout"),TEXT("Variant"),*LoadoutVariant.ToString(),GGameUserSettingsIni);
    GConfig->SetInt(TEXT("EclipseRecoil.Loadout"), TEXT("PrimaryWeapon"), PrimaryWeapon, GGameUserSettingsIni);
    GConfig->SetInt(TEXT("EclipseRecoil.Loadout"), TEXT("ReferenceStartingWeapon"), ReferenceStartingWeapon, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}
void UERGameInstance::SaveSkins()
{
    GConfig->SetInt(TEXT("EclipseRecoil.Appearance"), TEXT("AlphaSkin"), AlphaSkin, GGameUserSettingsIni);
    GConfig->SetInt(TEXT("EclipseRecoil.Appearance"), TEXT("OmegaSkin"), OmegaSkin, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}
void UERGameInstance::Shutdown()
{
    FTSTicker::GetCoreTicker().RemoveTicker(DiscoveryTicker);
    CloseDiscoverySocket();
    GetSubsystem<UEROnline>()->Leave();
    SaveProfile();
    SaveServerSettings();
    SaveMouseSensitivity();
    SaveMusicVolume();
    GEngine->OnNetworkFailure().Remove(NetworkFailureHandle); GEngine->OnTravelFailure().Remove(TravelFailureHandle); Super::Shutdown();
}
float UERGameInstance::GetMouseLookMultiplier() const
{
    // IA_Look already scales by 0.18. The slider midpoint lowers that to 0.16;
    // logarithmic travel gives equal proportional adjustment on either side.
    return (0.16f / 0.18f) * FMath::Pow(4.f, 2.f * MouseSensitivity - 1.f);
}
void UERGameInstance::SetMouseSensitivity(float Value)
{
    if (!FMath::IsFinite(Value)) return;
    MouseSensitivity = FMath::Clamp(Value, 0.f, 1.f);
    GConfig->SetFloat(TEXT("EclipseRecoil.Input"), TEXT("MouseSensitivity"), MouseSensitivity, GGameUserSettingsIni);
}
void UERGameInstance::SaveMouseSensitivity()
{
    GConfig->SetFloat(TEXT("EclipseRecoil.Input"), TEXT("MouseSensitivity"), MouseSensitivity, GGameUserSettingsIni);
    GConfig->SetFloat(TEXT("EclipseRecoil.Input"), TEXT("MouseAcceleration"), MouseAcceleration, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
    UE_LOG(LogTemp, Display, TEXT("ER_MOUSE_SENSITIVITY slider=%.0f%% multiplier=%.3f"), MouseSensitivity * 100.f, GetMouseLookMultiplier());
    UE_LOG(LogTemp, Display, TEXT("ER_MOUSE_ACCELERATION slider=%.0f%%"), MouseAcceleration * 100.f);
}
void UERGameInstance::SetMouseAcceleration(float Value)
{
    if (!FMath::IsFinite(Value)) return;
    MouseAcceleration = FMath::Clamp(Value, 0.f, 1.f);
    GConfig->SetFloat(TEXT("EclipseRecoil.Input"), TEXT("MouseAcceleration"), MouseAcceleration, GGameUserSettingsIni);
}
void UERGameInstance::SetSoundVolume(float Value)
{
    if (!FMath::IsFinite(Value)) return;
    SoundVolume = FMath::Clamp(Value, 0.f, 1.f);
    FApp::SetVolumeMultiplier(SoundVolume);
    GConfig->SetFloat(TEXT("EclipseRecoil.Audio"), TEXT("SoundVolume"), SoundVolume, GGameUserSettingsIni);
}
void UERGameInstance::SetMusicVolume(float Value)
{
    if (!FMath::IsFinite(Value)) return;
    MusicVolume = FMath::Clamp(Value, 0.f, 1.f);
    GConfig->SetFloat(TEXT("EclipseRecoil.Audio"), TEXT("MusicVolume"), MusicVolume, GGameUserSettingsIni);
    if (auto* PC = Cast<AERPlayerController>(GetFirstLocalPlayerController())) PC->UpdateMenuMusic();
}
void UERGameInstance::SaveMusicVolume()
{
    GConfig->SetFloat(TEXT("EclipseRecoil.Audio"), TEXT("MusicVolume"), MusicVolume, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}
void UERGameInstance::CommitServerRules()
{
    ServerRules.Normalize();SessionRules=ServerRules;SessionRules.Ready=true;SaveServerSettings();
}
void UERGameInstance::SaveServerSettings()
{
    ServerRules.Normalize();
    ERServer::Save(ServerRules,GGameUserSettingsIni,TEXT("RespawnAndRage.ServerRules"));
    InternetServerName = UEROnline::SanitizeServerName(InternetServerName);
    GConfig->SetString(TEXT("EclipseRecoil.Server"), TEXT("InternetServerName"), *InternetServerName, GGameUserSettingsIni);
    MaxPlayers = ERServer::ValidMaxPlayers(MaxPlayers);
    GConfig->SetInt(TEXT("EclipseRecoil.Server"), TEXT("MaxPlayers"), MaxPlayers, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}
void UERGameInstance::SaveProfile()
{
    LocalName = AERGameMode::SanitizeName(LocalName);
    GConfig->SetString(TEXT("EclipseRecoil.Profile"), TEXT("Name"), *LocalName, GGameUserSettingsIni);
    GConfig->SetInt(TEXT("EclipseRecoil.Profile"), TEXT("Team"), PlayerTeam, GGameUserSettingsIni);
    for (auto Pair : {TPair<const TCHAR*, int32*>(TEXT("Magazines"), &SpareMagazines),
        {TEXT("HE"), &HECount}, {TEXT("Smoke"), &SmokeCount}, {TEXT("Stun"), &StunCount}})
        GConfig->SetInt(TEXT("EclipseRecoil.Loadout"), Pair.Key, *Pair.Value, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}
int32 UERGameInstance::GetStartingWeapon() const
{
    const auto* PC = Cast<AERPlayerController>(GetFirstLocalPlayerController());
    return PC && !PC->bReferenceArsenalMenu ? PrimaryWeapon : ReferenceStartingWeapon;
}
void UERGameInstance::StartSession()
{
    if (SessionMode == 2) { Join(ServerAddress); return; }
    CommitServerRules();
    RoundMapHistory.Reset();
    SaveProfile();
    LastError.Empty();
    const auto& Map=ERMaps::At(MapChoice);
    const TCHAR* Behaviors[] = {TEXT("Target"), TEXT("Movement"), TEXT("Patrol")};
    FString Options = SessionMode == 1 ? TEXT("listen?Name=") : TEXT("Name=");
    Options += AERGameMode::SanitizeName(LocalName);
    Options += LoadoutOptions();
    Options += FString::Printf(TEXT("?Primary=%d?AlphaSkin=%d?OmegaSkin=%d?Team=%d?ERAlphaBots=%d?EROmegaBots=%d?ERBotMode=%s"),
        GetStartingWeapon(), AlphaSkin, OmegaSkin, FMath::Clamp(PlayerTeam,0,1), FMath::Clamp(AlphaBots,0,10), FMath::Clamp(OmegaBots,0,10), Behaviors[FMath::Clamp(BotBehavior,0,2)]);
    Options += FString::Printf(TEXT("?Magazines=%d?HE=%d?Smoke=%d?Stun=%d"), SpareMagazines, HECount, SmokeCount, StunCount);
    if(Map.Procedural)Options+=FString::Printf(TEXT("?MapSeed=%d"),SessionRules.Get("Map.FixedSeed")>0?int32(SessionRules.Get("Map.Seed")):FMath::Rand());
    UGameplayStatics::OpenLevel(GetWorld(), FName(Map.Package), true, Options);
}
void UERGameInstance::Host(int32 BotCount, bool bStationaryTarget)
{
    CommitServerRules();
    LastError.Empty();
    FString Options = TEXT("listen?Name=") + AERGameMode::SanitizeName(LocalName);
    if (BotCount > 0) Options += FString::Printf(TEXT("?ERBots=%d?ERBotMode=%s"), FMath::Clamp(BotCount,1,bStationaryTarget ? 2 * ERSkins::Count : 3), bStationaryTarget ? TEXT("Target") : TEXT("Patrol"));
    Options += LoadoutOptions();
    Options += FString::Printf(TEXT("?Primary=%d?AlphaSkin=%d?OmegaSkin=%d"), PrimaryWeapon, AlphaSkin, OmegaSkin);
    UGameplayStatics::OpenLevel(GetWorld(), TEXT("/Game/EclipseRecoil/Maps/L_Depot"), true, Options);
}
bool UERGameInstance::IsValidAddress(const FString& Address)
{
    FString Host, Port; TArray<FString> Segments;
    if (!Address.Split(TEXT(":"), &Host, &Port)) Host = Address;
    else
    {
        if (Port.IsEmpty() || !Port.IsNumeric() || Port.Contains(TEXT(".")) || Port.StartsWith(TEXT("-")) || Port.StartsWith(TEXT("+")) || Port.Len() > 5) return false;
        const int32 Number = FCString::Atoi(*Port); if (Number < 1 || Number > 65535) return false;
    }
    Host.ParseIntoArray(Segments, TEXT("."), false);
    if (Segments.Num() != 4) return false;
    for (const FString& Segment : Segments)
    {
        if (Segment.IsEmpty() || Segment.Len() > 3) return false;
        for (TCHAR C : Segment) if (!FChar::IsDigit(C)) return false;
        if (FCString::Atoi(*Segment) > 255) return false;
    }
    return true;
}
bool UERGameInstance::Join(const FString& RawAddress)
{
    const FString Address = RawAddress.TrimStartAndEnd();
    if (!IsValidAddress(Address)) { LastError = TEXT("Enter a valid IPv4 address, e.g. 192.168.1.20:7777."); return false; }
    if (APlayerController* PC = GetFirstLocalPlayerController())
    {
        SaveProfile();
        CancelLANSearch();
        LastError = TEXT("Server found. Connecting to ") + Address + TEXT("...");
        PC->ClientTravel(Address + LoadoutOptions() + TEXT("?Name=") + AERGameMode::SanitizeName(LocalName) + FString::Printf(TEXT("?Primary=%d?AlphaSkin=%d?OmegaSkin=%d?Team=%d?Magazines=%d?HE=%d?Smoke=%d?Stun=%d"), GetStartingWeapon(), AlphaSkin, OmegaSkin, FMath::Clamp(PlayerTeam,0,1), SpareMagazines, HECount, SmokeCount, StunCount), TRAVEL_Absolute); return true;
    }
    LastError = TEXT("Local player unavailable."); return false;
}
void UERGameInstance::ReturnToMenu() { GetSubsystem<UEROnline>()->Leave(); CancelLANSearch(); CloseDiscoverySocket(); UGameplayStatics::OpenLevel(GetWorld(), TEXT("/Game/EclipseRecoil/Maps/L_MainMenu")); }
void UERGameInstance::QuitGame()
{
    if (bQuitRequested) return;
    bQuitRequested = true;
    SaveProfile();
    CancelLANSearch();
    auto* Online = GetSubsystem<UEROnline>();
    Online->Leave();
    const double Deadline = FPlatformTime::Seconds() + 5;
    // Let the public session's destroy request finish before the SDK/platform
    // is released. A failed network or cancelled login must not trap Quit.
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Deadline](float)
    {
        const auto* Service = GetSubsystem<UEROnline>();
        const bool Clean = !Service->IsBusy() && !Service->HasSession();
        if (!Clean && FPlatformTime::Seconds() < Deadline) return true;
        UE_LOG(LogTemp, Display, TEXT("ER_ONLINE_QUIT cleaned=%d"), Clean);
        FPlatformMisc::RequestExit(false);
        return false;
    }), .1f);
}
void UERGameInstance::NetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
    if (World != GetWorld()) return;
    LastError = TEXT("Connection lost or failed: ") + Error.Left(180);
    ReturnToMenu();
}
void UERGameInstance::TravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
    if (World != GetWorld()) return;
    LastError = TEXT("Travel failed: ") + Error.Left(180);
    GetSubsystem<UEROnline>()->Leave();
    if (World->GetNetMode() != NM_Standalone || !UGameplayStatics::GetCurrentLevelName(World).Contains(TEXT("L_MainMenu"))) ReturnToMenu();
}
bool AERPlayerController::IsMenuLevel() const { return UGameplayStatics::GetCurrentLevelName(GetWorld()).Contains(TEXT("L_MainMenu")); }
AERPlayerController::AERPlayerController()
{
    Recon = CreateDefaultSubobject<UERReconComponent>(TEXT("Recon"));
}
void AERPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
#if !UE_BUILD_SHIPPING
    bInputAudit = FParse::Param(FCommandLine::Get(), TEXT("ERInputAudit"));
    bPerformanceAudit = FParse::Param(FCommandLine::Get(), TEXT("ERPerformanceAudit"));
#endif
    if (auto* GI = GetGameInstance<UERGameInstance>())
    {
        ServerSetName(GI->LocalName);
        if (!GIsEditor && IsMenuLevel() && !GI->bInitialWindowSizeApplied && !FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
        {
            GI->bInitialWindowSizeApplied = true;
            int32 ExplicitSize = 0;
            const bool Explicit = FParse::Value(FCommandLine::Get(), TEXT("ResX="), ExplicitSize) ||
                FParse::Value(FCommandLine::Get(), TEXT("ResY="), ExplicitSize) ||
                FParse::Param(FCommandLine::Get(), TEXT("windowed")) || FParse::Param(FCommandLine::Get(), TEXT("fullscreen"));
            bool Saved = false;
            GConfig->GetBool(TEXT("EclipseRecoil.Display"), TEXT("Configured"), Saved, GGameUserSettingsIni);
            if (!Explicit)
            {
                if (Saved) GEngine->GetGameUserSettings()->ApplyResolutionSettings(false);
                else ApplyDisplaySize(100);
            }
        }
    }
    if (ULocalPlayer* LP = GetLocalPlayer()) if (auto* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) if (GameplayMapping) Subsystem->AddMappingContext(GameplayMapping, 0);
    ERPerformance::Get().Load();
    ERPerformance::Get().Apply();
    ShowMenu(IsMenuLevel());
    ERInstallResolutionGovernor();
    UpdateRenderResolution();
    // Slate uses local logical units here; its geometry already accounts for DPI.
    const auto PerformanceLayout = [this]()
    {
        return FERHUDLayout(FrameRateWidget.IsValid() ? FrameRateWidget->GetCachedGeometry().GetLocalSize() : FVector2D(1280,720));
    };
    FrameRateWidget = SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top)
            .Padding(TAttribute<FMargin>::CreateLambda([PerformanceLayout]() { return FMargin(0,PerformanceLayout().Position(0,2).Y,0,0); }))
        [SNew(STextBlock)
            .Font_Lambda([PerformanceLayout]() { return ERInterface::Font(FMath::Max(1,FMath::RoundToInt(9*PerformanceLayout().Scale))); })
            .ShadowOffset_Lambda([PerformanceLayout]() { return FVector2D(1,1)*PerformanceLayout().Scale; })
            .ShadowColorAndOpacity(FLinearColor::Black).Text_Lambda([this]() { return GetFrameRateText(); })];
    FrameRateWidget->SetVisibility(EVisibility::HitTestInvisible);
    GetWorld()->GetGameViewport()->AddViewportWidgetContent(FrameRateWidget.ToSharedRef(), 30);
}
void AERPlayerController::ClientGameplayMessage_Implementation(const FString& Event, const FString& Title, const FString& Body,
    int32 Priority, float Duration, bool Confirmation)
{
    if (!IsLocalController()) return;
    const double Now = GetWorld()->GetTimeSeconds();
    KillConfirmations.RemoveAll([Now](const FERGameplayToast& Item) { return Item.Expires <= Now; });
    const FERGameplayToast Message{Event, Title, Body, Now + Duration, Priority};
    if (Confirmation)
    {
        if (KillConfirmations.Num() >= 3) KillConfirmations.RemoveAt(0);
        KillConfirmations.Add(Message);
    }
    else if (GameplayToast.Expires <= Now || Priority >= GameplayToast.Priority)
    {
        GameplayToast = Message;
    }
#if !UE_BUILD_SHIPPING
    UE_LOG(LogTemp, Display, TEXT("ER_MESSAGE recipient=%s event=%s confirmation=%d title=%s body=%s"),
        *GetName(), *Event, Confirmation, *Title, *Body);
#endif
}

void AERPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    TickPlayerScreenshots();
    UpdateWindowInput();
    if (IsLocalController() && bShowingLobby && !IsMenuLevel()) { MenuPage = 0; ShowMenu(false); }
    if (IsLocalController() && IsMenuLevel() && bShowingLobby != IsInLobby())
    { MenuPage = 0; ShowMenu(true); }
    FlashOpacity = FMath::Max(0.f, FlashOpacity - DeltaTime * .35f);
    if (IsLocalController()) UpdateRenderResolution();
    if (IsLocalController() && DeltaTime > 0)
        FrameTimeMs = FrameTimeMs > 0 ? FMath::Lerp(FrameTimeMs, DeltaTime * 1000.f, 1.f - FMath::Exp(-DeltaTime * 3.f)) : DeltaTime * 1000.f;
#if !UE_BUILD_SHIPPING
    if (bPerformanceAudit && IsLocalController() && GetWorld()->GetTimeSeconds() >= NextPerformanceAuditAt)
    {
        NextPerformanceAuditAt = GetWorld()->GetTimeSeconds() + 1;
        UE_LOG(LogTemp, Display, TEXT("ER_PERFORMANCE %s viewport=%dx%d secondary=%.1f%%"),
            *GetFrameRateText().ToString(), LastRenderViewport.X, LastRenderViewport.Y, SceneOutputFraction * 100.f);
    }
    if (!bInputAudit || !IsLocalController() || GetWorld()->GetTimeSeconds() < NextInputAuditAt) return;
    NextInputAuditAt = GetWorld()->GetTimeSeconds()+.1;
    const auto* PlayerCharacter = Cast<AERCharacter>(GetPawn());
    if (!PlayerCharacter || !PlayerCharacter->Weapon) return;
    const FVector Position = PlayerCharacter->GetActorLocation();
    const FVector Velocity = PlayerCharacter->GetVelocity();
    const auto* Weapon = PlayerCharacter->Weapon.Get();
    const auto* State = GetPlayerState<AERPlayerState>();
    UE_LOG(LogTemp,Display,TEXT("ER_INPUT_AUDIT pawn=%s pos=%.1f,%.1f,%.1f vel=%.1f,%.1f crouch=%d half=%.1f camz=%.1f aim=%.1f,%.1f ammo=%d/%d reload=%d dead=%d protected=%d menu=%d scoreboard=%d kills=%d deaths=%d ctrl=%d can_crouch=%d"),
        *PlayerCharacter->GetName(),Position.X,Position.Y,Position.Z,Velocity.Size2D(),Velocity.Z,PlayerCharacter->bIsCrouched,
        PlayerCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),PlayerCharacter->Camera->GetComponentLocation().Z,
        GetControlRotation().Pitch,GetControlRotation().Yaw,Weapon->MagazineAmmo,Weapon->ReserveAmmo,Weapon->bReloading,
        PlayerCharacter->Health->bDead,PlayerCharacter->Health->IsProtected(),bMenuOpen,bShowScoreboard,State ? State->Kills : 0,State ? State->Deaths : 0,
        IsInputKeyDown(EKeys::LeftControl),PlayerCharacter->CanCrouch());
#endif
}
void AERPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotProcessedHandle);
    ScreenshotProcessedHandle.Reset();
    if (ScreenshotCamera) { ScreenshotCamera->Destroy(); ScreenshotCamera = nullptr; }
    if (MenuMusicComponent)
    {
        MenuMusicComponent->Stop();
        MenuMusicComponent->DestroyComponent();
        MenuMusicComponent = nullptr;
        UE_LOG(LogTemp, Display, TEXT("ER_MENU_MUSIC stopped (end play)"));
    }
    if (MenuWidget.IsValid() && GetWorld() && GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
    if (FrameRateWidget.IsValid() && GetWorld() && GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(FrameRateWidget.ToSharedRef());
    FrameRateWidget.Reset(); DisplaySizeSelector.Reset();
    if (MenuPreview) { MenuPreview->Destroy(); MenuPreview = nullptr; }
    PreviewBrush.Reset();
    if (IsLocalController() && GetLocalPlayer()) if (auto* Subsystem = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) if (GameplayMapping) Subsystem->RemoveMappingContext(GameplayMapping);
    MenuWidget.Reset(); Super::EndPlay(Reason);
}
void AERPlayerController::CapturePlayerScreenshots()
{
    AERCharacter* Subject = Cast<AERCharacter>(GetPawn());
    if (!IsLocalController() || !Subject || bMenuOpen || !GEngine || !GEngine->GameViewport ||
        ScreenshotCamera || ScreenshotProcessedHandle.IsValid() || FScreenshotRequest::IsScreenshotRequested()) return;
    FViewport* GameViewport = GEngine->GameViewport->Viewport;
    if (!GameViewport) return;
    const FIntPoint Size = GameViewport->GetSizeXY();
    if (Size.X <= 0 || Size.Y <= 0) return;

    ScreenshotDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ScreenShotDir(), FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S-")) + FGuid::NewGuid().ToString()));
    SavedScreenshotCount = 0;
    ScreenshotIndex = 0;
    ScreenshotNoticeUntil = 0; // Keep the save notification out of screen.png.
    if (!IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true))
    {
        FinishPlayerScreenshots();
        return;
    }

    ScreenshotCamera = GetWorld()->SpawnActor<ASceneCapture2D>();
    if (ScreenshotCamera)
    {
        USceneCaptureComponent2D* Capture = ScreenshotCamera->GetCaptureComponent2D();
        Capture->bCaptureEveryFrame = true;
        Capture->bCaptureOnMovement = false;
        Capture->bAlwaysPersistRenderingState = true;
        Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
        Capture->FOVAngle = 60.f;
        // These work on deferred captures rendered alongside the main view family.
        // CaptureScene() alone has no main view and forces 100% screen percentage.
        Capture->bMainViewResolution = true;
        Capture->bIgnoreScreenPercentage = false;
        Capture->bInheritMainViewCameraPostProcessSettings = true;
        Capture->ShowFlags = GEngine->GameViewport->EngineShowFlags;
        UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(ScreenshotCamera);
        // FinalColorLDR already contains the display gamma from the tonemapper.
        // An sRGB render-target view would encode it a second time on GPU writes.
        // Keep the storage linear and let the tonemapper apply display gamma once.
        Target->TargetGamma = GEngine->GetDisplayGamma();
        Target->InitCustomFormat(Size.X, Size.Y, PF_B8G8R8A8, true);
        Capture->TextureTarget = Target;

        FVector Center, Extent;
        Subject->GetActorBounds(true, Center, Extent);
        const double Radius = FMath::Max(Extent.Size(), 100.0);
        const double Aspect = static_cast<double>(Size.X) / Size.Y;
        const double Distance = Radius * 1.25 / FMath::Sin(FMath::Atan(FMath::Tan(PI / 6.0) / FMath::Max(1.0, Aspect)));
        const FVector Forward = FRotator(0, Subject->GetActorRotation().Yaw, 0).Vector();
        const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
        double GroundZ = Subject->GetActorLocation().Z - Subject->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        FHitResult GroundHit;
        FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(ScreenshotGround), false, Subject);
        const FVector GroundStart(Subject->GetActorLocation().X, Subject->GetActorLocation().Y, GroundZ + 5.0);
        if (GetWorld()->LineTraceSingleByChannel(GroundHit, GroundStart, GroundStart - FVector(0, 0, 10000), ECC_Visibility, GroundQuery))
            GroundZ = GroundHit.ImpactPoint.Z;
        ScreenshotViews.Reset();
        // The two front-quarter views are 120 degrees apart (+/-60 from front).
        const double SideWeight = FMath::Sqrt(3.0) * .5;
        const FVector Directions[] = {Forward, Forward * .5 + Right * SideWeight,
            Forward * .5 - Right * SideWeight, -Forward};
        for (const FVector& Direction : Directions)
        {
            FVector CameraPosition = Center + Direction * Distance;
            CameraPosition.Z = GroundZ + 300.0;
            ScreenshotViews.Emplace((Center - CameraPosition).Rotation(), CameraPosition);
        }
        // Looking straight down, camera-right is -Forward: the player faces left.
        const FVector TopPosition(Center.X, Center.Y, GroundZ + 1000.0);
        ScreenshotViews.Emplace(FRotator(-90, Subject->GetActorRotation().Yaw + 90, 0), TopPosition);
        ScreenshotCamera->SetActorTransform(ScreenshotViews[0]);
        Capture->bCameraCutThisFrame = true;
        ScreenshotReadFrame = GFrameCounter + 4; // Render and warm up temporal effects before readback.
    }

    const FString ScreenFile = FPaths::Combine(ScreenshotDirectory, TEXT("screen.png"));
    ScreenshotProcessedHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddWeakLambda(this, [this]()
    {
        FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotProcessedHandle);
        ScreenshotProcessedHandle.Reset();
        FinishPlayerScreenshots();
    });
    // Let Unreal/Slate read the completed frame, including HUD, rather than reading
    // the swapchain during input (which can return a black backbuffer on Metal).
    FScreenshotRequest::RequestScreenshot(ScreenFile, true, false, false, FIntRect(), true);
}

void AERPlayerController::TickPlayerScreenshots()
{
    if (!ScreenshotCamera || GFrameCounter < ScreenshotReadFrame) return;
    USceneCaptureComponent2D* Capture = ScreenshotCamera->GetCaptureComponent2D();
    UTextureRenderTarget2D* Target = Capture->TextureTarget;
    TArray<FColor> Pixels;
    FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
    ReadFlags.SetLinearToGamma(false); // Preserve the already display-encoded LDR bytes.
    if (Target && Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, ReadFlags) &&
        Pixels.Num() == static_cast<int64>(Target->SizeX) * Target->SizeY)
    {
        for (FColor& Pixel : Pixels) Pixel.A = 255;
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(Target->SizeX, Target->SizeY, Pixels, Png);
        const TCHAR* Labels[] = {TEXT("front"), TEXT("right"), TEXT("left"), TEXT("back"), TEXT("top")};
        if (!Png.IsEmpty() && FFileHelper::SaveArrayToFile(Png,
            *FPaths::Combine(ScreenshotDirectory, FString(Labels[ScreenshotIndex]) + TEXT(".png")))) ++SavedScreenshotCount;
    }
    if (++ScreenshotIndex < ScreenshotViews.Num())
    {
        ScreenshotCamera->SetActorTransform(ScreenshotViews[ScreenshotIndex]);
        Capture->bCameraCutThisFrame = true;
        ScreenshotReadFrame = GFrameCounter + 4;
    }
    else
    {
        ScreenshotCamera->Destroy();
        ScreenshotCamera = nullptr;
        FinishPlayerScreenshots();
    }
}

void AERPlayerController::FinishPlayerScreenshots()
{
    if (ScreenshotCamera || ScreenshotProcessedHandle.IsValid()) return;
    // Screenshot tracing can finish writing screen.png after the processed delegate.
    const int32 ScreenSaved = IFileManager::Get().FileSize(*FPaths::Combine(ScreenshotDirectory, TEXT("screen.png"))) > 0 ? 1 : 0;
    ScreenshotStatus = FString::Printf(TEXT("Screenshots saved: %d/6"), SavedScreenshotCount + ScreenSaved);
    ScreenshotNoticeUntil = FPlatformTime::Seconds() + 15.0;
    UE_LOG(LogTemp, Display, TEXT("%s\n%s"), *ScreenshotStatus, *ScreenshotDirectory);
}

void AERPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AERPlayerController::CapturePlayerScreenshots);
    InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AERPlayerController::SprintPressed);
    InputComponent->BindKey(EKeys::E, IE_Released, this, &AERPlayerController::SprintReleased);
    InputComponent->BindKey(EKeys::W, IE_Pressed, this, &AERPlayerController::ForwardPressed);
    InputComponent->BindKey(EKeys::W, IE_Released, this, &AERPlayerController::ForwardReleased);
    InputComponent->BindKey(EKeys::H, IE_Pressed, this, &AERPlayerController::SelectClaymore);
    InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AERPlayerController::SelectPrimary);
    InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AERPlayerController::SelectSidearm);
    InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AERPlayerController::SelectHE);
    InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &AERPlayerController::SelectSmoke);
    InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &AERPlayerController::SelectStun);
    InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &AERPlayerController::SelectSix);
    InputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &AERPlayerController::SelectSeven);
    InputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &AERPlayerController::SelectEight);
    InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AERPlayerController::SelectReferenceHE);
    InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AERPlayerController::SelectReferenceSmoke);
    InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AERPlayerController::NextWeapon);
    InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AERPlayerController::PreviousWeapon);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AERPlayerController::ZoomPressed);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AERPlayerController::ZoomReleased);
    if (auto* Input = Cast<UEnhancedInputComponent>(InputComponent))
    {
        if (WalkAction)
        {
            Input->BindAction(WalkAction, ETriggerEvent::Started, this, &AERPlayerController::WalkPressed);
            Input->BindAction(WalkAction, ETriggerEvent::Completed, this, &AERPlayerController::WalkReleased);
            Input->BindAction(WalkAction, ETriggerEvent::Canceled, this, &AERPlayerController::WalkReleased);
        }
        Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AERPlayerController::Move);
        Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AERPlayerController::Look);
        Input->BindAction(FireAction, ETriggerEvent::Started, this, &AERPlayerController::FirePressed);
        Input->BindAction(FireAction, ETriggerEvent::Completed, this, &AERPlayerController::FireReleased);
        Input->BindAction(FireAction, ETriggerEvent::Canceled, this, &AERPlayerController::FireReleased);
        Input->BindAction(JumpAction, ETriggerEvent::Started, this, &AERPlayerController::JumpPressed);
        Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &AERPlayerController::JumpReleased);
        Input->BindAction(JumpAction, ETriggerEvent::Canceled, this, &AERPlayerController::JumpReleased);
        Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &AERPlayerController::CrouchPressed);
        Input->BindAction(CrouchAction, ETriggerEvent::Completed, this, &AERPlayerController::CrouchReleased);
        Input->BindAction(CrouchAction, ETriggerEvent::Canceled, this, &AERPlayerController::CrouchReleased);
        Input->BindAction(ReloadAction, ETriggerEvent::Started, this, &AERPlayerController::ReloadPressed);
        Input->BindAction(ScoreboardAction, ETriggerEvent::Started, this, &AERPlayerController::ScoreboardPressed);
        Input->BindAction(ScoreboardAction, ETriggerEvent::Completed, this, &AERPlayerController::ScoreboardReleased);
        Input->BindAction(ScoreboardAction, ETriggerEvent::Canceled, this, &AERPlayerController::ScoreboardReleased);
        Input->BindAction(MenuAction, ETriggerEvent::Started, this, &AERPlayerController::MenuPressed);
    }
}
void AERPlayerController::ResetSprintInput()
{
    bSprintKeyHeld = bForwardSprintHeld = bForwardHeld = false;
    LastForwardTap = -1;
    UpdateSprintInput();
}
void AERPlayerController::PrepareGesturePawn(AERCharacter* InCharacter)
{
    if (GesturePawn.Get() == InCharacter) return;
    GesturePawn = InCharacter;
    ResetSprintInput();
    LastCrouchTap = -1;
}
void AERPlayerController::UpdateSprintInput()
{
    if (auto* C = Cast<AERCharacter>(GetPawn()))
        Cast<UERMovement>(C->GetCharacterMovement())->bSprintHeld = bForwardSprintHeld || (bSprintKeyHeld && bForwardHeld);
}
void AERPlayerController::SprintPressed()
{
    auto* C = Cast<AERCharacter>(GetPawn());
    if (bMenuOpen || !C || C->Health->bDead) return;
    PrepareGesturePawn(C);
    bSprintKeyHeld = true;
    UpdateSprintInput();
}
void AERPlayerController::SprintReleased()
{
    bSprintKeyHeld = false;
    UpdateSprintInput();
}
void AERPlayerController::ForwardPressed()
{
    auto* C = Cast<AERCharacter>(GetPawn());
    if (bMenuOpen || !C || C->Health->bDead) return;
    PrepareGesturePawn(C);
    bForwardHeld = true;
    const double Now = GetWorld()->GetTimeSeconds();
    if (LastForwardTap >= 0 && Now - LastForwardTap <= .3)
    {
        bForwardSprintHeld = true;
        LastForwardTap = -1;
    }
    else LastForwardTap = Now;
    UpdateSprintInput();
}
void AERPlayerController::ForwardReleased()
{
    bForwardHeld = bForwardSprintHeld = false;
    UpdateSprintInput();
}
void AERPlayerController::WalkPressed() { if (!bMenuOpen) if (auto* C = Cast<AERCharacter>(GetPawn())) Cast<UERMovement>(C->GetCharacterMovement())->bWalkHeld = true; }
void AERPlayerController::WalkReleased() { if (auto* C = Cast<AERCharacter>(GetPawn())) Cast<UERMovement>(C->GetCharacterMovement())->bWalkHeld = false; }
void AERPlayerController::Move(const FInputActionValue& Value)
{
    auto* PlayerCharacter = Cast<AERCharacter>(GetPawn()); if (!PlayerCharacter || PlayerCharacter->Health->bDead || bMenuOpen) return;
    const FVector2D Axis = Value.Get<FVector2D>(); const FRotator Rotation(0, GetControlRotation().Yaw, 0);
    if(auto* M=Cast<UERMovement>(PlayerCharacter->GetCharacterMovement());M->IsOnLadder())
    {
        if(auto* L=M->FindLadder())PlayerCharacter->AddMovementInput(-L->GetActorForwardVector(),Axis.Y);
        return;
    }
    PlayerCharacter->AddMovementInput(Rotation.Vector(), Axis.Y);
    PlayerCharacter->AddMovementInput(FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y), Axis.X);
}
void AERPlayerController::ZoomPressed()
{
    if (!bMenuOpen) if (auto* C = Cast<AERCharacter>(GetPawn()); C && C->Weapon) C->Weapon->ServerCycleScope();
}
void AERPlayerController::Look(const FInputActionValue& Value)
{
    if (bMenuOpen) return;
    const UERGameInstance* GI = GetGameInstance<UERGameInstance>();
    const FVector2D Input = Value.Get<FVector2D>();
    if (!FMath::IsFinite(Input.X) || !FMath::IsFinite(Input.Y)) return;
    const float Gain = GI ? ERMouseResponse::Gain(Input, FApp::GetDeltaTime(), GI->GetMouseAcceleration()) : 1.f;
    const auto* C = Cast<AERCharacter>(GetPawn());
    const float ZoomGain = C && C->Weapon && !C->IsWeaponStowed() ? (C->Weapon->Data && C->Weapon->Data->ScopeMode==1 ? FMath::Lerp(90.f,C->Weapon->Tuned()->ZoomFOV,C->GetIronAimBlend()) : C->Weapon->GetScopeFOV())/90.f : 1.f;
    const FVector2D Axis = Input * Gain * (GI ? GI->GetMouseLookMultiplier() : 0.16f / 0.18f) * ZoomGain;
    AddYawInput(Axis.X); AddPitchInput(-Axis.Y);
#if !UE_BUILD_SHIPPING
    if (bInputAudit && FApp::GetCurrentTime() >= NextMouseAuditAt)
    {
        NextMouseAuditAt = FApp::GetCurrentTime() + .1;
        UE_LOG(LogTemp, Display, TEXT("ER_MOUSE_LOOK input=%.3f,%.3f dt=%.5f gain=%.3f output=%.3f,%.3f"),
            Input.X, Input.Y, FApp::GetDeltaTime(), Gain, Axis.X, Axis.Y);
    }
#endif
}
void AERPlayerController::FirePressed()
{
    if (bMenuOpen || bBlockMapLoading) return;
    // Started fires once per physical press. Returning here consumes the respawn click.
    if (bAwaitingRespawn) { ServerRequestRespawn(); return; }
    if (auto* PlayerCharacter = Cast<AERCharacter>(GetPawn()))
    {
        PlayerCharacter->SetGrenadeInputHeld(true);
        PlayerCharacter->SetFireInputHeld(true);
    }
}
void AERPlayerController::FireReleased() { if (auto* PlayerCharacter = Cast<AERCharacter>(GetPawn())) { PlayerCharacter->SetGrenadeInputHeld(false);PlayerCharacter->SetFireInputHeld(false); } }
void AERPlayerController::JumpPressed()
{
    if (bMenuOpen) return;
    if (bAwaitingRespawn) { ServerRequestRespawn(); return; }
    if (auto* PlayerCharacter = Cast<AERCharacter>(GetPawn()); PlayerCharacter && !PlayerCharacter->Health->bDead) { Cast<UERMovement>(PlayerCharacter->GetCharacterMovement())->bClimbHeld = true; PlayerCharacter->Jump(); }
}
void AERPlayerController::JumpReleased() { if (auto* PlayerCharacter = Cast<AERCharacter>(GetPawn())) { Cast<UERMovement>(PlayerCharacter->GetCharacterMovement())->bClimbHeld = false; PlayerCharacter->StopJumping(); } }
void AERPlayerController::CrouchPressed()
{
    auto* C = Cast<AERCharacter>(GetPawn());
    if (bMenuOpen || !C || C->Health->bDead) return;
    auto* M = Cast<UERMovement>(C->GetCharacterMovement());
    if (M->IsTraversing() || !M->IsMovingOnGround()) return;
    PrepareGesturePawn(C);
    const double Now = GetWorld()->GetTimeSeconds();
    if (M->bProneWanted)
    {
        M->bProneWanted = false; C->Crouch(); LastCrouchTap = -1;
    }
    else if (LastCrouchTap >= 0 && Now - LastCrouchTap <= .3)
    {
        M->bProneWanted = true; C->UnCrouch(); LastCrouchTap = -1;
    }
    else { C->Crouch(); LastCrouchTap = Now; }
    bForwardSprintHeld = false;
    UpdateSprintInput();
}
void AERPlayerController::CrouchReleased() { if (auto* C = Cast<AERCharacter>(GetPawn())) C->UnCrouch(); }
void AERPlayerController::ReloadPressed() { if (!bMenuOpen) if (auto* PlayerCharacter = Cast<AERCharacter>(GetPawn())) { PlayerCharacter->SetFireInputHeld(false);PlayerCharacter->ServerReload(); } }
void AERPlayerController::ScoreboardPressed() { bShowScoreboard = true; }
void AERPlayerController::ScoreboardReleased() { bShowScoreboard = false; }
void AERPlayerController::MenuPressed()
{
    if (IsMenuLevel()) { if (MenuPage != 0) { GetGameInstance<UERGameInstance>()->CancelLANSearch(); GetGameInstance()->GetSubsystem<UEROnline>()->Cancel(); OpenMenuPage(0); } }
    else if (MenuPage != 0) OpenMenuPage(0);
    else ShowMenu(!bMenuOpen);
}
void AERPlayerController::SelectSlot(int32 Slot)
{
    if (bMenuOpen) return;
    if (auto* C = Cast<AERCharacter>(GetPawn()); C && C->Weapon) C->Weapon->ServerClearScope();
    if (auto* C = Cast<AERCharacter>(GetPawn())) { C->SetFireInputHeld(false);C->ServerSelectWeapon(C->bPresetInventory ? C->FindCatalogSlot(Slot):Slot); }
}
void AERPlayerController::SelectNumber(int32 Index)
{
    if (const auto* C = Cast<AERCharacter>(GetPawn()); C && (C->UsesReferenceArsenal() || Index < 5)) SelectSlot(Index);
}
void AERPlayerController::NextWeapon()
{
    ScrollWeapon(1);
}
void AERPlayerController::PreviousWeapon()
{
    ScrollWeapon(-1);
}
void AERPlayerController::ScrollWeapon(int32 Direction)
{
    auto* C = Cast<AERCharacter>(GetPawn());
    if (bMenuOpen || !C || C->Health->bDead || !C->Weapon || C->Inventory.IsEmpty() ||
        (Direction != 1 && Direction != -1)) return;
    const int32 Current = C->Inventory.IndexOfByKey(C->Weapon);
    if (Current == INDEX_NONE) return;
    // One immediate step per gesture. Every momentum pulse extends the quiet
    // period, so a continuous wheel/trackpad burst never queues more switches.
    const double Now = FPlatformTime::Seconds();
    const bool bFreshGesture = Now >= WheelQuietUntil;
    WheelQuietUntil = Now + .25;
    if (!bFreshGesture) return;
    for(int32 Step=1;Step<C->Inventory.Num();++Step)
    {
        const int32 Slot=(Current+Direction*Step+C->Inventory.Num())%C->Inventory.Num();
        const auto* W=C->Inventory[Slot].Get();
        if(W && W->Data && (W->Data->Payload==EERPayload::Bullet || W->Data->Payload==EERPayload::Melee || W->MagazineAmmo+W->ReserveAmmo>0))
        { C->SetFireInputHeld(false);C->ServerSelectWeapon(Slot);break; }
    }
}
void AERPlayerController::ServerSetSkins_Implementation(int32 Alpha, int32 Omega)
{
    if(!GetWorld()->URL.HasOption(TEXT("ERLegacyArsenal")))return;
    if (const auto* GS = GetWorld()->GetGameState<AERGameState>(); GS && GS->bLobbyStarting) return;
    if (Alpha < 0 || Alpha >= ERSkins::Count || Omega < 0 || Omega >= ERSkins::Count) return;
    if (auto* PS = GetPlayerState<AERPlayerState>())
    { PS->bLobbyReady = false; PS->AlphaSkin = Alpha; PS->OmegaSkin = Omega; PS->ForceNetUpdate(); }
}
void AERPlayerController::ServerSetPrimary_Implementation(int32 Index)
{
    if(!GetWorld()->URL.HasOption(TEXT("ERLegacyArsenal")))return;
    if (const auto* GS = GetWorld()->GetGameState<AERGameState>(); GS && GS->bLobbyStarting) return;
    const bool Reference = bReferenceArsenalMenu && !GetWorld()->URL.HasOption(TEXT("ERLegacyArsenal"));
    if (Index < 0 || Index > (Reference ? 9 : 6)) return;
    if (auto* PS = GetPlayerState<AERPlayerState>()) { PS->bLobbyReady = false; PS->PrimaryWeapon = Index; PS->ForceNetUpdate(); }
}
void AERPlayerController::ClientFlash_Implementation(float Strength) { FlashOpacity = FMath::Max(FlashOpacity, FMath::Clamp(Strength, 0.f, .9f)); }
void AERPlayerController::ServerRequestRespawn_Implementation() { if (auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>()) Mode->RequestRespawn(this); }
void AERPlayerController::ServerSetName_Implementation(const FString& Name)
{
    if (const auto* GS = GetWorld()->GetGameState<AERGameState>(); GS && GS->bLobbyStarting) return;
    if (PlayerState) PlayerState->SetPlayerName(AERGameMode::SanitizeName(Name));
}
void AERPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME_CONDITION(AERPlayerController, bAwaitingRespawn, COND_OwnerOnly); DOREPLIFETIME_CONDITION(AERPlayerController, RespawnAllowedAt, COND_OwnerOnly);
}
void AERPlayerController::UpdateRenderResolution()
{
    const UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
    if (!Viewport || !Viewport->Viewport) return;
    const FIntPoint Size = Viewport->Viewport->GetSizeXY();
    if (Size.X <= 0 || Size.Y <= 0 || Size == LastRenderViewport) return;
    LastRenderViewport = Size;
    // Cap the scene AND temporal history, then spatially upscale to the window.
    // UI keeps the actual viewport resolution. Primary dynamic scaling can reduce
    // this scene output further when the GPU exceeds the 60 FPS budget.
    SceneOutputFraction = ERPerformance::Get().Automatic
        ? FMath::Min(1.f, FMath::Min(1920.f / Size.X, 1080.f / Size.Y))
        : ERPerformance::Get().Scale / (100.f * ERPerformance::Get().ManualPrimary());
    if (IConsoleVariable* Secondary = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SecondaryScreenPercentage.GameViewport")))
        Secondary->Set(SceneOutputFraction * 100.f, ECVF_SetByGameSetting);
    UE_LOG(LogTemp, Display, TEXT("ER_RENDER_OUTPUT viewport=%dx%d scene_max=%dx%d secondary=%.1f%%"),
        Size.X, Size.Y, FMath::RoundToInt(Size.X * SceneOutputFraction), FMath::RoundToInt(Size.Y * SceneOutputFraction), SceneOutputFraction * 100.f);
}

FText AERPlayerController::GetFrameRateText() const
{
    if (FrameTimeMs <= 0) return FText::FromString(TEXT("FPS —"));
    FDynamicResolutionStateInfos Info;
    GEngine->GetDynamicResolutionCurrentStateInfos(Info);
    const bool bDynamic = Info.Status == EDynamicResolutionStatus::Enabled;
    const float Primary = bDynamic ? Info.ResolutionFractionApproximations[GDynamicPrimaryResolutionFraction] : 1.f;
    const FIntPoint RenderSize(FMath::RoundToInt(LastRenderViewport.X * SceneOutputFraction * Primary),
        FMath::RoundToInt(LastRenderViewport.Y * SceneOutputFraction * Primary));
    return FText::FromString(FString::Printf(TEXT("%.0f FPS   ·   %d × %d"),
        1000.f / FrameTimeMs, RenderSize.X, RenderSize.Y));
}

void AERPlayerController::ResetRenderResolution()
{
    // Keep a single governor: Unreal already samples GPU timings and lowers
    // the primary resolution under load. Reset its history, not the window.
    if (GEngine->GetDynamicResolutionState())
        GEngine->GetDynamicResolutionState()->ResetHistory();
    LastRenderViewport = FIntPoint::ZeroValue;
    UpdateRenderResolution();
    UE_LOG(LogTemp, Display, TEXT("ER_RENDER_RESET dynamic resolution history reset; window unchanged"));
}

FIntPoint AERPlayerController::GetDisplayResolution() const
{
    FDisplayMetrics Metrics;
    FDisplayMetrics::RebuildDisplayMetrics(Metrics);
    if (GetWorld() && GetWorld()->GetGameViewport())
    {
        const TSharedPtr<SWindow> Window = GetWorld()->GetGameViewport()->GetWindow();
        if (Window.IsValid())
        {
            const FVector2D Center = Window->GetPositionInScreen() + Window->GetSizeInScreen() * .5f;
            for (const FMonitorInfo& Monitor : Metrics.MonitorInfo)
            {
                const FPlatformRect& Rect = Monitor.DisplayRect;
                if (Center.X >= Rect.Left && Center.X < Rect.Right && Center.Y >= Rect.Top && Center.Y < Rect.Bottom)
                    return FIntPoint(Rect.Right - Rect.Left, Rect.Bottom - Rect.Top);
            }
        }
    }
    return FIntPoint(FMath::Max(1, Metrics.PrimaryDisplayWidth), FMath::Max(1, Metrics.PrimaryDisplayHeight));
}

FText AERPlayerController::GetDisplaySizeText() const
{
    const UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
    const TSharedPtr<SWindow> Window = Viewport->GetWindow();
    if (Window.IsValid() && Window->GetWindowMode() != EWindowMode::Windowed)
        return FText::FromString(TEXT("Fullscreen"));
    const FIntPoint Resolution = Viewport->Viewport ? Viewport->Viewport->GetSizeXY() : GEngine->GetGameUserSettings()->GetScreenResolution();
    const FIntPoint Display = GetDisplayResolution();
    const int32 Percent = FMath::RoundToInt(100.f * FMath::Min(float(Resolution.X) / Display.X, float(Resolution.Y) / Display.Y));
    return FText::FromString(FString::Printf(TEXT("Windowed %d%%  ·  %d × %d"), Percent, Resolution.X, Resolution.Y));
}

void AERPlayerController::ApplyDisplaySize(int32 Percent)
{
    UGameUserSettings* Settings = GEngine->GetGameUserSettings();
    if (!Settings) return;
    const FIntPoint Display = GetDisplayResolution();
    const bool bFullscreen = Percent == 100;
    const int32 WindowPercent = FMath::Clamp(Percent, 50, 90);
    const FIntPoint Resolution = bFullscreen ? Display : FIntPoint(
        FMath::RoundToInt(Display.X * WindowPercent / 100.f), FMath::RoundToInt(Display.Y * WindowPercent / 100.f));
    Settings->SetFullscreenMode(bFullscreen ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
    Settings->SetScreenResolution(Resolution);
    // The local launcher supplies an initial size; menu choices must override it.
    Settings->ApplyResolutionSettings(false);
    bPendingWindowInput = true;
    InputWindowStableAt = FPlatformTime::Seconds();
    Settings->ConfirmVideoMode();
    Settings->SaveSettings();
    GConfig->SetBool(TEXT("EclipseRecoil.Display"), TEXT("Configured"), true, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
    if (DisplaySizeSelector.IsValid()) DisplaySizeSelector->SetIsOpen(false);
    UE_LOG(LogTemp, Display, TEXT("ER_DISPLAY size=%d%% resolution=%dx%d fullscreen=%d"), Percent, Resolution.X, Resolution.Y, bFullscreen);
}

void AERPlayerController::ApplyWindowInputMode()
{
    bShowMouseCursor = bMenuOpen;
    if (bMenuOpen)
        SetInputMode(FInputModeGameAndUI().SetWidgetToFocus(MenuFocusWidget.Pin())
            .SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock).SetHideCursorDuringCapture(false));
    else
        SetInputMode(FInputModeGameOnly().SetConsumeCaptureMouseDown(false));
}

void AERPlayerController::UpdateWindowInput()
{
    if (!IsLocalController() || GIsEditor || !FSlateApplication::IsInitialized()) return;
    auto* ViewportClient = GetWorld()->GetGameViewport();
    if (!ViewportClient || !ViewportClient->Viewport) return;
    const auto Window = ViewportClient->GetWindow();
    if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) return;
    const bool Active = FSlateApplication::Get().IsActive() && Window->GetNativeWindow()->IsForegroundWindow();
    const auto Size = ViewportClient->Viewport->GetSizeXY();
    const auto Position = Window->GetPositionInScreen();
    const int32 Mode = Window->GetWindowMode();
    const double Now = FPlatformTime::Seconds();
    if (InputWindow.Pin() != Window || InputViewportSize != Size || InputWindowPosition != Position ||
        InputWindowMode != Mode || bInputWindowActive != Active)
    {
        if(bInputWindowActive && !Active)if(auto* C=Cast<AERCharacter>(GetPawn()))C->SetFireInputHeld(false);
        InputWindow = Window; InputViewportSize = Size; InputWindowPosition = Position;
        InputWindowMode = Mode; bInputWindowActive = Active;
        InputWindowStableAt = Now;
        bPendingWindowInput = true;
    }
    // macOS applies fullscreen/activation asynchronously. Repair only after the
    // foreground game window settles; never activate it over another application.
    if (!Active || Size.X <= 0 || Size.Y <= 0 || Now - InputWindowStableAt < .25) return;
    const bool MissingGameCapture = !bMenuOpen && !ViewportClient->Viewport->HasMouseCapture();
    if (!bPendingWindowInput && !MissingGameCapture) return;
    ApplyWindowInputMode();
    bPendingWindowInput = false;
    InputWindowStableAt = Now; // allow Slate to consume the queued input operations
    UE_LOG(LogTemp, Display, TEXT("ER_WINDOW_INPUT mode=%s size=%dx%d recovered_capture=%d"),
        bMenuOpen ? TEXT("menu") : TEXT("game"), Size.X, Size.Y, MissingGameCapture);
}

void AERPlayerController::UpdateMenuMusic()
{
    const auto* GI = GetGameInstance<UERGameInstance>();
    const float Volume = MenuMusicVolume * (GI ? GI->GetMusicVolume() : 1.f) * (IsMenuLevel() ? 1.f : .5f);
    const bool bShouldPlay = IsLocalController() && GetNetMode() != NM_DedicatedServer &&
        bMenuOpen && MenuMusic && Volume > 0.f;
    if (bShouldPlay && !MenuMusicComponent)
    {
        MenuMusicComponent = UGameplayStatics::SpawnSound2D(this, MenuMusic,
            Volume, 1.f, 0.f, nullptr, false, false);
        if (MenuMusicComponent)
            UE_LOG(LogTemp, Display, TEXT("ER_MENU_MUSIC started sound=%s volume=%.3f main=%d"), *MenuMusic->GetPathName(), Volume, IsMenuLevel());
    }
    else if (!bShouldPlay && MenuMusicComponent)
    {
        MenuMusicComponent->Stop();
        MenuMusicComponent->DestroyComponent();
        MenuMusicComponent = nullptr;
        UE_LOG(LogTemp, Display, TEXT("ER_MENU_MUSIC stopped (menu closed)"));
    }
    if (bShouldPlay && MenuMusicComponent) MenuMusicComponent->SetVolumeMultiplier(Volume);
}
void AERPlayerController::ERBlockDamage(float Amount)
{
#if !UE_BUILD_SHIPPING
    if (!HasAuthority() || !GetPawn()) return;
    FVector Eye; FRotator Aim;
    GetPawn()->GetActorEyesViewPoint(Eye, Aim);
    if (auto* PlayerCharacter = Cast<AERCharacter>(GetPawn())) Eye = PlayerCharacter->Camera->GetComponentLocation();
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ERBlockDebug), false, GetPawn());
    if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Aim.Vector() * 10000, ECC_GameTraceChannel1, Params))
        if (auto* Room = Cast<AERBlockRoom>(Hit.GetActor())) Room->ApplyHitDamage(Hit, Amount);
#endif
}
void AERPlayerController::ERBlockDestroy()
{
    ERBlockDamage(TNumericLimits<float>::Max());
}
void AERPlayerController::ERBlockStats()
{
#if !UE_BUILD_SHIPPING
    for (TActorIterator<AERBlockRoom> It(GetWorld()); It; ++It)
    {
        const auto M=It->GetMemoryProfile();const auto& G=It->GenerationProfile;const auto& U=It->UpdateProfile;
        UE_LOG(LogTemp,Display,TEXT("ER_BLOCK_PROFILE %s logical=%.3fms grouping=%.3fms clear=%.3fms add=%.3fms mapping=%.3fms update_clear=%.3fms update_add=%.3fms uploaded=%d tracked=%.2fMiB resident=%.2fMiB bodies=%d decals=%d emitters=%d bursts=%d support_bytes=%llu support_build=%.3fms support_peak=%.3fms support_steps=%d unsupported=%d components=%d net_epoch=%u net_sequence=%u net_pages=%d net_ready=%d net_bytes=%llu"),
            *It->GetStatistics(),G.LogicalMs,G.GroupingMs,G.ClearMs,G.AddMs,G.MappingMs,U.ClearMs,U.AddMs,U.Uploaded,double(M.TrackedBytes())/1048576,double(M.ResidentBytes)/1048576,M.CollisionBodies,M.Decals,M.Emitters,M.Bursts,uint64(M.SupportBytes),It->GetSupportBuildMs(),It->PeakSupportMs,It->SupportSteps,It->LastUnsupported,It->LastUnsupportedComponents,It->GetMapEpoch(),It->GetMapSequence(),It->GetNetworkPageCount(),It->IsMapReady(),uint64(M.NetworkBytes));
    }
#endif
}
void AERPlayerController::ERBlockRegenerate()
{
#if !UE_BUILD_SHIPPING
    if (!HasAuthority()) return;
    for (TActorIterator<AERBlockRoom> It(GetWorld()); It; ++It)
    {
        if (!It->Regenerate()) continue;
        if (GetPawn())
        {
            GetPawn()->SetActorLocation(It->GetSpawnLocation(), false, nullptr, ETeleportType::TeleportPhysics);
            GetPawn()->GetMovementComponent()->StopMovementImmediately();
        }
    }
    for (TActorIterator<AERBlockTarget> It(GetWorld()); It; ++It) It->ResetHealth();
#endif
}
void AERPlayerController::ERMapGenerate(int32 Seed, int32 Rooms, int32 Floors)
{
#if !UE_BUILD_SHIPPING
    if (!HasAuthority()) return;
    for (TActorIterator<AERProceduralMap> It(GetWorld()); It; ++It)
    {
        const FERProceduralConfig Previous = It->LayoutConfig;
        It->LayoutConfig.Seed = Seed; It->LayoutConfig.RoomCount = Rooms;
        if(Floors!=0)It->LayoutConfig.Floors=Floors;
        if (!It->Regenerate())
        {
            It->LayoutConfig = Previous;
            UE_LOG(LogTemp, Warning, TEXT("ER_MAP_REJECTED invalid config or block budget; previous layout preserved"));
            continue;
        }
        if (GetPawn())
        {
            const auto* State=GetPlayerState<AERPlayerState>();
            const auto& Starts=State && State->Team==EERTeam::Omega ? It->GetLayout().OmegaSpawns:It->GetLayout().AlphaSpawns;
            const FVector Spawn=Starts.IsEmpty() ? It->GetSpawnLocation():It->GetActorTransform().TransformPosition(Starts[0].GetLocation());
            GetPawn()->SetActorLocation(Spawn, false, nullptr, ETeleportType::TeleportPhysics);
            GetPawn()->GetMovementComponent()->StopMovementImmediately();
        }
    }
#endif
}
void AERPlayerController::ERBlockShot(float Energy, float Damage)
{
#if !UE_BUILD_SHIPPING
    if (!HasAuthority() || !GetPawn()) return;
    FVector Eye; FRotator Aim;
    GetPawn()->GetActorEyesViewPoint(Eye, Aim);
    if (auto* PlayerCharacter = Cast<AERCharacter>(GetPawn())) Eye = PlayerCharacter->Camera->GetComponentLocation();
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERBlockDebugShot), false, GetPawn());
    if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Aim.Vector() * 10000, ECC_GameTraceChannel1, Query))
    {
        FERBlockShotSettings Settings; Settings.Energy = Energy;
        ERBlockCombat::ContinueShot(GetWorld(), Hit, Eye, Aim.Vector(), 10000, Damage, Settings, Query, this, GetPawn());
    }
#endif
}
void AERPlayerController::ERBlockExplosion(float Radius, float Damage)
{
#if !UE_BUILD_SHIPPING
    if (!HasAuthority() || !GetPawn()) return;
    FVector Eye; FRotator Aim;
    GetPawn()->GetActorEyesViewPoint(Eye, Aim);
    if (auto* PlayerCharacter = Cast<AERCharacter>(GetPawn())) Eye = PlayerCharacter->Camera->GetComponentLocation();
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERBlockDebugBlast), false, GetPawn());
    if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Aim.Vector() * 10000, ECC_GameTraceChannel1, Query))
        if (auto* Room = Cast<AERBlockRoom>(Hit.GetActor()))
        {
            const FVector Origin = Hit.ImpactPoint + Hit.ImpactNormal * 10;
            const auto Result = AERBlockRoom::ApplyWorldExplosion(GetWorld(), Origin, Radius, Damage, GetPawn());
            if (Result.Damaged > 0) Room->SpawnExplosionEffect(Origin);
        }
#endif
}
void AERPlayerController::ERBlockGrenade()
{
#if !UE_BUILD_SHIPPING
    if (!HasAuthority() || !GetPawn()) return;
    FVector Eye; FRotator Aim;
    GetPawn()->GetActorEyesViewPoint(Eye, Aim);
    if (auto* PlayerCharacter = Cast<AERCharacter>(GetPawn())) Eye = PlayerCharacter->Camera->GetComponentLocation();
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERBlockDebugGrenade), false, GetPawn());
    if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Aim.Vector() * 10000, ECC_GameTraceChannel1, Query))
        if (auto* Room = Cast<AERBlockRoom>(Hit.GetActor()))
        {
            const FVector Origin = Hit.ImpactPoint + Hit.ImpactNormal * 10;
            const auto Result = AERBlockRoom::ApplyWorldExplosion(GetWorld(), Origin, FERBlockExplosionProfile(), GetPawn());
            if (Result.Damaged > 0) Room->SpawnExplosionEffect(Origin);
            UE_LOG(LogTemp, Display, TEXT("ER_BLOCK_GRENADE candidates=%d occluded=%d damaged=%d removed=%d rebuilt=%d query=%.2fms exposure=%.2fms mutation=%.2fms total=%.2fms"),
                Result.Candidates, Result.Occluded, Result.Damaged, Result.Destroyed, Result.GroupsRebuilt, Result.QueryMs, Result.ExposureMs, Result.MutationMs, Result.TotalMs);
        }
#endif
}
void AERHUD::DrawHUDText(const FString& Value, float X, float Y, FLinearColor Color, float Scale, bool Centered)
{
    DrawHUDTextAnchored(Value,FVector2D(X,Y),Color,Scale,FVector2D(Centered ? .5f : 0.f,0));
}

FVector2D AERHUD::MeasureHUDText(const FString& Value, float Scale)
{
    if (!Canvas || !Canvas->Canvas) return FVector2D::ZeroVector;
    const float ViewScale = FERHUDLayout(FVector2D(Canvas->SizeX,Canvas->SizeY)).Scale;
    if (!HUDTypeface)
    {
        // Canvas requires a UFont even when given a composite Slate typeface.
        HUDTypeface = NewObject<UFont>(this);
        HUDTypeface->FontCacheType = EFontCacheType::Runtime;
        HUDTypeface->GetMutableInternalCompositeFont() = *ERInterface::Font(24).GetCompositeFont();
    }
    const FSlateFontInfo Font(HUDTypeface,FMath::Max(1,FMath::RoundToInt(24.f*Scale*ViewScale)),TEXT("SemiBold"));
    const float DPI=Canvas->Canvas->GetDPIScale();
    return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Value,Font,DPI)/DPI;
}

FVector2D AERHUD::DrawHUDTextAnchored(const FString& Value, FVector2D Position, FLinearColor Color, float Scale, FVector2D Anchor)
{
    if (!Canvas || !Canvas->Canvas) return FVector2D::ZeroVector;
    const float ViewScale = FERHUDLayout(FVector2D(Canvas->SizeX,Canvas->SizeY)).Scale;
    const FVector2D Size=MeasureHUDText(Value,Scale);
    const FSlateFontInfo Font(HUDTypeface,FMath::Max(1,FMath::RoundToInt(24.f*Scale*ViewScale)),TEXT("SemiBold"));
    Position-=Size*Anchor;
    FCanvasTextItem Item(FVector2D(FMath::RoundToFloat(Position.X),FMath::RoundToFloat(Position.Y)),FText::FromString(Value),Font,Color);
    // Keep the shadow close to the glyphs at every viewport resolution.
    Item.EnableShadow(FLinearColor(0,0,0,.9f*Color.A), FVector2D(2,2));
    Canvas->DrawItem(Item);
    return Size;
}

void AERHUD::DrawHUD()
{
    Super::DrawHUD();
    auto* PC = Cast<AERPlayerController>(PlayerOwner); if (!PC || PC->bMenuOpen || !Canvas) return;
    if (PC->FlashOpacity > 0)
    {
        FCanvasTileItem Flash(FVector2D::ZeroVector, GWhiteTexture, FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor(1,1,1,PC->FlashOpacity));
        Flash.BlendMode = SE_BLEND_Translucent; Canvas->DrawItem(Flash);
    }
    auto* GS = GetWorld()->GetGameState<AERGameState>(); if (!GS) return;
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    const FERHUDLayout Layout(FVector2D(W,H));
    const float HUDScale = Layout.Scale;
    const FVector2D EdgeMargin=Layout.Edge();
    auto Text = [this,&Layout](const FString& Value, float X, float Y, FLinearColor Color = FLinearColor::White, float Scale = 1.3f) { const auto P=Layout.Position(X,Y); DrawHUDText(Value, P.X, P.Y, Color, Scale); };
    for (TActorIterator<AERBlockRoom> It(GetWorld()); It; ++It)
    {
        if(PC->IsBlockMapLoading())Text(PC->GetMapSyncText(),30,83,FLinearColor::Yellow,1);
        if(const auto* Map=Cast<AERProceduralMap>(*It))if(Map->LayoutConfig.TemplateAsset && !FParse::Param(FCommandLine::Get(),TEXT("ERBlockDebug")))continue;
        Text(TEXT("DESTRUCTION LAB 5cm CELLS | ERBlockGrenade / ERBlockShot 100 36 / ERBlockDamage 25 / ERBlockCollapseDemo"), 30, 105, FLinearColor::Yellow, 1);
        if (const auto* Map=Cast<AERProceduralMap>(*It)) Text(Map->LayoutConfig.TemplateAsset ? TEXT("ERMapGenerate 42 8 1 | template seed"):TEXT("ERMapGenerate 42 6 3 | seed / rooms 6..10 / floors 1..3"), 30, 185, FLinearColor::Yellow, 1);
        Text(It->GetStatistics(), 30, 125, FLinearColor::White, .9f);
        Text(TEXT("Last block: ") + It->LastHit, 30, 145, FLinearColor::White, 1);
        Text(It->LastCombat, 30, 165, FLinearColor::White, 1);
    }
    const bool bLab = GetWorld()->GetMapName().Contains(TEXT("L_MovementLab"));
    const auto* Character = Cast<AERCharacter>(PC->GetPawn());
    if (const auto* Weapon = Character && !Character->Health->bDead && !Character->IsWeaponStowed() ? Character->Weapon.Get() : nullptr; Weapon && Weapon->HasOpticalScope() && Weapon->ScopeLevel)
    {
        const float R = FMath::Min(W,H)*.48f, CX=W*.5f, CY=H*.5f;
        // Opaque outside a circular lens, including ultrawide side bars.
        for (int32 Y=0; Y<FMath::CeilToInt(H); ++Y)
        {
            const float DY=Y+.5f-CY;
            const float X=FMath::Abs(DY)<R ? FMath::Sqrt(R*R-DY*DY) : 0.f;
            DrawRect(FLinearColor::Black,0,Y,CX-X,1);
            DrawRect(FLinearColor::Black,CX+X,Y,W-CX-X,1);
        }
        const float Blur=Weapon->GetScopeBlur();
        for(int32 I=-4; I<=4; ++I)
        {
            if(I && Blur<=0) continue;
            const float Offset=I*Blur*2.f*HUDScale;
            const FLinearColor Ink(0,0,0,I==0?1.f-Blur*.7f:Blur*.12f);
            DrawRect(Ink,CX-R,CY+Offset-.75f*HUDScale,2.f*R,1.5f*HUDScale);
            DrawRect(Ink,CX+Offset-.75f*HUDScale,CY-R,1.5f*HUDScale,2.f*R);
        }
    }

    DrawDamageFeedback(Character);
    DrawInflictedDamage();
    const FLinearColor AlphaColor=ERRecon::TeamColor(EERTeam::Alpha), OmegaColor=ERRecon::TeamColor(EERTeam::Omega);
    DrawHUDText(FString::Printf(TEXT("%02d"),GS->AlphaScore),Layout.Position(555,20).X,Layout.Position(555,20).Y,AlphaColor,1.45f,true);
    DrawHUDText(FString::FromInt(int32(ERServer::Get(GetWorld(),"Match.ScoreLimit"))),W/2,Layout.Position(640,27).Y,FLinearColor(.8f,.82f,.85f),1.f,true);
    DrawHUDText(FString::Printf(TEXT("%02d"),GS->OmegaScore),Layout.Position(725,20).X,Layout.Position(725,20).Y,OmegaColor,1.45f,true);
    FString Status = TEXT("Waiting for players");
    if (GS->IsMatchInProgress())
    {
        const int32 Seconds = FMath::Max(0, FMath::CeilToInt(GS->MatchEndServerTime - GS->GetServerWorldTimeSeconds()));
        Status = FString::Printf(TEXT("%02d:%02d"), Seconds/60, Seconds%60);
    }
    else if (GS->GetMatchState() == MatchState::WaitingPostMatch)
        Status = GS->Winner == EERTeam::None ? TEXT("DRAW") : TEXT("ROUND OVER");
    else if (GS->IsPreMatchCountdown())
    {
        // The countdown is outside the match: the clock waits at its full duration.
        const int32 Seconds = FMath::Max(0, FMath::CeilToInt32(ERServer::Get(GetWorld(),"Match.Duration")));
        Status = FString::Printf(TEXT("%02d:%02d"), Seconds/60, Seconds%60);
    }
    if (bLab && Character) Status = FString::Printf(TEXT("%.1f km/h   |   Feet height: %.0f cm"), Character->GetVelocity().Size2D()*.036f, Character->GetActorLocation().Z-Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-2.1f);
    DrawHUDText(Status, W/2, Layout.Position(640,59).Y, FLinearColor(.8f,.82f,.85f), .9f, true);
    const FString NetworkStatus=PC->GetNetworkStatusText();
    if(!NetworkStatus.IsEmpty())DrawHUDText(NetworkStatus,W/2,Layout.Position(640,86).Y,FLinearColor(.72f,.76f,.82f),.45f,true);
    if (TActorIterator<AERReconDirector> It(GetWorld()); It) It->DrawMinimap(this,PC);
    if (bLab)
    {
        Text(TEXT("SHIFT: walk    SPACE + forward: climb"),30,105);
        Text(TEXT("30 cm: step    80 cm: vault    150 cm: shoulder height"),30,128);
    }
    if (Character && !Character->Health->bDead)
    {
        DrawVitals(Character);
        const bool bOptical = Character->Weapon && Character->Weapon->HasOpticalScope();
        if (!bOptical)
        {
            const float Inner=3*HUDScale,Outer=8*HUDScale;
            DrawLine(W/2-Outer,H/2,W/2-Inner,H/2,FLinearColor::White,HUDScale); DrawLine(W/2+Inner,H/2,W/2+Outer,H/2,FLinearColor::White,HUDScale);
            DrawLine(W/2,H/2-Outer,W/2,H/2-Inner,FLinearColor::White,HUDScale); DrawLine(W/2,H/2+Inner,W/2,H/2+Outer,FLinearColor::White,HUDScale);
        }
        if (const auto* Weapon = Character->Weapon.Get())
        {
            const int32 Capacity = Weapon->Data ? FMath::Clamp(Weapon->Tuned()->MagazineSize, 1, 120) : 30;
            const int32 Remaining = FMath::Clamp(Weapon->MagazineAmmo, 0, Capacity);
            const float Scale = HUDScale;
            const float HalfFov = FMath::DegreesToRadians(PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() * .5f : 45.f);
            const float Spread = Weapon->GetSpreadDegrees();
            // Keep dots separated even with the M249's larger magazine.
            // Beyond that margin the radius follows the same cone used by the server.
            const float TargetRadius = FMath::Max(26.f, Capacity*.55f) * Scale + (W * .5f / FMath::Tan(HalfFov)) * FMath::Tan(FMath::DegreesToRadians(Spread));
            if (ReticleWeapon.Get() != Weapon) { ReticleWeapon = const_cast<AERWeapon*>(Weapon); ReticleRadius = TargetRadius / W; }
            ReticleRadius = FMath::FInterpTo(ReticleRadius, TargetRadius / W, GetWorld()->GetDeltaSeconds(), 18.f);
            const bool bUnavailable = Character->IsWeaponStowed() || Weapon->bReloading || Cast<UERMovement>(Character->GetCharacterMovement())->bHighClimb;
            const FLinearColor AmmoColor = bUnavailable ? FLinearColor(1.f,.7f,.25f,.8f) :
                (Remaining <= Capacity / 5 ? FLinearColor(1.f,.4f,.18f,.95f) : FLinearColor(.8f,.95f,1.f,.9f));
            // Keep ammunition and the real spread cone visible through every aiming mode.
            for (int32 I = 0; I < Capacity; ++I)
            {
                const float Angle = -PI * .5f + (2.f * PI * I) / Capacity;
                const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
                const FVector2D Center = FVector2D(W*.5f,H*.5f) + Direction * (ReticleRadius * W);
                const FLinearColor DotColor = I < Remaining ? AmmoColor : FLinearColor(.14f,.19f,.23f,.55f);
                constexpr int32 DotSegments = 16;
                const float DotRadius = 1.85f * Scale;
                TArray<FCanvasUVTri> DotTriangles;
                DotTriangles.Reserve(DotSegments * 3);
                const auto AddDotTriangle = [&DotTriangles](const FVector2D& A, const FLinearColor& ColorA,
                    const FVector2D& B, const FLinearColor& ColorB,
                    const FVector2D& C, const FLinearColor& ColorC)
                {
                    FCanvasUVTri Triangle;
                    Triangle.V0_Pos = A; Triangle.V0_Color = ColorA;
                    Triangle.V1_Pos = B; Triangle.V1_Color = ColorB;
                    Triangle.V2_Pos = C; Triangle.V2_Color = ColorC;
                    DotTriangles.Add(Triangle);
                };
                const auto AddDisc = [&](FVector2D At, float Radius, float Feather, FLinearColor Ink)
                {
                    const FLinearColor Clear = Ink.CopyWithNewOpacity(0);
                    for (int32 Segment = 0; Segment < DotSegments; ++Segment)
                    {
                        const float A = Segment * 2.f * PI / DotSegments, B = (Segment+1) * 2.f * PI / DotSegments;
                        const FVector2D DA(FMath::Cos(A),FMath::Sin(A)), DB(FMath::Cos(B),FMath::Sin(B));
                        const FVector2D InnerA=At+DA*(Radius-Feather), InnerB=At+DB*(Radius-Feather);
                        const FVector2D OuterA=At+DA*Radius, OuterB=At+DB*Radius;
                        AddDotTriangle(At,Ink,InnerA,Ink,InnerB,Ink);
                        AddDotTriangle(InnerA,Ink,OuterA,Clear,OuterB,Clear);
                        AddDotTriangle(InnerA,Ink,OuterB,Clear,InnerB,Ink);
                    }
                };
                AddDisc(Center+FVector2D(0,.75f*Scale),DotRadius+1.5f*Scale,1.4f*Scale,FLinearColor(0,0,0,.8f));
                AddDisc(Center,DotRadius,.7f*Scale,DotColor);
                FCanvasTriangleItem Dot(DotTriangles, GWhiteTexture);
                Dot.BlendMode = SE_BLEND_Translucent;
                Canvas->DrawItem(Dot);
            }
#if !UE_BUILD_SHIPPING
            if (FParse::Param(FCommandLine::Get(), TEXT("ERAccuracyAudit")) && GetWorld()->GetTimeSeconds() >= NextReticleAuditAt)
            {
                NextReticleAuditAt = GetWorld()->GetTimeSeconds() + .1;
                UE_LOG(LogTemp,Display,TEXT("ER_RETICLE ammo=%d ticks=%d capacity=%d spread=%.3f bloom=%.3f radius=%.1f reload=%d"),
                    Weapon->MagazineAmmo,Remaining,Capacity,Spread,Weapon->GetBloomDegrees(),ReticleRadius*W,Weapon->bReloading);
            }
#endif
        }
        DrawWeaponInventory(Character);
        if(Character->Weapon && Character->Weapon->bPlacingClaymore)DrawHUDText(TEXT("PLACING CLAYMORE..."),W/2,Layout.Position(640,570).Y,FLinearColor(.4f,.8f,1),1.f,true);
        if (Character->Health->IsProtected()) DrawHUDText(TEXT("SPAWN PROTECTION"), W/2,Layout.Position(640,601).Y,FLinearColor(.4f,.8f,1),1.3f,true);
    }
    if (Character && Character->IsDeathCameraActive()) DrawKillerHealth(Character);
    if (PC->bAwaitingRespawn)
    {
        const int32 Seconds = FMath::Max(0, FMath::CeilToInt(PC->RespawnAllowedAt-GS->GetServerWorldTimeSeconds()));
        const bool KillerView = Character && Character->IsDeathCameraActive();
        DrawHUDText(Seconds > 0 ? FString::Printf(TEXT("Respawn available in %d"),Seconds) : TEXT("Press fire or jump to respawn"),
            W/2,KillerView ? Layout.Position(640,620).Y : Layout.Position(640,420).Y,FLinearColor(1,.6f,.3f),KillerView ? 1.2f : 1.6f,true);
    }
    DrawPreMatchCountdown(GS);
    if (PC->bShowScoreboard || GS->GetMatchState() == MatchState::WaitingPostMatch) DrawScoreboard(GS);
    const double Now = GetWorld()->GetTimeSeconds();
    if (GS->GetMatchState() == MatchState::WaitingPostMatch && PC->GameplayToast.Expires > Now)
    {
        DrawHUDText(PC->GameplayToast.Title,W/2,Layout.Position(640,80).Y,FLinearColor(1.f,.76f,.38f),.82f,true);
        DrawHUDText(PC->GameplayToast.Body,W/2,Layout.Position(640,105).Y,FLinearColor(.82f,.85f,.9f),.72f,true);
    }
    if (!PC->bShowScoreboard)
    {
        const bool Results = GS->GetMatchState() == MatchState::WaitingPostMatch;
        const float X = EdgeMargin.X;
        const float Width = Results ? FMath::Max(80.f*HUDScale,Layout.Position(288,0).X-X) : W * .38f;
        float Y = Layout.Position(0,96).Y;
        const auto Line = [&](FString Value, FLinearColor Color, float Scale, float AtY)
        {
            if (Value.IsEmpty()) return;
            // Measure without drawing: player names remain plain Canvas text.
            const FSlateFontInfo Font(HUDTypeface,FMath::Max(1,FMath::RoundToInt(24.f*Scale*HUDScale)),TEXT("SemiBold"));
            const float DPI = Canvas->Canvas->GetDPIScale();
            auto Measure = [&](const FString& Text) { return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text,Font,DPI).X / DPI; };
            if (Measure(Value) > Width)
            {
                while (Value.Len() > 1 && Measure(Value + TEXT("…")) > Width) Value.LeftChopInline(1);
                Value += TEXT("…");
            }
            DrawHUDText(Value, X, AtY, Color, Scale);
        };
        if (!Results) DrawKillFeed(GS, X, Y);
        for (int32 I = 0; I < PC->KillConfirmations.Num(); ++I)
        {
            const auto& Item = PC->KillConfirmations[I];
            if (Item.Expires <= Now) continue;
            Line(Item.Title,FLinearColor(.88f,.9f,.94f),.82f,Y); Y += 24.f * HUDScale;
            if (I == PC->KillConfirmations.Num()-1 && !Item.Body.IsEmpty())
            { Line(Item.Body,FLinearColor(.72f,.76f,.82f),.72f,Y); Y += 24.f * HUDScale; }
        }
        if (!Results && PC->GameplayToast.Expires > Now)
        {
            Y = FMath::Max(Y + 8.f * HUDScale, Layout.Position(0,198).Y);
            Line(PC->GameplayToast.Title,FLinearColor(1.f,.76f,.38f),.82f,Y);
            Line(PC->GameplayToast.Body,FLinearColor(.82f,.85f,.9f),.72f,Y + 24.f * HUDScale);
        }
    }
    if (PC->ScreenshotNoticeUntil > FPlatformTime::Seconds())
    {
        DrawHUDText(PC->ScreenshotStatus, EdgeMargin.X, Layout.Position(0,270).Y, FLinearColor(1.f,.76f,.38f), .8f);
        // Wrap the absolute path instead of truncating the directory players need.
        const int32 CharactersPerLine = FMath::Max(20, FMath::FloorToInt32((W - 2.f * EdgeMargin.X) / (12.f * HUDScale)));
        for (int32 Offset = 0; Offset < PC->ScreenshotDirectory.Len(); Offset += CharactersPerLine)
            DrawHUDText(PC->ScreenshotDirectory.Mid(Offset, CharactersPerLine), EdgeMargin.X,
                Layout.Position(0,296).Y + (Offset / CharactersPerLine) * 22.f * HUDScale, FLinearColor::White, .65f);
    }
    TickHUDLayoutGallery();
}

void AERPlayerController::ERBlockCollapseDemo()
{
#if !UE_BUILD_SHIPPING
    if (!HasAuthority()) return;
    TActorIterator<AERBlockRoom> It(GetWorld());
    if (!It) return;
    // Cut a ring through every layer of the lab's south wall (wall 0), 20cm deep
    // below: the 145x95cm inside loses all support, collapses and has room to fall.
    TMap<FERCellId, float> Ring;
    const auto& Cells = It->GetCellWorld();
    for (int32 P = 0; P < Cells.GetPanels().Num(); ++P)
    {
        if (Cells.GetPanels()[P].Id / 8 != 0) continue;
        for (int32 X = 30; X <= 60; ++X) for (int32 Z = 17; Z <= 40; ++Z) for (int32 Y = 0; Y < Cells.GetPanels()[P].Dims.Y; ++Y)
            if (X == 30 || X == 60 || Z <= 20 || Z == 40) Ring.Add({P, X, Y, Z}, TNumericLimits<float>::Max());
    }
    It->ApplyCellBatch(Ring);
#endif
}

void UERGameInstance::HostProcedural()
{
    LastError.Empty();
    MapChoice=0;SessionMode=1;StartSession();
}
void AERPlayerController::OnPossess(APawn* PossessedPawn)
{
    Super::OnPossess(PossessedPawn);
    TActorIterator<AERBlockRoom> Room(GetWorld());if(Room)HoldForBlockMap(*Room);
}
void AERPlayerController::HoldForBlockMap(AERBlockRoom* Room,bool Reposition)
{
    if(!HasAuthority() || !Room || Room->GetWorld()!=GetWorld() || GetNetMode()==NM_Standalone)return;
    auto* C=Cast<AERCharacter>(GetPawn());if(!C)return;
    if(Reposition)
    {
        FVector P=Room->GetSpawnLocation();
        const auto* PS=GetPlayerState<AERPlayerState>();
        if(auto* Map=Cast<AERProceduralMap>(Room))if(PS && PS->Team==EERTeam::Omega)P=Map->GetActorTransform().TransformPosition(Map->GetLayout().Routes.Last()[0]);
        P.X+=(PS && (PS->GetPlayerId()/2)%2 ? -80:80);C->TeleportTo(P,FRotator::ZeroRotator);
        C->GetCharacterMovement()->StopMovementImmediately();
        if(IsLocalController() && !C->Health->bDead)C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        C->GetCharacterMovement()->ForceClientAdjustment();
    }
    const bool Ready=!bWaitingCellRecovery && Room->IsMapReady() && (IsLocalController() || (AcknowledgedBlockRoom.Get()==Room && AcknowledgedBlockEpoch==Room->GetMapEpoch()));
    if(!Ready && (WaitingBlockRoom.Get()!=Room || WaitingBlockEpoch!=Room->GetMapEpoch() || WaitingBlockPawn.Get()!=C))
    {
        if(WaitingBlockRoom.Get()!=Room || WaitingBlockEpoch!=Room->GetMapEpoch())bWaitingCellRecovery=false;
        WaitingBlockRoom=Room;WaitingBlockEpoch=Room->GetMapEpoch();WaitingBlockPawn=C;
        MapSyncDeadline.Reset(FPlatformTime::Seconds());
    }
    SetPawnNetworkReady(Ready);
}

void AERPlayerController::ServerBlockProbeReport_Implementation(AERBlockNetworkProbe* Probe,int32 Stage,uint32 Epoch,uint32 Sequence,uint32 Digest,bool Passed)
{
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("ERBlockNetworkProbe")) && Probe && Probe->GetWorld()==GetWorld())Probe->ReceiveReport(this,Stage,Epoch,Sequence,Digest,Passed);
#endif
}

void AERPlayerController::ServerAudioProbeReady_Implementation(AERAudioProbe* Probe,uint32 Token)
{
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("ERAudioProbe")) && Probe && Probe->GetWorld()==GetWorld())Probe->ConfirmReady(this,Token);
#endif
}

void AERPlayerController::ServerAimPunchProbeReady_Implementation(AERAimPunchProbe* Probe,int32 Stage)
{
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("ERAimPunchProbe")) && Probe && Probe->GetWorld()==GetWorld())Probe->ConfirmReady(this,Stage);
#endif
}

void AERPlayerController::ServerSetLoadout_Implementation(FName Preset,FName Variant)
{
    if(const auto* GS=GetWorld()->GetGameState<AERGameState>();GS && GS->bLobbyStarting)return;
    const auto Kit=ERLoadouts::Resolve(Preset,Variant);
    if(auto* PS=GetPlayerState<AERPlayerState>())
    { PS->LoadoutPreset=Kit.Preset;PS->LoadoutVariant=Kit.Variant;PS->bLobbyReady=false;PS->ForceNetUpdate(); }
    ClientLoadoutAccepted(Kit.Preset,Kit.Variant);
}
void AERPlayerController::ClientLoadoutAccepted_Implementation(FName Preset,FName Variant)
{
    auto* GI=GetGameInstance<UERGameInstance>();if(!GI)return;
    const auto Kit=ERLoadouts::Resolve(Preset,Variant);
    GI->LoadoutPreset=Kit.Preset;GI->LoadoutVariant=Kit.Variant;GI->SaveLoadout();bLoadoutPending=false;
}

void AERPlayerController::ServerChangeTeam_Implementation(int32 Team, bool Suicide)
{
    const auto* Mode=GetWorld()->GetAuthGameMode<AERGameMode>();
    auto* PS=GetPlayerState<AERPlayerState>();
    if(!Mode || Mode->IsLobby() || !Mode->IsMatchInProgress() || !PS || Team<0 || Team>1)return;
    const EERTeam Wanted=Team==1?EERTeam::Omega:EERTeam::Alpha;
    PS->PendingTeam=Wanted==PS->Team?EERTeam::None:Wanted;
    PS->ForceNetUpdate();
    if(Suicide && PS->PendingTeam!=EERTeam::None)
        if(auto* C=Cast<AERCharacter>(GetPawn()); C && !C->Health->bDead)
        {
            C->Health->ProtectionEndServerTime=0;
            C->Health->ReceiveDamage(10000,this);
        }
}
