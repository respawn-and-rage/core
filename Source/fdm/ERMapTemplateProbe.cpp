#include "ERMapTemplateProbe.h"
#include "ERMapTemplate.h"
#include "ERProceduralMap.h"
#include "ERCharacter.h"
#include "ERWeapon.h"
#include "ERPlayerController.h"
#include "ERMatch.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Components/PrimitiveComponent.h"
AERMapTemplateProbe::AERMapTemplateProbe(){bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;}
void AERMapTemplateProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps)const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERMapTemplateProbe,Phase);DOREPLIFETIME(AERMapTemplateProbe,ExpectedDigest);}
void AERMapTemplateProbe::Check(bool Condition,const TCHAR* Label)
{Failed|=!Condition;UE_LOG(LogTemp,Display,TEXT("ER_TEMPLATE_PROBE_%s %s"),Condition ? TEXT("PASS"):TEXT("FAIL"),Label);}
void AERMapTemplateProbe::Publish(int32 Next,AERProceduralMap* Map)
{Phase=Next;ExpectedDigest=Map->GetStateDigest();ForceNetUpdate();UE_LOG(LogTemp,Display,TEXT("ER_TEMPLATE_STATE phase=%d digest=%u epoch=%u blocks=%d"),Phase,ExpectedDigest,Map->GetMapEpoch(),int32(Map->GetCellWorld().GetCellCount()-Map->GetCellWorld().GetRemovedCount()));}
void AERMapTemplateProbe::Tick(float Delta)
{
    Super::Tick(Delta);Elapsed+=Delta;
    TActorIterator<AERProceduralMap> It(GetWorld());auto* Map=It ? *It:nullptr;
    if(!Map || !Map->LayoutConfig.TemplateAsset){if(Elapsed>45)FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("ERTownProbe"))){TickTown(Map);return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("ERParapetProbe"))){TickParapets(Delta,Map);return;}
    if(!HasAuthority())
    {
        if(Phase && SeenPhase!=Phase && Map->IsSnapshotCurrent() && Map->GetStateDigest()==ExpectedDigest)
        {
            bool Good=Map->ValidateCells();FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERTemplatePeer),false);
            // Sample the fixed shell and surviving logical collision after deltas.
            const float Edge=Map->GetLayout().ArenaSize.X*.5;
            Good &= GetWorld()->LineTraceSingleByChannel(Hit,FVector(Edge-100,0,800),FVector(Edge+100,0,800),ECC_WorldStatic,Q);
            int32 Samples=0;
            const auto& Cells=Map->GetCellWorld();
            for(int32 P=0;P<Cells.GetPanels().Num() && Samples<20;++P)
            {
                const auto& Panel=Cells.GetPanels()[P];const FERCellId Id{P,Panel.Dims.X/2,0,Panel.Dims.Z/2};
                if(!Cells.IsAlive(Id) || Panel.Transform.GetRotation().GetAxisZ().Z<.99)continue;
                ++Samples;
                const FVector C=Map->GetActorTransform().TransformPosition(Cells.GetCellCenter(Id)),N=-Map->GetActorTransform().TransformVectorNoScale(Panel.Transform.GetRotation().GetAxisY());
                Good &= GetWorld()->LineTraceSingleByChannel(Hit,C+N*(Panel.Cell.Y*.5+20),C,ECC_GameTraceChannel1,Q);
            }
            UE_LOG(LogTemp,Display,TEXT("ER_TEMPLATE_CLIENT_%s phase=%d digest=%u epoch=%u"),Good ? TEXT("PASS"):TEXT("FAIL"),Phase,ExpectedDigest,Map->GetMapEpoch());SeenPhase=Phase;
        }
        return;
    }
    auto* PC=Cast<AERPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(),0));
    auto* C=PC ? Cast<AERCharacter>(PC->GetPawn()):nullptr;
    if(!C || !C->Weapon){if(Elapsed>45)FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const bool Network=GetNetMode()!=NM_Standalone;
    const float ShotAt=Network ? 35.f:12.f, SnapshotAt=ShotAt+6, RegenerateAt=SnapshotAt+(Network ? 30.f:18.f);
    if(Phase==0 && Elapsed>3)
    {
        Check(Map->ValidateCells() && Map->GetLayout().AlphaSpawns.Num()==10 && Map->GetLayout().OmegaSpawns.Num()==10,TEXT("template_spawn_bands_and_mappings"));
        Check(C->GetActorLocation().Z>90 && C->GetActorLocation().Z<101,TEXT("actual_pawn_supported_on_floor"));
        C->TeleportTo(Map->GetLayout().Routes[1][0]+FVector(0,50,0),FRotator(0,90,0));C->GetCharacterMovement()->StopMovementImmediately();
        PC->SetControlRotation(FRotator(0,90,0));Publish(1,Map);
        if(FApp::CanEverRender())FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Testing/Killhouse-Spawn.png"),true,false);
    }
    else if(Phase==1)
    {
        if(Elapsed<7)C->AddMovementInput(FVector(0,1,0));
        if(Elapsed>ShotAt)
        {
            Check(C->GetActorLocation().Y>Map->GetLayout().Routes[1][0].Y+800 && C->GetActorLocation().Z>90,TEXT("actual_pawn_walks_side_lane"));
            const auto& Base=Map->GetLayout().Bases[0];
            const float Yaw=Base.Frame.Rotator().Yaw+180;
            C->TeleportTo(Base.Point(FVector(0,Base.Size.Y*.5-100,100)),FRotator(0,Yaw,0));C->GetCharacterMovement()->StopMovementImmediately();PC->SetControlRotation(FRotator(0,Yaw,0));
            FHitResult Hit;FERCellId Id;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERTemplateShot),false,C);
            const FVector Eye=C->Camera->GetComponentLocation();
            const bool Found=GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+FVector(-1200,0,0),ECC_GameTraceChannel1,Q) && Map->ResolveCell(Hit,Id);
            Check(Found,TEXT("bunker_panel_authoritative_hit_identity"));
            if(Found)
            {
                // A band of the hit panel 1 HP from removal, so real spread still breaks cells.
                TMap<FERCellId,float> Preparation;const auto& Cells=Map->GetCellWorld();const auto& Panel=Cells.GetPanels()[Id.Panel];
                for(int32 Z=0;Z<Panel.Dims.Z;++Z)for(int32 Y=0;Y<Panel.Dims.Y;++Y)for(int32 X=0;X<Panel.Dims.X;++X)
                {
                    const FERCellId Cell{Id.Panel,X,Y,Z};
                    if(Cells.IsAlive(Cell) && FMath::Abs(Map->GetActorTransform().TransformPosition(Cells.GetCellCenter(Cell)).Z-Hit.ImpactPoint.Z)<100)Preparation.Add(Cell,Cells.GetHealth(Cell)-1);
                }
                Map->ApplyCellBatch(Preparation);
            }
            const uint32 Before=C->Weapon->AuthorizedShotCount;C->Weapon->StartFire();C->Weapon->StopFire();
            Check(C->Weapon->AuthorizedShotCount==Before+1 && Map->GetCellWorld().GetRemovedCount()>0,TEXT("real_weapon_destroys_template_panel"));
            if(Found)AERBlockRoom::ApplyWorldExplosion(GetWorld(),Hit.ImpactPoint+Hit.ImpactNormal*25,250,2400,C);
            Publish(2,Map);
            if(FApp::CanEverRender())FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Testing/Killhouse-Breach.png"),true,false);
        }
    }
    else if(Phase==2 && Elapsed>SnapshotAt)
    {
        Check(Map->GetCellWorld().GetRemovedCount()>1 && Map->ValidateCells(),TEXT("explosion_and_collapse_keep_mappings"));Publish(3,Map);
        C->TeleportTo(Map->GetLayout().StairRoutes[0][0],FRotator(0,90,0));C->GetCharacterMovement()->StopMovementImmediately();PC->SetControlRotation(FRotator(0,90,0));
    }
    else if(Phase==3 && Elapsed<=RegenerateAt)
    {
        if(RoofTest<2)
        {
            const float Side=RoofTest==0 ? 1.f:-1.f;
            if(C->GetActorLocation().Y*Side < Map->GetLayout().StairRoutes[RoofTest].Last().Y*Side)C->AddMovementInput(FVector(0,Side,0));
            else
            {
                C->GetCharacterMovement()->StopMovementImmediately();
                if(!RoofCapture && FApp::CanEverRender())
                {
                    PC->SetControlRotation(FRotator(-40,-90*Side,0));
                    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/(RoofTest==0 ? TEXT("Testing/Killhouse-AlphaRoof.png"):TEXT("Testing/Killhouse-OmegaRoof.png")),true,false);
                }
                RoofCapture=true;
            }
            if(Elapsed>SnapshotAt+(RoofTest+1)*5)
            {
                Check(C->GetActorLocation().Y*Side > Map->GetLayout().StairRoutes[RoofTest].Last().Y*Side-50 && C->GetActorLocation().Z>Map->GetLayout().Bases[RoofTest].RoofZ+80,RoofTest==0 ? TEXT("actual_alpha_rear_entry_and_roof_access"):TEXT("actual_omega_rear_entry_and_roof_access"));
                ++RoofTest;RoofCapture=false;
                const FVector Next=Map->GetLayout().StairRoutes[RoofTest==1 ? 1:2][0];
                C->TeleportTo(Next,FRotator(0,RoofTest==1 ? -90:90,0));C->GetCharacterMovement()->StopMovementImmediately();PC->SetControlRotation(FRotator(0,RoofTest==1 ? -90:90,0));
            }
        }
        else if(C->GetActorLocation().Y < Map->GetLayout().StairRoutes[2].Last().Y)C->AddMovementInput(FVector(0,1,0));
        else C->GetCharacterMovement()->StopMovementImmediately();
    }
    else if(Phase==3 && Elapsed>RegenerateAt)
    {
        Check(C->GetActorLocation().Y>Map->GetLayout().StairRoutes[2].Last().Y-50 && C->GetActorLocation().Z>Map->GetLayout().StairRoutes[2].Last().Z-20,TEXT("actual_pawn_climbs_watch_platform_stairs"));
        const uint32 Old=Map->GetLayoutDigest();PC->ConsoleCommand(TEXT("ERMapGenerate 7919 8 1"));
        Check(Map->LayoutConfig.Seed==7919 && Old!=Map->GetLayoutDigest() && Map->GetCellWorld().GetRemovedCount()==0,TEXT("console_seed_regeneration_and_damage_reset"));
        Publish(4,Map);
        if(FApp::CanEverRender())
        {
            TInlineComponentArray<UPrimitiveComponent*> Parts(Map);for(auto* Part:Parts)if(Part->GetFName()==TEXT("Ceilings") || Part->ComponentHasTag(TEXT("ERTemplateRoof")))Part->SetVisibility(false);
            const FVector View(6400,-8500,9000);
            auto* Camera=GetWorld()->SpawnActor<ACameraActor>(View,(-View).Rotation());
            Camera->GetCameraComponent()->SetFieldOfView(65);PC->SetViewTarget(Camera);
        }
    }
    else if(Phase==4 && Elapsed>RegenerateAt+3 && SeenPhase!=4)
    {
        if(FApp::CanEverRender())
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Testing/Killhouse-Overview.png"),true,false);
        }
        SeenPhase=4;
    }
    if(Phase==4 && FApp::CanEverRender() && !Network && FParse::Param(FCommandLine::Get(),TEXT("ERTemplateGallery")))
    {
        if(GalleryIndex<2 && Elapsed>RegenerateAt+4+GalleryIndex*3)
        {
            PC->ConsoleCommand(FString::Printf(TEXT("ERMapGenerate %d 8 1"),GalleryIndex==0 ? 7:113));
            TInlineComponentArray<UPrimitiveComponent*> Parts(Map);for(auto* Part:Parts)if(Part->GetFName()==TEXT("Ceilings") || Part->ComponentHasTag(TEXT("ERTemplateRoof")))Part->SetVisibility(false);
            ++GalleryIndex;GalleryCapture=false;
        }
        if(GalleryIndex && !GalleryCapture && Elapsed>RegenerateAt+2+GalleryIndex*3)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/Killhouse-Variant-%d.png"),Map->LayoutConfig.Seed),true,false);GalleryCapture=true;
        }
    }
    if(Phase==4 && Elapsed>RegenerateAt+12){UE_LOG(LogTemp,Display,TEXT("ER_TEMPLATE_PROBE_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);Phase=5;}
}
