#include "ERMapTemplateProbe.h"
#include "ERProceduralMap.h"
#include "ERCharacter.h"
#include "ERPlayerController.h"
#include "ERMapPlayability.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

void AERMapTemplateProbe::TickTown(AERProceduralMap* Map)
{
    const auto& L=Map->GetLayout();
    if(!HasAuthority())
    {
        if(Phase>0 && Phase<4 && SeenPhase!=Phase && Map->IsSnapshotCurrent() && Map->GetStateDigest()==ExpectedDigest)
        {
            const bool Good=Map->ValidateCells() && ERPlayability::InitialSpawnsOccludedInWorld(GetWorld(),Map->GetActorTransform(),L);
            UE_LOG(LogTemp,Display,TEXT("ER_TOWN_CLIENT_%s phase=%d digest=%u"),Good ? TEXT("PASS"):TEXT("FAIL"),Phase,ExpectedDigest);SeenPhase=Phase;
        }
        return;
    }
    auto* PC=Cast<AERPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(),0));
    auto* Pawn=PC ? Cast<AERCharacter>(PC->GetPawn()):nullptr;
    if(!Pawn || !Map->IsMapReady()){if(Elapsed>90)FPlatformMisc::RequestExitWithStatus(false,1);return;}
    auto State=[&](int32 Next){Publish(Next,Map);TownStage=Elapsed;};
    if(Phase==0 && Elapsed>5)
    {
        Check(Map->ValidateCells(),TEXT("town_initial_cells"));
        Check(ERPlayability::InitialSpawnsOccludedInWorld(GetWorld(),Map->GetActorTransform(),L),TEXT("town_initial_spawn_occlusion"));
        Check(L.SpawnExitRoutes.Num()==4,TEXT("town_has_four_primary_spawn_exits"));
        if(L.RoomAccessRoutes.IsEmpty() || L.GroundRoutes.IsEmpty())
        {Check(false,TEXT("town_walk_metadata_missing"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        State(1);return;
    }
    if(Phase==1)
    {
        // Walk the recorded shop and street flank, then every primary spawn
        // exit on both teams. Teleport only between these independent paths.
        const int32 PathCount=2+L.SpawnExitRoutes.Num();
        if(TownPath<PathCount)
        {
            const auto& Path=TownPath==0 ? L.RoomAccessRoutes[0]:TownPath==1 ? L.GroundRoutes[0]:L.SpawnExitRoutes[TownPath-2];
            const TCHAR* PathKind=TownPath==0 ? TEXT("shop"):TownPath==1 ? TEXT("street_flank"):TEXT("spawn_exit");
            if(Path.Num()<2)
            {Check(false,TEXT("town_walk_path_missing"));TownPath=PathCount;return;}
            if(!TownWalking)
            {
                double Length=0;for(int32 I=1;I<Path.Num();++I)Length+=FVector::Dist2D(Path[I-1],Path[I]);
                TownWalkBudget=FMath::Clamp(float(8+Length/180),12.f,90.f);TownWalkStarted=Elapsed;
                Pawn->TeleportTo(Path[0],FRotator::ZeroRotator,false,true);
                Pawn->GetCharacterMovement()->StopMovementImmediately();Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
                TownPoint=1;TownWalking=true;
                UE_LOG(LogTemp,Display,TEXT("ER_TOWN_WALK_BEGIN kind=%s path=%d points=%d budget=%.1f"),PathKind,TownPath,Path.Num(),TownWalkBudget);
            }
            if(TownPoint<Path.Num())
            {
                const FVector Delta=Path[TownPoint]-Pawn->GetActorLocation();
                if(Delta.Size2D()<12 && FMath::Abs(Delta.Z)<45)++TownPoint;
                else
                {
                    PC->SetControlRotation(FRotator(0,Delta.Rotation().Yaw,0));
                    Pawn->AddMovementInput(Delta.GetSafeNormal2D(),FMath::Clamp(float(Delta.Size2D()/200),.1f,1.f));
                }
            }
            if(TownPoint==Path.Num())
            {
                const bool Grounded=Pawn->GetCharacterMovement()->IsMovingOnGround();
                Check(Grounded,TownPath==0 ? TEXT("town_pawn_crosses_shop_and_curbs"):TownPath==1 ? TEXT("town_pawn_walks_recorded_street_flank"):TEXT("town_pawn_walks_spawn_exit"));
                UE_LOG(LogTemp,Display,TEXT("ER_TOWN_WALK_END kind=%s path=%d result=%s"),PathKind,TownPath,Grounded ? TEXT("PASS"):TEXT("FAIL"));
                if(Grounded && TownPath>=2)++TownSpawnExitsWalked;
                Pawn->GetCharacterMovement()->StopMovementImmediately();++TownPath;TownWalking=false;
            }
            const int32 Bucket=FMath::FloorToInt32((Elapsed-TownStage)/5);
            if(Bucket!=SeenPhase)
            {SeenPhase=Bucket;UE_LOG(LogTemp,Display,TEXT("ER_TOWN_WALK path=%d point=%d position=%s velocity=%s mode=%d input_ignored=%d"),TownPath,TownPoint,*Pawn->GetActorLocation().ToString(),*Pawn->GetVelocity().ToString(),int32(Pawn->GetCharacterMovement()->MovementMode),int32(PC->IsMoveInputIgnored()));}
            if(TownWalking && Elapsed-TownWalkStarted>TownWalkBudget)
            {Check(false,TEXT("town_walk_timeout"));Pawn->GetCharacterMovement()->StopMovementImmediately();TownPath=PathCount;TownWalking=false;}
            return;
        }
        if(Elapsed-TownStage<25)return;
        Check(TownSpawnExitsWalked==L.SpawnExitRoutes.Num(),TEXT("town_pawn_walked_all_primary_spawn_exits"));
        const auto& Cells=Map->GetCellWorld();TMap<FERCellId,float> Damage;
        const FERCellId Cell{0,0,0,0};if(Cells.IsAlive(Cell))Damage.Add(Cell,Cells.GetHealth(Cell)+1);
        const uint32 Before=Map->GetStateDigest();Map->ApplyCellBatch(Damage);
        Check(Map->GetStateDigest()!=Before,TEXT("town_authoritative_wall_damage"));State(20);return;
    }
    if(Phase==20 && Elapsed-TownStage>2 && !Map->IsSupportPending()){State(2);return;}
    if(Phase==2 && Elapsed-TownStage>(GetNetMode()==NM_Standalone ? 4:25))
    {
        const int32 PreviousSeed=Map->LayoutConfig.Seed;const uint32 PreviousEpoch=Map->GetMapEpoch();
        const uint32 PreviousLayout=Map->GetLayoutDigest();
        Map->LayoutConfig.Seed=PreviousSeed==73 ? 42:73;
        const bool Generated=Map->Regenerate();
        const bool EpochPublished=GetNetMode()==NM_Standalone || Map->GetMapEpoch()!=PreviousEpoch;
        Check(Generated && Map->IsMapReady() && Map->LayoutConfig.Seed!=PreviousSeed && Map->GetLayoutDigest()!=PreviousLayout &&
            Map->GetCellWorld().GetRemovedCount()==0 && Map->ValidateCells() && EpochPublished,TEXT("town_regenerate_different_seed_restores_cells"));
        Pawn->TeleportTo(Map->GetLayout().Spawn,FRotator::ZeroRotator,false,true);State(3);return;
    }
    if(Phase==3 && Elapsed-TownStage>12)
    {
        if(!FApp::CanEverRender())
        {UE_LOG(LogTemp,Display,TEXT("ER_TOWN_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);Phase=99;return;}
        Pawn->SetActorHiddenInGame(true);PC->ClientSetHUD(AHUD::StaticClass());Phase=4;
    }
    if(Phase==4)
    {
        FVector Eye,Target;
        if(TownCapture==0){Eye={L.ArenaSize.X*.55,-L.ArenaSize.Y*.6,4200};Target={0,0,100};}
        else if(TownCapture==1){Eye=L.RoomAccessRoutes[0][0]+FVector(0,-180,60);Target=L.RoomAccessRoutes[0][1]+FVector(0,0,60);}
        else {Eye=L.Routes[1][0]+FVector(0,0,65);Target=L.Routes[1].Last()+FVector(0,0,65);}
        auto* Camera=GetWorld()->SpawnActor<ACameraActor>(Eye,(Target-Eye).Rotation());Camera->GetCameraComponent()->SetFieldOfView(85);PC->SetViewTarget(Camera);Phase=5;TownStage=Elapsed;
    }
    else if(Phase==5 && Elapsed-TownStage>3)
    {
        static const TCHAR* Names[]={TEXT("Aerial"),TEXT("Shop"),TEXT("Street")};
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/Town-%s.png"),Names[TownCapture]),false,false);Phase=6;TownStage=Elapsed;
    }
    else if(Phase==6 && Elapsed-TownStage>1)
    {
        if(++TownCapture<3){Phase=4;return;}
        UE_LOG(LogTemp,Display,TEXT("ER_TOWN_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);Phase=99;
    }
}
