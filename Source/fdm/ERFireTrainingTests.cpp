#include "ERProceduralMap.h"
#include "ERMapCatalog.h"
#include "ERMapPlayability.h"
#include "ERLadder.h"
#include "ERCharacter.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Components/InstancedStaticMeshComponent.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace ERProcedural { bool BuildTemplate(const FERProceduralConfig&,const FERBlockRoomConfig&,FERProceduralLayout&,const FERCellSettings&); }
namespace
{
UERMapTemplate* GreyboxFireTraining(const TCHAR* Id)
{
    auto* T=NewObject<UERMapTemplate>();T->TemplateId=Id;T->Archetype=EERMapArchetype::FireTraining;T->Symmetry=EERTemplateSymmetry::HalfTurn;T->ArenaSize=FVector(2460,5540,1120);
    T->FireTraining.SteelSurface=NewObject<UMaterial>(T,TEXT("TestSteel"));
    for(int32 I=0;I<3;++I)T->FireTraining.ContainerSurfaces.Add(NewObject<UMaterial>(T,*FString::Printf(TEXT("TestPaint%d"),I)));
    return T;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERFireTrainingLayoutTest,"EclipseRecoil.Destruction.FireTrainingLayout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERFireTrainingLayoutTest::RunTest(const FString& Parameters)
{
    auto* T=GreyboxFireTraining(TEXT("test.fire-training"));
    FERProceduralConfig C;C.TemplateAsset=T;FERBlockRoomConfig B;FERProceduralLayout L,Again;
    TSet<int32> Widths,Depths,Heights,CatwalkHeights,Variants;int32 Islands=0;
    TArray<int32> Seeds;for(int32 I=0;I<512;++I)Seeds.Add(I);Seeds.Append({7919,-17,INT32_MIN,INT32_MAX});
    for(int32 Seed:Seeds)
    {
        C.Seed=Seed;
        if(!ERProcedural::Build(C,B,L)){AddError(FString::Printf(TEXT("Fire Training rejected seed=%d"),Seed));return false;}
        Widths.Add(FMath::RoundToInt32(L.WarehouseWidth));Depths.Add(FMath::RoundToInt32(L.WarehouseLength));Heights.Add(FMath::RoundToInt32(L.IndoorCeiling));Variants.Add(L.WarehouseCatwalkVariant);
        TestEqual(TEXT("no nave grid"),L.WarehouseNaves,0);
        TestTrue(TEXT("hall stays compact"),FMath::Max(L.ArenaSize.X,L.ArenaSize.Y)<=FMath::Min(L.ArenaSize.X,L.ArenaSize.Y)*1.5f);
        TestTrue(TEXT("area in configured range"),ERProcedural::PlayableAreaM2(L)>=T->FireTraining.FloorArea.X && ERProcedural::PlayableAreaM2(L)<=T->FireTraining.FloorArea.Y);
        TestEqual(TEXT("10 starts per team"),L.AlphaSpawns.Num(),10);
        TestEqual(TEXT("two protected courts"),L.WarehouseBaseCourts.Num(),2);
        TestEqual(TEXT("two bases with local roofs"),L.Bases.Num(),2);
        TestTrue(TEXT("paired flanks and bridge segments"),L.WarehouseCatwalks.Num()>=4);
        TestTrue(TEXT("hall is longitudinal"),L.ArenaSize.Y>=L.ArenaSize.X*1.12f);
        TestTrue(TEXT("interior through-rooms"),L.FeatureNames.FilterByPredicate([](FName Feature){return Feature==TEXT("GroundRoom");}).Num()>=(L.WarehouseWidth*L.WarehouseLength<22000000 ? 0:2));
        TestTrue(TEXT("real crossing routes"),L.CrossingRoutes.Num()>=3);
        for(int32 I=0;I<L.AlphaSpawns.Num();++I){const auto P=L.AlphaSpawns[I].GetLocation();TestTrue(TEXT("paired starts"),L.OmegaSpawns[I].GetLocation().Equals({-P.X,-P.Y,P.Z}));}
        TestTrue(TEXT("all opposing starts occluded"),ERPlayability::InitialSpawnsOccluded(L));
        TestTrue(TEXT("elevated budget"),L.ElevatedRatio<=T->FireTraining.ElevatedMaxRatio+.001f);
        TestTrue(TEXT("overhead budget"),L.OverheadRatio<=T->FireTraining.OverheadMaxRatio+.001f);
        TestTrue(TEXT("large open volume"),L.OpenRegionRatio>=T->FireTraining.OpenRegionMinRatio);
        for(const auto& Deck:L.ElevatedDecks)TestTrue(TEXT("standing headroom"),Deck.Max.Z+200<=L.IndoorCeiling);
        for(int32 Access:L.ElevatedAccesses)TestTrue(TEXT("two independent accesses"),Access>=2);
        for(const auto& Walk:L.WarehouseCatwalks)CatwalkHeights.Add(FMath::RoundToInt32(Walk.Max.Z));
        Islands+=L.FeatureNames.Contains(FName(TEXT("UpperIsland"))) ? 1:0;
        int32 Roofs=0;
        for(const auto& Piece:L.FixedPieces)
        {
            TestNull(TEXT("geometry has no imported mesh dependency"),Piece.Mesh);
            TestFalse(TEXT("no hidden substitute shell"),Piece.CollisionOnly);
            if(Piece.Roof){++Roofs;TestTrue(TEXT("solid visible roof"),!Piece.VisualOnly && FMath::IsNearlyEqual(Piece.Transform.GetLocation().Z-Piece.Transform.GetScale3D().Z*50,L.IndoorCeiling));}
        }
        TestEqual(TEXT("one closed procedural ceiling"),Roofs,1);
        for(const auto& Plaza:L.Plazas)for(const auto& Cargo:L.Containers)TestFalse(TEXT("plazas stay clear"),FBox(-Cargo.Size*.5,Cargo.Size*.5).TransformBy(Cargo.Frame).Intersect(Plaza.ExpandBy(-1)));
    }
    TestTrue(TEXT("free widths"),Widths.Num()>10);TestTrue(TEXT("free depths"),Depths.Num()>10);TestTrue(TEXT("free ceiling heights"),Heights.Num()>5);
    TestTrue(TEXT("varied catwalk heights"),CatwalkHeights.Num()>5);TestEqual(TEXT("six high route patterns"),Variants.Num(),6);TestTrue(TEXT("separate-height islands sampled"),Islands>0);
    C.Seed=42;TestTrue(TEXT("deterministic build"),ERProcedural::Build(C,B,L));TestTrue(TEXT("repeat build"),ERProcedural::Build(C,B,Again));
    TestEqual(TEXT("panel count stable"),L.Panels.Num(),Again.Panels.Num());TestEqual(TEXT("fixed count stable"),L.FixedPieces.Num(),Again.FixedPieces.Num());
    for(int32 I=0;I<L.Panels.Num();++I)TestTrue(TEXT("cells stable"),L.Panels[I].Id==Again.Panels[I].Id && L.Panels[I].Transform.Equals(Again.Panels[I].Transform));
    C.LayoutAttempt=ERProcedural::FireTrainingAttempts-1;TestTrue(TEXT("safe reference layout"),ERProcedural::BuildTemplate(C,B,L,FERCellSettings()));
    const int32 Count=L.Panels.Num();T->FireTraining.ContinuousUpperFloors=true;
    TestFalse(TEXT("global upper floors rejected"),ERProcedural::Build(C,B,L));TestEqual(TEXT("atomic rejection"),L.Panels.Num(),Count);
    AddInfo(FString::Printf(TEXT("ER_FIRE_PROCEDURAL seeds=%d widths=%d depths=%d ceilings=%d catwalk_heights=%d patterns=%d islands=%d"),Seeds.Num(),Widths.Num(),Depths.Num(),Heights.Num(),CatwalkHeights.Num(),Variants.Num(),Islands));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERFireTrainingPhysicsTest,"EclipseRecoil.Destruction.FireTrainingPhysics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERFireTrainingPhysicsTest::RunTest(const FString& Parameters)
{
    auto* Asset=LoadObject<UERMapTemplate>(nullptr,TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_FireTraining.DA_FireTraining"));
    if(!TestNotNull(TEXT("Fire Training template asset"),Asset))return false;
    TestEqual(TEXT("asset archetype"),Asset->Archetype,EERMapArchetype::FireTraining);
    TestTrue(TEXT("warehouse mesh references unbound"),Asset->FireTraining.Widths.IsEmpty() && !Asset->FireTraining.SideWallBay && !Asset->FireTraining.ParapetModule);
    UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);if(!World)return false;
    auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=Asset;Map->bCeilings=true;
    auto* Pawn=World->SpawnActor<AERCharacter>(FVector(-20000,0,100),FRotator::ZeroRotator);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ERFireTrainingTest),false);Q.AddIgnoredActor(Pawn);FHitResult Hit;
    const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);
    TSet<int32> Widths;
    for(int32 Seed:{3,42,73,511,7919,1001,2024,90210})
    {
        Map->LayoutConfig.Seed=Seed;
        if(!TestTrue(FString::Printf(TEXT("physical generation seed=%d"),Seed),Map->Regenerate()))continue;
        const auto& L=Map->GetLayout();TestTrue(TEXT("valid cell chunks"),Map->ValidateCells());Widths.Add(FMath::RoundToInt(L.WarehouseWidth));
        for(const auto& Piece:L.FixedPieces)TestNull(TEXT("procedural geometry uses no warehouse mesh"),Piece.Mesh);
        const float HalfW=L.WarehouseWidth*.5f,HL=L.WarehouseLength*.5f;
        // The envelope closes on every side, up to the eave, with real collision.
        for(float Z:{100.f,L.IndoorCeiling-60})for(float Y:{-HL*.5f,0.f,HL*.5f})
        {
            TestTrue(TEXT("side wall blocks pawns"),World->SweepSingleByChannel(Hit,{HalfW-100,Y,Z},{HalfW+100,Y,Z},FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(20),Q));
            TestTrue(TEXT("side wall stops bullets"),World->LineTraceSingleByChannel(Hit,{-HalfW+100,-Y,Z},{-HalfW-100,-Y,Z},ECC_GameTraceChannel1,Q));
        }
        TestTrue(TEXT("end wall blocks pawns"),World->SweepSingleByChannel(Hit,{0,-HL+100,150},{0,-HL-100,150},FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(20),Q));
        TestTrue(TEXT("roof stops shots below the ridge"),World->LineTraceSingleByChannel(Hit,{0,0,L.IndoorCeiling-50},{0,0,L.IndoorCeiling+400},ECC_GameTraceChannel1,Q) && Hit.ImpactPoint.Z<L.IndoorCeiling+230);

        // The protected court blocks ground approaches on all four sides.
        for(const auto& Court:L.WarehouseBaseCourts)
        {
            const FVector Center=Court.GetCenter();
            for(const FVector Axis:{FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0)})
            {
                const FVector Edge=Center+Axis*Court.GetExtent();
                TestTrue(TEXT("base walls stop eye-height shots"),World->LineTraceSingleByChannel(Hit,Edge+Axis*80,Edge-Axis*80,ECC_GameTraceChannel1,Q));
                TestTrue(TEXT("base walls stop standing approaches"),World->SweepSingleByChannel(Hit,Edge+Axis*100,Edge-Axis*100,FQuat::Identity,ECC_Pawn,Capsule,Q));
            }
        }
        // Every metre of every elevated route has a continuous real steel floor.
        for(const auto* Paths:{&L.CrossingRoutes,&L.RooftopRoutes})for(const auto& Route:*Paths)for(int32 I=1;I<Route.Num();++I)
        {
            const int32 Samples=FMath::CeilToInt(FVector::Distance(Route[I-1],Route[I])/50);
            for(int32 J=0;J<=Samples;++J)
            {
                const FVector P=FMath::Lerp(Route[I-1],Route[I],float(J)/Samples);
                TestTrue(TEXT("roof and catwalk have uninterrupted walkable support"),World->LineTraceSingleByChannel(Hit,P,P-FVector(0,0,120),ECC_WorldStatic,Q) && FMath::IsNearlyEqual(Hit.ImpactPoint.Z,P.Z-100,1));
            }
        }
        const float CatwalkZ=L.WarehouseCatwalks[0].Max.Z;
        TestTrue(TEXT("standing clearance below flat ceiling"),CatwalkZ+200<=L.IndoorCeiling);
        const auto& Flank=L.WarehouseCatwalks[0];
        const float Inner=Flank.Max.X;
        for(float Y=Flank.Min.Y+50;Y<Flank.Max.Y-50;Y+=100)
        {
            bool Mouth=false;for(int32 I=2;I<L.CrossingRoutes.Num();++I)Mouth|=FMath::Abs(Y-L.CrossingRoutes[I][0].Y)<180;
            if(Mouth)continue;
            TestTrue(TEXT("inner parapet blocks walking off"),World->SweepSingleByChannel(Hit,{Inner-70,Y,CatwalkZ+100},{Inner+70,Y,CatwalkZ+100},FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(20),Q));
        }
        // No mezzanine: probe the plan for walkable surfaces above the floor.
        int32 Raised=0,Samples=0;
        for(float X=-HalfW+50;X<HalfW;X+=100)for(float Y=-HL+50;Y<HL;Y+=100)
        {
            ++Samples;
            if(World->LineTraceSingleByChannel(Hit,{X,Y,L.IndoorCeiling-30},{X,Y,-50},ECC_WorldStatic,Q) && Hit.ImpactPoint.Z>150)++Raised;
        }
        TestTrue(FString::Printf(TEXT("open hall: raised coverage %.2f"),float(Raised)/Samples),float(Raised)/Samples<=Asset->FireTraining.OverheadMaxRatio+.05f);
        for(const auto& Desc:L.Ladders)
        {
            auto* Ladder=World->SpawnActor<AERLadder>(AERLadder::StaticClass(),Desc.Frame);
            Ladder->Height=Desc.Height;Ladder->DeckHeight=Desc.DeckHeight;
            // Ladder heights are local to the actor, whose base may be a deck.
            bool LoggedClimb=false;
            for(float Z=0;Z<=Desc.Height;Z+=25)
            {
                TestTrue(TEXT("real destructible rungs cover climb"),Ladder->HasRung(Z));
                const auto Position=Ladder->ClimbPosition(Z+2.5f,92.5f);
                const bool Blocked=World->OverlapBlockingTestByChannel(Position,FQuat::Identity,ECC_Pawn,Capsule,Q);
                if(Blocked && !LoggedClimb)
                {
                    TArray<FOverlapResult> Hits;World->OverlapMultiByChannel(Hits,Position,FQuat::Identity,ECC_Pawn,Capsule,Q);
                    for(const auto& H:Hits)if(auto* Part=H.GetComponent())UE_LOG(LogTemp,Display,TEXT("ER_FIRE_CLIMB_BLOCK seed=%d base=%s point=%s part=%s bounds=%s"),Seed,*Desc.Frame.GetLocation().ToString(),*Position.ToString(),*Part->GetName(),*Part->Bounds.GetBox().ToString());
                    LoggedClimb=true;
                }
                TestFalse(FString::Printf(TEXT("climbing capsule clear seed=%d ladder=%s z=%.0f"),Seed,*Desc.Frame.GetLocation().ToString(),Z),Blocked);
            }
            const FVector Exit=Ladder->Landing(92.5f);
            // Exercise the actual swept roof transition at the authored top,
            // including the horizontal move before the character settles.
            const FVector Top=Ladder->ClimbPosition(Desc.Height+2.5f,92.5f);
            const FVector Across(Exit.X,Exit.Y,Top.Z);
            TestFalse(TEXT("ladder top transition clears overhead structure"),World->SweepSingleByChannel(Hit,Top,Across,FQuat::Identity,ECC_Pawn,Capsule,Q));
            if(FMath::IsNearlyEqual(Desc.DeckHeight,CatwalkZ))TestEqual(TEXT("high ladders step onto the notched deck without overshoot"),Desc.Height,CatwalkZ+5);
            TestFalse(TEXT("ladder landing clears standing capsule"),World->OverlapBlockingTestByChannel(Exit,FQuat::Identity,ECC_Pawn,Capsule,Q));
            TestTrue(TEXT("ladder landing has a real floor"),World->LineTraceSingleByChannel(Hit,Exit,Exit-FVector(0,0,120),ECC_WorldStatic,Q));
            Ladder->Destroy();
        }
        for(const auto* Starts:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& S:*Starts)
        {TestFalse(TEXT("spawn capsule clear"),World->OverlapBlockingTestByChannel(S.GetLocation(),FQuat::Identity,ECC_Pawn,Capsule,Q));TestTrue(TEXT("spawn supported"),World->LineTraceSingleByChannel(Hit,S.GetLocation(),S.GetLocation()-FVector(0,0,120),ECC_WorldStatic,Q));}
        for(const auto* Paths:{&L.CrossingRoutes,&L.RoomAccessRoutes,&L.RooftopRoutes})for(const auto& Route:*Paths)for(int32 I=1;I<Route.Num();++I)
            TestFalse(FString::Printf(TEXT("route standing clearance seed=%d %s->%s"),Seed,*Route[I-1].ToString(),*Route[I].ToString()),World->SweepSingleByChannel(Hit,Route[I-1],Route[I],FQuat::Identity,ECC_Pawn,Capsule,Q));
        for(const auto& Route:L.StairRoutes)for(int32 I=1;I<Route.Num();++I)
            TestTrue(TEXT("every stair tread has solid support"),World->LineTraceSingleByChannel(Hit,Route[I],Route[I]-FVector(0,0,145),ECC_WorldStatic,Q));
        const auto& R=L.Routes[1];
        for(int32 I=0;I<R.Num();++I)
            TestFalse(TEXT("patrol loop clear"),World->SweepSingleByChannel(Hit,R[I],R[(I+1)%R.Num()],FQuat::Identity,ECC_Pawn,Capsule,Q));
        const auto& Base=L.Bases[0];FERCellId Cell;
        TestTrue(TEXT("base timber is destructible"),World->LineTraceSingleByChannel(Hit,Base.Point({0,0,150}),Base.Point({400,0,150}),ECC_GameTraceChannel1,Q) && Map->ResolveCell(Hit,Cell));

    }
    TestTrue(TEXT("physical seeds cover several widths"),Widths.Num()>=2);
    World->DestroyWorld(false);return true;
}
#endif
