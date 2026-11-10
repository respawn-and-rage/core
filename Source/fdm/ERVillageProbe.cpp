#include "ERVillageProbe.h"
#include "ERProceduralMap.h"
#include "ERCharacter.h"
#include "ERPlayerController.h"
#include "ERMovement.h"
#include "Components/CapsuleComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UnrealClient.h"
#include "Misc/App.h"
#include "Misc/Paths.h"

AERVillageProbe::AERVillageProbe(){bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;}
void AERVillageProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERVillageProbe,Phase);DOREPLIFETIME(AERVillageProbe,ExpectedDigest);}
void AERVillageProbe::Check(bool Good,const TCHAR* Label)
{Failed|=!Good;UE_LOG(LogTemp,Display,TEXT("ER_VILLAGE_CHECK %s=%s"),Label,Good ? TEXT("PASS"):TEXT("FAIL"));}
void AERVillageProbe::Publish(int32 Next,AERProceduralMap* Map)
{Phase=Next;Stage=Elapsed;ExpectedDigest=Map->GetStateDigest();ForceNetUpdate();UE_LOG(LogTemp,Display,TEXT("ER_VILLAGE_STATE phase=%d digest=%u epoch=%u"),Phase,ExpectedDigest,Map->GetMapEpoch());}
void AERVillageProbe::Tick(float Delta)
{
    Super::Tick(Delta);Elapsed+=Delta;
    TActorIterator<AERProceduralMap> It(GetWorld());auto* Map=It ? *It:nullptr;if(!Map)return;
    const auto& L=Map->GetLayout();
    if(!HasAuthority())
    {
        if(Phase>0 && Phase<4 && Seen!=Phase && Map->IsSnapshotCurrent() && Map->GetStateDigest()==ExpectedDigest)
        {
            FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERVillagePeer),false);
            const float HX=L.ArenaSize.X*.5f;
            bool Good=Map->ValidateCells() && !L.ThemeName.IsNone() && GetWorld()->SweepSingleByChannel(Hit,{HX-80,0,650},{HX+80,0,650},FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,92.5),Q);
            UE_LOG(LogTemp,Display,TEXT("ER_VILLAGE_CLIENT_%s phase=%d digest=%u"),Good ? TEXT("PASS"):TEXT("FAIL"),Phase,ExpectedDigest);Seen=Phase;
        }
        return;
    }
    auto* PC=Cast<AERPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(),0));auto* Pawn=PC ? Cast<AERCharacter>(PC->GetPawn()):nullptr;
    if(!Pawn || L.StairRoutes.IsEmpty()){if(Elapsed>60)FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(Phase==0 && Elapsed>6)
    {
        Check(Map->ValidateCells(),TEXT("initial_cells"));
        Check(Map->GetCellWorld().GetDetachedCount()==0,TEXT("intact_structures_supported"));
        if(GetNetMode()==NM_Standalone && FApp::CanEverRender() && FParse::Param(FCommandLine::Get(),TEXT("ERVillageCaptureOnly")))
        {Pawn->SetActorHiddenInGame(true);PC->ClientSetHUD(AHUD::StaticClass());Phase=4;Stage=Elapsed;return;}
        Publish(1,Map);return;
    }
    if(Phase==1)
    {
        static const TCHAR* Labels[]={TEXT("main_platform_stairs"),TEXT("spawn_gallery_stairs"),TEXT("corner_lookout_stairs"),TEXT("hatched_ground_corridor"),TEXT("side_wall_bypass"),TEXT("upper_storey_ladder")};
        bool Finished=false;
        if(Flight<5)
        {
            const auto& Path=Flight<3 ? L.StairRoutes[L.VillageAccessFlights[Flight]]:Flight==3 ? L.VillagePassages[0]:L.VillageSideBypasses[0];
            if(!Walking)
            {
                Pawn->TeleportTo(Path[0],FRotator(0,90,0),false,true);Pawn->GetCharacterMovement()->StopMovementImmediately();Walking=true;Point=1;FlightTime=Elapsed;
            }
            if(Point<Path.Num())
            {
                const FVector D=Path[Point]-Pawn->GetActorLocation();
                if(D.Size2D()<(Flight>=3 ? 8:26) && FMath::Abs(D.Z)<35)++Point;
                else
                {
                    // Face the walk like a player: the ready weapon reserves space ahead.
                    PC->SetControlRotation(FRotator(0,D.Rotation().Yaw,0));
                    Pawn->AddMovementInput(D.GetSafeNormal2D(),1);
                }
            }
            Finished=Point==Path.Num();
            if(Finished && (Flight<4 || !L.VillageUpperLadders.IsEmpty()))
            {
                Check(FMath::Abs(Pawn->GetActorLocation().Z-Path.Last().Z)<35,Labels[Flight]);
                ++Flight;Walking=false;return;
            }
        }
        else
        {
            const auto& Ladder=L.VillageUpperLadders[0];auto* Move=Cast<UERMovement>(Pawn->GetCharacterMovement());
            if(!Walking)
            {
                Pawn->TeleportTo(Ladder.Frame.TransformPosition({55,0,100}),Ladder.Frame.GetRotation().Rotator(),false,true);
                Move->StopMovementImmediately();Move->bLadderNeedsRelease=false;Walking=true;FlightTime=Elapsed;
            }
            if(!UpperLadderDone)Pawn->AddMovementInput(-Ladder.Frame.GetRotation().GetForwardVector());
            SawUpperLadder|=Move->IsOnLadder();SawUpperExit|=Move->IsMantling() && Move->CustomMovementMode==3;
            UpperLadderDone|=SawUpperExit && Move->IsMovingOnGround() && Pawn->GetActorLocation().Z>Ladder.Frame.GetLocation().Z+380;
            Finished=UpperLadderDone;if(Finished){Pawn->ConsumeMovementInputVector();Move->StopMovementImmediately();}
        }
        if((Finished && (GetNetMode()==NM_Standalone || Elapsed-Stage>35)) || (!Finished && Elapsed-FlightTime>20))
        {
            Check(Finished && (Flight<5 || SawUpperLadder),Labels[Flight]);
            Pawn->ConsumeMovementInputVector();
            Pawn->GetCharacterMovement()->StopMovementImmediately();
            const FVector Center(0,-L.ArenaSize.Y*.5f+800,140);FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERVillageDamage),false,Pawn);
            const uint32 Before=Map->GetStateDigest();
            const bool Found=GetWorld()->LineTraceSingleByChannel(Hit,Center-FVector(0,200,0),Center+FVector(0,200,0),ECC_GameTraceChannel1,Q);
            Check(Found && Map->ApplyHitDamage(Hit,100000) && Map->GetStateDigest()!=Before,TEXT("timber_damage"));
            const FVector Wall=L.VillageSideWalls[0].GetCenter()+FVector(0,0,20);const uint32 SideBefore=Map->GetStateDigest();
            const bool SideHit=GetWorld()->LineTraceSingleByChannel(Hit,Wall-FVector(0,100,0),Wall+FVector(0,100,0),ECC_GameTraceChannel1,Q);
            Check(SideHit && Map->ApplyHitDamage(Hit,100000) && Map->GetStateDigest()!=SideBefore,TEXT("side_wall_damage"));
            bool ImportedHit=false;
            for(const auto& Panel:Map->GetCellWorld().GetPanels())
            {
                if(ImportedHit || !Panel.SkinName.ToString().Contains(TEXT("StorageBody")))continue;
                for(int32 I=0;I+2<Panel.Skin.Num() && !ImportedHit;I+=3)
                {
                    const auto& A=Panel.Skin[I];const auto& B=Panel.Skin[I+1];const auto& D=Panel.Skin[I+2];
                    const FVector N=Panel.Transform.TransformVectorNoScale(FVector::CrossProduct(B.Position-A.Position,D.Position-A.Position).GetSafeNormal());
                    if(FMath::Abs(N.Z)>.3)continue;
                    const FVector Target=Panel.Transform.TransformPosition((A.Position+B.Position+D.Position)/3);
                    if(GetWorld()->LineTraceSingleByChannel(Hit,Target+N*45,Target-N*45,ECC_GameTraceChannel1,Q) && Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(Panel.SkinName))
                    {const uint32 Old=Map->GetStateDigest();ImportedHit=Map->ApplyHitDamage(Hit,100000) && Old!=Map->GetStateDigest();}
                }
            }
            Check(ImportedHit,TEXT("imported_storage_damage"));
            // The imported wall can shed small fragments after the hit. Publish
            // the late-join reference only when authoritative support settles.
            Phase=20;Stage=Elapsed;ForceNetUpdate();
        }
        return;
    }
    if(Phase==20)
    {
        if((Elapsed-Stage>1 && !Map->IsSupportPending()) || Elapsed-Stage>10)
        {Check(!Map->IsSupportPending(),TEXT("damage_support_settled"));Publish(2,Map);}
        return;
    }
    if(Phase==2 && Elapsed-Stage>(GetNetMode()==NM_Standalone ? 8:35))
    {
        int32 NextSeed=73;FParse::Value(FCommandLine::Get(),TEXT("ERVillageNextSeed="),NextSeed);Map->LayoutConfig.Seed=NextSeed;Check(Map->Regenerate(),TEXT("regeneration"));
        Pawn->TeleportTo(Map->GetLayout().AlphaSpawns[0].GetLocation(),FRotator::ZeroRotator,false,true);Publish(3,Map);return;
    }
    if(Phase==3 && Elapsed-Stage>18)
    {
        Check(Map->GetCellWorld().GetDetachedCount()==0,TEXT("regenerated_structures_supported"));
        if(!FApp::CanEverRender())
        {UE_LOG(LogTemp,Display,TEXT("ER_VILLAGE_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);Phase=99;return;}
        Pawn->SetActorHiddenInGame(true);PC->ClientSetHUD(AHUD::StaticClass());Phase=4;Stage=Elapsed;
    }
    if(Phase>=4 && Phase<=6)
    {
        static const TCHAR* Names[]={TEXT("Plan"),TEXT("Aerial"),TEXT("Deck"),TEXT("Underpass"),TEXT("Spawn"),TEXT("SideCover")};
        if(Shot==6){UE_LOG(LogTemp,Display,TEXT("ER_VILLAGE_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);Phase=99;return;}
        if(Phase==4)
        {
            FVector Eye,Target;
            const auto Deck=L.ElevatedDecks[0];
            if(Shot==0){Eye={0,0,FMath::Max(6000.,L.ArenaSize.Y)};Target=FVector::ZeroVector;}
            else if(Shot==1){Eye={L.ArenaSize.X*.65,-L.ArenaSize.Y*.65,FMath::Max(2900.,L.ArenaSize.Y*.5)};Target={0,0,200};}
            else if(Shot==2){Eye={Deck.Min.X+180,Deck.Min.Y+42,Deck.Max.Z+172};Target={Deck.Max.X-100,Deck.Min.Y+42,Deck.Max.Z+150};}
            else if(Shot==3){Eye=L.VillagePassages[0][0]+FVector(0,0,70);Target=L.VillagePassages[0][1]+FVector(0,0,70);}
            else if(Shot==4){Eye={0,-L.ArenaSize.Y*.5f+1500,1100};Target={0,-L.ArenaSize.Y*.5f+280,250};}
            else
            {
                // Sparse variants have an unobstructed approach wall. Dense
                // plans need the camera inside the reserved cross lane.
                const auto& Wall=L.VillageSideWalls[L.VillageBlocks.Num()==2 ? 2:0];
                const bool Approach=L.VillageBlocks.Num()<=2;
                Eye={Wall.Max.X+300,Wall.Min.Y-(Approach ? 400:100),Approach ? 300.:240.};Target=Wall.GetCenter();
            }
            auto* Camera=GetWorld()->SpawnActor<ACameraActor>(Eye,Shot==0 ? FRotator(-90,-90,0):(Target-Eye).Rotation());Camera->GetCameraComponent()->SetFieldOfView(Shot==4 ? 100:85);
            if(Shot==0){Camera->GetCameraComponent()->SetProjectionMode(ECameraProjectionMode::Orthographic);Camera->GetCameraComponent()->OrthoWidth=FMath::Max(L.ArenaSize.X+1000,(L.ArenaSize.Y+1000)*16/9);}
            PC->SetViewTarget(Camera);Phase=5;Stage=Elapsed;
        }
        else if(Phase==5 && Elapsed-Stage>3){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/Village-%s.png"),Names[Shot]),false,false);Phase=6;Stage=Elapsed;}
        else if(Phase==6 && Elapsed-Stage>1){++Shot;Phase=4;}
    }
}
