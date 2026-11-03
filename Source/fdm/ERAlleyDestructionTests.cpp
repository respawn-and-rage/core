#include "ERAlleyDestruction.h"
#include "ERProceduralMap.h"
#include "ERWeapon.h"
#include "ERBlockTarget.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERAlleyArchitectureTest,"EclipseRecoil.Destruction.MiddleEasternArchitecture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERAlleyArchitectureTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);if(!W)return false;
    auto* M=W->SpawnActor<AERProceduralMap>();M->LayoutConfig.TemplateAsset=LoadObject<UERMapTemplate>(nullptr,TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_MiddleEasternAlleys"));M->LayoutConfig.Seed=73;M->bCeilings=false;
    if(!TestTrue(TEXT("destructible district builds"),M->Regenerate())){W->DestroyWorld(false);return false;}
    M->RequestSupportCheck();for(int32 I=0;I<500 && M->IsSupportPending();++I)M->StepSupport();
    TestEqual(TEXT("intact house shells have grounded structural support"),M->GetCellWorld().GetRemovedCount(),int64(0));
    ERAlleyDestruction::FBreach B;
    if(TestTrue(TEXT("source facade blocks a clear outside-to-room route"),ERAlleyDestruction::FindWall(M,B)))
    {
        auto* Target=W->SpawnActor<AERBlockTarget>(B.Inside,FRotator::ZeroRotator);Target->Mesh->SetRelativeScale3D(FVector(.25));
        FCollisionQueryParams ShotQuery(SCENE_QUERY_STAT(ERHouseInteriorShot),false);FHitResult First;
        W->LineTraceSingleByChannel(First,B.Outside,B.Inside,ECC_GameTraceChannel1,ShotQuery);
        const auto Shot=ERBlockCombat::ContinueShot(W,First,B.Outside,(B.Inside-B.Outside).GetSafeNormal(),2000,100000,FERBlockShotSettings(),ShotQuery,nullptr,nullptr);
        TestTrue(TEXT("penetration resumes world tracing and hits a target inside the hollow house"),Shot.TerminalHit.GetActor()==Target && Target->Health<500);
        Target->Destroy();
        // Use a fresh intact surface for the independent HP/capsule checks.
        if(!TestTrue(TEXT("another intact facade is available after the penetration shot"),ERAlleyDestruction::FindWall(M,B))){W->DestroyWorld(false);return false;}
        const float HP=M->GetCellWorld().GetHealth(B.Cell);TestTrue(TEXT("original surface hit resolves calibrated cell HP"),HP>0 && M->ApplyHitDamage(B.Hit,HP*.5));
        TestTrue(TEXT("partial damage remains server-side HP"),FMath::IsNearlyEqual(M->GetCellWorld().GetHealth(B.Cell),HP*.5));
        const double Floor=M->GetCellWorld().GetPanels()[B.Cell.Panel].Transform.GetLocation().Z;
        TestTrue(TEXT("controlled wall cut removes logical shell cells"),ERAlleyDestruction::Cut(M,B.Cell.Panel,ERAlleyDestruction::WallBounds(B.Outside,B.Inside,Floor))>0);
        FCollisionQueryParams Q(SCENE_QUERY_STAT(ERArchitectureCut),false);
        FHitResult WallHit;const bool Blocked=W->SweepSingleByChannel(WallHit,B.Outside+FVector(0,0,30),B.Inside+FVector(0,0,30),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,92.5),Q);
        if(Blocked)AddInfo(FString::Printf(TEXT("ER_ARCHITECTURE_BLOCKED component=%s hit=%s normal=%s outside=%s inside=%s"),WallHit.GetComponent() ? *WallHit.GetComponent()->GetName():TEXT("none"),*WallHit.ImpactPoint.ToString(),*WallHit.ImpactNormal.ToString(),*B.Outside.ToString(),*B.Inside.ToString()));
        TestFalse(TEXT("new wall hole admits a standing capsule through original collision"),Blocked);
        // The initial mask cannot be resurrected by a malformed client mask.
        const auto& CW=M->GetCellWorld();const int32 C=CW.GetPanels()[B.Cell.Panel].FirstChunk;TArray<uint8> Wire;TArray<uint64> Alive;
        CW.EncodeChunk(C,Wire);TestTrue(TEXT("skin mask round trip"),CW.DecodeChunk(C,Wire,Alive));
        bool Compressed=false;
        for(int32 CI=C;CI<C+CW.GetPanels()[B.Cell.Panel].ChunksX*CW.GetPanels()[B.Cell.Panel].ChunksZ;++CI)
        {TArray<uint8> Frame;TArray<uint64> Decoded;CW.EncodeChunk(CI,Frame);Compressed|=Frame[0]==2;TestTrue(TEXT("bounded skin compression restores the identical alive bits"),CW.DecodeChunk(CI,Frame,Decoded) && Decoded==CW.GetChunks()[CI].Alive);}
        TestTrue(TEXT("hollow source masks compress below the original RLE size"),Compressed);
        TArray<uint8> Oversize{2,255,255,255,127,0};
        TestFalse(TEXT("oversized compressed frames are rejected before allocation"),CW.DecodeChunk(C,Oversize,Alive));
        const auto& Chunk=CW.GetChunks()[C];const int32 Bits=Chunk.SX*Chunk.SZ*CW.GetPanels()[Chunk.Panel].Dims.Y;
        if(Chunk.AliveCount<Bits)
        {
            Wire={1};uint32 Run=Bits;do{const uint8 Byte=Run&127;Run>>=7;Wire.Add(Byte|(Run ? 128:0));}while(Run);
            TestFalse(TEXT("network mask cannot turn source room voids into solid cells"),CW.DecodeChunk(C,Wire,Alive));
        }
    }
    FVector Roof;FERCellId R;
    if(TestTrue(TEXT("original roof is a real walkable damageable surface"),ERAlleyDestruction::FindRoof(M,Roof,R)))
    {
        ERAlleyDestruction::Cut(M,R.Panel,FBox(Roof-FVector(110,110,160),Roof+FVector(110,110,20)));
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERArchitectureRoof),false);
        const bool Hits=W->LineTraceSingleByChannel(Hit,Roof+FVector(0,0,100),Roof-FVector(0,0,300),ECC_WorldStatic,Q);
        TestTrue(TEXT("roof cut opens a physical fall route"),!Hits || Hit.ImpactPoint.Z<Roof.Z-100);
        const auto Before=M->GetCellWorld().GetDetachedCount();
        ERAlleyDestruction::SeverSupports(M,R.Panel);
        for(int32 I=0;I<500 && M->IsSupportPending();++I)M->StepSupport();
        TestTrue(TEXT("loss of ground support detaches remaining upper house geometry"),M->GetCellWorld().GetDetachedCount()>Before);
        TestTrue(TEXT("damaged/collapsed mesh mappings stay valid"),M->ValidateCells());
    }
    M->LayoutConfig.Seed=73;TestTrue(TEXT("regeneration restores all original skin cells and collisions"),M->Regenerate() && M->GetCellWorld().GetRemovedCount()==0 && M->ValidateCells());
    W->DestroyWorld(false);AddInfo(TEXT("ER_ALLEY_ARCHITECTURE_TEST_COMPLETE"));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERAlleyBrickTowerTest,"EclipseRecoil.Destruction.MiddleEasternBrickTowers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERAlleyBrickTowerTest::RunTest(const FString&)
{
    UWorld::InitializationValues Values;Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);if(!World)return false;
    auto* Map=World->SpawnActor<AERProceduralMap>();
    Map->LayoutConfig.TemplateAsset=LoadObject<UERMapTemplate>(nullptr,TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_MiddleEasternAlleys"));
    Map->LayoutConfig.ExpectedTeamSize=3;Map->LayoutConfig.Seed=42;Map->bCeilings=false;
    if(!TestTrue(TEXT("3v3 brick tower district builds"),Map->Regenerate())){World->DestroyWorld(false);return false;}
    const auto& Layout=Map->GetLayout();
    if(!TestTrue(TEXT("paired climbable towers exist"),Layout.DistrictTowers.Num()>=2)){World->DestroyWorld(false);return false;}
    const auto& Ladder=Layout.DistrictTowers[0];const float Height=Layout.DistrictTowerHeights[0];
    // The first tower has a 6m footprint; its ladder is 35cm outside the east face.
    const FVector Center=Ladder.TransformPosition({-335,0,0});
    const FBox TowerBounds(Center-FVector(400,400,1),Center+FVector(400,400,Height+100));
    const auto& Cells=Map->GetCellWorld();int32 Deck=INDEX_NONE;TArray<int32> TowerPanels;
    for(int32 I=0;I<Cells.GetPanels().Num();++I)
    {
        const auto& Panel=Cells.GetPanels()[I];
        const FVector Mid=Panel.Transform.TransformPosition(Panel.Size*.5);
        if(Panel.Material!=EERBlockMaterial::Brick || !TowerBounds.IsInside(Mid))continue;
        TowerPanels.Add(I);
        TestTrue(TEXT("tower brick layers are at most10cm thick with ordinary strength"),Panel.Size.GetMin()<=10.01 && Panel.Strength==1);
        if(FVector::Dist2D(Mid,Center)<1 && FMath::IsNearlyEqual(float(Mid.Z-Center.Z),Height-5,.1f))Deck=I;
    }
    if(!TestTrue(TEXT("thin brick platform is present"),Deck!=INDEX_NONE)){World->DestroyWorld(false);return false;}
    const auto& Platform=Cells.GetPanels()[Deck];
    const FERCellId DeckCell{Deck,Platform.Dims.X/2,Platform.Dims.Y/2,Platform.Dims.Z/2};
    TestFalse(TEXT("sniper platform is not permanently anchored"),Platform.bAnchored || Platform.bAnchorBottom);
    Map->RequestSupportCheck();for(int32 I=0;I<500 && Map->IsSupportPending();++I)Map->StepSupport();
    TestEqual(TEXT("intact district and tower keep all support"),Cells.GetRemovedCount(),int64(0));
    const FBox CutBand(Center+FVector(-400,-400,100),Center+FVector(400,400,160));
    int32 CutCount=0;for(int32 Panel:TowerPanels)CutCount+=ERAlleyDestruction::Cut(Map,Panel,CutBand);
    TestTrue(TEXT("controlled damage removes thin brick supports"),CutCount>0);
    TestTrue(TEXT("platform survives direct damage to supports"),Cells.IsAlive(DeckCell));
    Map->RequestSupportCheck();for(int32 I=0;I<500 && Map->IsSupportPending();++I)Map->StepSupport();
    TestFalse(TEXT("loss of tower walls collapses the sniper platform"),Cells.IsAlive(DeckCell));
    TestTrue(TEXT("collapsed tower retains valid cell mappings"),Map->ValidateCells());
    World->DestroyWorld(false);return true;
}

#endif
