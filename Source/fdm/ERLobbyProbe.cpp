#include "ERLobbyProbe.h"
#include "EROnline.h"
#include "ERNetDriverEOS.h"
#include "ERPlayerController.h"
#include "ERCharacter.h"
#include "ERMatch.h"
#include "ERMapCatalog.h"
#include "ERWeapon.h"
#include "ERBlockRoom.h"
#include "ERProceduralMap.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"

AERLobbyProbe::AERLobbyProbe() { bReplicates = true; bAlwaysRelevant = true; PrimaryActorTick.bCanEverTick = true; }
void AERLobbyProbe::Check(bool Value, const TCHAR* Label)
{
    Failed |= !Value;
    UE_LOG(LogTemp, Display, TEXT("ER_LOBBY_%s %s"), Value ? TEXT("PASS") : TEXT("FAIL"), Label);
}
void AERLobbyProbe::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
#if !UE_BUILD_SHIPPING
    auto* GI = GetGameInstance<UERGameInstance>();
    auto* PC = Cast<AERPlayerController>(GetWorld()->GetFirstPlayerController());
    if (!GI || !PC || !PC->IsLocalController()) return;
    if (FParse::Param(FCommandLine::Get(), TEXT("ERWindowInputProbe")))
    {
        const double Now = FPlatformTime::Seconds();
        if (GetWorld()->GetTimeSeconds() < 3 || Now < Deadline) return;
        auto* Viewport = GetWorld()->GetGameViewport();
        auto& Slate = FSlateApplication::Get();
        if (Phase == 0)
        {
            Check(PC->bInputWindowActive && !PC->bPendingWindowInput, TEXT("startup_foreground_input_settled"));
            Check(PC->bMenuOpen && PC->bShowMouseCursor && !Viewport->Viewport->HasMouseCapture(), TEXT("startup_menu_releases_mouse"));
            PC->ERMenu(5);
            Phase = 1; Deadline = Now + 1;
        }
        else if (Phase == 1)
        {
            Check(PC->NameBox.IsValid(), TEXT("settings_name_widget_available"));
            if (PC->NameBox.IsValid()) Slate.SetKeyboardFocus(PC->NameBox);
            Phase = 2; Deadline = Now + 1;
        }
        else if (Phase == 2)
        {
            Check(PC->NameBox.IsValid() && (PC->NameBox->HasKeyboardFocus() || PC->NameBox->HasFocusedDescendants()), TEXT("stable_menu_preserves_text_focus"));
            PC->ShowMenu(false);
            Phase = 3; Deadline = Now + 1;
        }
        else if (Phase == 3)
        {
            Check(!PC->bShowMouseCursor && Viewport->Viewport->HasMouseCapture(), TEXT("game_captures_mouse_without_click"));
            Slate.ReleaseAllPointerCapture();
            Check(!Viewport->Viewport->HasMouseCapture(), TEXT("simulate_lost_game_capture"));
            Phase = 4; Deadline = Now + 1;
        }
        else if (Phase == 4)
        {
            Check(Viewport->Viewport->HasMouseCapture(), TEXT("game_recovers_lost_capture_without_click"));
            PC->ShowMenu(true);
            PC->ApplyDisplaySize(60);
            Phase = 5; Deadline = Now + 2;
        }
        else
        {
            Check(PC->bInputWindowActive && !PC->bPendingWindowInput && PC->bShowMouseCursor &&
                !Viewport->Viewport->HasMouseCapture(), TEXT("resized_menu_releases_mouse"));
            UE_LOG(LogTemp, Display, TEXT("ER_WINDOW_INPUT_COMPLETE result=%s"), Failed ? TEXT("FAIL") : TEXT("PASS"));
            FPlatformMisc::RequestExitWithStatus(false, Failed ? 1 : 0);
        }
        return;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("EROnlineUnconfiguredProbe")))
    {
        if (GetWorld()->GetTimeSeconds() < 2 || Phase) return;
        Phase = 1;
        auto* Online = GI->GetSubsystem<UEROnline>();
        if (GetNetMode() == NM_Standalone)
        {
            Online->Host();
            Check(!Online->IsBusy() && !Online->IsOnline() && Online->Status.Contains(TEXT("not configured")), TEXT("unconfigured_online_host_fails_without_travel"));
            Online->Search();
            Check(!Online->IsBusy() && Online->Results.IsEmpty() && Online->Status.Contains(TEXT("not configured")), TEXT("unconfigured_online_search_is_actionable"));
            Online->Cancel();
            Check(!Online->IsBusy() && Online->Status.IsEmpty(), TEXT("online_cancel_restores_idle"));
            if (Failed) { FPlatformMisc::RequestExitWithStatus(false, 1); return; }
            GI->StartLANServer();
        }
        else
        {
            auto* Driver = Cast<UERNetDriverEOS>(GetWorld()->GetNetDriver());
            Check(Driver && Driver->bIsPassthrough, TEXT("lan_host_uses_ip_with_eos_plugin_enabled"));
            Check(!Online->IsOnline() && !GetWorld()->GetGameState<AERGameState>()->bOnline, TEXT("lan_has_no_eos_dependency"));
            UE_LOG(LogTemp, Display, TEXT("ER_ONLINE_UNCONFIGURED_COMPLETE result=%s"), Failed ? TEXT("FAIL") : TEXT("PASS"));
            FPlatformMisc::RequestExitWithStatus(false, Failed ? 1 : 0);
        }
        return;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("ERSettingsWrite")) || FParse::Param(FCommandLine::Get(), TEXT("ERSettingsRead")))
    {
        if (GetWorld()->GetTimeSeconds() < 2 || Phase) return;
        Phase = 1;
        const bool Write = FParse::Param(FCommandLine::Get(), TEXT("ERSettingsWrite"));
        auto* Settings = GEngine->GetGameUserSettings();
        if (Write)
        {
            Check(Settings->GetFullscreenMode() == EWindowMode::WindowedFullscreen, TEXT("first_launch_fullscreen"));
            PC->ApplyDisplaySize(60);
            GI->SetSoundVolume(.67f); GI->SetMusicVolume(.27f); GI->SaveMusicVolume();
            GI->SetMouseSensitivity(.36f); GI->SetMouseAcceleration(.19f); GI->SaveMouseSensitivity();
            GI->MaxPlayers = 23; GI->SaveServerSettings();
            GI->LocalName = TEXT("Saved Recruit"); GI->PlayerTeam = 1; GI->SaveProfile();
        }
        else
        {
            Check(GI->MaxPlayers == 23, TEXT("server_capacity_survives_restart"));
            Check(Settings->GetFullscreenMode() == EWindowMode::Windowed, TEXT("saved_window_mode_restored"));
            const auto Display = PC->GetDisplayResolution();
            Check(Settings->GetScreenResolution() == FIntPoint(FMath::RoundToInt(Display.X * .6f), FMath::RoundToInt(Display.Y * .6f)), TEXT("saved_resolution_restored"));
            Check(FMath::IsNearlyEqual(GI->GetSoundVolume(), .67f) && FMath::IsNearlyEqual(GI->GetMusicVolume(), .27f), TEXT("sound_settings_survive_restart"));
            Check(FMath::IsNearlyEqual(GI->GetMouseSensitivity(), .36f) && FMath::IsNearlyEqual(GI->GetMouseAcceleration(), .19f), TEXT("input_settings_survive_restart"));
            Check(GI->LocalName == TEXT("Saved Recruit") && GI->PlayerTeam == 1, TEXT("profile_survives_restart"));
        }
        UE_LOG(LogTemp, Display, TEXT("ER_SETTINGS_COMPLETE %s result=%s ini=%s"), Write ? TEXT("write") : TEXT("read"), Failed ? TEXT("FAIL") : TEXT("PASS"), *GGameUserSettingsIni);
        FPlatformMisc::RequestExitWithStatus(false, Failed ? 1 : 0);
        return;
    }
    const double GalleryTime = GetWorld()->GetTimeSeconds();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERMenuGallery")))
    {
        if (GalleryTime < 3) return;
        if (GetNetMode() == NM_ListenServer)
        {
            if (Phase == 0) { PC->ERMenu(0); Phase = 1; Deadline = GalleryTime + 3; }
            else if (Phase == 1 && GalleryTime > Deadline)
            { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Menu/lobby.png"), true, false); Phase = 2; Deadline = GalleryTime + 2; }
            else if (Phase == 2 && GalleryTime > Deadline) { UE_LOG(LogTemp, Display, TEXT("ER_MENU_GALLERY_COMPLETE")); FPlatformMisc::RequestExit(false); }
            return;
        }
        static const int32 Pages[] = {0, 7, 1, 2, 4, 5, 6, 3};
        static const TCHAR* Names[] = {TEXT("home"),TEXT("credits"),TEXT("offline"),TEXT("server"),TEXT("setup"),TEXT("character"),TEXT("equipment"),TEXT("search")};
        const int32 Index = Phase / 2;
        if (Index >= UE_ARRAY_COUNT(Pages)) { GI->CancelLANSearch(); GI->StartLANServer(); return; }
        if (Phase % 2 == 0 && GalleryTime >= Deadline)
        {
            if (Pages[Index] == 2) GI->AlphaBots = GI->OmegaBots = 0;
            if (Pages[Index] == 3) GI->FindLANServer();
            PC->ERMenu(Pages[Index]); ++Phase; Deadline = GalleryTime + 3;
        }
        else if (Phase % 2 == 1 && GalleryTime >= Deadline)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / (FString(TEXT("Screenshots/Menu/")) + Names[Index] + TEXT(".png")), true, false);
            ++Phase; Deadline = GalleryTime + 1;
        }
        return;
    }
    const bool Host = FParse::Param(FCommandLine::Get(), TEXT("ERLobbyProbeHost"));
    const bool Timeout = FParse::Param(FCommandLine::Get(), TEXT("ERLobbyProbeTimeout"));
    int32 VotedMap = 2; FParse::Value(FCommandLine::Get(), TEXT("ERLobbyProbeMap="), VotedMap);
    int32 Client = 0; FParse::Value(FCommandLine::Get(), TEXT("ERLobbyProbeClient="), Client);
    const double Now = GetWorld()->GetTimeSeconds();
    auto* GS = GetWorld()->GetGameState<AERGameState>();
    if (Now < 1 || !GS) return;
    if (PC->IsMenuLevel() && GetNetMode() == NM_Standalone)
    {
        if (Phase == 0)
        {
            Phase = 1;
            GI->LocalName = Host ? TEXT("LobbyHost") : FString::Printf(TEXT("LobbyClient%d"), Client);
            if (Host) { if(FParse::Param(FCommandLine::Get(),TEXT("ERMapSizingLobbyTest")))GI->ServerRules.Set("Map.ExpectedTeamSize",3); GI->AlphaBots = 1; GI->OmegaBots = 0; GI->StartLANServer(); }
            else GI->FindLANServer();
        }
        if (Timeout && Phase == 1 && !GI->IsSearchingLAN())
        {
            Check(GI->LastError.Contains(TEXT("30 seconds")), TEXT("discovery_timeout_has_actionable_status"));
            Phase = 99;
            FPlatformMisc::RequestExitWithStatus(false, Failed ? 1 : 0);
        }
        return;
    }
    if (GS->bLobby)
    {
        if (!PC->GetPlayerState<AERPlayerState>()) return;
        if(FParse::Param(FCommandLine::Get(),TEXT("ERMapSizingLobbyTest")) && !GS->ServerRules.Ready)return;
        if (Phase == 0)
        {
            Check(!PC->GetPawn(), TEXT("lobby_has_no_gameplay_pawn"));
            PC->ServerSetName(Host ? TEXT("LobbyHost") : FString::Printf(TEXT("LobbyClient%d"), Client));
            PC->ServerLobbyTeam(Host ? 0 : 1);
            // Ordinary sessions choose a preset kit; per-item RPCs are ERLegacyArsenal fixtures only.
            PC->ServerSetLoadout(FName(TEXT("marksman")), FName(TEXT("defense")));
            if(FParse::Param(FCommandLine::Get(),TEXT("ERMapSizingLobbyTest")))
            {
                Check(GS->ServerRules.Get("Map.ExpectedTeamSize")==3,TEXT("creation_sizing_replicates_in_lobby"));
                Check(GS->ServerMaxPlayers==GI->MaxPlayers || !Host,TEXT("sizing_independent_of_capacity"));
                if(Host)PC->GetPlayerState<AERPlayerState>()->bLobbyReady=true;
                GI->ServerRules.Set("Map.ExpectedTeamSize",8);
                if(Host){Check(PC->GetPlayerState<AERPlayerState>()->bLobbyReady,TEXT("creation_only_setting_never_resets_ready"));PC->GetPlayerState<AERPlayerState>()->bLobbyReady=false;}
                Check(GS->ServerRules.Get("Map.ExpectedTeamSize")==3,TEXT("draft_changes_do_not_mutate_lobby_snapshot"));
            }
            PC->ServerLobbyVote(VotedMap);
            Phase = 1; Deadline = Now + 1;
        }
        if (Phase == 1 && Now > Deadline && Host && GS->PlayerArray.Num() == 3)
        {
            PC->ServerLobbyReady(true);
            Check(!GS->bLobbyStarting, TEXT("one_of_three_ready_does_not_start"));
            Phase = 2; Deadline = Now + 2;
        }
        if (Phase == 1 && Now > Deadline && Client == 1 && GS->PlayerArray.Num() == 3)
        {
            bool HostReady = false;
            for (auto Item : GS->PlayerArray) if (auto* PS = Cast<AERPlayerState>(Item); PS && PS->GetPlayerName() == TEXT("LobbyHost")) HostReady = PS->bLobbyReady;
            if (HostReady)
            {
                Phase = 2; Deadline = Now + 2;
            }
        }
        else if (Phase == 2 && Client == 1 && Now > Deadline)
        { PC->ServerLobbyReady(true); Phase = 3; }
        return;
    }
    if (!PC->IsMenuLevel() && Now > 4 && Phase < 90)
    {
        if (!PC->GetPawn() || GS->PlayerArray.Num() < 3) return;
        if (ERMaps::At(VotedMap).Procedural)
        {
            TActorIterator<AERBlockRoom> Room(GetWorld());
            if (!Room || !Room->IsMapReady() || PC->IsBlockMapLoading()) return;
            if (Host) for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
                if (auto* Remote = Cast<AERPlayerController>(It->Get()); Remote && !Remote->IsLocalController() && Remote->AcknowledgedBlockEpoch != Room->GetMapEpoch()) return;
        }
        const auto* PS = PC->GetPlayerState<AERPlayerState>();
        if (!PS) return;
        if(FParse::Param(FCommandLine::Get(),TEXT("ERMapSizingLobbyTest")))
        {
            Check(GS->ServerRules.Get("Map.ExpectedTeamSize")==3,TEXT("creation_sizing_survives_travel"));
            TActorIterator<AERProceduralMap> Map(GetWorld());
            Check(Map && Map->LayoutConfig.ExpectedTeamSize==3,TEXT("sizing_reaches_initial_client_geometry_descriptor"));
            Check(GS->ServerRules.Get("Map.ExpectedTeamSize")==3,TEXT("match_sizing_frozen"));
        }
        if (ERMaps::At(VotedMap).Procedural) Check(true, TEXT("generated_geometry_ready_on_all_peers"));
        Check(GetWorld()->GetMapName().Contains(FPaths::GetBaseFilename(ERMaps::At(VotedMap).Package)), TEXT("voted_map_loaded"));
        Check(PS->Team == (Host ? EERTeam::Alpha : EERTeam::Omega), TEXT("team_survives_travel"));
        if(FParse::Param(FCommandLine::Get(),TEXT("ERMapSizingLobbyTest")))
        {
            const auto* Pawn=Cast<AERCharacter>(PC->GetPawn());
            Check(Pawn && Pawn->Inventory.Num()==5,TEXT("current_closed_loadout_arrives"));
            if(Host)
            {
                auto* Mode=GetWorld()->GetAuthGameMode<AERGameMode>();
                Check(Mode && Mode->GetNumPlayers()==3 && Mode->GetNumTestBots()==1,TEXT("humans_and_test_bot_arrive"));
            }
            UE_LOG(LogTemp,Display,TEXT("ER_LOBBY_PROBE_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));
            Phase=90;Deadline=Now+(Host ? 8:2);return;
        }
        // The lobby choice (marksman/defense, not the rifleman/offense default) must
        // reach the match: player state, spawned preset, its inventory and skin.
        const FName Preset(TEXT("marksman")), Variant(TEXT("defense"));
        const auto* C = Cast<AERCharacter>(PC->GetPawn());
        const auto* Kit = GS->LoadoutCatalog.FindByPredicate([&](const FERResolvedLoadout& K)
            { return K.Preset == Preset && K.Variant == Variant && K.bOmega == (PS->Team == EERTeam::Omega); });
        if (!C || !Kit || C->ActivePreset.IsNone() || C->Inventory.IsEmpty()) return;
        bool bItems = C->Inventory.Num() == Kit->Items.Num();
        for (int32 Slot = 0; bItems && Slot < Kit->Items.Num(); ++Slot)
        {
            const auto* Item = C->Inventory[Slot].Get();
            if (!Item) return; // Inventory actors still replicating.
            bItems = Item->Data == Kit->Items[Slot].Data;
        }
        // Allow replication to settle before judging a mismatch.
        const bool bSettled = bItems && C->ActivePreset == Preset && C->ActiveVariant == Variant && PS->ActiveSkin == Kit->Skin;
        if (!bSettled && Deadline == 0) Deadline = Now + 15;
        if (!bSettled && Now < Deadline) return;
        Check(PS->LoadoutPreset == Preset && PS->LoadoutVariant == Variant, TEXT("loadout_choice_survives_travel"));
        Check(C->ActivePreset == Preset && C->ActiveVariant == Variant, TEXT("chosen_preset_spawned"));
        Check(bItems, TEXT("preset_inventory_spawned"));
        Check(PS->ActiveSkin == Kit->Skin, TEXT("preset_skin_applied"));
        if (Host)
        {
            auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>();
            Check(Mode && Mode->GetNumPlayers() == 3 && Mode->GetNumTestBots() == 1, TEXT("humans_and_test_bot_arrive"));
        }
        UE_LOG(LogTemp, Display, TEXT("ER_LOBBY_PROBE_COMPLETE result=%s"), Failed ? TEXT("FAIL") : TEXT("PASS"));
        Phase = 90; Deadline = Now + (Host ? 8 : 2);
    }
    if (Phase == 90 && Now > Deadline) FPlatformMisc::RequestExitWithStatus(false, Failed ? 1 : 0);
#endif
}
