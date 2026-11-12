#include "ERArsenalProbe.h"
#include "ERCharacter.h"
#include "ERPlayerController.h"
#include "ERMatch.h"
#include "ERWeapon.h"
#include "ERGrenadeExplosion.h"
#include "ERSmokeCloud.h"
#include "NiagaraComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "ERShotEffects.h"
#include "Engine/StaticMeshActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "GameFramework/HUD.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"

AERArsenalProbe::AERArsenalProbe() { bReplicates = true; bAlwaysRelevant = true; PrimaryActorTick.bCanEverTick = true; }
void AERArsenalProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AERArsenalProbe, Phase); }
void AERArsenalProbe::Check(bool Value, const TCHAR* Label)
{ Failed |= !Value; UE_LOG(LogTemp, Display, TEXT("ER_ARSENAL_%s %s"), Value ? TEXT("PASS") : TEXT("FAIL"), Label); }
void AERArsenalProbe::Advance(int32 Next, float Delay) { Phase=Next; Deadline=GetWorld()->GetTimeSeconds()+Delay; ForceNetUpdate(); }
void AERArsenalProbe::TickMuzzleGallery()
{
#if !UE_BUILD_SHIPPING
    auto* PC = GetWorld()->GetFirstPlayerController();
    auto* Character = PC ? Cast<AERCharacter>(PC->GetPawn()) : nullptr;
    if (!Character || !Character->Weapon || !Character->bNetworkReady ||
        GetWorld()->GetTimeSeconds() < FMath::Max(8., Deadline)) return;
    if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
    const auto Capture = [](const TCHAR* Name)
    {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Testing")/Name, false, false);
    };
    if (Phase == 0)
    {
        // Face a real room surface, close enough to receive the flash.
        const FVector Eye = Character->Camera->GetComponentLocation();
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(ERMuzzleGallery), false, Character);
        double Nearest = 800.;
        FRotator View = FRotator::ZeroRotator;
        for (int32 Yaw = 0; Yaw < 360; Yaw += 15)
        {
            const FRotator Candidate(0, Yaw, 0);
            FHitResult Hit;
            if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Candidate.Vector() * 800., ECC_Visibility, Query) &&
                Hit.Distance > 100.f && Hit.Distance < Nearest)
            {
                Nearest = Hit.Distance;
                View = Candidate;
            }
        }
        Check(Nearest < 800., TEXT("muzzle_gallery_found_room_surface"));
        if (Nearest < 800. && Nearest > 145.)
            Character->AddActorWorldOffset(View.Vector() * (Nearest - 145.), true);
        PC->SetControlRotation(View);
        PC->SetIgnoreLookInput(true);
        PC->SetIgnoreMoveInput(true);
        Advance(1, 1.f);
    }
    else if (Phase == 1)
    {
        Capture(TEXT("Rooms-Muzzle-Before.png"));
        Advance(2, 1.f);
    }
    else if (Phase == 2 || Phase == 3)
    {
        const int32 Ammo = Character->Weapon->MagazineAmmo;
        Character->SetFireInputHeld(true);
        Character->SetFireInputHeld(false);
        Check(Character->Weapon->MagazineAmmo == Ammo - 1 && Character->FirstPersonMuzzleFlash->IsVisible() &&
            Character->MuzzleLight->IsVisible() && Character->MuzzleLight->Intensity > 0,
            TEXT("muzzle_gallery_real_shot_starts_flash_and_light"));
        const FVector Scale = Character->FirstPersonMuzzleFlash->GetComponentScale();
        if (Phase == 2) FirstFlashScale = Scale;
        else Check(!Scale.Equals(FirstFlashScale, .0001), TEXT("muzzle_gallery_varies_between_shots"));
        Capture(Phase == 2 ? TEXT("Rooms-Muzzle-Shot-A.png") : TEXT("Rooms-Muzzle-Shot-B.png"));
        Advance(Phase + 1, 1.f);
    }
    else if (Phase == 4)
    {
        Check(!Character->FirstPersonMuzzleFlash->IsVisible() && !Character->MuzzleLight->IsVisible(),
            TEXT("muzzle_gallery_flash_and_light_expire"));
        Capture(TEXT("Rooms-Muzzle-After.png"));
        Advance(5, .5f);
    }
    else FPlatformMisc::RequestExitWithStatus(false, Failed ? 1 : 0);
#endif
}
void AERArsenalProbe::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("ERMuzzleGallery")))
    {
        TickMuzzleGallery();
        return;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("ERHitscanGallery")))
    {
        auto* PC = Cast<AERPlayerController>(GetWorld()->GetFirstPlayerController());
        auto* C = PC ? Cast<AERCharacter>(PC->GetPawn()) : nullptr;
        if (!C || !C->Weapon || !PC->IsLocalController()) return;
        const double GalleryNow = GetWorld()->GetTimeSeconds();
        if (FParse::Param(FCommandLine::Get(), TEXT("ERTracerMotion")) &&
            ((Phase == 2 && GalleryNow >= Deadline - .5) || Phase == 3))
            C->AddMovementInput(FVector::YAxisVector, .6f, true);
        if (GalleryNow < FMath::Max(5., Deadline)) return;
        if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
        if (Phase == 0)
        {
            PC->SetInputMode(FInputModeUIOnly());
            PC->SetIgnoreLookInput(true); PC->SetIgnoreMoveInput(true);
            C->Weapon->StopFire();
            const bool MotionGallery = FParse::Param(FCommandLine::Get(), TEXT("ERTracerMotion"));
            C->TeleportTo(FVector(-1000, MotionGallery ? -1700 : -1500,95), FRotator::ZeroRotator);
            C->GetCharacterMovement()->StopMovementImmediately();
            PC->SetControlRotation(FRotator::ZeroRotator);
            auto* Wall = GetWorld()->SpawnActor<AStaticMeshActor>(C->Camera->GetComponentLocation() + FVector(220,0,0), FRotator::ZeroRotator);
            Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
            Wall->SetActorScale3D(FVector(.2f, MotionGallery ? 6.f : 4.f,3.f));
            UE_LOG(LogTemp, Display, TEXT("ER_HITSCAN_GALLERY_WALL eye=%s wall=%s"), *C->Camera->GetComponentLocation().ToString(), *Wall->GetActorLocation().ToString());
            Advance(1, 1.f);
        }
        else if (Phase == 1)
        {
            const FVector Center = C->Camera->GetComponentLocation() + FVector(210,0,0);
            UE_LOG(LogTemp, Display, TEXT("ER_HITSCAN_GALLERY_SHOT eye=%s aim=%s"), *C->Camera->GetComponentLocation().ToString(), *PC->GetControlRotation().ToString());
            for (uint8 Variant = 0; Variant < 4; ++Variant)
                C->Weapon->MulticastImpact(Center + FVector(0, Variant % 2 ? 30 : -30, Variant / 2 ? -25 : 25), FVector(-1,0,0), 0, Variant);
            UERShotEffects::SpawnTracer(GetWorld(), C->TracerSystem, C->GetVisualMuzzle(true), Center);
            Advance(2, 8.f);
        }
        else if (Phase == 2)
        {
            // Isolate cosmetic cadence from any startup click used to focus the window.
            C->ShotFeedbackCount = 0;
            MotionAmmoBefore = C->Weapon->MagazineAmmo;
            C->Weapon->StartFire();
            if (!FParse::Param(FCommandLine::Get(), TEXT("ERTracerMotion"))) C->Weapon->StopFire();
            Check(C->Weapon->LastShotTraceCount == 1 && C->TracerSystem && C->Weapon->ImpactSystem && C->Weapon->ImpactDecalVariants.Num() == 4,
                TEXT("hitscan_gallery_assets_and_authorized_trace"));
            for (TActorIterator<AERSpentCasing> It(GetWorld()); It; ++It) GalleryCasing = *It;
            Check(GalleryCasing.IsValid() && GalleryCasing->Mesh->GetStaticMesh() && GalleryCasing->Mesh->IsSimulatingPhysics()
                && !GalleryCasing->GetIsReplicated() && GalleryCasing->Mesh->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore
                && GalleryCasing->Mesh->GetCollisionResponseToChannel(ECC_GameTraceChannel1) == ECR_Ignore,
                TEXT("casing_is_local_physics_ignores_players_and_shots"));
            if (GalleryCasing.IsValid()) CasingOrigin = GalleryCasing->GetActorLocation();
            Advance(3, .85f);
        }
        else if (Phase == 3)
        {
            C->Weapon->StopFire();
            if (FParse::Param(FCommandLine::Get(), TEXT("ERTracerMotion")))
                Check(C->GetVelocity().Size2D() > 100 && MotionAmmoBefore - C->Weapon->MagazineAmmo >= 7,
                    TEXT("moving_burst_keeps_weapon_cadence"));
            // Check physics before screenshot readback can stall the game thread.
            FHitResult Floor;
            const FVector Position = GalleryCasing.IsValid() ? GalleryCasing->GetActorLocation() : CasingOrigin;
            const bool OnFloor = GetWorld()->LineTraceSingleByObjectType(Floor, Position + FVector(0,0,5), Position - FVector(0,0,5),
                FCollisionObjectQueryParams(ECC_WorldStatic));
            UE_LOG(LogTemp, Display, TEXT("ER_CASING_FLOOR start=%s now=%s floor=%d"), *CasingOrigin.ToString(), *Position.ToString(), OnFloor);
            Check(GalleryCasing.IsValid() && Position.Z < CasingOrigin.Z - 50 && OnFloor,
                TEXT("casing_falls_and_lands_on_world_floor"));
            CasingOrigin = Position;
            // Hold this purely cosmetic tracer sample for screenshot readback.
            UERShotEffects::SpawnImpact(GetWorld(), C->Weapon->ImpactSystem, C->Weapon->LastShotTraceEnd, FVector(-1,0,0));
            AERAttachedTracer::Spawn(C, C->TracerSystem, C->Weapon->LastShotTraceEnd, true, .35f);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Testing/Hitscan-Effects.png"), true, false);
            Advance(4, 1.f);
        }
        else if (Phase == 4)
        {
            PC->SetControlRotation((CasingOrigin - C->Camera->GetComponentLocation()).Rotation());
            Advance(5, .1f);
        }
        else if (Phase == 5)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Testing/Casing-OnFloor.png"), true, false);
            Advance(6, 2.5f);
        }
        else
        {
            Check(!GalleryCasing.IsValid(), TEXT("casing_removed_after_three_seconds"));
            FPlatformMisc::RequestExitWithStatus(false, Failed ? 1 : 0);
        }
        return;
    }
    const bool SmokeGallery=FParse::Param(FCommandLine::Get(),TEXT("ERSmokeGallery"));
    if (FParse::Param(FCommandLine::Get(), TEXT("ERGrenadeGallery")) || SmokeGallery)
    {
        auto* PC=Cast<AERPlayerController>(GetWorld()->GetFirstPlayerController());
        auto* C=PC ? Cast<AERCharacter>(PC->GetPawn()) : nullptr;
        const bool NoShots=FParse::Param(FCommandLine::Get(),TEXT("ERGrenadeNoShots"));
        if (NoShots && Phase>0 && Phase<(SmokeGallery ? 6 : 5))
        {
            const double FrameMs=FApp::GetDeltaTime()*1000.;
            ++BurstFrames; BurstFrameTotal+=FrameMs; BurstFrameMax=FMath::Max(BurstFrameMax,FrameMs);
            if(FrameMs>33.34) ++BurstSlowFrames;
        }
        if (!C || !C->Weapon || !PC->IsLocalController() || GetWorld()->GetTimeSeconds()<FMath::Max(SmokeGallery ? 20. : 5.,Deadline)) return;
        if(SmokeGallery && Phase==0 && GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
        if (Phase==0)
        {
            PC->SetControlRotation(FRotator(-10,0,0));
            C->GetCharacterMovement()->StopMovementImmediately();
            const FVector Above = C->Camera->GetComponentLocation()+FVector(400,0,0);
            FHitResult Floor;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(ERGrenadeGallery),false,C);
            const bool Found = GetWorld()->LineTraceSingleByObjectType(Floor, Above, Above-FVector(0,0,1000),FCollisionObjectQueryParams(ECC_WorldStatic),Query);
            const FVector Position = Found ? Floor.ImpactPoint+Floor.ImpactNormal*4 : Above;
            auto* P=GetWorld()->SpawnActorDeferred<AERProjectile>(C->Weapon->ProjectileClass,FTransform(FRotator::ZeroRotator,Position),C->Weapon,C,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            P->Payload=SmokeGallery ? EERPayload::Smoke : EERPayload::HE; P->Damage=0; P->FuseDuration=.5f;
            P->ThrownMesh=C->Arsenal[SmokeGallery ? 9 : 8]->Mesh;
            P->Movement->InitialSpeed=0; P->Movement->MaxSpeed=0; P->Movement->Velocity=FVector::ZeroVector;
            UGameplayStatics::FinishSpawningActor(P,P->GetActorTransform());
            UE_LOG(LogTemp,Display,TEXT("ER_%s_GALLERY_START position=%s"),SmokeGallery ? TEXT("SMOKE") : TEXT("GRENADE"),*Position.ToString());
            Advance(1,SmokeGallery ? 1.f : .56f);
        }
        else
        {
            if (!NoShots) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/(SmokeGallery ? TEXT("Screenshots/Smoke") : TEXT("Screenshots/Grenade"))/FString::Printf(TEXT("Burst_%02d.png"),Phase),true,false);
            UE_LOG(LogTemp,Display,TEXT("ER_%s_GALLERY_FRAME %d"),SmokeGallery ? TEXT("SMOKE") : TEXT("GRENADE"),Phase);
            if(SmokeGallery && Phase==3)
                for(TActorIterator<AERSmokeCloud> It(GetWorld());It;++It)
                {
                    const FVector Center=It->GetActorLocation()+FVector(0,0,120);
                    C->MulticastShot(C->Camera->GetComponentLocation(),Center+(Center-C->Camera->GetComponentLocation()).GetSafeNormal()*1000,true,nullptr,{});
                }
            if (Phase==(SmokeGallery ? 6 : 5))
            {
                SetActorTickEnabled(false); UE_LOG(LogTemp,Display,TEXT("ER_%s_GALLERY_COMPLETE"),SmokeGallery ? TEXT("SMOKE") : TEXT("GRENADE"));
                if(NoShots && BurstFrames) UE_LOG(LogTemp,Display,TEXT("ER_%s_FRAME_SAMPLE frames=%d mean_ms=%.2f max_ms=%.2f frames_above_33ms=%d"),
                    SmokeGallery ? TEXT("SMOKE") : TEXT("GRENADE"),BurstFrames,BurstFrameTotal/BurstFrames,BurstFrameMax,BurstSlowFrames);
            }
            else Advance(Phase+1,SmokeGallery ? (Phase==1 ? 2.f : Phase==2 ? 3.f : Phase==3 ? .25f : Phase==4 ? 4.f : 2.5f)
                : (Phase==1 ? .29f : Phase==2 ? .5f : Phase==3 ? .65f : 1.f));
        }
        return;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("ERFPSPoseGallery")))
    {
        // Standalone first-person review: idle, scrubbed fire and reload frames, iron aim.
        auto* PC=Cast<AERPlayerController>(GetWorld()->GetFirstPlayerController());
        auto* C=PC ? Cast<AERCharacter>(PC->GetPawn()) : nullptr;
        if (!C || !C->Weapon || !PC->IsLocalController() || C->Inventory.Num()!=10) return;
        if (GetWorld()->GetTimeSeconds()<FMath::Max(8.,Deadline) || (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())) return;
        struct FPoseStep { int32 Kind; float Fraction; const TCHAR* Name; };
        static const FPoseStep Steps[]={{0,0,TEXT("Idle")},{1,.15f,TEXT("Fire15")},{1,.4f,TEXT("Fire40")},{2,.2f,TEXT("Reload20")},
            {2,.4f,TEXT("Reload40")},{2,.6f,TEXT("Reload60")},{2,.8f,TEXT("Reload80")},{3,0,TEXT("Aim")}};
        PC->SetControlRotation(FRotator::ZeroRotator);PC->SetIgnoreLookInput(true);PC->SetIgnoreMoveInput(true);
        if (PC->GetHUD()) PC->GetHUD()->bShowHUD=false;
        if (GalleryIndex>=10) { FPlatformMisc::RequestExitWithStatus(false,0); return; }
        if (C->Weapon!=C->Inventory[GalleryIndex]) { C->ServerSelectWeapon(GalleryIndex); Phase=0; Deadline=GetWorld()->GetTimeSeconds()+1.2; return; }
        const int32 Step=Phase/2;
        if (Step>=UE_ARRAY_COUNT(Steps)) { ++GalleryIndex; Phase=0; return; }
        const auto* Data=C->Weapon->Data.Get();
        const FPoseStep& Pose=Steps[Step];
        UAnimSequence* Arms=Pose.Kind==1 ? Data->FireAnimation.Get() : Pose.Kind==2 ? Data->ReloadAnimation.Get() : Data->IdleAnimation.Get();
        UAnimSequence* Gun=Pose.Kind==1 ? Data->WeaponFireAnimation.Get() : Pose.Kind==2 ? Data->WeaponReloadAnimation.Get() : Data->WeaponIdleAnimation.Get();
        const bool Aim=Pose.Kind==3;
        if ((Pose.Kind==1 || Pose.Kind==2) ? !Arms : (Aim && (C->Weapon->HasOpticalScope() || (GalleryIndex!=4 && GalleryIndex!=7)))) { Phase+=2; return; }
        if (Phase%2==0)
        {
            const bool Scrub=Pose.Kind==1 || Pose.Kind==2;
            if (Arms) { C->FirstPersonBody->PlayAnimation(Arms,!Scrub); if (Scrub) { C->FirstPersonBody->GetSingleNodeInstance()->SetPosition(Arms->GetPlayLength()*Pose.Fraction,false); C->FirstPersonBody->SetPlayRate(0.f); } else C->FirstPersonBody->SetPlayRate(1.f); }
            if (Gun) { C->FirstPersonRifle->PlayAnimation(Gun,!Scrub); if (Scrub) { C->FirstPersonRifle->GetSingleNodeInstance()->SetPosition(Gun->GetPlayLength()*Pose.Fraction,false); C->FirstPersonRifle->SetPlayRate(0.f); } else C->FirstPersonRifle->SetPlayRate(1.f); }
            C->Weapon->ScopeLevel=Aim ? 1 : 0;
            ++Phase; Deadline=GetWorld()->GetTimeSeconds()+(Aim ? .8 : .35); return;
        }
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/FPSPoses/S%02d_%d_%s.png"),GalleryIndex,Step,Pose.Name),false,false);
        UE_LOG(LogTemp,Display,TEXT("ER_FPS_POSE_GALLERY slot=%d pose=%s weapon=%s"),GalleryIndex,Pose.Name,*Data->DisplayName);
        ++Phase; Deadline=GetWorld()->GetTimeSeconds()+.3; return;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("ERArsenalGallery")))
    {
        auto* PC=Cast<AERPlayerController>(GetWorld()->GetFirstPlayerController());
        auto* C=PC ? Cast<AERCharacter>(PC->GetPawn()) : nullptr;
        if (!C || C->Arsenal.Num()!=11 || !C->Weapon || !PC->IsLocalController() || GetWorld()->GetTimeSeconds()<Deadline) return;
        if (GalleryIndex==0 && GetWorld()->GetTimeSeconds()<5.f) return;
        PC->SetControlRotation(FRotator(0,0,0));
        if (GalleryIndex==11) { C->Weapon->Data=C->Arsenal[0]; C->Weapon->MagazineAmmo=30; C->Weapon->ReserveAmmo=90; SetActorTickEnabled(false); return; }
        if (Phase==0) { C->Weapon->Data=C->Arsenal[GalleryIndex]; C->Weapon->MagazineAmmo=C->Weapon->Data->MagazineSize;
            C->Weapon->ReserveAmmo=C->Weapon->Data->InitialReserve; Advance(1,1.f); }
        else
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/Arsenal")/FString::Printf(TEXT("Weapon_%02d.png"),GalleryIndex),true,false);
            UE_LOG(LogTemp, Display, TEXT("ER_ARSENAL_GALLERY index=%d name=%s"), GalleryIndex, *C->Weapon->Data->DisplayName);
            ++GalleryIndex; Advance(0,.5f);
        }
        return;
    }
    if (!HasAuthority())
    {
        auto* PC=Cast<AERPlayerController>(GetWorld()->GetFirstPlayerController());
        auto* C=PC ? Cast<AERCharacter>(PC->GetPawn()) : nullptr;
        if (!C || !C->Weapon || !C->Weapon->Data) return;
        if (Phase!=ClientPhase)
        {
            ClientPhase=Phase;
            if (Phase==1) { C->ServerSelectWeapon(1); C->ServerStartFire(); }
            if (Phase==2) { C->ServerStopFire(); C->ServerReload(); }
            if (Phase==3) PC->ServerSetPrimary(4);
            if (Phase==7)
            {
                PC->ScrollWeapon(1);
                WheelBurstEnd=GetWorld()->GetTimeSeconds()+.4;
                NextWheelPulse=GetWorld()->GetTimeSeconds()+.05;
            }
            if (Phase==8) PC->ScrollWeapon(-1);
            UE_LOG(LogTemp, Display, TEXT("ER_ARSENAL_REMOTE phase=%d slots=%d name=%s mesh=%s ammo=%d"), Phase, C->Inventory.Num(),
                *C->Weapon->Data->DisplayName, *GetNameSafe(C->FirstPersonRifle->GetSkeletalMeshAsset()), C->Weapon->MagazineAmmo);
        }
        // Property phase updates and shot multicasts need not arrive together.
        // Observe the actual remote event until cloud cleanup, not only on the
        // first frame of phase 9. No event still fails the Python audit.
        if (Phase >= 9 && !bRemoteSmokeCutLogged)
            for (TActorIterator<AERSmokeCloud> It(GetWorld()); It; ++It)
                if (It->ShotInteractionCount > 0)
                {
                    UE_LOG(LogTemp, Display, TEXT("ER_SMOKE_REMOTE_CUT events=%u"), It->ShotInteractionCount);
                    bRemoteSmokeCutLogged = true;
                }
        if (Phase==7 && GetWorld()->GetTimeSeconds()<WheelBurstEnd && GetWorld()->GetTimeSeconds()>=NextWheelPulse)
        {
            PC->ScrollWeapon(1);
            NextWheelPulse=GetWorld()->GetTimeSeconds()+.05;
        }
        if (PC->FlashOpacity>.1f) UE_LOG(LogTemp, Display, TEXT("ER_ARSENAL_REMOTE_FLASH opacity=%.2f"),PC->FlashOpacity);
        return;
    }
    if (Phase==0)
    {
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
            if (auto* PC=Cast<AERPlayerController>(It->Get())) { if (PC->IsLocalController()) Host=PC; else Client=PC; }
        if (!Host || !Client || !Host->GetPawn() || !Client->GetPawn()) return;
        auto* H=CastChecked<AERCharacter>(Host->GetPawn()); auto* C=CastChecked<AERCharacter>(Client->GetPawn());
        Check(H->Inventory.Num()==5 && C->Inventory.Num()==5,TEXT("replicated_loadout_five_slots"));
        bool Valid=H->Arsenal.Num()==11;
        for (auto Data:H->Arsenal) Valid &= Data && Data->Mesh && Data->IdleAnimation && Data->FireAnimation;
        Check(Valid,TEXT("all_eleven_asset_profiles_present"));
        Host->SetControlRotation(FRotator(65,0,0)); Client->SetControlRotation(FRotator(65,180,0));
        H->Health->ProtectionEndServerTime=C->Health->ProtectionEndServerTime=GetWorld()->GetTimeSeconds()+100;
        Advance(1,1.f); return;
    }
    if (GetWorld()->GetTimeSeconds()<Deadline) return;
    auto* H=Cast<AERCharacter>(Host->GetPawn()); auto* C=Cast<AERCharacter>(Client->GetPawn());
    if (!H || !C || !H->Weapon || !C->Weapon) { Check(false,TEXT("valid_pawns")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto SpawnPayload=[this,H](EERPayload Kind, const FVector& Position)
    {
        auto* P=GetWorld()->SpawnActorDeferred<AERProjectile>(H->Weapon->ProjectileClass,FTransform(FRotator::ZeroRotator,Position),H->Weapon,H,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        // Keep this presentation/selection fixture alive after the now-full
        // close-range HE damage. Lethality is covered by the grenade probe.
        P->Payload=Kind; P->Damage=60; P->ExplosionRadius=600; P->FuseDuration=.2f;
        P->Movement->InitialSpeed=0; P->Movement->MaxSpeed=0; P->Movement->Velocity=FVector::ZeroVector;
        UGameplayStatics::FinishSpawningActor(P,P->GetActorTransform());
        return P;
    };
    switch (Phase)
    {
    case 1:
        Check(C->Weapon==C->Inventory[1] && C->Weapon->MagazineAmmo==6,TEXT("owner_switch_and_semi_auto_one_round_while_held"));
        Check(C->Inventory[0]->MagazineAmmo==30,TEXT("inactive_primary_keeps_ammo"));
        Advance(2,2.f); break;
    case 2:
        Check(C->Weapon->MagazineAmmo==7 && C->Weapon->ReserveAmmo==20,TEXT("sidearm_reload_consumes_reserve"));
        C->ServerSelectWeapon(99); Check(C->Weapon==C->Inventory[1],TEXT("invalid_slot_rejected"));
        Advance(3,.5f); break;
    case 3:
        Check(Client->GetPlayerState<AERPlayerState>()->PrimaryWeapon==4 && C->Inventory[0]->Data==C->Arsenal[0],TEXT("loadout_choice_waits_for_respawn"));
        C->Health->ProtectionEndServerTime=0; C->Health->ReceiveDamage(100,nullptr);
        Advance(4,3.4f); break;
    case 4:
        GetWorld()->GetAuthGameMode<AERGameMode>()->RequestRespawn(Client);
        Advance(5,.6f); break;
    case 5:
        Check(!C->Health->bDead && C->Weapon->Data==C->Arsenal[4] && C->Weapon->MagazineAmmo==100 && C->Inventory[1]->MagazineAmmo==7,
            TEXT("chosen_m249_and_sidearm_reset_on_respawn"));
        H->Weapon->Data=H->Arsenal[3]; H->Weapon->MagazineAmmo=7;
        Host->SetControlRotation(FRotator(80,0,0)); H->Weapon->StartFire(); H->Weapon->StopFire();
        { int32 Count=0; for(TActorIterator<AERProjectile> It(GetWorld());It;++It) if(It->GetOwner()==H->Weapon && It->Payload==EERPayload::Bullet) ++Count;
          Check(Count==0 && H->Weapon->LastShotTraceCount==6 && H->Weapon->MagazineAmmo==6,TEXT("shotgun_six_hitscan_traces_one_shell_no_bullet_actors")); }
        Advance(6,.6f); break;
    case 6:
        H->Weapon->Data=H->Arsenal[6]; H->Weapon->MagazineAmmo=1; H->Weapon->StartFire(); H->Weapon->StopFire();
        { bool Rocket=false; for(TActorIterator<AERProjectile> It(GetWorld());It;++It) if(It->Payload==EERPayload::Rocket)
            Rocket |= FMath::IsNearlyEqual(It->Movement->Velocity.Size(),2200.f,1.f);
          Check(Rocket && H->Weapon->MagazineAmmo==0,TEXT("rpg_authorized_slow_rocket_consumes_round")); }
        SpawnPayload(EERPayload::Smoke,H->GetActorLocation()+FVector(100,0,50));
        SpawnPayload(EERPayload::Stun,C->Camera->GetComponentLocation()+FVector(0,80,0));
        SpawnPayload(EERPayload::HE,H->Camera->GetComponentLocation()+FVector(0,80,0));
        Advance(7,1.f); break;
    case 7:
        Check(H->Health->Health<100,TEXT("he_applies_server_area_damage_to_thrower"));
        Check(C->Health->Health==100,TEXT("stun_does_not_deal_health_damage"));
        { bool Smoke=false; for(TActorIterator<AERSmokeCloud> It(GetWorld());It;++It)
            Smoke |= It->VolumeMaterial && It->FilledCellCount>0 && It->GetIsReplicated() && It->GetLifeSpan()>10 && !It->GetActorEnableCollision();
          Check(Smoke,TEXT("smoke_volume_persists_after_fuse")); }
        H->Weapon->Data=H->Arsenal[0]; H->Weapon->MagazineAmmo=30; H->Weapon->ForceNetUpdate();
        Check(C->Weapon==C->Inventory[1],TEXT("wheel_burst_changes_only_one_slot"));
        { bool Burst=false, HEProjectile=false;
          for(TActorIterator<AERGrenadeExplosion> It(GetWorld());It;++It)
              Burst |= It->BurstTemplate!=nullptr && It->Burst->Template!=nullptr && !It->GetIsReplicated()
                  && It->Burst->GetCollisionEnabled()==ECollisionEnabled::NoCollision;
          for(TActorIterator<AERProjectile> It(GetWorld());It;++It) HEProjectile |= It->Payload==EERPayload::HE;
          Check(Burst && !HEProjectile,TEXT("he_burst_outlives_projectile_without_collision_or_replication")); }
        Advance(8,.6f); break;
    case 8:
        Check(C->Weapon==C->Inventory[0],TEXT("wheel_reverse_after_quiet_changes_one_slot"));
        for(TActorIterator<AERSmokeCloud> It(GetWorld());It;++It)
        {
            const FVector Center=It->GetActorLocation()+FVector(0,0,It->SimSize.Z*.5f);
            Host->SetControlRotation((Center-H->Camera->GetComponentLocation()).Rotation());
            H->Weapon->StartFire(); H->Weapon->StopFire();
        }
        Advance(9,.15f); break;
    case 9:
        { bool Cut=false;for(TActorIterator<AERSmokeCloud> It(GetWorld());It;++It) Cut|=It->LastColliderCount>0;
          Check(Cut,TEXT("authorized_shot_submits_smoke_cut_positions")); }
        // Burst lifespan is 3.05s; leave a frame/timer margin after the fuse.
        Advance(10,1.7f); break;
    case 10:
        { int32 Bursts=0; for(TActorIterator<AERGrenadeExplosion> It(GetWorld());It;++It) ++Bursts;
          Check(Bursts==0,TEXT("he_animation_cleans_up_after_playback")); }
        Advance(11,10.5f); break;
    case 11:
        { int32 Clouds=0; for(TActorIterator<AERSmokeCloud> It(GetWorld());It;++It) ++Clouds;
          Check(Clouds==0,TEXT("smoke_cloud_cleans_up_after_duration")); }
        UE_LOG(LogTemp, Display, TEXT("ER_ARSENAL_COMPLETE result=%s"),Failed ? TEXT("FAIL") : TEXT("PASS"));
        FPlatformMisc::RequestExitWithStatus(false,Failed ? 1 : 0); Advance(12,1000); break;
    }
#endif
}
