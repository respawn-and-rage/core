#include "ERNetworkProbe.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameViewportClient.h"
#include "Misc/Paths.h"
#include "ERCharacter.h"
#include "ERCorpse.h"
#include "EngineUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "ERMatch.h"
#include "ERPlayerController.h"
#include "ERMovement.h"
#include "InputKeyEventArgs.h"
#include "ERWeapon.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
void SetProbeKeys(AERPlayerController* PC, std::initializer_list<FKey> Keys, EInputEvent Event)
{
    for (const FKey& DefaultKey : Keys)
    {
        FInputKeyEventArgs Args;
        Args.Key = PC->ControlKey(DefaultKey);
        Args.Event = Event;
        Args.AmountDepressed = Event == IE_Pressed ? 1.f : 0.f;
        Args.EventTimestamp = FPlatformTime::Cycles64();
        PC->InputKey(Args);
    }
}

bool ProtectionMaterialsMatch(AERCharacter* Character, bool Protected)
{
    int32 Checked = 0;
    TInlineComponentArray<UMeshComponent*> Meshes(Character);
    for (auto* Mesh : Meshes)
    {
        if (Mesh != Character->GetMesh() && Mesh != Character->FirstPersonBody && !Mesh->ComponentHasTag(TEXT("ERSkinGear"))) continue;
        for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
        {
            auto* Current = Mesh->GetMaterial(Slot);
            auto* Original = Character->GetUnprotectedMaterial(Mesh,Slot);
            if (Protected && !Character->ProtectionSurface) return false;
            if (Current != (Protected ? Character->ProtectionSurface.Get() : Original)) return false;
            ++Checked;
        }
    }
    return Checked > 0;
}
}

AERNetworkProbe::AERNetworkProbe() { PrimaryActorTick.TickGroup = TG_PostUpdateWork; bReplicates = true; bAlwaysRelevant = true; PrimaryActorTick.bCanEverTick = true; SetNetUpdateFrequency(30); }
void AERNetworkProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AERNetworkProbe, Phase);
}
void AERNetworkProbe::Advance(int32 Next, float Delay) { Phase = Next; Deadline = GetWorld()->GetTimeSeconds()+Delay; ForceNetUpdate(); }
void AERNetworkProbe::Check(bool Condition, const TCHAR* Label)
{
    if (Condition) { UE_LOG(LogTemp, Display, TEXT("ER_TEST_PASS %s"), Label); }
    else { UE_LOG(LogTemp, Error, TEXT("ER_TEST_FAIL %s phase=%d"), Label,Phase); bFailed = true; }
}
void AERNetworkProbe::PlacePlayers(bool AcrossWall)
{
    if (!Host->GetPawn() || !Client->GetPawn()) return;
    const float Y = AcrossWall ? 0 : -1500;
    Host->GetPawn()->TeleportTo(FVector(-1000,Y,90), FRotator::ZeroRotator);
    Client->GetPawn()->TeleportTo(FVector(1000,Y,90), FRotator(0,180,0));
    Host->SetControlRotation(FRotator::ZeroRotator); Host->ClientSetRotation(FRotator::ZeroRotator);
    Client->SetControlRotation(FRotator(0,180,0)); Client->ClientSetRotation(FRotator(0,180,0));
}
void AERNetworkProbe::HostShot()
{
    if (auto* Pawn = Cast<AERCharacter>(Host->GetPawn())) { Pawn->ServerStartFire(); Pawn->ServerStopFire(); }
}
void AERNetworkProbe::ClientStep()
{
    auto* PC = Cast<AERPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(),0));
    // Lethal beat: shortly after the first death the owner is still in first person,
    // with the lethal punch applied, and the kill camera has not started yet.
    if (const auto* Victim = PC ? Cast<AERCharacter>(PC->GetPawn()) : nullptr; Victim && Victim->Health->bDead && !bClientLethalBeatChecked)
    {
        if (ClientDeathSeenAt < 0) ClientDeathSeenAt = GetWorld()->GetTimeSeconds();
        else if (GetWorld()->GetTimeSeconds() - ClientDeathSeenAt >= .1)
        {
            bClientLethalBeatChecked = true;
            const FRotator Punch = Victim->GetLethalPunch();
            UE_LOG(LogTemp, Display, TEXT("ER_MESSAGE_CLIENT_%s lethal_beat_before_killer_camera beat=%d camera=%d punch=%s"),
                Victim->IsInDeathBeat() && !Victim->IsDeathCameraActive() && !Punch.IsNearlyZero(.1f) ? TEXT("PASS") : TEXT("FAIL"),
                Victim->IsInDeathBeat(), Victim->IsDeathCameraActive(), *Punch.ToString());
        }
    }
    if (!PC || !PC->IsLocalController() || !PC->GetPawn()) return;
    auto* Pawn = Cast<AERCharacter>(PC->GetPawn()); if (!Pawn || !Pawn->Weapon) return;
    const auto SetSprintKeys = [PC](EInputEvent Event) { SetProbeKeys(PC, {EKeys::W, EKeys::E}, Event); };
    // Give the sprint saved moves time to reach the host, then release before the shooting fixture.
    if (Phase == 6 && !bClientReleasedRespawnSprint && !Pawn->Health->bDead
        && Cast<UERMovement>(Pawn->GetCharacterMovement())->IsSprinting())
    {
        const double Now = GetWorld()->GetTimeSeconds();
        if (ClientRespawnSprintAt < 0) ClientRespawnSprintAt = Now;
        if (Now - ClientRespawnSprintAt >= .15)
        {
            SetSprintKeys(IE_Released);
            bClientReleasedRespawnSprint = true;
        }
    }
    if (ClientPhase == Phase) return;
    // Probe phase and pawn properties travel on different actor channels.
    // The kill camera starts after the owner's lethal beat.
    if (Phase == 5 && (!Pawn->Health->bDead || !Pawn->bHasDeathFocus || !Pawn->IsDeathCameraActive() || PC->KillConfirmations.IsEmpty())) return;
    if (Phase == 7 && Pawn->Health->bDead) return;
    ClientPhase = Phase;
    UE_LOG(LogTemp, Display, TEXT("ER_CLIENT_PHASE %d hp=%.0f ammo=%d"),Phase,Pawn->Health->Health,Pawn->Weapon->MagazineAmmo);
    switch (Phase)
    {
        case 2:
        {
            bool bAppearanceMatches = true;
            bool bProtectionMatches = true;
            bool bAlphaSeen = false, bOmegaSeen = false;
            for (TActorIterator<AERCharacter> It(GetWorld()); It; ++It)
            {
                const auto* PS = It->GetPlayerState<AERPlayerState>();
                if (!PS || PS->Team == EERTeam::None) continue;
                bProtectionMatches &= ProtectionMaterialsMatch(*It, It->Health->IsProtected() && !It->Health->bDead);
                bAlphaSeen |= PS->Team == EERTeam::Alpha;
                bOmegaSeen |= PS->Team == EERTeam::Omega;
                const auto& Appearance = It->GetTeamAppearance();
                bAppearanceMatches &= Appearance.Matches(It->GetMesh());
            }
            UE_LOG(LogTemp, Display, TEXT("ER_PROTECTION_CLIENT_%s owner_and_proxy_materials"), bProtectionMatches ? TEXT("PASS") : TEXT("FAIL"));
            UE_LOG(LogTemp, Display, TEXT("ER_TEAM_CLIENT_%s phase=%d"), bAppearanceMatches && bAlphaSeen && bOmegaSeen ? TEXT("PASS") : TEXT("FAIL"), Phase);
            break;
        }
        case 5:
        {
            FMinimalViewInfo View;
            Pawn->CalcCamera(.016f, View);
            const FVector Focus = Pawn->DeathFocus ? Pawn->DeathFocus->GetActorLocation() : FVector::ZeroVector;
            const bool Camera = Pawn->bHasDeathFocus && Pawn->DeathFocus &&
                FVector::DotProduct(View.Rotation.Vector(), (Focus - View.Location).GetSafeNormal()) > .999f;
            FTimerHandle Capture;
            const TWeakObjectPtr<AERCharacter> DeadPawn = Pawn;
            GetWorldTimerManager().SetTimer(Capture, [DeadPawn]()
            {
                auto* Dead = DeadPawn.Get();
                if (!Dead || !Dead->DeathFocus) return;
                FMinimalViewInfo Framed;
                Dead->CalcCamera(0.f,Framed);
                const FVector Center = Dead->DeathFocus->GetActorLocation();
                const float HalfHeight = Dead->DeathFocus->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
                const FRotationMatrix Axes(Framed.Rotation);
                const FVector Forward = Axes.GetUnitAxis(EAxis::X), Up = Axes.GetUnitAxis(EAxis::Z);
                int32 Width = 0, Height = 0;
                if (const auto* Controller = Cast<APlayerController>(Dead->Controller)) Controller->GetViewportSize(Width,Height);
                const float Aspect = Width > 0 && Height > 0 ? float(Width)/Height : 16.f/9.f;
                const float TanVertical = FMath::Tan(FMath::DegreesToRadians(Framed.FOV*.5f))/Aspect;
                const auto ProjectY = [&](float Z)
                {
                    const FVector Offset = Center+FVector(0,0,Z)-Framed.Location;
                    return FVector::DotProduct(Offset,Up)/(FMath::Max(1.f,float(FVector::DotProduct(Offset,Forward)))*TanVertical);
                };
                const float Top = ProjectY(HalfHeight), Bottom = ProjectY(-HalfHeight);
                const float Coverage = (Top-Bottom)*.5f;
                const bool Figure = Top < .95f && Bottom > -.95f && Coverage > .5f && Coverage < .85f && Framed.FOV < 65.f;
                UE_LOG(LogTemp,Display,TEXT("ER_MESSAGE_CLIENT_%s killer_full_figure coverage=%.2f fov=%.1f distance=%.1f"),
                    Figure ? TEXT("PASS") : TEXT("FAIL"),Coverage,Framed.FOV,FVector::Distance(Center,Framed.Location));
                if (FParse::Param(FCommandLine::Get(),TEXT("ERMessagesGallery")))
                    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Testing/Messages-DeathCamera.png"),true,false);
            },1.2f,false);
            const bool Message = !PC->KillConfirmations.IsEmpty() && PC->KillConfirmations.Last().Title.Contains(TEXT("KILLED BY")) &&
                !PC->KillConfirmations.Last().Body.IsEmpty();
            UE_LOG(LogTemp, Display, TEXT("ER_MESSAGE_CLIENT_%s mandatory_death_and_killer_camera"), Camera && Message ? TEXT("PASS") : TEXT("FAIL"));
        }
            if (FParse::Param(FCommandLine::Get(),TEXT("ERSkinProbe"))) PC->ServerSetSkins(1,FParse::Param(FCommandLine::Get(),TEXT("ERPhaseBelicaProbe")) ? 7 : 2);
            SetSprintKeys(IE_Pressed);
            PC->ServerRequestRespawn(); break;
        case 6: PC->ServerRequestRespawn(); break;
        case 7:
            SetSprintKeys(IE_Released);
            UE_LOG(LogTemp, Display, TEXT("ER_MESSAGE_CLIENT_%s respawn_clears_killer_camera"), !Pawn->Health->bDead && !Pawn->bHasDeathFocus ? TEXT("PASS") : TEXT("FAIL"));
            PC->SetControlRotation(FRotator(0,180,0)); Pawn->ServerStartFire(); Pawn->ServerStopFire(); break;
        case 8: case 9: Pawn->ServerReload(); break;
        case 10: for(int32 I=0;I<8;++I) { Pawn->ServerStartFire(); Pawn->ServerStopFire(); } break;
        case 11: Pawn->ServerStartFire(); break;
        case 12: Pawn->ServerStopFire(); break;
    }
}
void AERNetworkProbe::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
#if !UE_BUILD_SHIPPING
    if (!HasAuthority()) { ClientStep(); return; }
    auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>();
    auto* GS = GetWorld()->GetGameState<AERGameState>();
    if (!Mode || !GS) return;
    if (Phase == 0)
    {
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            auto* PC = Cast<AERPlayerController>(It->Get()); if (!PC) continue;
            if (PC->IsLocalController()) Host = PC; else Client = PC;
        }
        if (!Host || !Client || !Host->GetPawn() || !Client->GetPawn() || !Mode->IsMatchInProgress()) return;
        if (FParse::Param(FCommandLine::Get(),TEXT("ERMessagesGallery")))
        {
            // Let both Metal windows finish startup before movement corrections and timed shots.
            if (Deadline == 0) Deadline = GetWorld()->GetTimeSeconds() + 10;
            if (GetWorld()->GetTimeSeconds() < Deadline) return;
        }
        PlacePlayers();
        CastChecked<AERCharacter>(Host->GetPawn())->Health->ProtectionEndServerTime = GetWorld()->GetTimeSeconds()+4;
        CastChecked<AERCharacter>(Client->GetPawn())->Health->ProtectionEndServerTime = GetWorld()->GetTimeSeconds()+4;
        Check(Host->GetPlayerState<AERPlayerState>()->Team != Client->GetPlayerState<AERPlayerState>()->Team,TEXT("T03_balanced_teams"));
        Advance(1,.6f); return;
    }
    auto* H = Cast<AERCharacter>(Host->GetPawn()); auto* C = Cast<AERCharacter>(Client->GetPawn());
    if (Phase == 6 && C && !C->Health->bDead && Cast<UERMovement>(C->GetCharacterMovement())->IsSprinting())
        bSawRespawnSprint = true;
    if (GetWorld()->GetTimeSeconds() < Deadline) return;
    if (!H || !C || !H->Weapon || !C->Weapon) { Check(false,TEXT("valid_pawns_and_weapons")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const bool bNewCharacters = FParse::Param(FCommandLine::Get(),TEXT("ERPhaseBelicaProbe"));
    const int32 HostSkin = bNewCharacters ? 8 : 3, ClientSkin = bNewCharacters ? 9 : 4, NextSkin = bNewCharacters ? 7 : 2;
    switch (Phase)
    {
        case 1:
            if (FParse::Param(FCommandLine::Get(),TEXT("ERSkinProbe")))
            {
                Check(Host->GetPlayerState<AERPlayerState>()->ActiveSkin == HostSkin && Client->GetPlayerState<AERPlayerState>()->ActiveSkin == ClientSkin, TEXT("Skin_preferences_replicated"));
                Host->ServerSetSkins(999,-1);
                Check(Host->GetPlayerState<AERPlayerState>()->AlphaSkin == HostSkin, TEXT("Skin_invalid_index_rejected"));
            }
            Check(H->GetTeamAppearance().Matches(H->GetMesh()) && C->GetTeamAppearance().Matches(C->GetMesh()), TEXT("Team_human_mesh_animation_hidden_equipment"));
            Check(ProtectionMaterialsMatch(H,true) && ProtectionMaterialsMatch(C,true),TEXT("Protection_opaque_gray_materials_on_spawn"));
            HostShot(); Advance(2,.5f); break;
        case 2:
            Check(C->Health->Health==100 && H->Weapon->MagazineAmmo==29 && !H->Health->IsProtected(),TEXT("T14_protection_shot_cancels"));
            Check(ProtectionMaterialsMatch(H,false) && ProtectionMaterialsMatch(C,true),TEXT("Protection_original_materials_after_attack"));
            C->Health->ProtectionEndServerTime=0; HostShot();
            Check(C->Health->Health==64, TEXT("hitscan_damage_applied_before_shot_returns"));
            { bool Bullet=false; for (TActorIterator<AERProjectile> It(GetWorld()); It; ++It) Bullet |= It->Payload==EERPayload::Bullet;
              Check(!Bullet, TEXT("hitscan_no_physical_bullet_actors")); }
            Advance(3,.5f); break;
        case 3: Check(C->Health->Health==64,TEXT("T04_host_first_hitscan_64")); HostShot(); Advance(4,.5f); break;
        case 4: Check(C->Health->Health==28,TEXT("T04_host_second_hitscan_28")); HostShot();
            if (FParse::Param(FCommandLine::Get(),TEXT("ERMessagesGallery")))
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Testing/Messages-KillHUD.png"),true,false);
            Advance(5,3.4f); break;
        case 5:
            if (FParse::Param(FCommandLine::Get(),TEXT("ERSkinProbe")))
                Check(Client->GetPlayerState<AERPlayerState>()->OmegaSkin == NextSkin && Client->GetPlayerState<AERPlayerState>()->ActiveSkin == ClientSkin,TEXT("Skin_pending_until_respawn"));
            for (TActorIterator<AERCorpse> It(GetWorld()); It; ++It) FirstCorpse = *It;
            Check(FirstCorpse.IsValid() && FirstCorpse->Body->IsSimulatingPhysics(), TEXT("Death_corpse_ragdoll_created"));
            Check(FirstCorpse.IsValid() && FirstCorpse->GetTeamAppearance().Matches(FirstCorpse->Body, true), TEXT("Team_omega_corpse_matches_live_character"));
            Check(FirstCorpse.IsValid() && FirstCorpse->HasValidRagdollPose(), TEXT("Death_omega_limbs_and_hidden_accessories_during_physics"));
            Check(GetWorld()->GetSubsystem<UERGameplayMessages>()->CatalogSize() == 72, TEXT("Messages_complete_catalog_loaded"));
            Check(!Host->KillConfirmations.IsEmpty() && Host->KillConfirmations.Last().Title.Contains(TEXT("KILLED ·")) &&
                !Host->KillConfirmations.Last().Body.IsEmpty(), TEXT("Messages_killer_always_receives_confirmation"));
            Check(C->bHasDeathFocus && C->DeathFocus == H, TEXT("Messages_death_focus_is_actual_killer"));
            Check(C->Health->bDead && C->Health->Health==0 && Host->GetPlayerState<AERPlayerState>()->Kills==1,TEXT("T04_death_score_T12_no_auto_respawn"));
            Check(Client->bAwaitingRespawn && Client->GetPlayerState<AERPlayerState>()->Deaths==1,TEXT("T12_early_request_rejected"));
            Advance(6,1.f); break;
        case 6:
            if (FParse::Param(FCommandLine::Get(),TEXT("ERSkinProbe")))
            {
                Check(FirstCorpse.IsValid() && FirstCorpse->SkinIndex == ClientSkin,TEXT("Skin_corpse_preserves_old_selection"));
                Check(Client->GetPlayerState<AERPlayerState>()->ActiveSkin == NextSkin,TEXT("Skin_new_selection_on_respawn"));
            }
            Check(FirstCorpse.IsValid(), TEXT("Death_corpse_survives_respawn"));
            Check(C->GetTeamAppearance().Matches(C->GetMesh()), TEXT("Team_appearance_survives_respawn"));
            Check(FirstCorpse.IsValid() && FirstCorpse->Body->GetSocketLocation(TEXT("pelvis")).Z < FirstCorpse->GetActorLocation().Z - 35.f,
                TEXT("Death_corpse_falls_to_ground"));
            Check(!C->Health->bDead && C->Health->Health==100 && C->Weapon->MagazineAmmo==30 && C->Health->IsProtected(),TEXT("T12_requested_respawn_T13_no_shot"));
            Check(bSawRespawnSprint, TEXT("Sprint_keys_held_before_respawn_start_sprint"));
            Check(ProtectionMaterialsMatch(C,true),TEXT("Protection_opaque_gray_after_respawn"));
            PlacePlayers(); H->Health->ProtectionEndServerTime=0; Advance(7,1.f); break;
        case 7:
            Check(H->Health->Health==64 && C->Weapon->MagazineAmmo==29,TEXT("T04_client_rpc_hitscan_to_host"));
            C->Weapon->MagazineAmmo=20; C->Weapon->ReserveAmmo=90; Advance(8,2.1f); break;
        case 8:
            Check(C->Weapon->MagazineAmmo==30 && C->Weapon->ReserveAmmo==80 && !C->Weapon->bReloading,TEXT("T08_partial_reload"));
            C->Weapon->MagazineAmmo=20; C->Weapon->ReserveAmmo=5; Advance(9,2.1f); break;
        case 9:
            Check(C->Weapon->MagazineAmmo==25 && C->Weapon->ReserveAmmo==0,TEXT("T09_low_reserve"));
            H->Health->ProtectionEndServerTime=GetWorld()->GetTimeSeconds()+20;
            C->Weapon->MagazineAmmo=30;
            ClientFeedbackBefore = C->ShotFeedbackCount;
            ClientAimBefore = Client->GetControlRotation();
            Advance(10,1.f); break;
        case 10:
            Check(C->Weapon->MagazineAmmo==29,TEXT("T06_rapid_rpc_presses_one_shot"));
            if (FParse::Param(FCommandLine::Get(), TEXT("ERShotAudit")))
                Check(C->ShotFeedbackCount == ClientFeedbackBefore + 1, TEXT("Shot_rapid_requests_one_feedback"));
            C->Weapon->MagazineAmmo=30; EmptyClipTimeout=GetWorld()->GetTimeSeconds()+8; Advance(11,4.2f); break;
        case 11:
            // Timers fire on game frames. A low headless tick rate can leave
            // the last round pending at 4.2s; wait for the observed empty clip,
            // with a hard timeout. Exact ammo/feedback and cadence checks stay.
            if (C->Weapon->MagazineAmmo > 0 && GetWorld()->GetTimeSeconds() < EmptyClipTimeout)
            { Deadline = GetWorld()->GetTimeSeconds()+.05; return; }
            UE_LOG(LogTemp,Display,TEXT("ER_TEST_CLIP ammo=%d host_hp=%.0f"),C->Weapon->MagazineAmmo,H->Health->Health);
            Check(C->Weapon->MagazineAmmo==0 && H->Health->Health==64,TEXT("T07_empty_clip_30_shots"));
            if (FParse::Param(FCommandLine::Get(), TEXT("ERShotAudit")))
            {
                Check(C->ShotFeedbackCount == ClientFeedbackBefore + 31, TEXT("Shot_30_rounds_30_feedback_events"));
                Check(Client->GetControlRotation().Equals(ClientAimBefore, .01f), TEXT("Shot_cosmetic_recoil_preserves_server_aim"));
                C->ServerStartFire(); C->ServerStopFire();
            }
            PlacePlayers(true); C->Health->Health=100; C->Health->ProtectionEndServerTime=0; Advance(12,.7f); break;
        case 12: HostShot(); Advance(13,.5f); break;
        case 13:
            if (FParse::Param(FCommandLine::Get(), TEXT("ERShotAudit")))
                Check(C->ShotFeedbackCount == ClientFeedbackBefore + 31, TEXT("Shot_empty_clip_no_feedback"));
            Check(C->Health->Health==100,TEXT("T05_central_wall_blocks_hitscan"));
            Client->GetPlayerState<AERPlayerState>()->Team=Host->GetPlayerState<AERPlayerState>()->Team;
            PlacePlayers(); Advance(130,.7f); break;
        case 130:
            // Let the autonomous client's correction settle before the friendly burst.
            H->ServerStartFire(); Advance(14,.7f); break;
        case 14:
            H->ServerStopFire();
            Check(C->Health->bDead && Host->GetPlayerState<AERPlayerState>()->Kills==1 && Host->GetPlayerState<AERPlayerState>()->TeamKills==1 &&
                (Host->GetPlayerState<AERPlayerState>()->Team==EERTeam::Alpha ? GS->AlphaScore : GS->OmegaScore)==0 &&
                (Host->GetPlayerState<AERPlayerState>()->Team==EERTeam::Alpha ? GS->OmegaScore : GS->AlphaScore)==0,TEXT("T11_friendly_fire_no_enemy_score_team_penalty"));
            Check(!Host->KillConfirmations.IsEmpty() && Host->KillConfirmations.Last().Title.Contains(TEXT("TEAMKILL ·")), TEXT("Messages_teamkill_always_confirmed"));
            Client->GetPlayerState<AERPlayerState>()->Team=Host->GetPlayerState<AERPlayerState>()->Team==EERTeam::Alpha ? EERTeam::Omega : EERTeam::Alpha;
            GS->AlphaScore=GS->OmegaScore=0;
            GS->MatchEndServerTime=GetWorld()->GetTimeSeconds()+.1f; Advance(15,.6f); break;
        case 15:
            Check(GS->GetMatchState()==MatchState::WaitingPostMatch && GS->Winner==EERTeam::None,TEXT("T16_time_limit_draw")); Advance(16,10.8f); break;
        case 16:
            Check(GS->IsMatchInProgress() && GS->AlphaScore==0 && GS->OmegaScore==0 && !C->Health->bDead && H->Health->Health==100,TEXT("T16_round_restarts"));
            H->Health->ProtectionEndServerTime=0; H->Weapon->MagazineAmmo=20;
            H->ServerReload(); H->Health->ReceiveDamage(100,nullptr); H->Health->ReceiveDamage(100,nullptr);
            SetProbeKeys(Host, {EKeys::RightShift, EKeys::LeftControl}, IE_Pressed);
            Advance(17,3.4f); break;
        case 17:
            Check(H->Health->bDead && !H->Weapon->bReloading && H->Weapon->MagazineAmmo==20 && Host->GetPlayerState<AERPlayerState>()->Deaths==1,TEXT("T10_reload_cancel_T15_single_death"));
            Mode->RequestRespawn(Host); Advance(18,1.7f); break;
        case 18:
            for (TActorIterator<AERCorpse> It(GetWorld()); It; ++It)
                Check(It->HasValidRagdollPose(), TEXT("Death_alpha_limbs_and_hidden_accessories_after_settle"));
            Check(!H->Health->bDead && H->Weapon->MagazineAmmo==30 && H->Weapon->ReserveAmmo==90,TEXT("T10_new_life_ammo_unchanged"));
            Check(Cast<UERMovement>(H->GetCharacterMovement())->bWalkHeld, TEXT("Walk_key_held_before_respawn"));
            Check(H->bIsCrouched, TEXT("Crouch_key_held_before_respawn"));
            SetProbeKeys(Host, {EKeys::RightShift, EKeys::LeftControl}, IE_Released);
            GS->AlphaScore=50; Advance(19,.6f); break;
        case 19:
            Check(GS->GetMatchState()==MatchState::WaitingPostMatch && GS->Winner==EERTeam::Alpha,TEXT("T16_score_limit_winner"));
            UE_LOG(LogTemp,Display,TEXT("ER_NETWORK_PROBE_COMPLETE result=%s"),bFailed ? TEXT("FAIL") : TEXT("PASS"));
            FPlatformMisc::RequestExitWithStatus(false,bFailed ? 1 : 0); Advance(20,1000); break;
    }
#endif
}
