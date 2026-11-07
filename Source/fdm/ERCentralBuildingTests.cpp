#include "ERProceduralMap.h"
#include "ERMapCatalog.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "ERCharacter.h"
#include "ERMovement.h"
#include "ERLadder.h"
#include "ERIndustrialFence.h"
#include "ERIndustrialMapTestUtils.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PCGDataAsset.h"
#include "Materials/Material.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCentralBuildingLayoutTest,"EclipseRecoil.Destruction.CentralBuildingLayout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERCentralBuildingLayoutTest::RunTest(const FString& Parameters)
{
    // Rectangular studies support 1-3 floors; the shared recipe defaults to
    // the production circular villa's 1-4 range. Keep this fixture explicit.
    auto* T=NewObject<UERMapTemplate>();T->TemplateId=TEXT("test.central");T->Archetype=EERMapArchetype::CentralBuilding;T->CentralBuilding.CircularTerrain=false;T->CentralBuilding.MaxFloors=3;T->Symmetry=EERTemplateSymmetry::HalfTurn;T->ArenaSize=FVector(8000,10000,1600);
    T->CentralBuilding.SteelSurface=NewObject<UMaterial>(T,TEXT("TestGraySteel"));
    for(int32 I=0;I<4;++I)T->CentralBuilding.ContainerSurfaces.Add(NewObject<UMaterial>(T,*FString::Printf(TEXT("TestPaint%d"),I)));
    FERProceduralConfig C;C.TemplateAsset=T;FERBlockRoomConfig B;FERProceduralLayout L,Again;
    TSet<FString> Shapes,Compositions;TSet<int32> Rules,Floors,SecondaryFloors,Angles,Materials;int32 MinOpenCover=MAX_int32,MinOtherCover=MAX_int32;bool Tower=false,Building=false,AsymmetricPaint=false,TerracedSeen=false;TSet<float> CoreOffsets;
    auto Key=[](FVector Center,FVector Size,uint8 M){return FString::Printf(TEXT("%d:%d:%d/%d:%d:%d/%d"),FMath::RoundToInt32(Center.X*1000),FMath::RoundToInt32(Center.Y*1000),FMath::RoundToInt32(Center.Z*1000),FMath::RoundToInt32(Size.X*1000),FMath::RoundToInt32(Size.Y*1000),FMath::RoundToInt32(Size.Z*1000),M);};
    TArray<int32> Seeds;for(int32 I=0;I<512;++I)Seeds.Add(I);Seeds.Append({7919,-17,INT32_MIN,INT32_MAX});
    for(int32 Seed:Seeds)
    {
        C.Seed=Seed;
        if(!ERProcedural::Build(C,B,L)){AddError(FString::Printf(TEXT("Central layout rejected seed=%d"),Seed));return false;}
        CoreOffsets.Add(L.CentralBuildingCenter.X);TerracedSeen|=L.TerracedBuilding;
        if(!L.CentralBuildingCenter.IsNearlyZero())
        {
            FVector A=FVector::ZeroVector,D=FVector::ZeroVector;for(const auto& P:L.AlphaSpawns)A+=P.GetLocation();for(const auto& P:L.OmegaSpawns)D+=P.GetLocation();A/=L.AlphaSpawns.Num();D/=L.OmegaSpawns.Num();
            TestTrue(TEXT("lateral building is equidistant from team base centres"),FMath::IsNearlyEqual(FVector::Dist2D(A,L.CentralBuildingCenter),FVector::Dist2D(D,L.CentralBuildingCenter),.01));
        }
        Shapes.Add(L.ArenaSize.ToString()+L.CentralBuildingSize.ToString());Rules.Add(uint8(L.CrossingRule));Floors.Add(L.BuildingFloors);
        FString Composition;for(auto K:L.ModuleKinds)Composition+=FString::FromInt(uint8(K));Compositions.Add(Composition);
        TestEqual(TEXT("10 starts per team"),L.AlphaSpawns.Num(),10);TestEqual(TEXT("paired starts"),L.OmegaSpawns.Num(),10);
        for(int32 I=0;I<10;++I){auto P=L.AlphaSpawns[I].GetLocation();P.X=-P.X;P.Y=-P.Y;TestTrue(TEXT("equal spawn geometry"),P.Equals(L.OmegaSpawns[I].GetLocation()));}
        for(const auto& P:L.Panels)if(P.Material==EERBlockMaterial::Metal)
            TestTrue(TEXT("steel and containers use separate visual surfaces"),P.Strength==8 ? T->CentralBuilding.ContainerSurfaces.Contains(P.Surface):P.Surface==T->CentralBuilding.SteelSurface);
        TMap<FString,UMaterialInterface*> Painted;
        for(const auto& P:L.Panels)if(P.Strength==8)
        {
            const auto Box=FBox(FVector::ZeroVector,P.Size).TransformBy(P.Transform);
            Painted.Add(Key(Box.GetCenter(),Box.GetSize(),uint8(P.Material)),P.Surface);
        }
        for(const auto& P:L.Panels)if(P.Strength==8)
        {
            const auto Box=FBox(FVector::ZeroVector,P.Size).TransformBy(P.Transform);auto Center=Box.GetCenter();Center.X=-Center.X;Center.Y=-Center.Y;
            if(auto* Other=Painted.Find(Key(Center,Box.GetSize(),uint8(P.Material))))AsymmetricPaint|=*Other!=P.Surface;
        }
        TSet<FString> Solids;
        for(const auto& P:L.Panels){const auto Box=FBox(FVector::ZeroVector,P.Size).TransformBy(P.Transform);Solids.Add(Key(Box.GetCenter(),Box.GetSize(),uint8(P.Material)));}
        for(const auto& P:L.Panels){const auto Box=FBox(FVector::ZeroVector,P.Size).TransformBy(P.Transform);auto V=Box.GetCenter();V.X=-V.X;V.Y=-V.Y;if(!Solids.Contains(Key(V,Box.GetSize(),uint8(P.Material))) && !Solids.Contains(Key(V+2*L.CentralBuildingCenter,Box.GetSize(),uint8(P.Material)))){AddError(TEXT("unpaired cell geometry"));return false;}}
        Solids.Reset();
        for(const auto& P:L.FixedPieces)Solids.Add(Key(P.Transform.GetLocation(),P.Transform.GetScale3D(),uint8(P.Material)));
        for(const auto& P:L.FixedPieces){auto V=P.Transform.GetLocation();V.X=-V.X;V.Y=-V.Y;if(!Solids.Contains(Key(V,P.Transform.GetScale3D(),uint8(P.Material))) && !Solids.Contains(Key(V+2*L.CentralBuildingCenter,P.Transform.GetScale3D(),uint8(P.Material)))){AddError(TEXT("unpaired fixed collision"));return false;}}
        for(const auto& P:L.FixedPieces)if(P.PawnBarrier)
        {
            const FVector Size=P.Transform.GetScale3D()*100;
            TestTrue(TEXT("one original-height steel course"),FMath::IsNearlyEqual(Size.Z,ERIndustrialFence::ModuleHeightCm));
            const float Bottom=P.Transform.GetLocation().Z-Size.Z*.5;
            const auto* Plinth=L.FixedPieces.FindByPredicate([&](const auto& B)
            {
                const auto BS=B.Transform.GetScale3D()*100;
                return B.Material==EERBlockMaterial::Concrete && FMath::IsNearlyEqual(BS.Z,ERIndustrialFence::PlinthHeightCm)
                    && B.Transform.GetLocation().Equals(P.Transform.GetLocation()-FVector(0,0,(Size.Z+BS.Z)*.5),.01);
            });
            TestNotNull(TEXT("steel rests on a concrete plinth"),Plinth);
            TestTrue(TEXT("plinth starts on its floor"),FMath::IsNearlyZero(FMath::Fmod(Bottom-ERIndustrialFence::PlinthHeightCm,320.f),.01));
        }
        int32 Flights=(L.TerracedBuilding ? 2:L.BuildingFloors*2)+L.WallAccessFlights.Num(),Ladders=(L.TerracedBuilding ? L.BuildingFloors*2:2)+L.SecondaryStructures.Num();
        for(const auto& S:L.SecondaryStructures){if(S.LadderAccess)Ladders+=S.Floors;else Flights+=S.Floors;
            TestTrue(TEXT("towers and compact buildings prefer ladders"),S.LadderAccess==(S.Tower || S.Size.X<800 || S.Size.Y<1150));SecondaryFloors.Add(S.Floors);Tower|=S.Tower;Building|=!S.Tower;Materials.Add(uint8(S.Material));}
        for(float A:L.CoverAngles)Angles.Add(FMath::RoundToInt(FMath::Fmod(A+180,180)));
        TestEqual(TEXT("main and large buildings retain stairs"),L.StairRoutes.Num(),Flights);
        TestEqual(TEXT("every compact storey and paired exterior has a ladder"),L.Ladders.Num(),Ladders);
        for(int32 I=0;I<L.Ladders.Num();I+=2)
            TestTrue(TEXT("ladder access is half-turn symmetric"),(L.Ladders[I].Frame*FTransform(FRotator(0,180,0))).Equals(L.Ladders[I+1].Frame) || (L.Ladders[I].Frame*FTransform(FRotator(0,180,0))).GetLocation().Equals(L.Ladders[I+1].Frame.GetLocation()-2*L.CentralBuildingCenter));
        if(L.CrossingRule==EERCrossingRule::OpenYard)MinOpenCover=FMath::Min(MinOpenCover,L.CoverSites.Num());
        else MinOtherCover=FMath::Min(MinOtherCover,L.CoverSites.Num());
        TestTrue(TEXT("paired destructible containers"),L.Containers.Num()>=2 && L.Containers.Num()%2==0);
        if(L.CrossingRule==EERCrossingRule::FencedCrossings)TestEqual(TEXT("roof and two side crossings"),L.CrossingRoutes.Num(),3);
    }
    TestTrue(FString::Printf(TEXT("open yard has distributed intermediate cover min=%d"),MinOpenCover),MinOpenCover>=16);
    TestTrue(FString::Printf(TEXT("separated variants retain intermediate cover min=%d"),MinOtherCover),MinOtherCover>=12);
    AddInfo(FString::Printf(TEXT("ER_CENTRAL_COVER min_open=%d min_other=%d"),MinOpenCover,MinOtherCover));
    TestTrue(TEXT("cosmetic container rolls can differ across symmetric geometry"),AsymmetricPaint);
    TestTrue(TEXT("centred and both lateral cores sampled"),CoreOffsets.Num()==3);TestTrue(TEXT("setback upper floors sampled"),TerracedSeen);
    TestTrue(TEXT("hundreds of different building/arena dimensions"),Shapes.Num()>450);
    TestTrue(TEXT("over a hundred distinct cover compositions"),Compositions.Num()>100);
    TestEqual(TEXT("all crossing rules sampled"),Rules.Num(),3);TestEqual(TEXT("one to three floors sampled"),Floors.Num(),3);
    TestEqual(TEXT("secondary buildings vary across three storeys"),SecondaryFloors.Num(),3);
    TestTrue(TEXT("buildings and climbable towers sampled"),Tower && Building);
    TestTrue(TEXT("secondary wall materials vary"),Materials.Num()>=2);
    TestTrue(TEXT("oblique rectangular cover across multiple angles"),Angles.Num()>=5);
    C.Seed=42;TestTrue(TEXT("first deterministic build"),ERProcedural::Build(C,B,L));TestTrue(TEXT("repeat"),ERProcedural::Build(C,B,Again));
    TestEqual(TEXT("panel count stable"),L.Panels.Num(),Again.Panels.Num());
    for(int32 I=0;I<L.Panels.Num();++I)TestTrue(TEXT("panel identity and transform stable"),L.Panels[I].Id==Again.Panels[I].Id && L.Panels[I].Transform.Equals(Again.Panels[I].Transform) && L.Panels[I].Size==Again.Panels[I].Size && L.Panels[I].Surface==Again.Panels[I].Surface);
    for(int32 I=0;I<L.FixedPieces.Num();++I)TestTrue(TEXT("fixed collision stable"),L.FixedPieces[I].Transform.Equals(Again.FixedPieces[I].Transform));
    const int32 Count=L.Panels.Num();T->CentralBuilding.MaxFloors=4;
    TestFalse(TEXT("invalid recipe rejected"),ERProcedural::Build(C,B,L));TestEqual(TEXT("rejection is atomic"),L.Panels.Num(),Count);
    T->CentralBuilding.MaxFloors=3;C.CrossingOverride=4;TestFalse(TEXT("invalid descriptor rule rejected"),ERProcedural::Build(C,B,L));
    TestEqual(TEXT("five production archetypes in rotation"),ERMaps::Rotation().Num(),5);
    TestTrue(TEXT("depot excluded from rotation"),!ERMaps::At(1).InRotation);
    auto* Preview=T->CreatePCGPreview(42);TestNotNull(TEXT("PCGEx authoring data"),Preview);
    if(Preview)TestTrue(TEXT("spawns, crossings and fixed volumes exported"),Preview->Data.TaggedData.Num()>=4);
    AddInfo(FString::Printf(TEXT("ER_CENTRAL_LAYOUT seeds=%d shapes=%d cover_compositions=%d"),Seeds.Num(),Shapes.Num(),Compositions.Num()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCentralBuildingFinishesTest,"EclipseRecoil.Destruction.CentralBuildingFinishes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERCentralBuildingFinishesTest::RunTest(const FString& Parameters)
{
    auto* T=NewObject<UERMapTemplate>();T->TemplateId=TEXT("test.central.finishes");T->Archetype=EERMapArchetype::CentralBuilding;T->CentralBuilding.CircularTerrain=false;T->CentralBuilding.MaxFloors=3;T->Symmetry=EERTemplateSymmetry::HalfTurn;T->ArenaSize=FVector(8000,10000,1600);
    FERProceduralConfig C;C.TemplateAsset=T;FERBlockRoomConfig B;
    auto& V=T->CentralBuilding;
    TSet<int32> GroundIndices;
    for(int32 Seed=0;Seed<32;++Seed)
    {
        C.Seed=Seed;FERProceduralLayout Before,After,Repeat;
        V.ConcreteSurfaces.Reset();V.BrickSurfaces.Reset();V.WoodSurfaces.Reset();V.GroundSurfaces.Reset();
        if(!TestTrue(TEXT("baseline layout"),ERProcedural::Build(C,B,Before)))return false;
        TestNull(TEXT("empty palette preserves saved floor"),Before.GroundSurface);
        for(auto* Palette:{&V.ConcreteSurfaces,&V.BrickSurfaces,&V.WoodSurfaces,&V.GroundSurfaces})
            for(int32 I=0;I<3;++I)Palette->Add(NewObject<UMaterial>(T));
        if(!TestTrue(TEXT("visual palette layout"),ERProcedural::Build(C,B,After)) || !TestTrue(TEXT("repeat palette layout"),ERProcedural::Build(C,B,Repeat)))return false;
        TestEqual(TEXT("palette keeps panel count"),Before.Panels.Num(),After.Panels.Num());
        TestEqual(TEXT("palette keeps fixed count"),Before.FixedPieces.Num(),After.FixedPieces.Num());
        TestEqual(TEXT("palette keeps arena"),Before.ArenaSize,After.ArenaSize);
        TestEqual(TEXT("ground choice repeats"),After.GroundSurface,Repeat.GroundSurface);
        TestTrue(TEXT("ground belongs to palette"),V.GroundSurfaces.Contains(After.GroundSurface));
        GroundIndices.Add(V.GroundSurfaces.IndexOfByKey(After.GroundSurface));
        for(int32 I=0;I<After.Panels.Num();++I)
        {
            const auto& A=After.Panels[I];const auto& Old=Before.Panels[I];
            TestTrue(TEXT("palette preserves collision, material type and strength"),A.Id==Old.Id && A.Transform.Equals(Old.Transform) && A.Size==Old.Size && A.Material==Old.Material && A.Strength==Old.Strength);
            TestEqual(TEXT("finish repeats on clients"),A.Surface,Repeat.Panels[I].Surface);
            const auto* Palette=A.Material==EERBlockMaterial::Concrete ? &V.ConcreteSurfaces:A.Material==EERBlockMaterial::Brick ? &V.BrickSurfaces:A.Material==EERBlockMaterial::Wood ? &V.WoodSurfaces:nullptr;
            if(Palette)TestTrue(TEXT("construction uses its visual palette"),Palette->Contains(A.Surface));
        }
        for(int32 I=0;I<After.FixedPieces.Num();++I)TestTrue(TEXT("palette preserves fixed collision"),After.FixedPieces[I].Transform.Equals(Before.FixedPieces[I].Transform));
    }
    TestEqual(TEXT("all three ground finishes sampled"),GroundIndices.Num(),3);
    FERProceduralLayout L;
    V.GroundSurfaces.Add(nullptr);TestFalse(TEXT("null ground rejected"),ERProcedural::Build(C,B,L));V.GroundSurfaces.Pop();
    while(V.BrickSurfaces.Num()<9)V.BrickSurfaces.Add(NewObject<UMaterial>(T));
    TestFalse(TEXT("oversized finish palette rejected"),ERProcedural::Build(C,B,L));
    AddInfo(TEXT("ER_CENTRAL_FINISHES seeds=32 geometry_preserved=1 deterministic=1"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCentralBuildingCollisionTest,"EclipseRecoil.Destruction.CentralBuildingCollision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERCentralBuildingCollisionTest::RunTest(const FString& Parameters)
{
    auto* T=NewObject<UERMapTemplate>();T->TemplateId=TEXT("test.central.physics");T->Archetype=EERMapArchetype::CentralBuilding;T->CentralBuilding.CircularTerrain=false;T->CentralBuilding.MaxFloors=3;T->Symmetry=EERTemplateSymmetry::HalfTurn;T->ArenaSize=FVector(8000,10000,1600);
    T->CentralBuilding.FenceModule=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/EclipseRecoil/Art/Industrial/SM_ER_AluminiumFenceModule.SM_ER_AluminiumFenceModule"));
    if(!TestNotNull(TEXT("imported single aluminium module"),T->CentralBuilding.FenceModule.Get()))return false;
    UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);if(!World)return false;
    auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->bCeilings=false;
    auto* Pawn=World->SpawnActor<AERCharacter>(FVector(-20000,0,100),FRotator::ZeroRotator);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ERCentralTest),false);Q.AddIgnoredActor(Pawn);FHitResult Hit;
    const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);
    for(int32 Rule=1;Rule<=3;++Rule)for(int32 Floors=1;Floors<=3;++Floors)
    {
        T->CentralBuilding.MinFloors=T->CentralBuilding.MaxFloors=Floors;Map->LayoutConfig.CrossingOverride=Rule;
        if(!TestTrue(TEXT("physical generation"),Map->Regenerate()))continue;
        const auto& L=Map->GetLayout();TestTrue(TEXT("valid cell chunks"),Map->ValidateCells());
        int32 ExpectedLadders=(L.TerracedBuilding ? L.BuildingFloors*2:2)+L.SecondaryStructures.Num();
        for(const auto& S:L.SecondaryStructures)if(S.LadderAccess)ExpectedLadders+=S.Floors;
        TestEqual(TEXT("paired interior and exterior ladders"),L.Ladders.Num(),ExpectedLadders);
        for(const auto& Desc:L.Ladders)
        {
            auto* Ladder=World->SpawnActor<AERLadder>(AERLadder::StaticClass(),Desc.Frame);
            Ladder->Height=Desc.Height;Ladder->DeckHeight=Desc.DeckHeight;
            for(float Z=0;Z<=Desc.Height;Z+=25)
            {
                TestTrue(TEXT("real destructible rungs cover climb"),Ladder->HasRung(Z));
                TestFalse(TEXT("standing capsule clears exterior ladder"),World->OverlapBlockingTestByChannel(Ladder->ClimbPosition(Z+2.5f,92.5f),FQuat::Identity,ECC_Pawn,Capsule,Q));
            }
            const FVector Exit=Ladder->Landing(92.5f);
            TestFalse(TEXT("ladder landing clears standing capsule"),World->OverlapBlockingTestByChannel(Exit,FQuat::Identity,ECC_Pawn,Capsule,Q));
            TestTrue(TEXT("ladder landing has a real floor"),World->LineTraceSingleByChannel(Hit,Exit,Exit-FVector(0,0,120),ECC_WorldStatic,Q));
            Ladder->Destroy();
        }
        for(const auto& Site:L.CoverSites)
        {
            const auto Body=FCollisionShape::MakeCapsule(34,Site.HalfHeight);
            TestFalse(TEXT("cover pocket fits its standing/crouching occupant"),World->OverlapBlockingTestByChannel(Site.Hide,FQuat::Identity,ECC_Pawn,Body,Q));
            TestFalse(TEXT("cover pocket has a clear entry"),World->SweepSingleByChannel(Hit,Site.Entry,Site.Hide,FQuat::Identity,ECC_Pawn,Body,Q));
            TestTrue(TEXT("cover hides eye height from the opposing side"),World->LineTraceSingleByChannel(Hit,Site.SightFrom,Site.SightTo,ECC_GameTraceChannel1,Q));
            FERCellId CoverId;TestTrue(TEXT("cover is actual destructible construction"),Map->ResolveCell(Hit,CoverId));
        }
        if(Rule==1)for(float Sign:{-1.f,1.f})
            TestTrue(TEXT("open flank eye-height sightline is broken"),World->LineTraceSingleByChannel(Hit,{Sign*(L.CentralBuildingSize.X*.5+500),-L.CentralBuildingSize.Y*.5-100,172.5},{Sign*(L.CentralBuildingSize.X*.5+500),L.CentralBuildingSize.Y*.5+100,172.5},ECC_GameTraceChannel1,Q));
        TInlineComponentArray<UInstancedStaticMeshComponent*> FenceInstances(Map);int32 Modules=0;
        for(auto* Part:FenceInstances)if(Part->GetName()==TEXT("IndustrialFenceModules"))
        {Modules+=Part->GetInstanceCount();TestTrue(TEXT("one imported module reused"),Part->GetStaticMesh()==T->CentralBuilding.FenceModule);}
        TestTrue(TEXT("small module repeated across barriers"),Modules>L.FixedPieces.FilterByPredicate([](const auto& P){return P.PawnBarrier;}).Num());
        for(const auto* Starts:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& S:*Starts)
        {TestFalse(TEXT("spawn capsule clear"),World->OverlapBlockingTestByChannel(S.GetLocation(),FQuat::Identity,ECC_Pawn,Capsule,Q));TestTrue(TEXT("spawn supported"),World->LineTraceSingleByChannel(Hit,S.GetLocation(),S.GetLocation()-FVector(0,0,120),ECC_WorldStatic,Q));}
        for(const auto& Route:L.RoomAccessRoutes)for(int32 I=1;I<Route.Num();++I)
        {
            TestFalse(TEXT("terrace doorway has standing clearance"),World->SweepSingleByChannel(Hit,Route[I-1],Route[I],FQuat::Identity,ECC_Pawn,Capsule,Q));
            for(int32 J=0;J<=10;++J){const FVector P=FMath::Lerp(Route[I-1],Route[I],J/10.f);TestTrue(TEXT("terrace doorway has continuous floor support"),World->LineTraceSingleByChannel(Hit,P,P-FVector(0,0,120),ECC_WorldStatic,Q));}
        }
        for(const auto& Route:L.CrossingRoutes)for(int32 I=1;I<Route.Num();++I)
            TestFalse(TEXT("crossing has standing clearance"),World->SweepSingleByChannel(Hit,Route[I-1],Route[I],FQuat::Identity,ECC_Pawn,Capsule,Q));
        for(const auto& Route:L.StairRoutes)for(int32 I=1;I<Route.Num();++I)
        {
            const FVector P=Route[I];TestFalse(FString::Printf(TEXT("standing headroom rule=%d floors=%d p=%s"),Rule,Floors,*P.ToString()),World->OverlapBlockingTestByChannel(P,FQuat::Identity,ECC_Pawn,Capsule,Q));
            TestTrue(TEXT("every stair tread has solid support"),World->LineTraceSingleByChannel(Hit,P,P-FVector(0,0,145),ECC_WorldStatic,Q));
        }
        const float X=-L.CentralBuildingSize.X*.5-400;
        const bool Blocked=World->SweepSingleByChannel(Hit,{X,-200,100},{X,200,100},FQuat::Identity,ECC_Pawn,Capsule,Q);
        TestEqual(TEXT("outside crossing obeys rule"),Blocked,Rule!=1);
        if(Rule==2)
        {
            auto* Move=Cast<UERMovement>(Pawn->GetCharacterMovement());Pawn->SetActorLocation({X-250,-55,100});Pawn->SetActorRotation(FRotator(0,90,0));
            // Use the movement component's real 150cm ledge query.
            Move->SetMovementMode(MOVE_Walking);Move->bClimbHeld=true;
            FVector Landing;float Height=0;TestFalse(TEXT("3m wall cannot be climbed directly from ground"),Move->FindLedge(Landing,Height));
            TestEqual(TEXT("wall has four ground access points"),L.ClimbPoints.Num(),4);
            for(float Wing:{X,-X})
            {
                TestTrue(TEXT("wall reaches 3m"),World->LineTraceSingleByChannel(Hit,{Wing-250,-60,299},{Wing-250,60,299},ECC_Pawn,Q));
                TestFalse(TEXT("above wall is clear"),World->LineTraceSingleByChannel(Hit,{Wing-250,-60,301},{Wing-250,60,301},ECC_Pawn,Q));
            }
        }
        if(Rule==3)
        {
            TestTrue(TEXT("central ground route sealed"),World->LineTraceSingleByChannel(Hit,{0,-100,100},{0,100,100},ECC_Pawn,Q));
            FERCellId Id;TestFalse(TEXT("fixed separator is not a destructible chunk"),Map->ResolveCell(Hit,Id));
        }
        // Exercise the imported module on both perimeter axes and the internal
        // separator. Apertures must agree for bullets, visibility and blast rays.
        for(bool AlongX:{true,false})
        {
            const auto* Fence=L.FixedPieces.FindByPredicate([&](const auto& P)
            {const auto S=P.Transform.GetScale3D();return P.PawnBarrier && (S.X>=S.Y)==AlongX && (AlongX ? P.Transform.GetLocation().Y<-L.ArenaSize.Y*.5+10:P.Transform.GetLocation().X<-L.ArenaSize.X*.5+10) && P.Transform.GetLocation().Z<ERIndustrialFence::PlinthHeightCm+ERIndustrialFence::ModuleHeightCm;});
            if(!TestNotNull(TEXT("modular fence panel"),Fence))continue;
            const FVector Center=Fence->Transform.GetLocation(),S=Fence->Transform.GetScale3D()*100;
            const FVector Across=AlongX ? FVector(1,0,0):FVector(0,1,0);
            const FVector Normal=AlongX ? FVector(0,1,0):FVector(1,0,0);
            const float Top=ERIndustrialFence::PlinthHeightCm+ERIndustrialFence::ModuleHeightCm;
            FHitResult BaseHit;
            const FVector BasePoint(Center.X,Center.Y,ERIndustrialFence::PlinthHeightCm*.5);
            TestTrue(TEXT("concrete plinth stops bullets"),World->LineTraceSingleByChannel(BaseHit,BasePoint-Normal*60,BasePoint+Normal*60,ECC_GameTraceChannel1,Q));
            FERCellId BaseCell;TestFalse(TEXT("plinth preserves fixed boundary"),Map->ResolveCell(BaseHit,BaseCell));
            const FVector Above(Center.X,Center.Y,Top+1);
            TestFalse(TEXT("no invisible wall above visible barrier"),World->LineTraceSingleByChannel(BaseHit,Above-Normal*60,Above+Normal*60,ECC_Pawn,Q));
            auto* Move=Cast<UERMovement>(Pawn->GetCharacterMovement());
            const float JumpApex=FMath::Square(Move->JumpZVelocity)/(-2*Move->GetGravityZ());
            TestTrue(TEXT("combined height exceeds jump plus mantle reach"),Top>JumpApex+150);
            for(float Side:{-1.f,1.f})for(float Rise=0;Rise<=JumpApex+1;Rise+=5)
            {
                Pawn->SetActorLocation(FVector(Center.X,Center.Y,92.5f+Rise)+Normal*Side*42);
                Pawn->SetActorRotation((-Normal*Side).Rotation());
                Move->SetMovementMode(Rise>0 ? MOVE_Falling:MOVE_Walking);Move->bClimbHeld=true;
                FVector Destination;float LedgeHeight=0;
                TestFalse(TEXT("cannot mantle plinth or steel during jump"),Move->FindLedge(Destination,LedgeHeight));
            }
            Move->bClimbHeld=false;Pawn->SetActorLocation(FVector(-20000,0,100));
            int32 BlockedShots=0;
            for(int32 I=0;I<256;++I)
            {
                const FVector P=Center+Across*((-.47+(I%16)*.94/15)*(AlongX ? S.X:S.Y))+FVector(0,0,(-.47+(I/16)*.94/15)*S.Z);
                auto Ray=[&](ECollisionChannel Channel,bool Reverse=false)
                {return World->LineTraceSingleByChannel(Hit,P-Normal*(Reverse ? -60:60),P+Normal*(Reverse ? -60:60),Channel,Q);};
                const bool HitMetal=Ray(ECC_GameTraceChannel1);BlockedShots+=HitMetal;
                if(HitMetal){FERCellId Id;TestFalse(TEXT("module stays indestructible"),Map->ResolveCell(Hit,Id));}
                TestEqual(TEXT("module shots agree from both sides"),Ray(ECC_GameTraceChannel1,true),HitMetal);
                TestEqual(TEXT("visibility follows actual module bars"),Ray(ECC_Visibility),HitMetal);
                TestEqual(TEXT("blast object queries follow actual module bars"),World->LineTraceSingleByObjectType(Hit,P-Normal*60,P+Normal*60,FCollisionObjectQueryParams(ECC_WorldStatic),Q),HitMetal);
                TestTrue(TEXT("pawn cannot cross module apertures"),Ray(ECC_Pawn));
            }
            AddInfo(FString::Printf(TEXT("ER_FENCE_RAYS rule=%d floors=%d axis=%d blocked=%d triangles=%d flag=%d never_cook=%d"),Rule,Floors,AlongX,BlockedShots,T->CentralBuilding.FenceModule->GetBodySetup()->TriMeshGeometries.Num(),int32(T->CentralBuilding.FenceModule->GetBodySetup()->GetCollisionTraceFlag()),T->CentralBuilding.FenceModule->GetBodySetup()->bNeverNeedsCookedCollisionData));
            TestTrue(TEXT("original module has solid bars and open apertures"),BlockedShots>0 && BlockedShots<256);
        }
        const auto& Container=L.Containers[0];
        TestTrue(TEXT("container stops bullets"),World->LineTraceSingleByChannel(Hit,Container.Frame.TransformPosition({0,-Container.Size.Y*.5-50,0}),Container.Frame.TransformPosition({0,Container.Size.Y*.5+50,0}),ECC_GameTraceChannel1,Q));
        FERCellId ContainerCell;TestTrue(TEXT("container collision resolves destructible steel"),Map->ResolveCell(Hit,ContainerCell));
        if(Map->ResolveCell(Hit,ContainerCell))TestEqual(TEXT("steel is reinforced separately from shared profiles"),Map->GetCellWorld().GetPanels()[ContainerCell.Panel].Strength,8.f);
    }
    World->DestroyWorld(false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCentralBuildingEvolutionTest,"EclipseRecoil.Destruction.CentralBuildingEvolution",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERCentralBuildingEvolutionTest::RunTest(const FString& Parameters)
{
    auto* T=NewObject<UERMapTemplate>();T->TemplateId=TEXT("test.central.evolution");T->Archetype=EERMapArchetype::CentralBuilding;T->CentralBuilding.CircularTerrain=false;T->CentralBuilding.MaxFloors=3;T->Symmetry=EERTemplateSymmetry::HalfTurn;T->ArenaSize={8000,10000,1600};
    UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);if(!World)return false;
    auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->LayoutConfig.CrossingOverride=2;Map->bCeilings=false;Map->Collapse.DebrisPerStep=0;
    TestTrue(TEXT("evolution map generated"),Map->Regenerate());
    for(int32 Seed:{42,10,16,7919})
    {
        Map->LayoutConfig.Seed=Seed;if(!Map->Regenerate()){AddError(TEXT("evolution generation failed"));continue;}
        Map->RequestSupportCheck();ERIndustrialTest::Settle(*Map);
        TestEqual(TEXT("intact secondary structures and access need no artificial anchors"),Map->GetCellWorld().GetDetachedCount(),int64(0));
    }
    const auto& L=Map->GetLayout();FCollisionQueryParams Q(SCENE_QUERY_STAT(ERIndustrialEvolution),false);FHitResult Hit;FERCellId Id;
    if(!L.CoverSites.IsEmpty())
    {
        const auto& Site=L.CoverSites[0];
        TestTrue(TEXT("intermediate cover initially hides occupant"),World->LineTraceSingleByChannel(Hit,Site.SightFrom,Site.SightTo,ECC_GameTraceChannel1,Q));
        ERIndustrialTest::Cut(*Map,Site.Frame,FBox(FVector(-80,-200,0),FVector(80,200,200)),Site.Material);
        TestFalse(TEXT("breaching intermediate cover exposes its hiding place"),World->LineTraceSingleByChannel(Hit,Site.SightFrom,Site.SightTo,ECC_GameTraceChannel1,Q));
    }

    if(L.Containers.IsEmpty() || L.WallBridges.Num()!=2){World->DestroyWorld(false);return false;}
    if(!L.Ladders.IsEmpty())
    {
        const auto Desc=L.Ladders[0];
        auto* Ladder=World->SpawnActor<AERLadder>(AERLadder::StaticClass(),Desc.Frame);
        Ladder->Height=Desc.Height;Ladder->DeckHeight=Desc.DeckHeight;
        TestTrue(TEXT("intact ladder has real rung"),Ladder->HasRung(90));
        ERIndustrialTest::Cut(*Map,Desc.Frame,FBox(FVector(-5,-38,149),FVector(5,38,151)),EERBlockMaterial::Metal);
        TestFalse(TEXT("destroyed steel rung interrupts climbing"),Ladder->HasRung(90));
        Ladder->Destroy();
    }
    const auto Container=L.Containers[0];
    const FVector A=Container.Frame.TransformPosition({0,-Container.Size.Y*.5-50,0}),B=Container.Frame.TransformPosition({0,Container.Size.Y*.5+50,0});
    TestTrue(TEXT("reinforced container initially blocks a standing crossing"),ERIndustrialTest::ContainerBlocked(*World,Container,Q));
    if(World->LineTraceSingleByChannel(Hit,A,B,ECC_GameTraceChannel1,Q) && Map->ResolveCell(Hit,Id))
    {
        const float Before=Map->GetCellWorld().GetHealth(Id);float RemainingDamage=36;
        const auto Shot=Map->ShootCells(Hit,A,(B-A).GetSafeNormal(),500,RemainingDamage,8);
        TestTrue(TEXT("reinforced steel stops rifle penetration"),Shot.bStop);
        TestTrue(TEXT("rifle damages steel without erasing it"),Map->GetCellWorld().IsAlive(Id) && Map->GetCellWorld().GetHealth(Id)<Before);
        TestTrue(TEXT("container is substantially tougher than the sheet default"),FMath::IsNearlyEqual(Before,8*Map->MaterialCatalog->Find(EERBlockMaterial::Metal)->MaxHealth));
    }
    else AddError(TEXT("container shot missed destructible steel"));
    ERIndustrialTest::BreachContainer(*Map,Container);ERIndustrialTest::Settle(*Map);
    TestFalse(TEXT("container damage creates a real capsule-sized passage"),ERIndustrialTest::ContainerBlocked(*World,Container,Q));
    TestTrue(TEXT("bridge initially carries the player"),ERIndustrialTest::BridgeSupported(*World,L.WallBridges[0],Q));
    const auto Detached=Map->GetCellWorld().GetDetachedCount();ERIndustrialTest::CutBridgeSupports(*Map,L.WallBridges[0]);ERIndustrialTest::Settle(*Map);
    TestTrue(TEXT("removing timber supports triggers actual detachment"),Map->GetCellWorld().GetDetachedCount()>Detached);
    TestFalse(TEXT("destroyed access loses its walkable bridge"),ERIndustrialTest::BridgeSupported(*World,L.WallBridges[0],Q));
    TestTrue(TEXT("opposite bridge remains available"),ERIndustrialTest::BridgeSupported(*World,L.WallBridges[1],Q));
    TestTrue(TEXT("brick separator initially blocks ground"),ERIndustrialTest::WallBlocked(*World,ERIndustrialTest::BreachPoint(L),Q));
    ERIndustrialTest::BreachWall(*Map);ERIndustrialTest::Settle(*Map);
    TestFalse(TEXT("brick damage opens a new ground route"),ERIndustrialTest::WallBlocked(*World,ERIndustrialTest::BreachPoint(L),Q));
    TestTrue(TEXT("cell state remains valid after route changes"),Map->ValidateCells());
    World->DestroyWorld(false);return true;
}
#endif
