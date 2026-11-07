#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "ERServerSettings.h"
#include "ERLoadout.h"
#include "ERGameplayMessages.h"
#include "ERNetworkQuality.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "GameFramework/HUD.h"
#include "InputActionValue.h"
#include "ERPlayerController.generated.h"

UCLASS()
class FDM_API UERGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite) FString LocalName = TEXT("Recruit");
    UPROPERTY(BlueprintReadOnly) FString LastError;
    UPROPERTY(BlueprintReadOnly) FString BuildVersion;
    FString GetNetworkBuildVersion() const {return ERNetworkProtocol::BuildSignature(BuildVersion);}
    virtual void Init() override;
    virtual void Shutdown() override;
    void Host(int32 BotCount = 0, bool bStationaryTarget = false);
    void HostProcedural();
    void StartSession();
    int32 GetStartingWeapon() const;
    void StartLANServer();
    int32 MaxPlayers = ERServer::DefaultMaxPlayers;
    void SaveServerSettings();
    UPROPERTY() FERServerRules ServerRules; // Saved draft for the next hosted session.
    UPROPERTY() FERServerRules SessionRules; // Frozen at session creation and retained through lobby/travel/rounds.
    void CommitServerRules();
    void FindLANServer();
    void CancelLANSearch();
    bool IsSearchingLAN() const { return SearchDeadline > 0; }
    int32 LANSecondsLeft() const;
    void SaveProfile();
    int32 SpareMagazines = 3;
    int32 HECount = 2;
    int32 SmokeCount = 2;
    int32 StunCount = 2;
    bool bInternetMenu = true;
    FString InternetServerName = TEXT("Respawn & Rage");
    int32 SessionMode = 0; // local, listen server, client
    // Server-only session history; survives seamless map travel, never saved.
    TArray<FName> RoundMapHistory;
    int32 MapChoice = 0;
    int32 PlayerTeam = 0;
    int32 AlphaBots = 0;
    int32 OmegaBots = 1;
    int32 BotBehavior = 0; // stationary, movement, combat
    FString ServerAddress = TEXT("127.0.0.1:7777");
    bool Join(const FString& Address);
    void ReturnToMenu();
    void QuitGame();
    static bool IsValidAddress(const FString& Address);
    float GetMouseSensitivity() const { return MouseSensitivity; }
    float GetMouseAcceleration() const { return MouseAcceleration; }
    void SetMouseAcceleration(float Value);
    float GetMouseLookMultiplier() const;
    void SetMouseSensitivity(float Value);
    void SaveMouseSensitivity();
    float GetSoundVolume() const { return SoundVolume; }
    void SetSoundVolume(float Value);
    float GetMusicVolume() const { return MusicVolume; }
    void SetMusicVolume(float Value);
    void SaveMusicVolume();
    int32 PrimaryWeapon = 0; // legacy primary preference
    FName LoadoutPreset = "rifleman";
    FName LoadoutVariant = "offense";
    FString LoadoutOptions() const;
    int32 ReferenceStartingWeapon = 4;
    int32 AlphaSkin = 0;
    int32 OmegaSkin = 0;
    void SaveSkins();
    void SaveLoadout();
    bool bInitialWindowSizeApplied = false;
private:
    bool bQuitRequested = false;
    class FSocket* DiscoverySocket = nullptr;
    FTSTicker::FDelegateHandle DiscoveryTicker;
    double SearchDeadline = 0;
    double NextDiscoveryAt = 0;
    FString DiscoveryNonce;
    bool TickDiscovery(float DeltaTime);
    void CloseDiscoverySocket();
    float MouseSensitivity = .5f;
    float MouseAcceleration = 0.f;
    float SoundVolume = 1.f;
    float MusicVolume = 1.f;
    FDelegateHandle NetworkFailureHandle, TravelFailureHandle;
    void NetworkFailure(UWorld* World, class UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
    void TravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);
};

UCLASS()
class FDM_API AERPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    AERPlayerController();
    struct FDamageNotice { float Health=0, Armor=0; double Time=0; };
    TArray<FDamageNotice> DamageNotices;
    UFUNCTION(Client, Reliable) void ClientInflictedDamage(float HealthDamage, float ArmorDamage);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UERReconComponent> Recon;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bAwaitingRespawn = false;
    UPROPERTY(Replicated, BlueprintReadOnly) double RespawnAllowedAt = 0;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UInputMappingContext> GameplayMapping;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UInputAction> MoveAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UInputAction> LookAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UInputAction> JumpAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UInputAction> CrouchAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UInputAction> FireAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UInputAction> ReloadAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UInputAction> ScoreboardAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UInputAction> MenuAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite) TObjectPtr<class UInputAction> WalkAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Menu audio") TObjectPtr<class USoundBase> MenuMusic;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Menu audio", meta=(ClampMin="0", ClampMax="1")) float MenuMusicVolume = .35f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Arsenal") TArray<TObjectPtr<class UERWeaponData>> PrimaryOptions;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Arsenal") bool bReferenceArsenalMenu = false;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Team appearance") TSubclassOf<class AERCharacter> SkinCatalog;

    UFUNCTION(Client, Reliable) void ClientGameplayMessage(const FString& Event, const FString& Title, const FString& Body,
        int32 Priority, float Duration, bool Confirmation);
    TArray<FERGameplayToast> KillConfirmations;
    FERGameplayToast GameplayToast;
    float FlashOpacity = 0;
    bool bShowScoreboard = false;
    bool bMenuOpen = false;
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaTime) override;
    virtual void TickActor(float DeltaSeconds,ELevelTick TickType,FActorTickFunction& ThisTickFunction) override;
    virtual void OnPossess(APawn* PossessedPawn) override;
    void HoldForBlockMap(class AERBlockRoom* Room,bool Reposition=false);
    UFUNCTION(Server,Reliable) void ServerAimPunchProbeReady(class AERAimPunchProbe* Probe, int32 Stage);
    UFUNCTION(Server,Reliable) void ServerAudioProbeReady(class AERAudioProbe* Probe, uint32 Token);
    UFUNCTION(Server,Reliable) void ServerBlockProbeReport(class AERBlockNetworkProbe* Probe,int32 Stage,uint32 Epoch,uint32 Sequence,uint32 Digest,bool Passed);
    UFUNCTION(Server,Reliable) void ServerBlockMapReady(class AERBlockRoom* Room,uint32 Epoch,class APawn* ReadyPawn,uint32 Sequence);
    UFUNCTION(Server,Unreliable) void ServerBlockMapProgress(class AERBlockRoom* Room,uint32 Epoch,uint64 Progress);
    UFUNCTION(Server,Reliable) void ServerRequestCellRecovery(class AERBlockRoom* Room,uint32 Epoch);
    UFUNCTION(Server,Reliable) void ServerAckCellRecovery(class AERBlockRoom* Room,uint32 RecoveryId);
    UFUNCTION(Client,Reliable) void ClientCellRecoveryResult(bool Accepted);
    void NotifyCellRecoveryApplied(class AERBlockRoom* Room,uint32 RecoveryId);
    bool IsCellRecoveryPending() const {return bWaitingCellRecovery;}
    UFUNCTION(Server,Unreliable) void ServerNetworkSample(uint16 Token);
    UFUNCTION(Client,Unreliable) void ClientNetworkSample(uint16 Token,float ServerFrameMs);
    FString GetNetworkStatusText() const;
    FString GetMapSyncText() const;
    FString ScreenshotStatus, ScreenshotDirectory;
    double ScreenshotNoticeUntil = 0;
    UFUNCTION(Exec) void CapturePlayerScreenshots();
    void TickPlayerScreenshots();
    void FinishPlayerScreenshots();
    UPROPERTY(Transient) TObjectPtr<class ASceneCapture2D> ScreenshotCamera;
    FDelegateHandle ScreenshotProcessedHandle;
    TArray<FTransform> ScreenshotViews;
    int32 ScreenshotIndex = 0, SavedScreenshotCount = 0;
    uint64 ScreenshotReadFrame = 0;
    virtual void SetupInputComponent() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION(Server, Reliable) void ServerRequestRespawn();
    UFUNCTION(Server, Reliable) void ServerSetName(const FString& Name);
    UFUNCTION(Server, Reliable) void ServerSetLoadout(FName Preset, FName Variant);
    UFUNCTION(Client, Reliable) void ClientLoadoutAccepted(FName Preset, FName Variant);
    UFUNCTION(Server, Reliable) void ServerSetPrimary(int32 Index);
    UFUNCTION(Server, Reliable) void ServerSetSkins(int32 Alpha, int32 Omega);
    int32 RequestedTeam = 0;
    UFUNCTION(Server, Reliable) void ServerChangeTeam(int32 Team, bool Suicide);
    UFUNCTION(Server, Reliable) void ServerLobbyTeam(int32 Team);
    UFUNCTION(Server, Reliable) void ServerLobbyVote(int32 Map);
    UFUNCTION(Server, Reliable) void ServerLobbyReady(bool Ready);
    UFUNCTION(Server, Reliable) void ServerSetSupplies(int32 Magazines, int32 HE, int32 Smoke, int32 Stun);
    UFUNCTION(Client, Reliable) void ClientFlash(float Strength);
    virtual void PostSeamlessTravel() override;
    bool IsMenuLevel() const;
    bool IsBlockMapLoading() const {return bBlockMapLoading;}
    void ShowMenu(bool bShow);
    void ScrollWeapon(int32 Direction);
    UFUNCTION(Exec) void ERMenu(int32 Page = 0);
    UFUNCTION(Exec) void ERBlockDamage(float Amount = 25);
    UFUNCTION(Exec) void ERBlockDestroy();
    UFUNCTION(Exec) void ERBlockRegenerate();
    UFUNCTION(Exec) void ERBlockStats();
    UFUNCTION(Exec) void ERBlockCollapseDemo();
    UFUNCTION(Exec) void ERMapGenerate(int32 Seed = 42, int32 Rooms = 8, int32 Floors = 0);
    UFUNCTION(Exec) void ERBlockShot(float Energy = 100, float Damage = 10);
    UFUNCTION(Exec) void ERBlockExplosion(float Radius = 250, float Damage = 180);
    // HE structural profile (400/25/200/^2) 10cm before the aimed surface.
    UFUNCTION(Exec) void ERBlockGrenade();
private:
    friend class UERGameInstance;
    friend class AERLobbyProbe;
    friend class AERServerSettingsProbe;
    friend class AERLoadoutProbe;
    friend class AERGameState;
    FName DraftPreset, DraftVariant;
    bool bLoadoutPending = false;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<class UTextureRenderTarget2D>> LoadoutPortraits;
    TArray<TSharedPtr<FSlateBrush>> PortraitBrushes;
    UPROPERTY(Transient) TArray<TObjectPtr<class UTexture2D>> LoadoutIconTextures;
    TArray<TSharedPtr<FSlateBrush>> LoadoutIconBrushes;
    void RefreshLoadoutPreview();
    FERResolvedLoadout ResolveMenuLoadout(FName Preset,FName Variant) const;
    int32 SubmenuParent = 0;
    int32 ServerRulesParent = 2;
    FString ServerRulesSection = TEXT("Match");
    bool bServerRulesAdvanced = false;
    FString ServerPresetName = TEXT("Custom");
    int32 MenuPage = 0; // home, offline, server, search, setup, character, loadout
    bool bShowingLobby = false;
    void OpenMenuPage(int32 Page);
    bool IsInLobby() const;
    UPROPERTY(Transient) TObjectPtr<class AERMenuPreview> MenuPreview;
    TSharedPtr<struct FSlateBrush> PreviewBrush;
    UPROPERTY(Transient) TObjectPtr<class UAudioComponent> MenuMusicComponent;
    void UpdateMenuMusic();
    uint32 AcknowledgedBlockEpoch=0;
    TWeakObjectPtr<class AERBlockRoom> AcknowledgedBlockRoom;
    TWeakObjectPtr<APawn> AcknowledgedBlockPawn;
    bool bBlockMapLoading=false;
    void UpdateBlockMapLoading();
    void SetPawnNetworkReady(bool Ready);
    void TickNetworkQuality(float DeltaSeconds);
    TWeakObjectPtr<class AERBlockRoom> WaitingBlockRoom;
    TWeakObjectPtr<APawn> WaitingBlockPawn;
    uint32 WaitingBlockEpoch=0;
    FERMapSyncDeadline MapSyncDeadline;
    double NextMapProgressAt=0,NextRecoveryRequestAt=0,NextServerRecoveryAt=0;
    double LastClientProgressAt=0;
    uint64 LastClientProgress=0;
    int32 RecoveryAttempts=0;
    bool bWaitingCellRecovery=false,bCellRecoveryRequested=false;
    double NextNetworkSampleAt=0,NextServerSampleAt=0,LastNetworkReplyAt=0;
    float ServerFramePeriodMs=0,RemoteServerFrameMs=0;
    uint16 NetworkSampleToken=0;
    struct FPendingNetworkSample {uint16 Token=0;double SentAt=0;bool Pending=false;};
    FPendingNetworkSample NetworkSamples[8];
    FERNetworkQuality NetworkQuality;
#if !UE_BUILD_SHIPPING
    uint32 DebugSyncEpoch=0;
    double DebugSyncReadyAt=0;
#endif
    double LastForwardTap = -1;
    double LastCrouchTap = -1;
    TWeakObjectPtr<APawn> GesturePawn;
    bool bSprintKeyHeld = false, bForwardSprintHeld = false, bForwardHeld = false;
    void ResetSprintInput();
    void PrepareGesturePawn(class AERCharacter* InCharacter);
    void UpdateSprintInput();
    void SprintPressed();
    void SprintReleased();
    void ForwardPressed();
    void ForwardReleased();
    double WheelQuietUntil = 0;
    TSharedPtr<class SWidget> MenuWidget;
    TWeakPtr<class SWidget> MenuFocusWidget;
    TWeakPtr<class SWindow> InputWindow;
    FIntPoint InputViewportSize = FIntPoint::ZeroValue;
    FVector2D InputWindowPosition = FVector2D::ZeroVector;
    int32 InputWindowMode = -1;
    bool bInputWindowActive = false;
    bool bPendingWindowInput = true;
    double InputWindowStableAt = 0;
    void ApplyWindowInputMode();
    void UpdateWindowInput();
    TSharedPtr<class SWidget> FrameRateWidget;
    TSharedPtr<class SComboButton> DisplaySizeSelector;
    float FrameTimeMs = 0;
    FIntPoint LastRenderViewport = FIntPoint::ZeroValue;
    float SceneOutputFraction = 1;
    bool bPerformanceAudit = false;
    double NextPerformanceAuditAt = 0;
    void UpdateRenderResolution();
    void ResetRenderResolution();
    FText GetFrameRateText() const;
    FIntPoint GetDisplayResolution() const;
    FText GetDisplaySizeText() const;
    void ApplyDisplaySize(int32 Percent);
    TSharedPtr<class SEditableTextBox> NameBox, AddressBox;
    bool bInputAudit = false;
    double NextInputAuditAt = 0;
    double NextMouseAuditAt = 0;
    void WalkPressed();
    void WalkReleased();
    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void FirePressed();
    void FireReleased();
    void JumpPressed();
    void JumpReleased();
    void CrouchPressed();
    void CrouchReleased();
    void ReloadPressed();
    void ScoreboardPressed();
    void ScoreboardReleased();
    void MenuPressed();
    void SelectSlot(int32 Slot);
    void SelectNumber(int32 Index);
    void SelectPrimary() { SelectNumber(0); }
    void SelectSidearm() { SelectNumber(1); }
    void SelectHE() { SelectNumber(2); }
    void SelectSmoke() { SelectNumber(3); }
    void SelectStun() { SelectNumber(4); }
    void SelectSix() { SelectNumber(5); }
    void SelectSeven() { SelectNumber(6); }
    void SelectEight() { SelectNumber(7); }
    void SelectReferenceHE() { SelectSlot(8); }
    void SelectReferenceSmoke() { SelectSlot(9); }
    void SelectClaymore() { SelectSlot(10); }
    void NextWeapon();
    void PreviousWeapon();
    void ZoomPressed();
    void ZoomReleased() {}
};

UCLASS()
class FDM_API AERHUD : public AHUD
{
    GENERATED_BODY()
    friend class AERReconDirector;
public:
    AERHUD();
    virtual void DrawHUD() override;
    void DrawHUDText(const FString& Value, float X, float Y, FLinearColor Color = FLinearColor::White,
        float Scale = 1.3f, bool Centered = false);
private:
    void DrawDamageFeedback(const class AERCharacter* Character);
    void DrawInflictedDamage();
    void DrawVitals(const class AERCharacter* Character);
    void DrawWeaponInventory(const class AERCharacter* Character);
    UPROPERTY() TArray<TObjectPtr<class UTexture2D>> WeaponIcons;
    UPROPERTY() TObjectPtr<class UTexture2D> DroneIcon;
    void TickHUDLayoutGallery();
    int32 HUDGalleryStep = 0;
    double HUDGalleryNextAt = 6;
    FVector2D DrawHUDTextAnchored(const FString& Value, FVector2D Position, FLinearColor Color,
        float Scale, FVector2D Anchor);
    UPROPERTY() TArray<TObjectPtr<class UTexture2D>> VitalsTextures;
    UPROPERTY(Transient) TObjectPtr<class UFont> HUDTypeface;
    TWeakObjectPtr<class AERWeapon> ReticleWeapon;
    float ReticleRadius = 0; // Fraction of viewport width, so resizing has no pixel lag.
    double NextReticleAuditAt = 0;
};
