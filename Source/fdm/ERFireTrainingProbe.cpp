#include "ERFireTrainingProbe.h"
#include "ERProceduralMap.h"
#include "ERRecon.h"
#include "ERPlayerController.h"
#include "ERCharacter.h"
#include "ERMovement.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

AERFireTrainingProbe::AERFireTrainingProbe(){PrimaryActorTick.bCanEverTick=true;bReplicates=true;bAlwaysRelevant=true;}
void AERFireTrainingProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERFireTrainingProbe,NetworkStage);DOREPLIFETIME(AERFireTrainingProbe,ExpectedDigest);}
void AERFireTrainingProbe::Check(bool Good,const TCHAR* Label)
{
    UE_LOG(LogTemp,Display,TEXT("ER_FIRE_PROBE_CHECK %s=%s"),Label,Good ? TEXT("PASS"):TEXT("FAIL"));Failed|=!Good;
}
void AERFireTrainingProbe::Tick(float Delta)
{
    Super::Tick(Delta);Elapsed+=Delta;
    TActorIterator<AERProceduralMap> MapIt(GetWorld());AERProceduralMap* Map=MapIt ? *MapIt:nullptr;
    auto* PC=Cast<AERPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(),0));
    if(!Map || !PC || !PC->GetPawn()){if(Elapsed>60){Check(false,TEXT("startup"));FPlatformMisc::RequestExitWithStatus(false,1);Phase=99;}return;}
    const auto& L=Map->GetLayout();
    if(!HasAuthority())
    {
        if(NetworkStage && SeenNetworkStage!=NetworkStage && Map->IsSnapshotCurrent() && Map->GetStateDigest()==ExpectedDigest)
        {
            bool Good=L.Bases.Num()==2 && L.WarehouseBaseCourts.Num()==2 && L.WarehouseCatwalks.Num()>=4;
            FCollisionQueryParams Q(SCENE_QUERY_STAT(ERFireNetwork),false,PC->GetPawn());FHitResult Hit;
            for(const auto& Route:L.CrossingRoutes)for(int32 I=1;I<Route.Num();++I)
            {
                Good&=!GetWorld()->SweepSingleByChannel(Hit,Route[I-1],Route[I],FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,92.5),Q);
                const int32 Count=FMath::CeilToInt(FVector::Distance(Route[I-1],Route[I])/100);
                for(int32 J=0;J<=Count;++J)
                {
                    const FVector P=FMath::Lerp(Route[I-1],Route[I],float(J)/Count);
                    Good&=GetWorld()->LineTraceSingleByChannel(Hit,P,P-FVector(0,0,120),ECC_WorldStatic,Q) && FMath::IsNearlyEqual(Hit.ImpactPoint.Z,P.Z-100,1);
                }
            }
            UE_LOG(LogTemp,Display,TEXT("ER_FIRE_CLIENT_%s phase=%d digest=%u epoch=%u"),Good ? TEXT("PASS"):TEXT("FAIL"),NetworkStage,ExpectedDigest,Map->GetMapEpoch());
            SeenNetworkStage=NetworkStage;
        }
        return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("ERFireTrainingNetwork")))
    {
        auto Stage=[&](int32 Next)
        {
            NetworkStage=Next;ExpectedDigest=Map->GetStateDigest();StageTime=Elapsed;ForceNetUpdate();
            UE_LOG(LogTemp,Display,TEXT("ER_FIRE_NETWORK_STATE phase=%d digest=%u epoch=%u"),Next,ExpectedDigest,Map->GetMapEpoch());
        };
        if(!Map->IsSnapshotCurrent())return;
        if(NetworkStage==0 && Elapsed>10)Stage(1);
        else if(NetworkStage==1 && Elapsed-StageTime>25)Stage(2);
        else if(NetworkStage==2 && Elapsed-StageTime>25)
        {int32 NextSeed=73;FParse::Value(FCommandLine::Get(),TEXT("ERFireTrainingNextSeed="),NextSeed);Map->LayoutConfig.Seed=NextSeed;Check(Map->Regenerate(),TEXT("network_regeneration"));Stage(3);}
        else if(NetworkStage==3 && Elapsed-StageTime>25)
        {UE_LOG(LogTemp,Display,TEXT("ER_FIRE_NETWORK_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);NetworkStage=4;}
        return;
    }
    if(Phase==90)
    {
        auto* C=Cast<AERCharacter>(PC->GetPawn());auto* Move=C ? Cast<UERMovement>(C->GetCharacterMovement()):nullptr;
        if(!Move){Check(false,TEXT("high_ladder_character"));FPlatformMisc::RequestExitWithStatus(false,1);Phase=99;return;}
        C->AddMovementInput(-HighLadder.GetRotation().GetForwardVector());
        SawLadder|=Move->IsOnLadder();SawLadderExit|=Move->IsMantling() && Move->CustomMovementMode==3;
        if(SawLadderExit && Move->IsMovingOnGround())
        {
            Check(SawLadder && C->GetActorLocation().Z>HighLadderRise+80,TEXT("actual_high_ladder_climb_and_deck_exit"));
            C->ConsumeMovementInputVector();Move->StopMovementImmediately();
            ProbedHeights.Add(HighLadderRise);
            if(const auto* Next=L.Ladders.FindByPredicate([&](const auto& D){return D.Frame.GetLocation().Z<1 && !ProbedHeights.Contains(D.DeckHeight);}))
            {
                HighLadder=Next->Frame;HighLadderRise=Next->DeckHeight;SawLadder=false;SawLadderExit=false;
                C->SetActorLocation(HighLadder.TransformPosition({55,0,C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3}),false,nullptr,ETeleportType::TeleportPhysics);
                Move->StopMovementImmediately();Move->SetMovementMode(MOVE_Walking);PC->SetControlRotation((-HighLadder.GetRotation().GetForwardVector()).Rotation());StageTime=Elapsed;return;
            }
            C->SetActorHiddenInGame(true);PC->ClientSetHUD(AHUD::StaticClass());Phase=1;StageTime=Elapsed;
        }
        else if(Elapsed-StageTime>15){Check(false,TEXT("actual_high_ladder_timeout"));FPlatformMisc::RequestExitWithStatus(false,1);Phase=99;}
        return;
    }
    if(Phase==0)
    {
        if(Elapsed<12)return;
#if WITH_EDITOR
        FAssetCompilingManager::Get().FinishAllCompilation();
#endif
        // Indoor recon: the frame stays under the eave and both cases found ground.
        TActorIterator<AERReconDirector> ReconIt(GetWorld());AERReconDirector* Recon=ReconIt ? *ReconIt:nullptr;
        Check(Recon && FMath::IsNearlyEqual(Recon->Frame.Indoor,L.IndoorCeiling,1.f),TEXT("recon_indoor_frame"));
        int32 Bases=0;for(TActorIterator<AERReconBase> It(GetWorld());It;++It)++Bases;
        Check(Bases==2,TEXT("recon_cases_placed_indoors"));
        int32 Below=0,Drones=0;
        for(TActorIterator<AERReconDrone> It(GetWorld());It;++It){++Drones;Below+=It->Flight.Center.Z<L.IndoorCeiling+200;}
        Check(Drones>0 && Below==Drones,TEXT("drones_orbit_under_the_roof"));
        // Use normal character movement to climb a real map ladder and step
        // through its notched high landing without hitting roof ribs or lights.
        const float Rise=L.WarehouseCatwalks[0].Max.Z;
        const auto* Desc=L.Ladders.FindByPredicate([&](const auto& D){return FMath::IsNearlyEqual(D.DeckHeight,Rise);});
        auto* C=Cast<AERCharacter>(PC->GetPawn());
        if(!Desc || !C){Check(false,TEXT("high_ladder_setup"));FPlatformMisc::RequestExitWithStatus(false,1);Phase=99;return;}
        HighLadder=Desc->Frame;HighLadderRise=Rise;
        C->SetActorLocation(HighLadder.TransformPosition({55,0,C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3}),false,nullptr,ETeleportType::TeleportPhysics);
        C->GetCharacterMovement()->StopMovementImmediately();C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        PC->SetControlRotation((-HighLadder.GetRotation().GetForwardVector()).Rotation());
        Phase=90;StageTime=Elapsed;return;
    }
    // -ERFireTrainingSeeds=a,b,c picks the gallery seeds (default 42,73,7919).
    static TArray<int32> Seeds=[]{TArray<int32> Out;FString List;
        if(FParse::Value(FCommandLine::Get(),TEXT("ERFireTrainingSeeds="),List,false))
        {TArray<FString> Parts;List.ParseIntoArray(Parts,TEXT(","));for(const auto& P:Parts)Out.Add(FCString::Atoi(*P));}
        if(Out.IsEmpty())Out={42,73,7919};return Out;}();
    static const TCHAR* Names[]={TEXT("Plan"),TEXT("Base"),TEXT("Overview"),TEXT("Catwalk"),TEXT("Roof"),TEXT("Bridge"),TEXT("SteelDetail"),TEXT("Distant")};
    constexpr int32 PerSeed=UE_ARRAY_COUNT(Names);
    if(Phase==1)
    {
        if(Shot>=PerSeed*Seeds.Num())
        {
            UE_LOG(LogTemp,Display,TEXT("ER_FIRE_PROBE_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));
            FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);Phase=99;return;
        }
        const int32 Wanted=Seeds[Shot/PerSeed];
        if(Seed!=Wanted)
        {
            Map->LayoutConfig.Seed=Wanted;Check(Map->Regenerate(),TEXT("gallery_seed"));Seed=Wanted;
            FString Features;for(const FName& F:L.FeatureNames)Features+=F.ToString()+TEXT(",");
            UE_LOG(LogTemp,Display,TEXT("ER_FIRE_GALLERY seed=%d theme=%s width=%.0f length=%.0f ceiling=%.0f catwalk_z=%.0f attempt=%d elevated=%.3f overhead=%.3f open=%.3f lights=%s features=%s digest=%u"),
                Seed,*L.ThemeName.ToString(),L.WarehouseWidth,L.WarehouseLength,L.IndoorCeiling,L.WarehouseCatwalks[0].Max.Z,L.LayoutAttempt,L.ElevatedRatio,L.OverheadRatio,L.OpenRegionRatio,TEXT("procedural"),*Features,Map->GetStateDigest());
            StageTime=Elapsed;
        }
        if(Elapsed-StageTime<2)return;
        const int32 Kind=Shot%PerSeed;const float HL=L.WarehouseLength*.5f,W=L.WarehouseWidth;
        const bool Plan=Kind==0;
        TInlineComponentArray<UPrimitiveComponent*> Parts(Map);
        for(auto* Part:Parts)if(Part->ComponentHasTag(TEXT("ERTemplateRoof")))Part->SetVisibility(!Plan);
        FVector Eye,Target;
        if(Plan){Eye={0,0,4000};Target=FVector::ZeroVector;}
        else if(Kind==1){const auto& B=L.Bases[0];Eye=B.Point({500,650,550});Target=B.Point({0,50,120});}
        else if(Kind==2){Eye={W*.5f-120,-HL+150,L.IndoorCeiling-60};Target={-W*.2f,HL*.2f,120};}
        else if(Kind==3 && !L.CrossingRoutes.IsEmpty()){const auto& R=L.CrossingRoutes[0];Eye=R[0]+FVector(0,-40,70);Target=R.Last()+FVector(0,0,40);}
        else if(Kind==4){const auto& B=L.Bases[0];Eye=B.Point({0,-100,470});Target=B.Point({0,2000,400});}
        else if(Kind==5){const auto& R=L.CrossingRoutes[2];Eye=R[0]+FVector(0,0,70);Target=R.Last()+FVector(0,0,50);}
        else if(Kind==6)
        {
            const auto* Ladder=L.Ladders.FindByPredicate([&](const auto& D){return FMath::IsNearlyEqual(D.DeckHeight,L.WarehouseCatwalks[0].Max.Z);});
            Eye=Ladder->Frame.TransformPosition({350,240,170});Target=Ladder->Frame.TransformPosition({0,0,300});
        }
        else if(Kind==7){Eye={-W*.5f+600,-HL+1400,172};Target={W*.25f,HL*.25f,350};}
        else{Eye={-W*.5f+150,0,L.IndoorCeiling-80};Target={W*.3f,-HL*.5f,100};}
        auto* Camera=GetWorld()->SpawnActor<ACameraActor>(Eye,Plan ? FRotator(-90,0,0):(Target-Eye).Rotation());
        Camera->GetCameraComponent()->SetFieldOfView(Kind==3 ? 80:90);
        if(Plan)
        {
            Camera->GetCameraComponent()->SetProjectionMode(ECameraProjectionMode::Orthographic);
            Camera->GetCameraComponent()->SetAspectRatio(16.f/9);
            Camera->GetCameraComponent()->OrthoWidth=FMath::Max(L.ArenaSize.Y,L.ArenaSize.X*16.f/9)*1.04f;
        }
        PC->SetViewTarget(Camera);Phase=2;StageTime=Elapsed;
    }
    else if(Phase==2 && Elapsed-StageTime>3)
    {
        if(FApp::CanEverRender())FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/FireTraining-%d-%s.png"),Seed,Names[Shot%PerSeed]),false,false);
        Phase=3;StageTime=Elapsed;
    }
    else if(Phase==3 && Elapsed-StageTime>1){++Shot;Phase=1;}
}
