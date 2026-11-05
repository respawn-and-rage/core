#include "ERRoomsProbe.h"
#include "ERProceduralMap.h"
#include "ERCharacter.h"
#include "ERPlayerController.h"
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
#include "Misc/CommandLine.h"

AERRoomsProbe::AERRoomsProbe(){bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;}
void AERRoomsProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps)const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERRoomsProbe,Phase);DOREPLIFETIME(AERRoomsProbe,ExpectedDigest);}
void AERRoomsProbe::Check(bool Good,const TCHAR* Label)
{Failed|=!Good;UE_LOG(LogTemp,Display,TEXT("ER_ROOMS_CHECK %s=%s"),Label,Good ? TEXT("PASS"):TEXT("FAIL"));}
void AERRoomsProbe::Publish(int32 Next,AERProceduralMap* Map)
{Phase=Next;Stage=Elapsed;ExpectedDigest=Map->GetStateDigest();ForceNetUpdate();UE_LOG(LogTemp,Display,TEXT("ER_ROOMS_STATE phase=%d digest=%u epoch=%u"),Phase,ExpectedDigest,Map->GetMapEpoch());}
void AERRoomsProbe::Tick(float Delta)
{
    Super::Tick(Delta);Elapsed+=Delta;
    TActorIterator<AERProceduralMap> It(GetWorld());auto* Map=It ? *It:nullptr;if(!Map)return;
    const auto& L=Map->GetLayout();
    if(!HasAuthority())
    {
        if(Phase>0 && Phase<4 && Seen!=Phase && Map->IsSnapshotCurrent() && Map->GetStateDigest()==ExpectedDigest)
        {
            FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERRoomsPeer),false);bool Good=Map->ValidateCells();
            for(const auto& E:L.RoomLinks)
            {
                const FVector P=(E.Start+E.End)*.5f;
                Good&=GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(0,0,E.Height-30),P+FVector(0,0,E.Height+30),ECC_Visibility,Q);
            }
            for(const auto& E:L.RoomLinks)if(L.RoomScreens.ContainsByPredicate([&](const auto& S){return S.Link==int32(&E-L.RoomLinks.GetData());}))Good&=GetWorld()->LineTraceSingleByChannel(Hit,E.Start+FVector(0,0,172.5f),E.End+FVector(0,0,172.5f),ECC_GameTraceChannel1,Q);
            UE_LOG(LogTemp,Display,TEXT("ER_ROOMS_CLIENT_%s phase=%d digest=%u"),Good ? TEXT("PASS"):TEXT("FAIL"),Phase,ExpectedDigest);Seen=Phase;
        }
        return;
    }
    auto* PC=Cast<AERPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(),0));auto* Pawn=PC ? Cast<AERCharacter>(PC->GetPawn()):nullptr;
    if(!Pawn || L.RoomLinks.IsEmpty()){if(Elapsed>60)FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(Phase==0 && Elapsed>5)
    {
        Check(Map->ValidateCells(),TEXT("initial_cells"));
        FCollisionQueryParams Q(SCENE_QUERY_STAT(ERRoomsScreens),false,Pawn);FHitResult Hit;bool Blocked=true;
        for(const auto& E:L.RoomLinks)if(L.RoomScreens.ContainsByPredicate([&](const auto& S){return S.Link==int32(&E-L.RoomLinks.GetData());}))
        {
            FERCellId Id;
            Blocked&=GetWorld()->LineTraceSingleByChannel(Hit,E.Start+FVector(0,0,172.5f),E.End+FVector(0,0,172.5f),ECC_GameTraceChannel1,Q) && Map->ResolveCell(Hit,Id);
        }
        Check(Blocked,TEXT("corridor_timber_blocks_sight"));
        if(FApp::CanEverRender() && GetNetMode()==NM_Standalone && FParse::Param(FCommandLine::Get(),TEXT("ERRoomsGalleryOnly"))){Publish(3,Map);Stage=Elapsed-18;}
        else Publish(1,Map);
        return;
    }
    if(Phase==1)
    {
        if(!Walking && Walk<3)
        {
            Path.Reset();
            if(Walk==0)
            {
                for(int32 I=0;I<L.RoomLinks.Num();++I)if(FMath::Abs(L.RoomLinks[I].End.Z-L.RoomLinks[I].Start.Z)>1)
                {const auto& Access=L.RoomAccessRoutes[I];for(int32 J=1;J<Access.Num()-1;++J)Path.Add(Access[J]);break;}
            }
            else if(Walk==1){if(!L.StairRoutes.IsEmpty())Path=L.StairRoutes[0];}
            else for(int32 I=0;I<L.RoomLinks.Num();++I)
            {
                const auto& E=L.RoomLinks[I];if(E.Start.Z<L.RoomNodes[E.A].Center.Z+300)continue;
                bool Started=false;
                for(const auto P:L.RoomAccessRoutes[I])
                {
                    if(P.Z>=E.Start.Z+90)Started=true;if(Started)Path.Add(P);
                    if(P.Equals(E.Start+FVector(0,0,100),.1))break;
                }
                Path.Add(E.Start+(E.End-E.Start).GetSafeNormal2D()*FMath::Min(180.f,float(FVector::Dist2D(E.Start,E.End)*.5))+FVector(0,0,100));break;
            }
            // A flat plan has no ramp or stairs: walk what this one has. test-rooms.py --macro requires all three.
            if(Path.IsEmpty()){UE_LOG(LogTemp,Display,TEXT("ER_ROOMS_WALK_SKIPPED walk=%d"),Walk);++Walk;}
            else
            {
                Pawn->TeleportTo(Path[0],FRotator::ZeroRotator,false,true);Pawn->GetCharacterMovement()->StopMovementImmediately();Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
                Point=1;Walking=true;WalkStarted=Elapsed;
            }
        }
        if(Walking)
        {
            const FVector Target=Path[Point],Position=Pawn->GetActorLocation();
            if(FVector::Dist2D(Target,Position)<8 && FMath::Abs(Target.Z-Position.Z)<40)
            {
                Pawn->GetCharacterMovement()->StopMovementImmediately();
                if(++Point>=Path.Num()){Check(true,Walk==0 ? TEXT("pawn_walks_corridor_ramp"):Walk==1 ? TEXT("pawn_walks_upper_switchback"):TEXT("pawn_walks_upper_corridor_portal"));Walking=false;++Walk;Pawn->GetCharacterMovement()->StopMovementImmediately();}
            }
            else Pawn->AddMovementInput((Target-Position).GetSafeNormal2D(),FMath::Clamp(float(FVector::Dist2D(Target,Position)/100),.2f,1.f),true);
        }
        if(Walking && Elapsed-WalkStarted>45 && Walk<3)
        {UE_LOG(LogTemp,Display,TEXT("ER_ROOMS_WALK_BLOCKED walk=%d point=%d actual=%s target=%s"),Walk,Point,*Pawn->GetActorLocation().ToString(),*Path[Point].ToString());Check(false,TEXT("pawn_walk_timeout"));Walk=3;}
        if(Walk==3 && Elapsed-Stage>(GetNetMode()==NM_Standalone ? 20:30))
        {
            Pawn->TeleportTo(L.AlphaSpawns[0].GetLocation(),FRotator::ZeroRotator,false,true);
            const FBox Cover=L.LowCovers[0];FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERRoomsDamage),false);Q.AddIgnoredActor(Pawn);
            const FVector Center=Cover.GetCenter();const uint32 Before=Map->GetStateDigest();
            const bool Found=GetWorld()->LineTraceSingleByChannel(Hit,Center+FVector(0,0,200),Center,ECC_GameTraceChannel1,Q);
            Check(Found && Map->ApplyHitDamage(Hit,100000) && Map->GetStateDigest()!=Before,TEXT("wood_cover_damage"));Publish(2,Map);
        }
        return;
    }
    // A cold late client must finish loading characters before regeneration.
    if(Phase==2 && Elapsed-Stage>(GetNetMode()==NM_Standalone ? 18:35))
    {
        Map->LayoutConfig.Seed=73;Check(Map->Regenerate(),TEXT("regeneration"));
        int32 Index=0;for(TActorIterator<AERCharacter> P(GetWorld());P;++P)if(P->GetController())P->TeleportTo(Map->GetLayout().AlphaSpawns[(Index++)%8].GetLocation(),FRotator::ZeroRotator,false,true);
        Publish(3,Map);return;
    }
    if(Phase==3 && Elapsed-Stage>18)
    {
        if(!FApp::CanEverRender())
        {UE_LOG(LogTemp,Display,TEXT("ER_ROOMS_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);Phase=99;return;}
        Pawn->SetActorHiddenInGame(true);PC->ClientSetHUD(AHUD::StaticClass());Phase=4;Stage=Elapsed;
    }
    if(Phase>=4 && Phase<=6)
    {
        // Three seeds, six views each. Flat plans use a ceiling view instead of stairs.
        static const int32 Seeds[]={42,73,13};static const TCHAR* Names[]={TEXT("Plan"),TEXT("Base"),TEXT("Corridor"),TEXT("Upper"),TEXT("RoomPanels"),TEXT("Slalom")};
        constexpr int32 PerSeed=UE_ARRAY_COUNT(Names);
        if(Shot>=PerSeed*UE_ARRAY_COUNT(Seeds)){UE_LOG(LogTemp,Display,TEXT("ER_ROOMS_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);Phase=99;return;}
        if(Phase==4)
        {
            if(Shot%PerSeed==0){Map->LayoutConfig.Seed=Seeds[Shot/PerSeed];Check(Map->Regenerate(),TEXT("gallery_seed"));UE_LOG(LogTemp,Display,TEXT("ER_ROOMS_GALLERY seed=%d rooms=%d theme=%d screens=%d digest=%u"),Seeds[Shot/PerSeed],L.Rooms.Num(),L.RoomsTheme,L.RoomScreens.Num(),Map->GetStateDigest());}
            TInlineComponentArray<UPrimitiveComponent*> Parts(Map);for(auto* Part:Parts)if(Part->ComponentHasTag(TEXT("ERTemplateRoof")))Part->SetVisibility(Shot%PerSeed!=0);
            FVector Eye,Target;const int32 Kind=Shot%PerSeed;
            if(Kind==0){Eye={0,0,7000};Target=FVector::ZeroVector;}
            else if(Kind==1){Eye=L.AlphaSpawns[0].GetLocation()+FVector(0,0,70);Target=L.RoomNodes[0].Center+FVector(-700,0,160);}
            else if(Kind==2){const auto& E=L.RoomLinks[0];const FVector D=(E.End-E.Start).GetSafeNormal2D();Eye=E.Start+D*60+FVector(0,0,170);Target=E.End+FVector(0,0,160);}
            else if(Kind==3)
            {
                const auto* Stairs=L.StairRoutes.FindByPredicate([](const auto& Route){return !Route.IsEmpty();});
                if(Stairs){Eye=Stairs->Last()+FVector(0,0,60);Target=L.RoomNodes[2].Center+FVector(0,0,140);}
                else{const auto& V=L.RoomNodes[0];Eye=V.Center+FVector(0,0,170);Target=V.Center+FVector(150,150,V.Height-5);}
            }
            else if(Kind==4){const auto& V=L.RoomNodes[3];Eye=V.Center+FVector(0,0,170);Target=V.Center+FVector(V.Size.X*.35f,V.Size.Y*.3f,160);}
            else
            {
                const auto* R=L.RoomAccessRoutes.FindByPredicate([](const auto& V){return V.Num()>=6;});
                if(R){Eye=(*R)[2]+FVector(0,0,70);Target=(*R)[4]+FVector(0,0,70);}
                else{Eye=L.RoomNodes[0].Center+FVector(0,0,170);Target=L.RoomNodes[1].Center+FVector(0,0,170);}
            }
            auto* Camera=GetWorld()->SpawnActor<ACameraActor>(Eye,Kind==0 ? FRotator(-90,0,0):(Target-Eye).Rotation());
            Camera->GetCameraComponent()->SetFieldOfView(90);
            if(Kind==0){Camera->GetCameraComponent()->SetProjectionMode(ECameraProjectionMode::Orthographic);Camera->GetCameraComponent()->OrthoWidth=FMath::Max(L.ArenaSize.Y,L.ArenaSize.X*16.f/9)*1.08f;}
            PC->SetViewTarget(Camera);Phase=5;Stage=Elapsed;
        }
        else if(Phase==5 && Elapsed-Stage>2){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/Rooms-%d-%s.png"),Seeds[Shot/PerSeed],Names[Shot%PerSeed]),false,false);Phase=6;Stage=Elapsed;}
        else if(Phase==6 && Elapsed-Stage>1){++Shot;Phase=4;}
    }
}
