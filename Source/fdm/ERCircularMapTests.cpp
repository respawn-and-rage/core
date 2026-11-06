#include "ERProceduralMap.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ERIndustrialMapTestUtils.h"
#include "Components/DynamicMeshComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCircularCentralTest,"EclipseRecoil.Maps.CircularCentralBuilding",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERCircularCentralTest::RunTest(const FString& Parameters)
{
    auto* T=NewObject<UERMapTemplate>();T->TemplateId=TEXT("test.circular");T->Archetype=EERMapArchetype::CentralBuilding;
    FERProceduralConfig C;C.TemplateAsset=T;FERBlockRoomConfig B;TSet<bool> Modes;TSet<int32> Shapes,Rules;TSet<FIntPoint> Dimensions;TSet<FName> CoverKinds;TSet<FIntVector> CoverDimensions;int32 FiringSites=0,GroundOnlySites=0;
    int32 PoolLayouts=0,SinglePoolLayouts=0,PairedPoolLayouts=0,WaterfrontLayouts=0,RoofPoolLayouts=0,SmallRoofPoolLayouts=0;
    int32 PhysicalRoofSeed=INDEX_NONE,PhysicalSingleSeed=INDEX_NONE,PhysicalPairSeed=INDEX_NONE,PhysicalWaterfrontSeed=INDEX_NONE;
    int32 PhysicalAlphaStairSeed=INDEX_NONE,PhysicalOmegaStairSeed=INDEX_NONE;
    for(int32 TeamSize:{1,2,3,4,8,16})for(int32 Seed=0;Seed<516;++Seed)
    {
        C.ExpectedTeamSize=TeamSize;C.Seed=Seed;FERProceduralLayout L;
        if(!ERProcedural::Build(C,B,L)){AddError(FString::Printf(TEXT("circular layout failed size=%d seed=%d"),TeamSize,Seed));return false;}
        if(L.CentralBuildingSize.X>1500 || L.CentralBuildingSize.Y>2200 ||
            L.BuildingFloors!=(TeamSize==1 ? 2 : TeamSize>=4 ? 4 : 3))
        {AddError(TEXT("villa exceeds its footprint or storey budget"));return false;}
        if(!L.FeatureNames.Contains(FName(TEXT("VillaTerraces"))))
        {AddError(TEXT("upper storey is missing its terraces"));return false;}
        if(!L.FeatureNames.Contains(FName(TEXT("VillaSingleStair"))) ||
           !L.FeatureNames.Contains(FName(TEXT("VillaSlopedGlassGuard"))) ||
           !L.FeatureNames.Contains(FName(TEXT("VillaInteriorRooms"))))
        {AddError(TEXT("villa needs its single stairwell and divided rooms"));return false;}
        const int32 InteriorFlights=L.BuildingFloors-1+int32(L.FeatureNames.Contains(FName(TEXT("RooftopPool"))));
        if(L.StairRoutes.Num()<InteriorFlights)
        {AddError(TEXT("villa stairwell is missing a floor"));return false;}
        int32 StairGlassPanes=0,WoodTableLegs=0;
        for(const auto& Panel:L.Panels)
        {
            if(Panel.Material==EERBlockMaterial::Glass && FMath::IsNearlyEqual(Panel.Size.X,25,.01) &&
               FMath::IsNearlyEqual(Panel.Size.Z,100,.01))
                ++StairGlassPanes;
            if(Panel.Material==EERBlockMaterial::Wood && FMath::IsNearlyEqual(Panel.Size.X,8,.01) &&
               FMath::IsNearlyEqual(Panel.Size.Z,73,.01))
                ++WoodTableLegs;
        }
        if(StairGlassPanes!=InteriorFlights*32 || WoodTableLegs<L.BuildingFloors*8)
        {AddError(FString::Printf(TEXT("stair glass and wood table legs must use destructible cells: glass=%d/%d wood=%d/%d"),
            StairGlassPanes,InteriorFlights*32,WoodTableLegs,L.BuildingFloors*8));return false;}
        const bool AlphaStair=L.StairRoutes[0][0].Y<0;
        if(TeamSize==2)
        {
            int32& PhysicalSeed=AlphaStair ? PhysicalAlphaStairSeed:PhysicalOmegaStairSeed;
            if(PhysicalSeed==INDEX_NONE)PhysicalSeed=Seed;
        }
        for(int32 Flight=0;Flight<InteriorFlights;++Flight)
            if((L.StairRoutes[Flight][0].Y<0)!=AlphaStair ||
               (L.StairRoutes[Flight].Last().Y>0)!=AlphaStair ||
               FMath::Abs(L.StairRoutes[Flight][0].X-L.CentralBuildingCenter.X)>300.f)
            {AddError(TEXT("single central ramp must face the same team on every floor"));return false;}
        if(!L.PoolWater.IsEmpty())
        {
            ++PoolLayouts;
            const bool RoofPool=L.FeatureNames.Contains(FName(TEXT("RooftopPool")));
            if(RoofPool)
            {
                ++RoofPoolLayouts;
                if(TeamSize==1)++SmallRoofPoolLayouts;
                if(TeamSize==2 && PhysicalRoofSeed==INDEX_NONE)PhysicalRoofSeed=Seed;
                const auto& RoofWater=L.PoolWater.Last();
                const float RoofZ=L.CentralBuildingCenter.Z+L.BuildingFloors*320.f;
                const bool InteriorRoofAccess=L.StairRoutes.ContainsByPredicate([&](const TArray<FVector>& Stair)
                {return !Stair.IsEmpty() && FMath::IsNearlyEqual(Stair.Last().Z,RoofZ+100.f,1.f) &&
                    FMath::Abs(Stair.Last().X-L.CentralBuildingCenter.X)<L.CentralBuildingSize.X*.5f;});
                if(!FMath::IsNearlyEqual(RoofWater.Max.Z,RoofZ,.01f) ||
                   RoofWater.GetSize().X>(TeamSize==1 ? 240.f:300.f)+.01f ||
                   RoofWater.GetSize().Y>(TeamSize==1 ? 360.f:500.f)+.01f || !InteriorRoofAccess)
                {AddError(TEXT("rooftop pool must be compact, flush with the roof and stair-accessible"));return false;}
            }
            const int32 GroundPoolCount=L.PoolWater.Num()-(RoofPool ? 1:0);
            const bool Waterfront=L.FeatureNames.Contains(FName(TEXT("VillaWaterfront")));
            if(Waterfront)
            {
                ++WaterfrontLayouts;
                if(TeamSize==2 && PhysicalWaterfrontSeed==INDEX_NONE)PhysicalWaterfrontSeed=Seed;
                if(GroundPoolCount!=9 || RoofPool || (TeamSize>1 && L.CoverSites.Num()<2) ||
                   L.PoolWater[0].Min.X>=-L.ArenaSize.X*.475f ||
                   L.PoolWater[0].Max.X>=L.CentralBuildingCenter.X-L.CentralBuildingSize.X*.5f)
                {AddError(TEXT("waterfront must span the villa shore to the perimeter"));return false;}
            }
            const bool Single=GroundPoolCount==1 && L.PoolWater[0].Max.Z<600 &&
                FMath::IsNearlyZero(L.PoolWater[0].GetCenter().Y,.001);
            const bool Pair= GroundPoolCount==2 && L.PoolWater[0].Max.Z<600 &&
                L.PoolWater[0].GetCenter().Equals(L.PoolWater[1].GetCenter()*FVector(1,-1,1),.001) &&
                L.PoolWater[0].GetSize().Equals(L.PoolWater[1].GetSize(),.001);
            if((GroundPoolCount==0 && !RoofPool) || (GroundPoolCount>0 && !Single && !Pair && !Waterfront))
            {AddError(TEXT("courtyard pool must be central or mirrored"));return false;}
            if(Single){++SinglePoolLayouts;if(TeamSize==3 && PhysicalSingleSeed==INDEX_NONE)PhysicalSingleSeed=Seed;}
            if(Pair){++PairedPoolLayouts;if(TeamSize==2 && PhysicalPairSeed==INDEX_NONE)PhysicalPairSeed=Seed;}
        }
        for(const auto& Name:L.FeatureNames)if(Name.ToString().StartsWith(TEXT("Cover.")) && Name!=FName(TEXT("Cover.FiringSteps")))CoverKinds.Add(Name);
        for(const auto& Site:L.CoverSites)CoverDimensions.Add(FIntVector(FMath::RoundToInt32(Site.Size.X),FMath::RoundToInt32(Site.Size.Y),FMath::RoundToInt32(Site.Size.Z)));
        for(const auto& Site:L.CoverSites)
        {
            if(Site.FiringStair==INDEX_NONE){++GroundOnlySites;continue;}
            ++FiringSites;
            if(!L.StairRoutes.IsValidIndex(Site.FiringStair) || !L.StairRoutes[Site.FiringStair].Last().Equals(Site.FiringPosition,.001)){AddError(TEXT("invalid firing stair route"));return false;}
            if(FMath::Abs(Site.FiringPosition.Z-Site.Frame.GetLocation().Z-160)>.001){AddError(TEXT("firing platform must be 60cm above ground"));return false;}
        }
        Modes.Add(L.CentralHill);Shapes.Add(L.CentralShape);Rules.Add(int32(L.CrossingRule));
        if(TeamSize==3)Dimensions.Add(FIntPoint(FMath::RoundToInt32(L.CentralBuildingSize.X),FMath::RoundToInt32(L.CentralBuildingSize.Y)));
        if(!L.CentralHill && L.SecondaryStructures.Num()<2){AddError(TEXT("lateral hill missing secondary buildings"));return false;}
        if(L.CentralEntrances.Num()!=2 || FVector::DotProduct(L.CentralEntrances[0].GetRotation().GetForwardVector(),L.CentralEntrances[1].GetRotation().GetForwardVector())<.99){AddError(TEXT("main entries must share side-facing direction"));return false;}
        if(L.CrossingRule!=EERCrossingRule::OpenYard && !L.CrossingRoutes.ContainsByPredicate([&](const TArray<FVector>& Route)
            {return Route.ContainsByPredicate([&](const FVector& P){return FMath::Abs(P.Y)<1.f &&
                FMath::Abs(P.X-L.CentralBuildingCenter.X)<L.CentralBuildingSize.X*.5f;});}))
        {AddError(TEXT("central divider must stop at the villa, leaving an interior route"));return false;}
        if(!L.CircularArena || L.LayoutSymmetry!=EERTemplateSymmetry::Mirror || L.AlphaSpawns.Num()!=10 || L.OmegaSpawns.Num()!=10)return false;
        if(L.CrossingRule==EERCrossingRule::ClimbableWall ? L.ArenaSize.Y<L.ArenaSize.X*1.2f :
            !FMath::IsNearlyEqual(L.ArenaSize.X,L.ArenaSize.Y,.01f))return false;
        if(FMath::Abs(ERProcedural::PlayableAreaM2(L)-TeamSize*1500)>TeamSize*150+.01)return false;
        for(int32 I=0;I<10;++I)
        {
            const auto A=L.AlphaSpawns[I].GetLocation(),O=L.OmegaSpawns[I].GetLocation();
            if(!O.Equals({A.X,-A.Y,A.Z},.001) ||
                FMath::Square(A.X/(L.ArenaSize.X*.5-100))+
                FMath::Square(A.Y/(L.ArenaSize.Y*.5-100))>1)return false;
        }
        for(const auto& Route:L.CrossingRoutes)for(const auto P:Route)
            if(FMath::Abs(P.Z-ERProcedural::CentralGroundHeight(L,FVector2D(P))-
                (L.FeatureNames.Contains(FName(TEXT("VillaWaterfront"))) ? 120.f:100.f))>.001)return false;
    }
    TestTrue(TEXT("some layouts have courtyard pools"),PoolLayouts>0);
    TestTrue(TEXT("some layouts have one central pool"),SinglePoolLayouts>0);
    TestTrue(TEXT("some layouts retain mirrored courtyard pools"),PairedPoolLayouts>0);
    TestTrue(TEXT("some layouts have a perimeter waterfront"),WaterfrontLayouts>0);
    TestTrue(TEXT("some 2v2 layouts have a physical waterfront probe"),PhysicalWaterfrontSeed!=INDEX_NONE);
    TestTrue(TEXT("some open layouts have rooftop pools"),RoofPoolLayouts>0);
    TestTrue(TEXT("some 1v1 layouts have rooftop pools"),SmallRoofPoolLayouts>0);
    TestTrue(TEXT("some 2v2 layouts have a physical rooftop pool probe"),PhysicalRoofSeed!=INDEX_NONE);
    TestTrue(TEXT("both teams can receive the lower stair approach"),
        PhysicalAlphaStairSeed!=INDEX_NONE && PhysicalOmegaStairSeed!=INDEX_NONE);
    TestTrue(TEXT("some cover has firing steps"),FiringSites>0);
    TestTrue(TEXT("some cover stays at ground level"),GroundOnlySites>0);
    TestEqual(TEXT("five cover profiles sampled"),CoverKinds.Num(),5);
    TestTrue(TEXT("cover dimensions vary"),CoverDimensions.Num()>8);
    TestEqual(TEXT("both hill configurations sampled"),Modes.Num(),2);
    TestEqual(TEXT("four building profiles sampled"),Shapes.Num(),4);
    TestEqual(TEXT("three crossing rules sampled"),Rules.Num(),3);
    TestTrue(TEXT("3v3 building dimensions vary"),Dimensions.Num()>10);
    UWorld::InitializationValues Values;Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);if(!World)return false;
    auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->LayoutConfig.ExpectedTeamSize=2;Map->bCeilings=false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ERCircularTerrain),false);FHitResult Hit;const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);
    TArray<FIntPoint> PhysicalCases{{2,42},{2,73},{2,7919},{2,1},{1,11}};
    if(PhysicalAlphaStairSeed!=INDEX_NONE)PhysicalCases.AddUnique({2,PhysicalAlphaStairSeed});
    if(PhysicalOmegaStairSeed!=INDEX_NONE)PhysicalCases.AddUnique({2,PhysicalOmegaStairSeed});
    if(PhysicalRoofSeed!=INDEX_NONE)PhysicalCases.AddUnique({2,PhysicalRoofSeed});
    if(PhysicalSingleSeed!=INDEX_NONE)PhysicalCases.AddUnique({3,PhysicalSingleSeed});
    if(PhysicalPairSeed!=INDEX_NONE)PhysicalCases.AddUnique({2,PhysicalPairSeed});
    if(PhysicalWaterfrontSeed!=INDEX_NONE)PhysicalCases.AddUnique({2,PhysicalWaterfrontSeed});
    for(const FIntPoint& Case:PhysicalCases)
    {
        Map->LayoutConfig.ExpectedTeamSize=Case.X;
        const int32 Seed=Case.Y;
        Map->LayoutConfig.Seed=Seed;
        if(!TestTrue(TEXT("physical circular map generated"),Map->Regenerate()))continue;
        const auto& L=Map->GetLayout();TestTrue(TEXT("valid cell world"),Map->ValidateCells());
        Map->RequestSupportCheck();ERIndustrialTest::Settle(*Map);TestEqual(TEXT("intact building remains supported"),Map->GetCellWorld().GetDetachedCount(),int64(0));
        for(const auto& Entrance:L.CentralEntrances)
        {
            const FVector FloorPoint=Entrance.GetLocation()+FVector(100,0,0);
            TestTrue(TEXT("villa ground floor supports a pawn"),World->LineTraceSingleByChannel(
                Hit,FloorPoint+FVector(0,0,100),FloorPoint-FVector(0,0,20),ECC_WorldStatic,Q));
            TestTrue(TEXT("villa floor is above terrain at the threshold"),FMath::IsNearlyEqual(Hit.ImpactPoint.Z,FloorPoint.Z,.1));
        }
        const auto& Panels=Map->GetCellWorld().GetPanels();
        const auto* Pane=Panels.FindByPredicate([](const auto& Panel)
        {
            return Panel.Material==EERBlockMaterial::Glass && Panel.Size.Z>=230;
        });
        if(TestNotNull(TEXT("panoramic opening has a glass pane"),Pane))
        {
            const FVector Center=Pane->Transform.TransformPosition(Pane->Size*.5);
            const FVector Across=Pane->Transform.GetUnitAxis(EAxis::Y)*30;
            FERCellId GlassCell;
            TestTrue(TEXT("intact glazing intercepts projectiles"),
                World->LineTraceSingleByChannel(Hit,Center-Across,Center+Across,ECC_GameTraceChannel1,Q) &&
                Map->ResolveCell(Hit,GlassCell) && Panels.IsValidIndex(GlassCell.Panel) && &Panels[GlassCell.Panel]==Pane);
        }
        for(const auto* Starts:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& S:*Starts)
        {
            TestFalse(TEXT("spawn capsule clear"),World->OverlapBlockingTestByChannel(S.GetLocation(),FQuat::Identity,ECC_Pawn,Capsule,Q));
            TestTrue(TEXT("spawn supported"),World->LineTraceSingleByChannel(Hit,S.GetLocation(),S.GetLocation()-FVector(0,0,120),ECC_WorldStatic,Q));
        }
        for(const auto& Site:L.CoverSites)
        {
            const bool Blocked=World->SweepSingleByChannel(Hit,Site.Entry,Site.Hide,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,Site.HalfHeight),Q);
            if(Blocked)AddError(FString::Printf(TEXT("cover access seed=%d from=%s to=%s hit=%s"),Seed,*Site.Entry.ToString(),*Site.Hide.ToString(),*GetNameSafe(Hit.GetComponent())));
            TestFalse(TEXT("cover pocket accessible"),Blocked);
            TestTrue(TEXT("cover hides crouching or standing eye"),World->LineTraceSingleByChannel(Hit,Site.SightFrom,Site.SightTo,ECC_GameTraceChannel1,Q));
            if(Site.FiringStair!=INDEX_NONE)
            {
                TestFalse(TEXT("firing stance capsule clear"),World->OverlapBlockingTestByChannel(Site.FiringPosition,FQuat::Identity,ECC_Pawn,Capsule,Q));
                TestTrue(TEXT("firing platform supports capsule"),World->LineTraceSingleByChannel(Hit,Site.FiringPosition,Site.FiringPosition-FVector(0,0,120),ECC_WorldStatic,Q));
                const FVector FiringEye=Site.FiringPosition+FVector(0,0,72.5);
                TestFalse(TEXT("firing sight clears cover"),World->LineTraceSingleByChannel(Hit,FiringEye,FiringEye+Site.Frame.GetUnitAxis(EAxis::Y)*160,ECC_GameTraceChannel1,Q));
            }
        }
        for(const auto& Path:L.CrossingRoutes)for(int32 I=1;I<Path.Num();++I)
            if(World->SweepSingleByChannel(Hit,Path[I-1],Path[I],FQuat::Identity,ECC_Pawn,Capsule,Q)){AddError(FString::Printf(TEXT("circular crossing obstructed seed=%d at %s by %s"),Seed,*Path[I].ToString(),*GetNameSafe(Hit.GetComponent())));break;}
        for(const auto& Path:L.RoomAccessRoutes)
        {
            // Test the upper storey on its level deck. Ground routes follow
            // the terrain and are swept separately at their sampled heights.
            if(Path[0].Z<L.CentralBuildingCenter.Z+400)continue;
            if(World->SweepSingleByChannel(Hit,Path[0],Path.Last(),FQuat::Identity,ECC_Pawn,Capsule,Q))
                AddError(FString::Printf(TEXT("villa upper door obstructed seed=%d from=%s to=%s hit=%s"),
                    Seed,*Path[0].ToString(),*Path.Last().ToString(),*GetNameSafe(Hit.GetComponent())));
        }
        for(const auto& Bounds:L.PoolWater)
        {
            const FVector Surface=Bounds.GetCenter();
            if(Surface.Z>600)
            {
                TestEqual(TEXT("roof pool does not wet the living floor below"),
                    Map->WaterDepthAt(Surface-FVector(0,0,320)),0.f);
                FVector SpuriousSurface;
                TestFalse(TEXT("indoor impact below roof pool is dry"),
                    Map->WaterSurfaceAt(Surface-FVector(0,0,110),SpuriousSurface));
                TestTrue(TEXT("recessed roof basin has a solid floor"),World->LineTraceSingleByChannel(
                    Hit,Surface+FVector(0,0,40),Surface-FVector(0,0,130),ECC_WorldStatic,Q));
                TestTrue(TEXT("roof basin floor is 100cm below flush water"),FMath::IsNearlyEqual(Hit.ImpactPoint.Z,Surface.Z-100,.1));
                TestEqual(TEXT("roof pool reaches full wading depth"),Map->WaterDepthAt(Surface-FVector(0,0,100)),100.f);
                const FVector Corner=Surface+FVector(-Bounds.GetExtent().X+15,-Bounds.GetExtent().Y+30,0);
                TestTrue(TEXT("square corner step supports entry"),World->LineTraceSingleByChannel(
                    Hit,Corner+FVector(0,0,40),Corner-FVector(0,0,100),ECC_WorldStatic,Q));
                TestTrue(TEXT("upper corner step is flush with roof"),FMath::IsNearlyEqual(Hit.ImpactPoint.Z,Surface.Z,.1));
                const FVector DeepStep=Corner+FVector(120,0,0);
                TestTrue(TEXT("deep corner step supports exit"),World->LineTraceSingleByChannel(
                    Hit,DeepStep+FVector(0,0,40),DeepStep-FVector(0,0,120),ECC_WorldStatic,Q));
                TestTrue(TEXT("deep corner step is 80cm below water"),FMath::IsNearlyEqual(Hit.ImpactPoint.Z,Surface.Z-80,.1));
                continue;
            }
            if(L.FeatureNames.Contains(FName(TEXT("VillaWaterfront"))))
            {
                TestTrue(TEXT("waterfront has a solid deep floor"),World->LineTraceSingleByChannel(
                    Hit,Surface+FVector(0,0,50),Surface-FVector(0,0,120),ECC_WorldStatic,Q));
                TestTrue(TEXT("waterfront floor is 90cm below water"),FMath::IsNearlyEqual(Hit.ImpactPoint.Z,Surface.Z-90,.1));
                const FVector Shallow(Bounds.Max.X-50,Surface.Y,Surface.Z);
                TestTrue(TEXT("waterfront has a shallow entry"),World->LineTraceSingleByChannel(
                    Hit,Shallow+FVector(0,0,50),Shallow-FVector(0,0,120),ECC_WorldStatic,Q));
                TestTrue(TEXT("waterfront entry is 20cm below water"),FMath::IsNearlyEqual(Hit.ImpactPoint.Z,Surface.Z-20,.1));
                continue;
            }
            TestEqual(TEXT("space below the ground basin is dry"),
                Map->WaterDepthAt(Surface-FVector(0,0,120)),0.f);
            TestTrue(TEXT("pool has a solid floor"),World->LineTraceSingleByChannel(
                Hit,Surface+FVector(0,0,50),Surface-FVector(0,0,120),ECC_WorldStatic,Q));
            TestTrue(TEXT("deep pool floor is 90cm below water"),FMath::IsNearlyEqual(Hit.ImpactPoint.Z,Surface.Z-90,.1));
            const float ShallowY=L.FeatureNames.Contains(FName(TEXT("VillaPoolSingle"))) ? 600.f:300.f;
            for(float Side:{-1.f,1.f})
            {
                const FVector Shallow=Surface+FVector(0,Side*ShallowY,0);
                TestTrue(TEXT("shallow pool floor supports a pawn"),World->LineTraceSingleByChannel(
                    Hit,Shallow+FVector(0,0,50),Shallow-FVector(0,0,120),ECC_WorldStatic,Q));
                TestTrue(TEXT("shallow pool floor is 20cm below water"),FMath::IsNearlyEqual(Hit.ImpactPoint.Z,Surface.Z-20,.1));
            }
        }
        UDynamicMeshComponent* TerrainComponent=nullptr;
        TArray<UDynamicMeshComponent*> TerrainCandidates;Map->GetComponents(TerrainCandidates);
        for(auto* Candidate:TerrainCandidates)if(Candidate->GetFName()==FName(TEXT("AlleyTerrain")))TerrainComponent=Candidate;
        if(!TestNotNull(TEXT("terrain collision mesh"),TerrainComponent))continue;
        for(int32 I=0;I<32;++I)
        {
            const FVector2D P(L.ArenaSize.X*.4*FMath::Cos(I*.7),L.ArenaSize.Y*.4*FMath::Sin(I*.7));
            const float Height=ERProcedural::CentralGroundHeight(L,P);
            TestTrue(TEXT("terrain triangles reflect exactly"),FMath::Abs(Height-ERProcedural::CentralGroundHeight(L,{P.X,-P.Y}))<.001);
            if(TerrainComponent->LineTraceComponent(Hit,FVector(P,Height+10),FVector(P,Height-10),Q))
                TestTrue(TEXT("terrain sampler matches actual collision triangles"),FMath::Abs(Hit.ImpactPoint.Z-Height)<.1);
            else AddError(TEXT("missing terrain collision"));
        }
        if(Case==FIntPoint(2,42))
        {
            const auto& CellPanels=Map->GetCellWorld().GetPanels();
            int32 GlassIndex=INDEX_NONE,WoodIndex=INDEX_NONE;
            for(int32 I=0;I<CellPanels.Num();++I)
            {
                if(GlassIndex==INDEX_NONE && CellPanels[I].Material==EERBlockMaterial::Glass &&
                   FMath::IsNearlyEqual(CellPanels[I].Size.X,25,.01) &&
                   FMath::IsNearlyEqual(CellPanels[I].Size.Z,100,.01))GlassIndex=I;
                if(WoodIndex==INDEX_NONE && CellPanels[I].Material==EERBlockMaterial::Wood &&
                   FMath::IsNearlyEqual(CellPanels[I].Size.X,8,.01) &&
                   FMath::IsNearlyEqual(CellPanels[I].Size.Z,73,.01))WoodIndex=I;
            }
            if(GlassIndex==INDEX_NONE || WoodIndex==INDEX_NONE)
            {AddError(TEXT("physical villa lacks destructible stair glass or table legs"));continue;}
            const FERCellId GlassCell{GlassIndex,0,0,0},WoodCell{WoodIndex,0,0,0};
            TMap<FERCellId,float> Damage;Damage.Add(GlassCell,1000);Damage.Add(WoodCell,1000);
            Map->ApplyCellBatch(Damage);
            TestFalse(TEXT("stair glass cell breaks"),Map->GetCellWorld().IsAlive(GlassCell));
            TestFalse(TEXT("wood furniture cell breaks"),Map->GetCellWorld().IsAlive(WoodCell));
        }
    }
    World->DestroyWorld(false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERTemplateMirrorTest,"EclipseRecoil.Maps.TemplateMirror",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERTemplateMirrorTest::RunTest(const FString& Parameters)
{
    for(const FString& Name:{TEXT("CentralBuilding"),TEXT("MiddleEasternAlleys"),TEXT("FireTraining"),TEXT("Rooms"),TEXT("Village")})
    {
        const FString Path=FString::Printf(TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_%s.DA_%s"),*Name,*Name);
        auto* Asset=LoadObject<UERMapTemplate>(nullptr,*Path);if(!TestNotNull(*Name,Asset))continue;
        auto* T=DuplicateObject<UERMapTemplate>(Asset,GetTransientPackage());T->Symmetry=EERTemplateSymmetry::Mirror;
        FERProceduralConfig C;C.TemplateAsset=T;C.ExpectedTeamSize=2;
        UWorld::InitializationValues Values;Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
        auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);if(!World)return false;
        auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig=C;Map->bCeilings=false;
        if(TestTrue(*(Name+TEXT(" physical mirror")),Map->Regenerate()))
        {
            TestTrue(TEXT("reflected cell grids are valid"),Map->ValidateCells());
            const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);FCollisionQueryParams Q(SCENE_QUERY_STAT(ERTemplateMirror),false);FHitResult Hit;
            for(const auto* Starts:{&Map->GetLayout().AlphaSpawns,&Map->GetLayout().OmegaSpawns})for(const auto& Start:*Starts)
            {
                TestFalse(*(Name+TEXT(" reflected spawn clear")),World->OverlapBlockingTestByChannel(Start.GetLocation(),FQuat::Identity,ECC_Pawn,Capsule,Q));
                TestTrue(*(Name+TEXT(" reflected spawn supported")),World->LineTraceSingleByChannel(Hit,Start.GetLocation(),Start.GetLocation()-FVector(0,0,125),ECC_WorldStatic,Q));
            }
        }
        World->DestroyWorld(false);
        for(int32 Seed:{42,73,7919,1})
        {
            C.Seed=Seed;FERProceduralLayout L;
            if(!TestTrue(*FString::Printf(TEXT("%s mirror seed=%d"),*Name,Seed),ERProcedural::Build(C,FERBlockRoomConfig(),L)))continue;
            TestEqual(TEXT("reflection mode retained"),L.LayoutSymmetry,EERTemplateSymmetry::Mirror);
            for(int32 I=0;I<L.AlphaSpawns.Num();++I)
            {const auto A=L.AlphaSpawns[I].GetLocation();TestTrue(TEXT("teams reflect across Y=0"),L.OmegaSpawns[I].GetLocation().Equals({A.X,-A.Y,A.Z},.001));}
        }
    }
    return true;
}
#endif
