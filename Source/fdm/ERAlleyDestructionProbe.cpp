#include "ERDustTemplateProbe.h"
#include "ERAlleyDestruction.h"
#include "ERProceduralMap.h"
#include "ERCharacter.h"
#include "ERPlayerController.h"
#include "ERWeapon.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "UnrealClient.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

void AERDustTemplateProbe::TickArchitecture(AERProceduralMap* M,AERCharacter* C,AERPlayerController* PC)
{
#if !UE_BUILD_SHIPPING
    auto* Move=C->GetCharacterMovement();const auto& L=M->GetLayout();
    auto Camera=[&](FVector At,FVector Target)
    {
        auto* View=GetWorld()->SpawnActor<ACameraActor>(At,(Target-At).Rotation());View->GetCameraComponent()->FieldOfView=65;PC->SetViewTarget(View);
    };
    auto FacadeCamera=[&]()
    {
        const auto At=ArchitectureOutside+FVector(0,0,50),Target=WallPoint+FVector(0,0,30);
        auto* View=GetWorld()->SpawnActor<ACameraActor>(At,(Target-At).Rotation());auto* Lens=View->GetCameraComponent();
        Lens->SetProjectionMode(ECameraProjectionMode::Orthographic);Lens->OrthoWidth=650;Lens->SetAutoCalculateOrthoPlanes(false);Lens->SetOrthoNearClipPlane(1);Lens->SetOrthoFarClipPlane(5000);PC->SetViewTarget(View);
    };
    auto RoofCamera=[&](){FVector Inward=-ArchitectureRoof;Inward.Z=0;Inward.Normalize();Camera(ArchitectureRoof+Inward*800+FVector(0,0,1000),ArchitectureRoof-FVector(0,0,60));};
    auto Capture=[&](const TCHAR* Name){if(FApp::CanEverRender())FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Testing")/Name,false,false);};
    auto Place=[&](FVector P){PC->SetViewTarget(C);C->TeleportTo(P,FRotator::ZeroRotator);Move->StopMovementImmediately();Move->SetMovementMode(MOVE_Walking);};
    auto Query=[&](){FCollisionQueryParams Q(SCENE_QUERY_STAT(ERArchitectureProbe),false);Q.AddIgnoredActor(C);return Q;};
    const auto* HE=LoadObject<UERWeaponData>(nullptr,TEXT("/Game/EclipseRecoil/Weapons/LowPoly/DA_Grenade_RGD5"));
    if(ArchitectureStage==0 && Elapsed>5)
    {
#if WITH_EDITOR
        FAssetCompilingManager::Get().FinishAllCompilation();
#endif
        ERAlleyDestruction::FBreach B;FERCellId R;
        const bool Found=ERAlleyDestruction::FindWall(M,B,EERBlockMaterial::Brick) && ERAlleyDestruction::FindRoof(M,ArchitectureRoof,R,EERBlockMaterial::Brick);
        Check(Found,TEXT("architecture_original_brick_facade_and_roof_resolve"));
        if(!Found){FPlatformMisc::RequestExitWithStatus(false,1);return;}
        ArchitectureOutside=B.Outside;ArchitectureInside=B.Inside;WallPoint=B.Hit.ImpactPoint;WallNormal=B.Hit.ImpactNormal;WallPanel=B.Cell.Panel;RoofPanel=R.Panel;
        WallFloor=M->GetCellWorld().GetPanels()[WallPanel].Transform.GetLocation().Z;
        PC->ClientSetHUD(AHUD::StaticClass());PC->SetIgnoreLookInput(true);Move->StopMovementImmediately();
        FacadeCamera();
        Publish(1,M);StageTime=Elapsed;ArchitectureStage=1;
    }
    else if(ArchitectureStage==1 && Elapsed-StageTime>6)
    {
        Capture(TEXT("MiddleEasternDestruction-Facade-Intact.png"));ArchitectureStage=2;StageTime=Elapsed;
    }
    else if(ArchitectureStage==2 && Elapsed-StageTime>.4)
    {
        FString InitialReceipt;
        if(FParse::Value(FCommandLine::Get(),TEXT("ERArchitectureInitialReceipt="),InitialReceipt) && !FPaths::FileExists(InitialReceipt))
        {if(Elapsed-StageTime<120)return;Check(false,TEXT("architecture_initial_peer_snapshot_receipt_timeout"));}
        Place(ArchitectureOutside);PC->SetControlRotation((WallPoint-C->Camera->GetComponentLocation()).Rotation());
        FHitResult Hit;FERCellId Id;const auto Eye=C->Camera->GetComponentLocation();
        const bool Aim=GetWorld()->LineTraceSingleByChannel(Hit,Eye,WallPoint-WallNormal*20,ECC_GameTraceChannel1,Query()) && M->ResolveCell(Hit,Id);
        Check(Aim,TEXT("architecture_real_firearm_aim_hits_original_surface"));
        if(Aim)
        {
            TMap<FERCellId,float> Prep;
            for(int32 Z=Id.Z-6;Z<=Id.Z+6;++Z)for(int32 Y=Id.Y-6;Y<=Id.Y+6;++Y)for(int32 X=Id.X-6;X<=Id.X+6;++X)
            {const FERCellId Nearby{Id.Panel,X,Y,Z};if(M->GetCellWorld().IsAlive(Nearby))Prep.Add(Nearby,M->GetCellWorld().GetHealth(Nearby)-1);}
            M->ApplyCellBatch(Prep);
            const int64 Before=M->GetCellWorld().GetRemovedCount();const uint32 Shots=C->Weapon->AuthorizedShotCount;
            C->Weapon->StartFire();C->Weapon->StopFire();
            Check(C->Weapon->AuthorizedShotCount==Shots+1 && M->GetCellWorld().GetRemovedCount()>Before,TEXT("architecture_real_weapon_opens_weakened_source_cell"));
        }
        if(HE){const auto Blast=AERBlockRoom::ApplyWorldExplosion(GetWorld(),WallPoint+WallNormal*25,HE->BlockExplosion,C);Check(Blast.Destroyed>0,TEXT("architecture_standard_HE_breaks_original_house_shell"));}
        ERAlleyDestruction::Cut(M,WallPanel,ERAlleyDestruction::WallBounds(ArchitectureOutside,ArchitectureInside,WallFloor));
        Check(!GetWorld()->SweepTestByChannel(ArchitectureOutside+FVector(0,0,30),ArchitectureInside+FVector(0,0,30),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,92.5),Query()),TEXT("architecture_new_facade_opening_has_capsule_clearance"));
        FacadeCamera();ArchitectureStage=3;StageTime=Elapsed;
    }
    else if(ArchitectureStage==3 && Elapsed-StageTime>3)
    {
        Capture(TEXT("MiddleEasternDestruction-Facade-Breached.png"));ArchitectureStage=4;StageTime=Elapsed;
    }
    else if(ArchitectureStage==4 && Elapsed-StageTime>.4)
    {Place(ArchitectureOutside);ArchitectureStage=5;StageTime=Elapsed;}
    else if(ArchitectureStage==5)
    {
        FVector Delta=ArchitectureInside-C->GetActorLocation();Delta.Z=0;
        if(Delta.Size()<30 && Move->IsMovingOnGround())
        {
            Check(true,TEXT("architecture_real_pawn_enters_new_facade_hole"));FERCellId RoofCell;
            Check(ERAlleyDestruction::FindRoof(M,ArchitectureRoof,RoofCell,EERBlockMaterial::Brick),TEXT("architecture_undamaged_brick_roof_selected_after_facade_blast"));RoofPanel=RoofCell.Panel;
            Place(ArchitectureRoof+FVector(0,0,98));ArchitectureStage=6;StageTime=Elapsed;
        }
        else if(Elapsed-StageTime>10){Check(false,TEXT("architecture_facade_pawn_timeout"));Place(ArchitectureRoof+FVector(0,0,98));ArchitectureStage=6;StageTime=Elapsed;}
        else C->AddMovementInput(Delta.GetSafeNormal());
    }
    else if(ArchitectureStage==6 && Elapsed-StageTime>1)
    {
        Check(Move->IsMovingOnGround() && FMath::Abs(C->GetActorLocation().Z-ArchitectureRoof.Z-94)<15,TEXT("architecture_real_pawn_stands_on_original_roof"));
        RoofCamera();ArchitectureStage=60;StageTime=Elapsed;
    }
    else if(ArchitectureStage==60 && Elapsed-StageTime>1.5)
    {Capture(TEXT("MiddleEasternDestruction-Roof-Intact.png"));ArchitectureStage=61;StageTime=Elapsed;}
    else if(ArchitectureStage==61 && Elapsed-StageTime>.4)
    {
        ERAlleyDestruction::Cut(M,RoofPanel,FBox(ArchitectureRoof-FVector(110,110,160),ArchitectureRoof+FVector(110,110,20)));
        ArchitectureStage=7;StageTime=Elapsed;
    }
    else if(ArchitectureStage==7 && Elapsed-StageTime>2)
    {
        Check(C->GetActorLocation().Z<ArchitectureRoof.Z-150,TEXT("architecture_real_pawn_falls_through_roof_cut"));Capture(TEXT("MiddleEasternDestruction-Roof-Breached.png"));
        ArchitectureStage=8;StageTime=Elapsed;
    }
    else if(ArchitectureStage==8 && Elapsed-StageTime>.5)
    {
        BeforeArchitectureDetach=M->GetCellWorld().GetDetachedCount();
        ERAlleyDestruction::SeverSupports(M,RoofPanel);
        ArchitectureStage=9;StageTime=Elapsed;
    }
    else if(ArchitectureStage==9 && ((!M->IsSupportPending() && Elapsed-StageTime>3) || Elapsed-StageTime>90))
    {
        Check(M->GetCellWorld().GetDetachedCount()>BeforeArchitectureDetach && !M->IsSupportPending(),TEXT("architecture_loss_of_support_collapses_upper_house"));
        Check(M->ValidateCells(),TEXT("architecture_damage_and_collapse_collision_mappings"));Capture(TEXT("MiddleEasternDestruction-House-Collapsed.png"));
        if(HE && L.UnderpassBreaches.Num()){const auto P=L.UnderpassBreaches[0].GetCenter();AERBlockRoom::ApplyWorldExplosion(GetWorld(),P+FVector(0,0,10),HE->BlockExplosion,C);}
        ArchitectureStage=10;StageTime=Elapsed;
    }
    else if(ArchitectureStage==10 && Elapsed-StageTime>3 && !M->IsSupportPending())
    {Publish(6,M);ArchitectureStage=11;StageTime=Elapsed;}
    else if(ArchitectureStage==11)
    {
        if(PlacedPhase!=6 && Elapsed-StageTime>3){PlaceRemote(false,L.TownCrossing+FVector(0,0,414));PlacedPhase=6;}
        FString Receipt;const bool Pending=FParse::Value(FCommandLine::Get(),TEXT("ERDustLateReceipt="),Receipt) && !FPaths::FileExists(Receipt);
        if(Elapsed-StageTime>30 && (!Pending || Elapsed-StageTime>120))
        {
            if(!Receipt.IsEmpty())Check(!Pending,TEXT("architecture_late_peer_received_destroyed_building"));
            M->LayoutConfig.Seed=7919;Check(M->Regenerate() && M->GetCellWorld().GetRemovedCount()==0,TEXT("architecture_seed_rebuild_restores_original_buildings"));
            Publish(7,M);ArchitectureStage=12;StageTime=Elapsed;
        }
    }
    else if(ArchitectureStage==12)
    {
        if(PlacedPhase!=7 && Elapsed-StageTime>3){PlaceRemote(true,M->GetLayout().TownCrossing+FVector(0,0,414));PlacedPhase=7;}
        if(Elapsed-StageTime>16){UE_LOG(LogTemp,Display,TEXT("ER_DUST_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);ArchitectureStage=13;}
    }
#endif
}
