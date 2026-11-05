#include "ERProceduralMap.h"
#include "ERLayoutSolids.h"
#include "ERMapPlayability.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

namespace
{
using ERLayoutQueries::FLayoutSolids;
struct FSpawnPocket
{
    FTransform Frame;
    TArray<FVector> Exit;
};
FVector Paired(FVector P,bool Mirror){return {Mirror ? P.X:-P.X,-P.Y,P.Z};}
// Conservative pre-physics queries: unknown hollow buildings are excluded in
// their entirety, rather than guessing that a new pocket fits inside the skin.
struct FSpawnSpace
{
    const FERProceduralLayout& Layout;
    FLayoutSolids Solids;
    TArray<FBox> Excluded,WallFaces;
    explicit FSpawnSpace(const FERProceduralLayout& L):Layout(L),Solids(L)
    {
        for(const auto& P:L.Panels)if(!P.Skin.IsEmpty() && !P.bSolidSkin)
            Excluded.Add(FBox(FVector::ZeroVector,P.Size).TransformBy(P.Transform));
        for(const auto& P:L.FixedPieces)if(P.MeshCollision)
            Excluded.Add(FBox(-P.Transform.GetScale3D()*50,P.Transform.GetScale3D()*50).TransformBy(FTransform(P.Transform.GetRotation(),P.Transform.GetLocation())));
        for(const auto& F:L.RoomShell)
        {
            if(FMath::Abs(FVector::CrossProduct(F.B-F.A,F.C-F.A).GetSafeNormal().Z)<.7)
                WallFaces.Add((FBox(ForceInit)+F.A+F.B+F.C+F.D).ExpandBy(1));
        }
        for(const auto& Ramp:L.UnderpassRamps)Excluded.Add(FBox(FVector(Ramp.Bounds.Min.X,Ramp.Bounds.Min.Y,-10000),FVector(Ramp.Bounds.Max.X,Ramp.Bounds.Max.Y,10000)));
        for(const auto& Hole:L.UnderpassBreaches)Excluded.Add(FBox(FVector(Hole.Min.X,Hole.Min.Y,-10000),FVector(Hole.Max.X,Hole.Max.Y,10000)));
        Excluded.Append(L.VillageFloorHoles);
    }
    bool Clear(FVector A,FVector B) const
    {
        if(!Solids.PawnClear(A,B))return false;
        const FVector Margin(35,35,93);
        for(const auto* Set:{&Excluded,&WallFaces})for(const auto& Box:*Set)
            if(FMath::LineBoxIntersection(Box.ExpandBy(Margin),A,B,B-A))return false;
        return true;
    }
    bool Floor(FVector P) const
    {
        const FVector2D XY(P);
        if(Layout.CircularArena &&
            FMath::Square(XY.X/(Layout.ArenaSize.X*.5-100))+
            FMath::Square(XY.Y/(Layout.ArenaSize.Y*.5-100))>1)return false;
        if(FMath::Abs(P.X)>Layout.ArenaSize.X*.5-80 || FMath::Abs(P.Y)>Layout.ArenaSize.Y*.5-80)return false;
        if(Layout.GradedTerrain)
        {
            const double Ground=Layout.CircularArena ? ERProcedural::CentralGroundHeight(Layout,XY):ERProcedural::AlleyGroundHeight(Layout,XY);
            return FMath::Abs(P.Z-100-Ground)<3;
        }
        // Secondary pockets stay on the primary floor, never on a guessed roof.
        if(FMath::Abs(P.Z-100)>50)return false;
        // Fire Training and Village use the primary floor cube emitted by
        // ConfigureTemplateFloors; it is not a FixedPiece in the descriptor.
        if(Layout.RoomShell.IsEmpty() && FMath::Abs(P.Z-100)<3)return true;
        const FVector A=P-FVector(0,0,92),B=P-FVector(0,0,108);
        for(const auto& Solid:Solids.Solids)if(Solid.bPawn && Solids.Crosses(Solid,A,B,FVector::ZeroVector))return true;
        for(int32 I=0;I+2<Solids.Triangles.Num();I+=3)
        {
            const auto& T0=Solids.Triangles[I];const auto& T1=Solids.Triangles[I+1];const auto& T2=Solids.Triangles[I+2];
            if(FMath::Abs(FVector::CrossProduct(T1-T0,T2-T0).GetSafeNormal().Z)<.7)continue;
            FVector Hit,Normal;if(FMath::SegmentTriangleIntersection(A,B,T0,T1,T2,Hit,Normal))return true;
        }
        return false;
    }
    bool FreeBox(const FBox& Box) const
    {
        for(const auto* Set:{&Excluded,&WallFaces})for(const auto& Other:*Set)if(Box.Intersect(Other) && Box.Overlap(Other).GetSize().GetMin()>.01)return false;
        for(const auto& Solid:Solids.Solids)if(Solid.bPawn && Box.Intersect(Solid.Bounds) && Box.Overlap(Solid.Bounds).GetSize().GetMin()>.01)return false;
        return true;
    }
};
const FVector WallCenters[]={{0,130,150},{-150,0,150},{150,0,150}};
const FVector WallSizes[]={{320,20,300},{20,240,300},{20,240,300}};
}

bool ERProcedural::BuildSpawnZones(int32 Seed,FERProceduralLayout& L,const FERCellSettings& Cells)
{
    if(L.AlphaSpawns.IsEmpty() || L.AlphaSpawns.Num()!=L.OmegaSpawns.Num())return false;
    const int32 Total=L.AlphaSpawns.Num(),Requested=1+uint32(Seed)%3;
    const bool Mirror=L.LayoutSymmetry==EERTemplateSymmetry::Mirror;
    const auto OriginalAlpha=L.AlphaSpawns,OriginalOmega=L.OmegaSpawns;
    auto Primary=[&](int32 Count,int32 Team)
    {
        FERLayoutSpawnZone Zone;Zone.Team=Team;Zone.StartCount=Count;
        const auto& Bank=Team ? OriginalOmega:OriginalAlpha;
        for(int32 I=0;I<Count;++I)Zone.Center+=Bank[I].GetLocation();Zone.Center/=Count;
        return Zone;
    };
    L.SpawnZones={Primary(Total,0),Primary(Total,1)};
    if(Requested==1 || Total<4 || Total>12)return true;
    const FSpawnSpace Space(L);
    FVector Alpha=FVector::ZeroVector,Omega=Alpha;
    for(const auto& S:OriginalAlpha)Alpha+=S.GetLocation();Alpha/=Total;
    for(const auto& S:OriginalOmega)Omega+=S.GetLocation();Omega/=Total;
    const FVector Forward=(Omega-Alpha).GetSafeNormal2D();
    if(Forward.IsNearlyZero())return false;
    const double TeamDistance=FVector::Dist2D(Alpha,Omega);
    // Y faces the opponents and Z must remain world-up. MakeFromY alone
    // may select a rolled basis, which would put the footprint underground.
    const FQuat Facing=FRotator(0,Forward.Rotation().Yaw-90,0).Quaternion();
    TArray<TArray<FVector>> Paths=L.RoomAccessRoutes;Paths.Append(L.CrossingRoutes);Paths.Append(L.DistrictRoutes);Paths.Append(L.VillagePassages);Paths.Append(L.VillageSideBypasses);Paths.Append(L.Routes);
    TArray<TArray<FVector>> ProtectedPaths=Paths;
    for(const auto& Cover:L.CoverSites)if(!Cover.Entry.Equals(Cover.Hide,.01))ProtectedPaths.Add({Cover.Entry,Cover.Hide});
    // Sample candidate sites along existing standing routes; a short, checked
    // rear entrance joins each new pocket to the same circulation network.
    TArray<FVector> Candidates,Joins;
    for(const auto& Path:Paths)for(int32 I=1;I<Path.Num();++I)
    {
        const FVector A=Path[I-1],B=Path[I],Delta=B-A;
        const double Length=Delta.Size2D();if(Length<100 || FMath::Abs(Delta.Z)>Length*.12)continue;
        const FVector Side=FVector::CrossProduct(Delta.GetSafeNormal2D(),FVector::UpVector);
        const int32 Steps=FMath::Max(1,FMath::CeilToInt32(Length/150));
        for(int32 J=0;J<=Steps;++J)
        {
            const FVector Join=FMath::Lerp(A,B,double(J)/Steps);
            const double Progress=FVector::DotProduct(Join-Alpha,Forward);
            if(Progress< -300 || Progress>TeamDistance*.40 || Join.Z<90 || Join.Z>500)continue;
            for(float Offset:{350.f,-350.f,450.f,-450.f,650.f,-650.f,850.f,-850.f,1050.f,-1050.f})
            {
                const FVector Center=Join+Side*Offset;
                if(FVector::Dist2D(Center,Alpha)<1250 || FVector::DotProduct(Center-Alpha,Forward)>TeamDistance*.35)continue;
                Candidates.Add(Center);Joins.Add(Join);
            }
        }
    }
    FRandomStream Random(int32(HashCombine(uint32(Seed),0x53504157u)));
    for(int32 I=Candidates.Num()-1;I>0;--I){const int32 J=Random.RandRange(0,I);Candidates.Swap(I,J);Joins.Swap(I,J);}
    int32 FootprintFits=0,WallFits=0,RouteFits=0,ExitFits=0;
    TArray<FSpawnPocket> Pockets;
    for(int32 Site=0;Site<FMath::Min(1024,Candidates.Num()) && Pockets.Num()<Requested-1;++Site)
    {
        const FVector Center=Candidates[Site],Join=Joins[Site];
        bool Apart=true;for(const auto& Pocket:Pockets)Apart&=FVector::Dist2D(Center,Pocket.Frame.GetLocation())>=1000;
        if(!Apart)continue;
        const FTransform Frame(Facing,Center-FVector(0,0,100));
        bool Fits=true;
        for(float X:{-170.f,0.f,170.f})for(float Y:{-150.f,0.f,150.f})
        {
            const FVector Point=Frame.TransformPosition({X,Y,100});
            Fits&=Space.Floor(Point) && Space.Clear(Point,Point) && Space.Floor(Paired(Point,Mirror)) && Space.Clear(Paired(Point,Mirror),Paired(Point,Mirror));
        }
        if(!Fits)continue;
        ++FootprintFits;
        TArray<FBox> Bounds;
        for(int32 W=0;W<3;++W)
        {
            const FBox Box=FBox(WallCenters[W]-WallSizes[W]*.5,WallCenters[W]+WallSizes[W]*.5).TransformBy(Frame);
            const FBox Mate(Paired(Box.Max,Mirror).ComponentMin(Paired(Box.Min,Mirror)),Paired(Box.Max,Mirror).ComponentMax(Paired(Box.Min,Mirror)));
            Fits&=Space.FreeBox(Box) && Space.FreeBox(Mate);Bounds.Add(Box);Bounds.Add(Mate);
        }
        if(Fits)++WallFits;
        // Do not silently obstruct a previously validated route with a screen.
        for(const auto& Path:ProtectedPaths)for(int32 I=1;I<Path.Num() && Fits;++I)for(const auto& Box:Bounds)
            if(FMath::LineBoxIntersection(Box.ExpandBy(FVector(35,35,93)),Path[I-1],Path[I],Path[I]-Path[I-1])){Fits=false;break;}
        if(!Fits)continue;
        ++RouteFits;
        const FVector LocalJoin=Frame.InverseTransformPosition(Join);
        const double BypassX=FMath::Abs(LocalJoin.X)>=230 ? LocalJoin.X:(LocalJoin.X<0 ? -230.:230.);
        const TArray<FVector> Knots{Frame.TransformPosition({0,0,100}),Frame.TransformPosition({0,-190,100}),
            Frame.TransformPosition({BypassX,-190,100}),Frame.TransformPosition({BypassX,LocalJoin.Y,100}),Join};
        TArray<FVector> Exit{Knots[0]};
        for(int32 Leg=1;Leg<Knots.Num();++Leg)
        {
            const int32 Steps=FMath::Max(1,FMath::CeilToInt32(FVector::Dist2D(Knots[Leg-1],Knots[Leg])/75));
            for(int32 I=1;I<=Steps;++I)Exit.Add(FMath::Lerp(Knots[Leg-1],Knots[Leg],double(I)/Steps));
        }
        // The route must clear its own new walls as well as the old district.
        for(int32 I=1;I<Exit.Num() && Fits;++I)for(const auto& Box:Bounds)
            if(FMath::LineBoxIntersection(Box.ExpandBy(FVector(35,35,93)),Exit[I-1],Exit[I],Exit[I]-Exit[I-1]) ||
               FMath::LineBoxIntersection(Box.ExpandBy(FVector(35,35,93)),Paired(Exit[I-1],Mirror),Paired(Exit[I],Mirror),Paired(Exit[I]-Exit[I-1],Mirror)))
            {Fits=false;break;}
        for(int32 I=0;I<Exit.Num();++I)
        {
            Fits&=Space.Floor(Exit[I]) && Space.Floor(Paired(Exit[I],Mirror));
            if(I)Fits&=Space.Clear(Exit[I-1],Exit[I]) && Space.Clear(Paired(Exit[I-1],Mirror),Paired(Exit[I],Mirror));
        }
        for(int32 I=0;I<6 && Fits;++I)
        {
            const FVector Point=Frame.TransformPosition({(I%3-1)*90.,(I/3)*90.-45,100});
            Fits&=Space.Floor(Point) && Space.Clear(Point,Center) && Space.Floor(Paired(Point,Mirror)) && Space.Clear(Paired(Point,Mirror),Paired(Center,Mirror));
        }
        if(!Fits)continue;
        ++ExitFits;
        // Newly accepted pockets also exclude the next one's walls and route.
        bool Conflict=false;
        for(const auto& Pocket:Pockets)for(const auto& Box:Bounds)for(int32 I=1;I<Pocket.Exit.Num();++I)
            Conflict|=FMath::LineBoxIntersection(Box.ExpandBy(FVector(35,35,93)),Pocket.Exit[I-1],Pocket.Exit[I],Pocket.Exit[I]-Pocket.Exit[I-1]);
        for(const auto& Pocket:Pockets)for(int32 W=0;W<3;++W)
        {
            const FBox Box=FBox(WallCenters[W]-WallSizes[W]*.5,WallCenters[W]+WallSizes[W]*.5).TransformBy(Pocket.Frame);
            for(int32 I=1;I<Exit.Num();++I)Conflict|=FMath::LineBoxIntersection(Box.ExpandBy(FVector(35,35,93)),Exit[I-1],Exit[I],Exit[I]-Exit[I-1]);
        }
        if(Conflict)continue;
        Pockets.Add({Frame,Exit});
    }
    UE_LOG(LogTemp,Verbose,TEXT("ER_SPAWN_ZONE_FIT seed=%d candidates=%d footprint=%d walls=%d routes=%d exits=%d pockets=%d"),Seed,Candidates.Num(),FootprintFits,WallFits,RouteFits,ExitFits,Pockets.Num());
    const int32 ZoneCount=Pockets.Num()+1;
    if(ZoneCount==1)return true;
    // A deterministic bounded best fit may retain fewer zones on a compact
    // template; no unsupported or exposed fallback is ever published.
    L.SpawnZones.Reset();L.AlphaSpawns.Reset();L.OmegaSpawns.Reset();
    const int32 PrimaryCount=(Total+ZoneCount-1)/ZoneCount;
    for(int32 I=0;I<PrimaryCount;++I){L.AlphaSpawns.Add(OriginalAlpha[I]);L.OmegaSpawns.Add(OriginalOmega[I]);}
    L.SpawnZones.Add(Primary(PrimaryCount,0));
    int32 PieceId=0;for(const auto& P:L.Panels)PieceId=FMath::Max(PieceId,P.Id/8+1);
    for(int32 Z=0;Z<Pockets.Num();++Z)
    {
        const auto& Pocket=Pockets[Z];
        const int32 Count=(Total-L.AlphaSpawns.Num()+Pockets.Num()-Z-1)/(Pockets.Num()-Z);
        FERLayoutSpawnZone Zone;Zone.FirstStart=L.AlphaSpawns.Num();Zone.StartCount=Count;Zone.Center=Pocket.Frame.GetLocation()+FVector(0,0,100);Zone.ExitRoute=Pocket.Exit;
        L.SpawnZones.Add(Zone);
        for(int32 I=0;I<Count;++I)
        {
            const FVector Point=Pocket.Frame.TransformPosition({(I%3-1)*90.,(I/3)*90.-45,100});
            L.AlphaSpawns.Emplace(FRotator(0,Forward.Rotation().Yaw,0),Point);
            const auto& A=L.AlphaSpawns.Last();L.OmegaSpawns.Emplace(FRotator(0,Mirror ? -A.Rotator().Yaw:A.Rotator().Yaw+180,0),Paired(Point,Mirror));
        }
        L.RoomAccessRoutes.Add(Pocket.Exit);TArray<FVector> MateExit;for(const auto& P:Pocket.Exit)MateExit.Add(Paired(P,Mirror));L.RoomAccessRoutes.Add(MateExit);
        for(int32 W=0;W<3;++W)for(int32 Team=0;Team<2;++Team)
        {
            const FVector Center=Pocket.Frame.TransformPosition(WallCenters[W]);
            const double Yaw=Pocket.Frame.Rotator().Yaw;
            const FTransform Placement(FRotator(0,Team ? (Mirror ? -Yaw:Yaw+180):Yaw,0),Team ? Paired(Center,Mirror):Center);
            const int32 First=L.Panels.Num();AddPieceCells(PieceId++,EERBlockMaterial::Brick,FBox(-WallSizes[W]*.5,WallSizes[W]*.5),true,Cells,L.Panels);
            for(int32 I=First;I<L.Panels.Num();++I)L.Panels[I].Transform=L.Panels[I].Transform*Placement;
        }
    }
    const int32 AlphaZones=L.SpawnZones.Num();for(int32 I=0;I<AlphaZones;++I)
    {
        auto Zone=L.SpawnZones[I];Zone.Team=1;Zone.Center=Paired(Zone.Center,Mirror);for(auto& P:Zone.ExitRoute)P=Paired(P,Mirror);L.SpawnZones.Add(MoveTemp(Zone));
    }
    return L.AlphaSpawns.Num()==Total && L.OmegaSpawns.Num()==Total;
}

bool ERProcedural::SpawnZonesClearInWorld(UWorld* World,const FTransform& Frame,const FERProceduralLayout& L)
{
    if(!World)return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERSpawnZones),false);
    for(TActorIterator<APawn> It(World);It;++It)Query.AddIgnoredActor(*It);
    const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);
    for(const auto& Zone:L.SpawnZones)
    {
        const auto& Bank=Zone.Team ? L.OmegaSpawns:L.AlphaSpawns;
        for(int32 I=Zone.FirstStart;I<Zone.FirstStart+Zone.StartCount;++I)
        {
            if(!Bank.IsValidIndex(I))return false;
            const FVector Point=Frame.TransformPosition(Bank[I].GetLocation());FHitResult Hit;
            if(Zone.FirstStart>0 && World->SweepSingleByChannel(Hit,Point,Frame.TransformPosition(Zone.Center),FQuat::Identity,ECC_Pawn,Capsule,Query) && Hit.ImpactNormal.Z<.72)return false;
            if(World->OverlapBlockingTestByChannel(Point,FQuat::Identity,ECC_Pawn,Capsule,Query) ||
               !World->SweepSingleByChannel(Hit,Point,Point-FVector(0,0,125),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(20),Query))
            {UE_LOG(LogTemp,Warning,TEXT("ER_SPAWN_ZONE_PHYSICS_REJECT team=%d start=%d point=%s"),Zone.Team,I,*Point.ToString());return false;}
        }
        for(int32 I=1;I<Zone.ExitRoute.Num();++I)
        {
            FHitResult Hit;
            if(World->SweepSingleByChannel(Hit,Frame.TransformPosition(Zone.ExitRoute[I-1]),Frame.TransformPosition(Zone.ExitRoute[I]),FQuat::Identity,ECC_Pawn,Capsule,Query) && Hit.ImpactNormal.Z<.72)
            {UE_LOG(LogTemp,Warning,TEXT("ER_SPAWN_ZONE_EXIT_REJECT team=%d component=%s"),Zone.Team,*GetNameSafe(Hit.GetComponent()));return false;}
        }
    }
    return true;
}
