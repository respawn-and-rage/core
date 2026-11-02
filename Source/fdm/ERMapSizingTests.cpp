#include "ERMapPlayability.h"
#include "ERProceduralMap.h"
#include "ERServerSettings.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
TArray<FString> Templates()
{
    FString Filter;
    if(FParse::Value(FCommandLine::Get(),TEXT("ERMapPlanTemplates="),Filter,false))
    {TArray<FString> Ids;Filter.ParseIntoArray(Ids,TEXT(","));return Ids;}
    return {TEXT("CentralBuilding"),TEXT("MiddleEasternAlleys"),TEXT("FireTraining"),TEXT("Rooms"),TEXT("Village")};
}
double RoomFloorAreaM2(const FERProceduralLayout& L)
{
    double Area=0;
    for(const auto& V:L.RoomNodes)
    {
        double Twice=0;for(int32 I=0;I<V.Polygon.Num();++I)Twice+=FVector2D::CrossProduct(V.Polygon[I],V.Polygon[(I+1)%V.Polygon.Num()]);
        Area+=FMath::Abs(Twice)*.5;
    }
    for(const auto& E:L.RoomLinks)Area+=FVector::Dist2D(E.Start,E.End)*(E.Width-2*E.Cut);
    return Area/10000;
}
bool IndependentRoomBaseAccess(const FERProceduralLayout& L)
{
    auto BaseFor=[&](FVector Start)
    {
        int32 Result=INDEX_NONE;double Best=TNumericLimits<double>::Max();
        for(int32 I=0;I<L.RoomNodes.Num();++I)if(L.RoomNodes[I].Base)
        {const double D=FVector::DistSquared(Start,L.RoomNodes[I].Center);if(D<Best){Best=D;Result=I;}}
        return Result;
    };
    if(L.AlphaSpawns.IsEmpty() || L.OmegaSpawns.IsEmpty())return false;
    const int32 A=BaseFor(L.AlphaSpawns[0].GetLocation()),B=BaseFor(L.OmegaSpawns[0].GetLocation());
    if(A==INDEX_NONE || B==INDEX_NONE || A==B)return false;
    if(L.Rooms.Num()==4)
    {
        const auto P=L.RoomNodes[A].Center,Q=L.RoomNodes[B].Center;
        if(P.X>=0 || P.Y>=0 || Q.X<=0 || Q.Y<=0 || L.RoomNodes.Num()!=4)return false;
        if(RoomFloorAreaM2(L)<L.ArenaSize.X*L.ArenaSize.Y/10000*.55-.01)return false;
        const bool Linked=L.RoomLinks.ContainsByPredicate([](const auto& E){return (E.A==2 && E.B==3) || (E.A==3 && E.B==2);});
        for(int32 I=0;I<L.RoomNodes.Num();++I)
        {
            int32 Degree=0;for(const auto& E:L.RoomLinks)Degree+=E.A==I || E.B==I;
            if(Degree!=(L.RoomNodes[I].Base ? 2:Linked ? 3:2))return false;
            if(!L.RoomNodes[I].Base && L.RoomNodes[I].Size.X*L.RoomNodes[I].Size.Y<2*L.RoomNodes[A].Size.X*L.RoomNodes[A].Size.Y)return false;
        }
    }
    for(int32 Base:{A,B})
    {
        TArray<FVector> Mouths;
        for(const auto& E:L.RoomLinks)if(E.A==Base || E.B==Base)Mouths.Add(E.A==Base ? E.Start:E.End);
        bool Separated=false;for(const auto& P:Mouths)for(const auto& Q:Mouths)Separated|=FVector::Dist(P,Q)>=500;
        if(!Separated)return false;
    }
    // No single corridor edge may disconnect the teams' bases.
    for(int32 Removed=0;Removed<L.RoomLinks.Num();++Removed)
    {
        TSet<int32> Reached{A};TArray<int32> Queue{A};
        for(int32 I=0;I<Queue.Num();++I)for(int32 J=0;J<L.RoomLinks.Num();++J)if(J!=Removed)
        {
            const auto& E=L.RoomLinks[J];const int32 Next=E.A==Queue[I] ? E.B:E.B==Queue[I] ? E.A:INDEX_NONE;
            if(Next!=INDEX_NONE && !Reached.Contains(Next)){Reached.Add(Next);Queue.Add(Next);}
        }
        if(!Reached.Contains(B))return false;
    }
    return true;
}
TSharedPtr<FJsonValue> Vector(FVector P)
{
    return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(P.X/100),MakeShared<FJsonValueNumber>(P.Y/100),MakeShared<FJsonValueNumber>(P.Z/100)});
}
TSharedPtr<FJsonValue> Points(const TArray<FVector>& Path)
{
    TArray<TSharedPtr<FJsonValue>> A;for(auto P:Path)A.Add(Vector(P));return MakeShared<FJsonValueArray>(A);
}
TSharedPtr<FJsonObject> Plan(const FERProceduralLayout& L,const FERProceduralConfig& C,const FString& Id,int32 TeamSize,int32 Seed)
{
    auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("template"),Id);O->SetNumberField(TEXT("team_size"),TeamSize);O->SetNumberField(TEXT("seed"),Seed);
    O->SetNumberField(TEXT("target_area_m2"),ERProcedural::TargetAreaM2(TeamSize));O->SetNumberField(TEXT("area_m2"),ERProcedural::PlayableAreaM2(L));O->SetNumberField(TEXT("area_tolerance_percent"),10);
    O->SetBoolField(TEXT("circular"),L.CircularArena);O->SetBoolField(TEXT("central_hill"),L.CentralHill);
    O->SetField(TEXT("hill_center"),Vector(L.HillCenter));O->SetNumberField(TEXT("hill_height_m"),L.HillHeight/100);
    O->SetField(TEXT("building_center"),Vector(L.CentralBuildingCenter));
    O->SetField(TEXT("dimensions_m"),Vector(L.ArenaSize));O->SetStringField(TEXT("theme"),L.ThemeName.ToString());
    O->SetNumberField(TEXT("building_floors"),L.BuildingFloors);O->SetNumberField(TEXT("blocks"),L.VillageBlocks.Num());
    int32 Storeys=0;for(const auto& P:L.VillageBlocks)Storeys=FMath::Max(Storeys,P.Storeys);
    O->SetNumberField(TEXT("storeys"),Storeys);O->SetBoolField(TEXT("upper_crossing"),!L.RoomNodes.IsEmpty() && L.RoomNodes.ContainsByPredicate([](const auto& V){return V.Upper;}));
    O->SetNumberField(TEXT("ground_cover_pockets"),L.VillageBlocks.IsEmpty() ? 0:L.LowCovers.Num());
    if(L.Rooms.Num()==4 && L.RoomNodes.Num()==4)
    {
        O->SetNumberField(TEXT("ground_floor_area_m2"),RoomFloorAreaM2(L));
        O->SetBoolField(TEXT("hall_connection"),L.FeatureNames.Contains(TEXT("HallPassage")));
        O->SetStringField(TEXT("hall_shape"),L.FeatureNames.Contains(TEXT("IrregularHalls")) ? TEXT("irregular"):L.FeatureNames.Contains(TEXT("CompactHalls")) ? TEXT("compact"):TEXT("rectangular"));
        TArray<TSharedPtr<FJsonValue>> Details;
        for(const auto& V:L.RoomNodes)
        {
            auto D=MakeShared<FJsonObject>();D->SetBoolField(TEXT("base"),V.Base);D->SetField(TEXT("center"),Vector(V.Center));D->SetField(TEXT("size_m"),Vector(FVector(V.Size,0)));Details.Add(MakeShared<FJsonValueObject>(D));
        }
        O->SetArrayField(TEXT("room_details"),Details);
    }
    O->SetNumberField(TEXT("naves"),L.WarehouseNaves);O->SetNumberField(TEXT("bays"),L.WarehouseBays);O->SetNumberField(TEXT("rooms"),L.Rooms.Num());
    TArray<TSharedPtr<FJsonValue>> Solids,Floors,Routes,Spawns,Shell;
    auto Box=[&](const FTransform& Frame,FVector Min,FVector Max,int32 Material)
    {
        const FBox Bounds=FBox(Min,Max).TransformBy(Frame);if(Bounds.GetSize().Z<80)return;
        TArray<FVector> Corners;for(int32 Z=0;Z<2;++Z)for(int32 Y=0;Y<2;++Y)for(int32 X=0;X<2;++X)
            Corners.Add(Frame.TransformPosition({X ? Max.X:Min.X,Y ? Max.Y:Min.Y,Z ? Max.Z:Min.Z}));
        auto S=MakeShared<FJsonObject>();S->SetField(TEXT("corners"),Points(Corners));S->SetNumberField(TEXT("material"),Material);Solids.Add(MakeShared<FJsonValueObject>(S));
    };
    for(const auto& P:L.Panels)
    {
        // Rectangular holes split the solid in local X/Z, so doors really appear
        // open on the plan. Do not paint white holes over other constructions.
        TArray<double> X{0,P.Size.X},Z{0,P.Size.Z};
        for(const auto& H:P.Holes){X.AddUnique(FMath::Clamp(H.Min.X,0.,P.Size.X));X.AddUnique(FMath::Clamp(H.Max.X,0.,P.Size.X));Z.AddUnique(FMath::Clamp(H.Min.Y,0.,P.Size.Z));Z.AddUnique(FMath::Clamp(H.Max.Y,0.,P.Size.Z));}
        X.Sort();Z.Sort();
        for(int32 I=1;I<X.Num();++I)for(int32 J=1;J<Z.Num();++J)
        {
            const FVector2D Mid((X[I-1]+X[I])*.5,(Z[J-1]+Z[J])*.5);bool Hole=false;for(const auto& H:P.Holes)Hole|=H.IsInside(Mid);
            if(!Hole)Box(P.Transform,{X[I-1],0,Z[J-1]},{X[I],P.Size.Y,Z[J]},uint8(P.Material));
        }
    }
    for(const auto& P:L.FixedPieces)if(!P.VisualOnly && !P.CollisionOnly)
        Box(FTransform(P.Transform.GetRotation(),P.Transform.GetLocation()),-P.Transform.GetScale3D()*50,P.Transform.GetScale3D()*50,uint8(P.Material));
    auto Floor=[&](const TArray<FVector>& P){Floors.Add(Points(P));};
    for(const auto& R:L.RoomNodes)
    {TArray<FVector> P;for(auto V:R.Polygon)P.Add(R.Center+FVector(V,0));Floor(P);}
    for(const auto& E:L.RoomLinks)
    {
        const FVector Across=FVector::CrossProduct((E.End-E.Start).GetSafeNormal2D(),FVector::UpVector)*E.Width*.5;
        Floor({E.Start+Across,E.End+Across,E.End-Across,E.Start-Across});
    }
    for(const auto& B:L.TownBuildings)Floor({{B.Min.X,B.Min.Y,B.Min.Z},{B.Max.X,B.Min.Y,B.Min.Z},{B.Max.X,B.Max.Y,B.Min.Z},{B.Min.X,B.Max.Y,B.Min.Z}});
    auto Paths=[&](const TArray<TArray<FVector>>& Set,const TCHAR* Kind)
    {for(const auto& Path:Set)if(Path.Num()>1){auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("kind"),Kind);R->SetField(TEXT("points"),Points(Path));Routes.Add(MakeShared<FJsonValueObject>(R));}};
    Paths(L.CrossingRoutes,TEXT("crossing"));Paths(L.DistrictRoutes,TEXT("crossing"));Paths(L.VillagePassages,TEXT("passage"));Paths(L.VillageSideBypasses,TEXT("flank"));
    Paths(L.RooftopRoutes,TEXT("upper"));Paths(L.StairRoutes,TEXT("stairs"));
    if(L.Rooms.Num()==4 && L.FeatureNames.Contains(TEXT("HallPassage")))Paths({L.RoomAccessRoutes.Last()},TEXT("connector"));
    if(!L.UnderpassRoute.IsEmpty())Paths({L.UnderpassRoute},TEXT("lower"));
    for(int32 Team=0;Team<2;++Team)for(const auto& S:Team ? L.OmegaSpawns:L.AlphaSpawns)
    {auto P=MakeShared<FJsonObject>();P->SetNumberField(TEXT("team"),Team);P->SetField(TEXT("point"),Vector(S.GetLocation()));Spawns.Add(MakeShared<FJsonValueObject>(P));}
    for(const auto& F:L.RoomShell)
    {
        const FVector Normal=FVector::CrossProduct(F.B-F.A,F.C-F.A).GetSafeNormal();
        if(!F.Roof && FMath::Abs(Normal.Z)<.7)Shell.Add(Points({F.A,F.B,F.C,F.D}));
    }
    // Same layout-stage report as -ERMapPlayability, for preview-map-sizing.py --playability.
    const auto Report=ERPlayability::Analyze(L,C,ERPlayability::PolicyFor(C.TemplateAsset->Archetype,TeamSize));
    TArray<TSharedPtr<FJsonValue>> Edges,Chokes,Sight;
    for(const auto& E:Report.Graph.Edges)Edges.Add(Points({Report.Graph.Nodes[E.A].Position,Report.Graph.Nodes[E.B].Position}));
    for(const auto& V:Report.Find(TEXT("BASE_EXIT_PATHS"))->Violations)for(const auto& Q:V.Positions)Chokes.Add(Vector(Q));
    for(const auto& V:Report.Find(TEXT("INITIAL_SPAWN_LOS"))->Violations)Sight.Add(Points(V.Positions));
    auto Tactical=MakeShared<FJsonObject>();Tactical->SetArrayField(TEXT("edges"),Edges);Tactical->SetArrayField(TEXT("chokes"),Chokes);Tactical->SetArrayField(TEXT("sight"),Sight);
    O->SetObjectField(TEXT("tactical"),Tactical);
    O->SetArrayField(TEXT("shell"),Shell);
    O->SetArrayField(TEXT("solids"),Solids);O->SetArrayField(TEXT("floors"),Floors);O->SetArrayField(TEXT("routes"),Routes);O->SetArrayField(TEXT("spawns"),Spawns);
    return O;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERMapSizingTest,"EclipseRecoil.Maps.TeamSizing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERMapSizingTest::RunTest(const FString&)
{
    int32 TeamSize=2,Count=32;FParse::Value(FCommandLine::Get(),TEXT("ERMapPlanTeamSize="),TeamSize);FParse::Value(FCommandLine::Get(),TEXT("ERMapPlanVerifySeeds="),Count);
    FString SeedsString=TEXT("42,73,7919");FParse::Value(FCommandLine::Get(),TEXT("ERMapPlanSeeds="),SeedsString,false);
    TArray<FString> SeedWords;SeedsString.ParseIntoArray(SeedWords,TEXT(","));TArray<int32> Seeds;for(const auto& S:SeedWords)Seeds.Add(FCString::Atoi(*S));
    if(!TestTrue(TEXT("supported design size"),TeamSize>=1 && TeamSize<=16) || !TestTrue(TEXT("bounded seed count"),Count>=0 && Count<=4096))return false;
    const auto Ids=Templates();
    TArray<TSharedPtr<FJsonValue>> Plans;
    for(const auto& Id:Ids)
    {
        auto* T=LoadObject<UERMapTemplate>(nullptr,*(TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_")+Id));if(!TestNotNull(*Id,T))return false;
        const auto OriginalCentral=T->CentralBuilding.ArenaMin,OriginalTown=T->Town.ArenaMin;const auto OriginalBlocks=T->Village.MaxCentralBlocks;
        FERProceduralConfig C;C.TemplateAsset=T;C.ExpectedTeamSize=TeamSize;FERBlockRoomConfig B;FERProceduralLayout L,Again;
        TArray<int32> Cases=Seeds;for(int32 I=0;I<Count;++I)Cases.AddUnique(I);Cases.Append({-17,INT32_MIN,INT32_MAX});
        TSet<int32> RoomCounts,BlockCounts;TSet<FIntPoint> Bounds;
        for(int32 Seed:Cases)
        {
            C.Seed=Seed;
            if(!TestTrue(*FString::Printf(TEXT("%s %dv%d seed%d builds valid geometry"),*Id,TeamSize,TeamSize,Seed),ERProcedural::Build(C,B,L)))return false;
            TestTrue(TEXT("actual arena dimensions in metres are positive and finite"),!L.ArenaSize.ContainsNaN() && L.ArenaSize.GetMin()>0);
            TestTrue(TEXT("750 square metres per expected player, within ten percent"),FMath::Abs(ERProcedural::PlayableAreaM2(L)-ERProcedural::TargetAreaM2(TeamSize))<=ERProcedural::TargetAreaM2(TeamSize)*.1+.01);
            RoomCounts.Add(L.Rooms.Num());BlockCounts.Add(L.VillageBlocks.Num());Bounds.Add({FMath::RoundToInt32(L.ArenaSize.X),FMath::RoundToInt32(L.ArenaSize.Y)});
            TestTrue(TEXT("starts remain independent of expected team size"),L.AlphaSpawns.Num()>=8 && L.OmegaSpawns.Num()==L.AlphaSpawns.Num());
            for(int32 I=0;I<L.AlphaSpawns.Num();++I)
            {auto P=L.AlphaSpawns[I].GetLocation();if(L.LayoutSymmetry!=EERTemplateSymmetry::Mirror)P.X=-P.X;P.Y=-P.Y;TestTrue(TEXT("equal paired spawn distances"),P.Equals(L.OmegaSpawns[I].GetLocation(),.02));}
            for(const auto& Node:L.RoomNodes)
                TestTrue(TEXT("room footprints remain inside measured arena"),FMath::Abs(Node.Center.X)+Node.Size.X*.5+20<=L.ArenaSize.X*.5 && FMath::Abs(Node.Center.Y)+Node.Size.Y*.5+20<=L.ArenaSize.Y*.5);
            if(L.UnderpassAxis==1)for(int32 I=1;I<L.UnderpassRoute.Num();++I)
                TestTrue(TEXT("underground approach never folds back into ramp"),L.UnderpassRoute[I].Y>=L.UnderpassRoute[I-1].Y-.01);
            if(Id==TEXT("CentralBuilding"))TestTrue(TEXT("central arena is near square"),FMath::Max(L.ArenaSize.X,L.ArenaSize.Y)<=FMath::Min(L.ArenaSize.X,L.ArenaSize.Y)*1.1f);
            if(Id==TEXT("FireTraining") && TeamSize<=3)TestEqual(TEXT("through3vs3 the warehouse has one nave"),L.WarehouseNaves,1);
            if(Id==TEXT("FireTraining"))TestTrue(TEXT("warehouse length always exceeds width by at least twenty percent"),L.ArenaSize.Y>=L.ArenaSize.X*1.2f);
            if(Id==TEXT("Rooms"))TestTrue(TEXT("bases have separate mouths and no single-corridor choke"),IndependentRoomBaseAccess(L));
            if(Id==TEXT("Village") && TeamSize>=2)
            {
                if(TeamSize>=3)TestTrue(TEXT("village block count grows with ground area"),L.VillageBlocks.Num()>=FMath::Clamp((TeamSize+1)/2,2,5));
                TestTrue(TEXT("village has distributed paired ground shelters"),L.LowCovers.Num()>=2*FMath::CeilToInt(L.ArenaSize.X*L.ArenaSize.Y/15000000));
                for(int32 I=0;I<L.LowCovers.Num();I+=2)TestTrue(TEXT("ground shelter bounds are half-turn pairs"),L.LowCovers[I].TransformBy(FTransform(FRotator(0,180,0))).GetCenter().Equals(L.LowCovers[I+1].GetCenter(),.02));
            }
            if(TeamSize<=2)
            {
                TestTrue(TEXT("compact central building has one floor"),Id!=TEXT("CentralBuilding") || L.BuildingFloors==1);
                TestTrue(TEXT("compact alleys have two approaches"),Id!=TEXT("MiddleEasternAlleys") || L.DistrictAccesses==2);
                TestTrue(TEXT("compact warehouse has one nave"),Id!=TEXT("FireTraining") || L.WarehouseNaves==1);
                TestTrue(TEXT("compact rooms have four, six or eight rooms"),Id!=TEXT("Rooms") || L.Rooms.Num()==6 || (TeamSize==2 && (L.Rooms.Num()==4 || L.Rooms.Num()==8)));
                TestTrue(TEXT("compact village has one or two central blocks"),Id!=TEXT("Village") || L.VillageBlocks.Num()==1 || (TeamSize==2 && L.VillageBlocks.Num()==2));
            }
            if(Seeds.Contains(Seed))
            {
                TestTrue(TEXT("repeat seed accepted"),ERProcedural::Build(C,B,Again));
                TestEqual(TEXT("deterministic bounds"),L.ArenaSize,Again.ArenaSize);TestEqual(TEXT("deterministic panel count"),L.Panels.Num(),Again.Panels.Num());
                Plans.Add(MakeShared<FJsonValueObject>(Plan(L,C,Id,TeamSize,Seed)));
                UE_LOG(LogTemp,Display,TEXT("ER_MAP_PLAN template=%s team=%d seed=%d dimensions_m=%.2fx%.2f"),*Id,TeamSize,Seed,L.ArenaSize.X/100,L.ArenaSize.Y/100);
            }
        }
        if(Count>=32 && TeamSize>=2 && TeamSize<=3)
        {
            if(Id==TEXT("Rooms"))TestTrue(TEXT("small Rooms samples multiple real graph sizes"),RoomCounts.Num()>=2);
            if(Id==TEXT("Village"))TestTrue(TEXT("small Village samples multiple central block counts"),BlockCounts.Num()>=2);
            if(Id==TEXT("Rooms") || Id==TEXT("Village"))TestTrue(TEXT("small plans sample multiple arena proportions"),Bounds.Num()>=4);
        }
        TestEqual(TEXT("authored central recipe untouched"),T->CentralBuilding.ArenaMin,OriginalCentral);TestEqual(TEXT("authored town recipe untouched"),T->Town.ArenaMin,OriginalTown);TestEqual(TEXT("authored village recipe untouched"),T->Village.MaxCentralBlocks,OriginalBlocks);
        UE_LOG(LogTemp,Display,TEXT("ER_MAP_SIZING_VERIFIED template=%s team=%d seeds=%d"),*Id,TeamSize,Cases.Num());
    }
    FString JSON;auto Writer=TJsonWriterFactory<>::Create(&JSON);FJsonSerializer::Serialize(Plans,Writer);
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Testing/MapPlans");IFileManager::Get().MakeDirectory(*Directory,true);
    return TestTrue(TEXT("native plan export saved"),FFileHelper::SaveStringToFile(JSON,*(Directory/FString::Printf(TEXT("plans-%dv%d.json"),TeamSize,TeamSize)),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERMapSizingPhysicsTest,"EclipseRecoil.Maps.TeamSizingPhysics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERMapSizingPhysicsTest::RunTest(const FString&)
{
    int32 TeamSize=2;FParse::Value(FCommandLine::Get(),TEXT("ERMapPlanTeamSize="),TeamSize);
    const auto Ids=Templates();
    FString SeedString=TEXT("42,73,7919");FParse::Value(FCommandLine::Get(),TEXT("ERMapPhysicsSeeds="),SeedString,false);
    TArray<FString> SeedWords;SeedString.ParseIntoArray(SeedWords,TEXT(","));TArray<int32> Seeds;for(const auto& S:SeedWords)Seeds.Add(FCString::Atoi(*S));
    for(const auto& Id:Ids)
    {
        auto* T=LoadObject<UERMapTemplate>(nullptr,*(TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_")+Id));if(!TestNotNull(*Id,T))return false;
        UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
        auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);if(!World)return false;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->LayoutConfig.ExpectedTeamSize=TeamSize;Map->bCeilings=false;
        for(int32 Seed:Seeds)
        {
        Map->LayoutConfig.Seed=Seed;
        if(TestTrue(*(Id+TEXT(" physical generation")),Map->Regenerate()))
        {
            const auto& L=Map->GetLayout();TestTrue(TEXT("valid calibrated cells"),Map->ValidateCells());
            FCollisionQueryParams Q(SCENE_QUERY_STAT(ERMapSizingPhysics),false);FHitResult Hit;const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);
            for(const auto* Starts:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& P:*Starts)
            {
                TestFalse(*(Id+TEXT(" clear standing spawn")),World->OverlapBlockingTestByChannel(P.GetLocation(),FQuat::Identity,ECC_Pawn,Capsule,Q));
                TestTrue(*(Id+TEXT(" supported spawn")),World->SweepSingleByChannel(Hit,P.GetLocation(),P.GetLocation()-FVector(0,0,125),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(20),Q));
            }
            if(Id==TEXT("Village"))for(const auto& Site:L.CoverSites)if(Site.HalfHeight>90)
            {
                if(World->SweepSingleByChannel(Hit,Site.Entry,Site.Hide,FQuat::Identity,ECC_Pawn,Capsule,Q))AddError(FString::Printf(TEXT("Village seed%d ground shelter entry %s -> %s blocked by %s at %s"),Seed,*Site.Entry.ToString(),*Site.Hide.ToString(),*GetNameSafe(Hit.GetComponent()),*Hit.ImpactPoint.ToString()));
                for(float Height:{164.f,220.f})
                {
                    const auto Raise=FVector(0,0,Height-164);
                    TestTrue(TEXT("ground shelter stops standing sight"),World->LineTraceSingleByChannel(Hit,Site.SightFrom+Raise,Site.SightTo+Raise,ECC_GameTraceChannel1,Q));
                }
            }
            TArray<TArray<FVector>> Paths=L.CrossingRoutes;Paths.Append(L.RoomAccessRoutes);Paths.Append(L.VillagePassages);Paths.Append(L.VillageSideBypasses);
            if(Id==TEXT("Rooms"))Paths.Append(L.Routes);
            for(const auto& Path:Paths)for(int32 I=1;I<Path.Num();++I)
            {
                const FVector A=Path[I-1],B=Path[I];
                const bool Blocked=World->SweepSingleByChannel(Hit,A,B,FQuat::Identity,ECC_Pawn,Capsule,Q);
                // Walking is allowed to contact a walkable terrain slope. A
                // vertical wall, ceiling or steep face must still fail.
                const bool WalkableFloor=Blocked && Hit.GetComponent() && Hit.GetComponent()->GetName()==TEXT("AlleyTerrain") && Hit.ImpactNormal.Z>=.72;
                if(Blocked && !WalkableFloor)AddError(FString::Printf(TEXT("%s seed%d standing route %s -> %s component=%s normal=%s point=%s"),*Id,Seed,*A.ToString(),*B.ToString(),*GetNameSafe(Hit.GetComponent()),*Hit.ImpactNormal.ToString(),*Hit.ImpactPoint.ToString()));
            }
            UE_LOG(LogTemp,Display,TEXT("ER_MAP_SIZING_PHYSICS template=%s team=%d seed=%d routes=%d"),*Id,TeamSize,Seed,Paths.Num());
        }
        }
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERMapDensityBudgetTest,"EclipseRecoil.Maps.DensityBudgets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERMapDensityBudgetTest::RunTest(const FString&)
{
    const auto Ids=Templates();
    for(const auto& Id:Ids)
    {
        auto* T=LoadObject<UERMapTemplate>(nullptr,*(TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_")+Id));if(!TestNotNull(*Id,T))return false;
        for(int32 N=1;N<=16;++N)for(int32 Seed:{0,1,2,3,7,42,73,7919,-17,INT32_MIN,INT32_MAX})
        {
            FERProceduralConfig C;C.TemplateAsset=T;C.ExpectedTeamSize=N;C.Seed=Seed;FERBlockRoomConfig B;FERProceduralLayout L;
            if(!TestTrue(*FString::Printf(TEXT("%s %dv%d seed%d builds inside density budget"),*Id,N,N,Seed),ERProcedural::Build(C,B,L)))return false;
            TestTrue(TEXT("actual perimeter area within ten percent of1500m² per team member"),FMath::Abs(ERProcedural::PlayableAreaM2(L)-ERProcedural::TargetAreaM2(N))<=ERProcedural::TargetAreaM2(N)*.1+.01);
            if(Id==TEXT("FireTraining"))TestTrue(TEXT("every team size keeps the warehouse longitudinal"),L.ArenaSize.Y>=L.ArenaSize.X*1.2f);
            if(Id==TEXT("Rooms"))TestTrue(TEXT("every room profile has independent base access"),IndependentRoomBaseAccess(L));
            if(N>1)for(const auto& Hole:L.UnderpassBreaches)
            {
                const FVector2D Gap=(FVector2D(Hole.GetCenter())-FVector2D(L.TownCrossing)).GetAbs(),Extent=FVector2D(Hole.GetExtent())+FVector2D(1300,200);
                TestFalse(TEXT("breachable road stays clear of the bridge"),Gap.X<Extent.X && Gap.Y<Extent.Y);
            }
        }
        UE_LOG(LogTemp,Display,TEXT("ER_MAP_DENSITY_VERIFIED template=%s sizes=1..16 seeds=11"),*Id);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERRoomsFloorBudgetTest,"EclipseRecoil.Maps.RoomsFloorBudget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERRoomsFloorBudgetTest::RunTest(const FString&)
{
    FERProceduralLayout L;L.ArenaSize={10000,10000,1700};
    for(int32 I=0;I<2;++I)
    {
        auto& V=L.RoomNodes.AddDefaulted_GetRef();V.Center={I*2000.,0,0};
        V.Polygon={{-500,-500},{500,-500},{500,500},{-500,500}};
    }
    L.RoomLinks.Add({0,1,{500,0,0},{1500,0,0},360,560,40});
    TestEqual(TEXT("room polygons plus narrow corridor floor, excluding empty perimeter"),ERProcedural::PlayableAreaM2(L),228.);
    L.RoomLinks.Add({0,1,{500,0,620},{1500,0,620},360,560,40});
    L.ElevatedDecks.Add(FBox(FVector(-500,-500,600),FVector(500,500,620)));
    TestEqual(TEXT("upper crossing and decks add no primary floor"),ERProcedural::PlayableAreaM2(L),228.);
    TestEqual(TEXT("750 square metres for each player on both teams"),ERProcedural::TargetAreaM2(2),3000.f);
    return true;
}
#endif
